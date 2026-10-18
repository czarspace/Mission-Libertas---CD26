#include "Config.h"
#include "mode_manager.h"
#include "can_manager.h"
#include "sensores_manager.h"
#include <sys/time.h>
#include <esp_system.h>

// ==========================================
// INSTANCIAÇÃO DAS VARIÁVEIS GLOBAIS
// ==========================================
MissionMode currentMode = MODE_IDLE; 
bool motorInicializado = false; // Começa desligado e descalibrado


double ParadaAngulo = 2; double ParadaEst = 0.5; int limitA = 1000;

SemaphoreHandle_t semCanInit; 
QueueHandle_t filaTransmissaoCAN;

void sincronizarTempoComSolo() {
    Serial.println("[INIT ADCS] Aguardando sincronizacao de tempo do HK (CMD: 0x00)...");
    bool tempo_sincronizado = false; twai_message_t rx_message;
    uint32_t tempoInicioEsperando = millis();
    const uint32_t TIMEOUT_SINC = 60000;

    while (!tempo_sincronizado) {
        if (millis() - tempoInicioEsperando >= TIMEOUT_SINC) {
            Serial.println("⚠️ [INIT ADCS] ALERTA: Timeout! ADCS dessincronizado.");
            break; 
        }
        if (twai_receive(&rx_message, pdMS_TO_TICKS(100)) == ESP_OK) {
            if (!(rx_message.rtr) && rx_message.identifier == CAN_ID_RX_BASE && rx_message.data_length_code >= 5 && rx_message.data[0] == 0x00) {
                uint32_t unix_time = 0;
                memcpy(&unix_time, &rx_message.data[1], sizeof(unix_time));
                struct timeval tv; tv.tv_sec = unix_time; tv.tv_usec = 0;        
                settimeofday(&tv, NULL);
                Serial.printf("✅ [INIT ADCS] Tempo Sincronizado: %lu\n", unix_time);
                enviarConfirmacaoCAN();
                tempo_sincronizado = true; 
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void setup() {
    Serial.begin(115200);
    Serial.printf("[INIT ADCS] Motivo do reset: %d (esp_reset_reason_t)\n", (int)esp_reset_reason());

    // Inicializa APENAS os barramentos lógicos I2C e periféricos frios
    Wire.begin(I2C_SDA, I2C_SCL);

    semCanInit = xSemaphoreCreateBinary();
    
    filaTransmissaoCAN = xQueueCreate(5, sizeof(LoteTelemetria));
    if (filaTransmissaoCAN == NULL) {
        Serial.println("❌ [ERRO] Falha ao criar a Fila do CAN!");
        while(1) { delay(100); }
    }
    xTaskCreatePinnedToCore(TaskInitCanCore0, "InitCan0", 4096, NULL, 5, NULL, 0);
    xSemaphoreTake(semCanInit, portMAX_DELAY);
    
    // Inicializa sensores (BH1750s e MPU6500) que não dependem do barramento de potência do motor
    initSensores();

    // PID: limites simétricos (a correção pode ter os dois sinais), período igual ao
    // do laço de controle (20 ms) e modo AUTOMATIC. Sem isso, a biblioteca mantém o
    // padrão (0 a 255, 100 ms, MANUAL) e Compute() não altera a saída.
    myPID_e.SetOutputLimits(-limitA, limitA);
    myPID_e.SetSampleTime(20);
    carregarGanhosPID(); // ganhos de cada modo gravados por SET_PID (ou os padrões)
    prepararNovoModo(ganhosPID[PID_ESTABILIZACAO]);

    sincronizarTempoComSolo();

    // Dispara as tarefas operacionais
    xTaskCreatePinnedToCore(TaskMotorADCS, "Motor_Loop", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(TaskControleADCS, "Control_Loop", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(TaskCanRx_Routine, "CAN_RX", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskCanTx_Routine, "CAN_TX", 4096, NULL, 2, NULL, 0);

    Serial.println("🚀 [INIT ADCS] Setup operacional. Motor Protegido em Standby.");
}

// ==========================================
// TASK DO MOTOR: GERENCIAMENTO DINÂMICO (1kHz)
// ==========================================
void TaskMotorADCS(void *pvParameters) {
    for(;;) {
        // Se o satélite sair do IDLE (Entrar em ESTABILIZACAO, APONTAMENTO, etc)
        // MODE_DETERMINACAO e MODE_TP não usam o motor — entram aqui e o motor
        // ficaria ligado à toa (e, no caso do TP, tentando calibrar sem energia
        // no driver, já que o HK desliga o MOSFET do ADCS antes desse comando).
        if (currentMode != MODE_IDLE && currentMode != MODE_DETERMINACAO && currentMode != MODE_TP){
            
            // Subgatilho: Se ainda não inicializou o software, roda agora
            if (!motorInicializado) {
                initMotorFOC();
                motorInicializado = true;
            }
            
            // Loop de alta frequência do SimpleFOC
            motor.loopFOC();
            motor.move();
            
        } else {
            // Se o satélite cair ou voltar para o Modo IDLE
            
            // Subgatilho: Se o software ainda estava ligado, desliga imediatamente
            if (motorInicializado) {
                desligarMotorFOC();
                motorInicializado = false;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}

void TaskControleADCS(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    for(;;) {
        aplicarGanhosPendentes(); // SET_PID recebido com o modo ativo

        if (currentMode != MODE_IDLE) {
            switch(currentMode) {
                case MODE_ESTABILIZACAO: executarEstabilizacao(); break;
                case MODE_APONTAMENTO:   executarApontamento(); break;
                case MODE_DETERMINACAO:  executarDeterminacao(); break;
                case MODE_CONTROLE:  executarControle(); break;
                case MODE_TP:        executarTesteSensores(); break;
                case MODE_TI:        executarTesteMotor(); break;
                default: break;
            }
        } else {
            executarModoIdle();
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20)); 
    }
}

void TaskInitCanCore0(void *pvParameters) {
    initCanBus(); 
    xSemaphoreGive(semCanInit); 
    vTaskDelete(NULL); 
}

void TaskCanRx_Routine(void *pvParameters) {
    for(;;) { receberComandosCAN(); vTaskDelay(pdMS_TO_TICKS(10)); }
}

void TaskCanTx_Routine(void *pvParameters) {
    LoteTelemetria loteRecebido;

    for(;;) {
        // Fica aguardando eternamente (sem gastar CPU) até que a fila receba algo
        if (xQueueReceive(filaTransmissaoCAN, &loteRecebido, portMAX_DELAY) == pdPASS) {
            // Despacha no barramento físico
            despacharRajadaCAN(loteRecebido.pacotes, loteRecebido.quantidade);
        }
    }
}

void loop() { vTaskDelete(NULL); }