#include "Config.h"
#include "can_manager.h"
#include "sd_manager.h"
#include "sensores_manager.h"
#include "mode_manager.h"
#include "aquecedor.h"
#include "sistema.h"
#include <Preferences.h>


// ==========================================
// INSTÂNCIA DAS VARIÁVEIS GLOBAIS
// ==========================================
volatile obdh_mode_t modo_secundario = MODE_IDLE;
volatile TickType_t delay_modo_secundario = pdMS_TO_TICKS(1000);
volatile bool payload_aguardando_desligamento = false;
volatile uint32_t payload_desligando_desde = 0;
volatile bool hk_ocupado = false;
volatile obdh_mode_t sequencia_pendente = MODE_IDLE;
volatile bool missao1_ativando = false;
volatile bool missao1_cancelada = false;
volatile uint32_t missao1_inicio = 0;
volatile bool tempo_sincronizado = false;
uint8_t motivo_reset = 0;
uint16_t contador_boots = 0;

SemaphoreHandle_t sdMutex;
SemaphoreHandle_t sensoresMutex;
SemaphoreHandle_t modoMutex;

QueueHandle_t filaGravacaoSD;

TaskHandle_t TaskSdWrite_Handle;
TaskHandle_t TaskCanRx_Handle;
TaskHandle_t TaskCanTx_Handle;
TaskHandle_t TaskComandos_Handle;
TaskHandle_t TaskIdle_Handle;
TaskHandle_t TaskMode_Handle;

// ==========================================
// SINCRONIZAÇÃO DE TEMPO COM O SOLO (boot)
// ==========================================
// Roda antes da criação das tasks, então ainda lê o TWAI diretamente.
void sincronizarTempoComSolo() {
    Serial.println("[INIT] Aguardando sincronizacao de tempo do solo (CMD: 0x00)...");

    twai_message_t rx_message;
    uint32_t tempoInicioEsperando = millis();
    const uint32_t TIMEOUT_SINC = 60000; // 1 minuto

    while (!tempo_sincronizado) {
        if (millis() - tempoInicioEsperando >= TIMEOUT_SINC) {
            Serial.println("⚠️ [INIT] Timeout de 1 minuto: iniciando sistema dessincronizado.");
            registrarLog(LOG_HK_GERAL, "TIME", "SEM_SINCRONIZACAO");
            break;
        }

        if (twai_receive(&rx_message, pdMS_TO_TICKS(100)) != ESP_OK) continue;
        if (rx_message.rtr || rx_message.identifier != CAN_ID_RX_TTC ||
            rx_message.data_length_code < 5 || rx_message.data[0] != 0x00) continue;

        uint32_t unix_time = 0;
        memcpy(&unix_time, &rx_message.data[1], sizeof(unix_time));
        struct timeval tv;
        tv.tv_sec = unix_time;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        Serial.printf("✅ [INIT] Tempo HK sincronizado! UNIX Epoch: %lu\n", unix_time);

        // Repassa ao ADCS e espera a confirmação dele (até 2 s) lendo o TWAI aqui mesmo
        twai_message_t tx = rx_message;
        tx.identifier = CAN_ID_TX_ADCS;
        twai_transmit(&tx, pdMS_TO_TICKS(10));

        bool adcs_ok = false;
        uint32_t inicio = millis();
        while (millis() - inicio < TIMEOUT_CONF_SET_TIME_MS) {
            twai_message_t conf;
            if (twai_receive(&conf, pdMS_TO_TICKS(10)) == ESP_OK &&
                conf.identifier == CAN_ID_CONF_ADCS && conf.data_length_code > 0 &&
                conf.data[0] == CAN_ACK) {
                adcs_ok = true;
                break;
            }
        }

        if (adcs_ok) {
            Serial.println("✅ [INIT] ADCS sincronizado com sucesso.");
            registrarLog(LOG_HK_GERAL, "TIME", "0,%lu", (unsigned long)unix_time);
            responderACK(0x00);
            tempo_sincronizado = true;
        } else {
            // Sem resposta: aguarda o próximo SET_TIME, retransmitido pelo TT&C
            Serial.println("❌ [INIT] FALHA na sincronizacao do ADCS. Aguardando novo SET_TIME...");
        }
    }
}

