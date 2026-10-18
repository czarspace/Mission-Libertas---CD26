#include "can_manager.h"
#include "remontagem.h"
#include "sistema.h"

LoteTelemetria ultimo_lote_adcs;
volatile bool tem_dado_novo_adcs = false;

// Remontagem independente por faixa de identificadores
static Remontagem remHK;
static Remontagem remADCS;
static Remontagem remM1;
static Remontagem remPLD;
static Remontagem remRepasse;

// ==========================================
// INICIALIZAÇÃO DO CAN (TWAI)
// ==========================================
bool initCanBus() {
    Serial.println("[CAN TT&C] Inicializando barramento...");

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = CAN_RX_QUEUE_LEN;
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS(); // igual ao HK (500 kbit/s)
    // FILTRO TT&C: aceita IDs de 0x100 até 0x1FF (toda a telemetria e as confirmações)
    twai_filter_config_t f_config = {
        .acceptance_code = (0x100 << 21),
        .acceptance_mask = (0x0FF << 21) | 0x1FFFFF,
        .single_filter = true
    };

    remontagemIniciar(&remHK, 0x110);
    remontagemIniciar(&remADCS, 0x120);
    remontagemIniciar(&remM1, 0x140);
    remontagemIniciar(&remPLD, 0x150);
    remontagemIniciar(&remRepasse, 0x170);

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        if (twai_start() == ESP_OK) {
            Serial.println("✅ [CAN TT&C] Driver TWAI iniciado com sucesso!");
            return true;
        }
    }
    Serial.println("❌ [ERRO] Falha ao iniciar TWAI no TT&C.");
    return false;
}

// Converte uma rajada remontada em item da fila do downlink
static void montarLote(const Remontagem* r, LoteTelemetria* lote) {
    lote->tipo_lora = TIPO_TEL;
    lote->quantidade = r->total;
    for (uint8_t i = 0; i < r->total; i++) {
        lote->pacotes[i].tamanho = 8;
        memcpy(lote->pacotes[i].dados, &r->buffer[i * 8], 8);
    }
}

static void encaminharParaGS(const Remontagem* r, const char* origem) {
    static LoteTelemetria lote;
    montarLote(r, &lote);
    if (xQueueSend(filaTelemetriaParaGS, &lote, pdMS_TO_TICKS(10)) != pdTRUE) {
        Serial.printf("⚠️ [TT&C] Fila LoRa cheia! Lote %s descartado.\n", origem);
    }
}

// ==========================================
// RX: RECEBE TELEMETRIA, CONFIRMAÇÕES E REPASSES - CORE 1
// ==========================================
void receberMensagensCAN() {
    twai_message_t rx;
    // Espera limitada para a task poder alimentar o watchdog
    if (twai_receive(&rx, pdMS_TO_TICKS(1000)) != ESP_OK) return;
    if (rx.rtr || rx.data_length_code == 0) return;

    // A. Resposta do HK ao telecomando que enviamos (0x11F). As confirmações
    // internas (0x1AF, 0x1BF, 0x1CF) são assunto do HK e são ignoradas.
    if (rx.identifier == 0x11F) {
        if (rx.data[0] == 0xCF || rx.data[0] == 0xFF) {
            uint8_t resp = rx.data[0];
            xQueueOverwrite(filaAckHK, &resp);
        }
        return;
    }

    // B. Telemetria do HK -> envia direto
    if (remontagemPertence(&remHK, rx.identifier)) {
        if (remontagemProcessar(&remHK, &rx)) encaminharParaGS(&remHK, "HK");
        return;
    }

    // C. Telemetria do ADCS (50 Hz) -> só atualiza a "foto"; a injeção manda a 2 Hz
    if (remontagemPertence(&remADCS, rx.identifier)) {
        if (remontagemProcessar(&remADCS, &rx)) {
            montarLote(&remADCS, &ultimo_lote_adcs);
            tem_dado_novo_adcs = true;
        }
        return;
    }

    // D. Missão 1 (RPi) e distâncias do ESP32 de payload -> envia direto
    if (remontagemPertence(&remM1, rx.identifier)) {
        if (remontagemProcessar(&remM1, &rx)) encaminharParaGS(&remM1, "Missao1");
        return;
    }
    if (remontagemPertence(&remPLD, rx.identifier)) {
        if (remontagemProcessar(&remPLD, &rx)) encaminharParaGS(&remPLD, "Distancias");
        return;
    }

    // E. Pacote de repasse do HK (0x170) -> transmissão em 433 MHz ao destino do header
    if (remontagemPertence(&remRepasse, rx.identifier)) {
        if (remontagemProcessar(&remRepasse, &rx)) {
            RepasseTx tx;
            tx.destino = remRepasse.destino;
            memcpy(tx.dados, remRepasse.buffer, sizeof(tx.dados));
            if (xQueueSend(filaRepasseTx, &tx, pdMS_TO_TICKS(10)) != pdTRUE) {
                Serial.println("⚠️ [TT&C] Fila de repasse cheia! Pacote descartado.");
            }
        }
        return;
    }
}