// ==========================================
// NVS: CONTADOR DE BOOTS E TERM PENDENTE
// ==========================================
// Retorna true se o TERM deve ser restaurado: ele estava ativo quando o HK
// reiniciou e o reset não foi um liga/desliga pela chave. Um POWERON com o TERM
// gravado (satélite desligado no meio da ciclagem) apaga a marca, para o satélite
// nunca ligar direto no TERM numa sessão nova.
bool lerEstadoPersistente(esp_reset_reason_t motivo) {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE_HK, false)) return false;
    contador_boots = nvs.getUShort(NVS_CHAVE_BOOTS, 0) + 1;
    nvs.putUShort(NVS_CHAVE_BOOTS, contador_boots);
    bool term = nvs.getBool(NVS_CHAVE_TERM, false);
    if (term && motivo == ESP_RST_POWERON) {
        nvs.putBool(NVS_CHAVE_TERM, false);
        term = false;
        Serial.println("[INIT] TERM estava gravado, mas o boot foi por POWERON: nao restaurado.");
    }
    nvs.end();
    return term;
}

// ==========================================
// TASKS DO CORE 0 (Comunicação)
// ==========================================
// Única leitora do TWAI: roteia telecomandos, confirmações e rajadas.
void TaskCanRx_Routine(void *pvParameters) {
    wdtRegistrar();
    for(;;) {
        wdtAlimentar();
        receberMensagensCAN(); // espera no máximo 1 s por um frame
    }
}

void TaskCanTx_Routine(void *pvParameters) {
    wdtRegistrar();
    LoteTelemetria loteRecebido;
    for(;;) {
        wdtAlimentar();
        if (xQueueReceive(filaTransmissaoCAN, &loteRecebido, pdMS_TO_TICKS(1000)) == pdPASS) {
            despacharRajadaCAN(&loteRecebido);
        }
    }
}

// ==========================================
// TASKS DO CORE 1 (Lógica e Operação)
// ==========================================
// Executa os telecomandos, um por vez, na ordem de chegada
void TaskComandos_Routine(void *pvParameters) {
    wdtRegistrar();
    ComandoRecebido cmd;
    for(;;) {
        wdtAlimentar();
        if (xQueueReceive(filaComandos, &cmd, pdMS_TO_TICKS(1000)) == pdPASS) {
            processarTelecomando(&cmd);
        }
    }
}

void TaskIdle_Routine(void *pvParameters) {
    wdtRegistrar();
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000); // 1 Hz

    for(;;) {
        wdtAlimentar();

        // Desligamento gracioso do payload: corta o MOSFET de carga útil 40 s
        // depois de avisar o computador de payload. Independe do modo atual.
        if (payload_aguardando_desligamento &&
            millis() - payload_desligando_desde >= TEMPO_DESLIGAMENTO_PLD_MS) {
            digitalWrite(PIN_MOSFET_CARGA_UTIL, LOW);
            payload_aguardando_desligamento = false;
            registrarLog(LOG_HK_GERAL, "ACT", "PAYLOAD,OFF");
        }

        // Pausa o housekeeping durante a ciclagem térmica: o TERM já registra a
        // temperatura e a tensão da bateria a 2 Hz.
        if (modo_secundario != MODE_TERM) {
            executarModoIdle();
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void TaskSecondaryMode_Routine(void *pvParameters) {
    wdtRegistrar();
    TickType_t xLastWakeTime = xTaskGetTickCount();
    for(;;) {
        wdtAlimentar();

        // Sequências de tiro único (TP1, TP2, TI): bloqueantes aqui (o TI leva ~15 s,
        // abaixo do timeout do watchdog), com a recepção do CAN e a task de comandos
        // livres em paralelo.
        executarSequenciaPendente();

        // Conclusão da ativação da Missão 1 (após o boot de 60 s da RPi)
        atualizarAtivacaoMissao1();

        switch(modo_secundario) {
            case MODE_ABERTURA: executarModoAbertura(); break;
            case MODE_CARGA:    executarModoCarga(); break;
            case MODE_TERM:     executarModoTerm(); break;
            case MODE_MISSAO3:  executarModoMissao3(); break;

            // Modos do ADCS e Missões 1/2: a telemetria chega de forma assíncrona
            // pela task de recepção; não há trabalho por ciclo aqui.
            default: break;
        }

        vTaskDelayUntil(&xLastWakeTime, delay_modo_secundario);
    }
}

void TaskSdWrite_Routine(void *pvParameters) {
    wdtRegistrar();
    MensagemLog logRecebido;

    for(;;) {
        wdtAlimentar();
        if (xQueueReceive(filaGravacaoSD, &logRecebido, pdMS_TO_TICKS(1000)) == pdPASS) {
            // LOG_MODO_BLOCO vai pro arquivo do modo aberto; LOG_HK_GERAL vai pro
            // LOG_OP.txt. Abrir/fechar arquivo viajam na mesma fila, para que tudo
            // que foi enfileirado antes da troca de modo vá para o arquivo antigo.
            switch (logRecebido.tipo) {
                case LOG_MODO_BLOCO:  gravarLogModo(logRecebido.texto); break;
                case LOG_ABRIR_MODO:  abrirLogModo(logRecebido.modo_alvo); break;
                case LOG_FECHAR_MODO: fecharLogModo(); break;
                default:              gravarLogHousekeeping(logRecebido.texto); break;
            }
        }
    }
}

// ==========================================
// SETUP (Boot Sequence)
// ==========================================
void setup() {
    Serial.begin(115200);
    while(!Serial) { delay(10); }

    esp_reset_reason_t motivo = esp_reset_reason();
    motivo_reset = (uint8_t)motivo;
    Serial.printf("\n[INIT] Ligando OBDH Libertas (Gabriella)... reset: %s\n", textoMotivoReset(motivo));

    bool restaurar_term = lerEstadoPersistente(motivo);
    wdtConfigurar(WDT_TIMEOUT_S);

    // 1. Mutexes e filas
    sdMutex = xSemaphoreCreateMutex();
    sensoresMutex = xSemaphoreCreateMutex();
    modoMutex = xSemaphoreCreateMutex();

    // 40 mensagens: a fila carrega os dados e os controles de abrir/fechar arquivo,
    // com folga para a telemetria do ADCS (~50 registros/s).
    filaGravacaoSD = xQueueCreate(40, sizeof(MensagemLog));
    if (filaGravacaoSD == NULL || !criarFilasCAN() || !sdMutex || !sensoresMutex || !modoMutex) {
        Serial.println("❌ [ERRO CRITICO] Falha ao criar filas/mutexes!");
        while(1) { delay(1000); }
    }

    // 2. Hardware (MOSFETs desligados em initSensores, aquecedor no PWM em seguida)
    initSD(textoMotivoReset(motivo), contador_boots);
    initSensores();
    aquecedorIniciar();

    // 3. Barramento
    if (!initCanBus()) {
        Serial.println("❌ [ERRO CRITICO] CAN falhou na inicializacao!");
        while(1) { delay(1000); }
    }

    // 4. Task de gravação já ativa, para registrar a sincronização de tempo
    xTaskCreatePinnedToCore(TaskSdWrite_Routine, "SD_TASK", 4096, NULL, 7, &TaskSdWrite_Handle, 1);

    // 5. Sincronização de tempo (timeout de 60 s). Ao restaurar o TERM, o controle
    // dos aquecedores não pode esperar: o relógio fica dessincronizado até o
    // próximo SET_TIME, que é aceito em qualquer modo.
    if (restaurar_term) {
        Serial.println("⚠️ [INIT] TERM estava ativo antes do reset: restaurando sem esperar o SET_TIME.");
        registrarLog(LOG_HK_GERAL, "TIME", "SEM_SINCRONIZACAO");
        transicionarModo(MODE_TERM, GATILHO_RESET);
    } else {
        sincronizarTempoComSolo();
    }

    // 6. Core 0 - comunicação
    xTaskCreatePinnedToCore(TaskCanRx_Routine, "CAN_RX", 4096, NULL, 10, &TaskCanRx_Handle, 0);
    xTaskCreatePinnedToCore(TaskCanTx_Routine, "CAN_TX", 4096, NULL, 8,  &TaskCanTx_Handle, 0);

    // 7. Core 1 - lógica. A task de comandos fica acima da gravação no SD: um
    // telecomando precisa ser respondido dentro da janela de 5 s do TT&C, e ela
    // passa a maior parte do tempo bloqueada na fila (não consome CPU).
    xTaskCreatePinnedToCore(TaskComandos_Routine, "CMD_TSK", 8192, NULL, 8, &TaskComandos_Handle, 1);
    xTaskCreatePinnedToCore(TaskSecondaryMode_Routine, "MODE_TSK", 8192, NULL, 6, &TaskMode_Handle, 1);
    xTaskCreatePinnedToCore(TaskIdle_Routine, "IDLE_TSK", 8192, NULL, 5, &TaskIdle_Handle, 1);
    Serial.println("🚀 [INIT] Setup concluido. Satelite operacional.");
}

// ==========================================
// LOOP VAZIO (FreeRTOS no controle)
// ==========================================
void loop() {
    vTaskDelete(NULL);
}