// ==========================================
// TX: ENVIA PARA O HK E AGUARDA A RESPOSTA - CORE 0
// ==========================================
static void enviarConfirmacaoParaGS(uint8_t comando, uint8_t resposta) {
    LoteTelemetria item;
    item.tipo_lora = TIPO_ACK;
    item.quantidade = 0;
    item.ack_comando = comando;
    item.ack_resposta = resposta;
    xQueueSend(filaTelemetriaParaGS, &item, pdMS_TO_TICKS(50));
}

void enviarComandoCAN() {
    ComandoSat cmd;

    // Dorme até o LoRa receber um comando da Terra (espera limitada por causa do watchdog)
    if (xQueueReceive(filaComandosParaHK, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) return;

    twai_message_t tx_msg;
    memset(&tx_msg, 0, sizeof(tx_msg));
    tx_msg.identifier = 0x010;
    tx_msg.extd = 0;
    tx_msg.rtr = 0;
    tx_msg.data_length_code = cmd.tamanho + 1;
    tx_msg.data[0] = (uint8_t)cmd.id_comando;
    if (cmd.tamanho > 0) {
        memcpy(&tx_msg.data[1], cmd.dados, cmd.tamanho);
    }

    // Descarta qualquer resposta antiga antes de enviar
    xQueueReset(filaAckHK);

    uint8_t resposta = RESPOSTA_SEM_RESPOSTA;
    for (int tentativa = 1; tentativa <= MAX_TENTATIVAS_TC; tentativa++) {
        wdtAlimentar(); // as 5 tentativas somam até 25 s
        if (twai_transmit(&tx_msg, pdMS_TO_TICKS(10)) != ESP_OK) {
            Serial.println("❌ [CAN TT&C] Falha física no barramento. Tentando novamente em 1s...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        Serial.printf("📤 [CAN TT&C] Comando 0x%02X despachado (tentativa %d). Aguardando resposta...\n",
                      cmd.id_comando, tentativa);

        uint8_t r;
        if (xQueueReceive(filaAckHK, &r, pdMS_TO_TICKS(TIMEOUT_ACK_HK_MS)) == pdTRUE) {
            resposta = r;
            break;
        }
        Serial.println("⚠️ [CAN TT&C] Timeout de 5s. HK não respondeu. Reenviando comando...");
    }

    // Confirmação do telecomando para a GS: ACK, INV ou sem resposta
    enviarConfirmacaoParaGS((uint8_t)cmd.id_comando, resposta);
}

// ==========================================
// REPASSE RECEBIDO EM 433 MHz -> HK (Missão 3)
// ==========================================
void enviarRajadaRepasseCAN(const uint8_t* dados32) {
    twai_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.identifier = 0x160;
    msg.data_length_code = 2;
    msg.data[0] = 0x02;
    msg.data[1] = 4;
    twai_transmit(&msg, pdMS_TO_TICKS(10));
    vTaskDelay(pdMS_TO_TICKS(2));

    for (int k = 0; k < 4; k++) {
        msg.identifier = 0x161 + k;
        msg.data_length_code = 8;
        memcpy(msg.data, &dados32[8 * k], 8);
        twai_transmit(&msg, pdMS_TO_TICKS(10));
    }
}
