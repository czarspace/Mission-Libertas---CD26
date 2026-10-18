#include "can_manager.h"
#include "remontagem.h"
#include "mode_manager.h"

// ==========================================
// FILAS
// ==========================================
QueueHandle_t filaTransmissaoCAN;
QueueHandle_t filaComandos;
QueueHandle_t filaDistancias;
QueueHandle_t filaRepasse;

// Uma fila de confirmação por nó: a task de recepção deposita o byte (0xCF/0xFF)
// e quem enviou o comando lê dali. Assim, só a task de recepção chama
// twai_receive(), e duas esperas simultâneas (de nós diferentes) não roubam a
// confirmação uma da outra.
static QueueHandle_t filaConf[3];

// Bit por nó (NO_ADCS, NO_RPI, NO_PLD_ESP32): 1 = o último comando enviado a ele
// ficou sem confirmação. Vai para as falhas do housekeeping.
static volatile uint8_t nos_sem_resposta = 0;
uint8_t nosSemResposta() { return nos_sem_resposta; }

// Remontagem das rajadas recebidas
static Remontagem remADCS;
static Remontagem remRPi;
static Remontagem remPLD;
static Remontagem remRepasse;

bool criarFilasCAN() {
    filaTransmissaoCAN = xQueueCreate(6, sizeof(LoteTelemetria));
    filaComandos       = xQueueCreate(8, sizeof(ComandoRecebido));
    filaDistancias     = xQueueCreate(1, sizeof(MedidaDistancias)); // xQueueOverwrite exige tamanho 1
    filaRepasse        = xQueueCreate(4, sizeof(PacoteRepasse));
    for (int i = 0; i < 3; i++) filaConf[i] = xQueueCreate(4, sizeof(uint8_t));

    remontagemIniciar(&remADCS, CAN_ID_RX_BASE_ADCS);
    remontagemIniciar(&remRPi, CAN_ID_RX_BASE_PLD_M1);
    remontagemIniciar(&remPLD, CAN_ID_RX_BASE_PLD_M23);
    remontagemIniciar(&remRepasse, CAN_ID_RX_BASE_REPASSE);

    return filaTransmissaoCAN && filaComandos && filaDistancias && filaRepasse &&
           filaConf[0] && filaConf[1] && filaConf[2];
}

// ==========================================
// INICIALIZAÇÃO DO BARRAMENTO
// ==========================================
bool initCanBus() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = CAN_RX_QUEUE_LEN;
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    // Aceita toda a faixa do protocolo (0x000-0x1FF): o HK concentra a decisão.
    twai_filter_config_t f_config = {
        .acceptance_code = (0x000 << 21),
        .acceptance_mask = (0x1FF << 21) | 0x1FFFFF,
        .single_filter = true
    };

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        if (twai_start() == ESP_OK) {
            Serial.println("[CAN] TWAI iniciado. Barramento pronto para operar.");
            return true;
        }
    }
    Serial.println("[CAN] ERRO: Falha ao iniciar barramento.");
    return false;
}

// ==========================================
// RECEPÇÃO (única leitora do TWAI)
// ==========================================
static int indiceConfirmacao(uint32_t id) {
    switch (id) {
        case CAN_ID_CONF_ADCS:    return NO_ADCS;
        case CAN_ID_CONF_PLD_M1:  return NO_RPI;
        case CAN_ID_CONF_PLD_M23: return NO_PLD_ESP32;
        default:                  return -1;
    }
}

void receberMensagensCAN() {
    twai_message_t rx;
    // Espera limitada para a task poder alimentar o watchdog
    if (twai_receive(&rx, pdMS_TO_TICKS(1000)) != ESP_OK) return;
    if (rx.rtr || rx.data_length_code == 0) return;

    // Telecomando do TT&C -> task de comandos
    if (rx.identifier == CAN_ID_RX_TTC) {
        ComandoRecebido cmd;
        cmd.dlc = rx.data_length_code;
        memcpy(cmd.dados, rx.data, rx.data_length_code);
        if (xQueueSend(filaComandos, &cmd, 0) != pdTRUE) {
            registrarLog(LOG_HK_GERAL, "TC", "0x%02X,FILA_CHEIA", rx.data[0]);
        }
        return;
    }

    // Confirmação de um nó -> fila daquele nó
    int idx = indiceConfirmacao(rx.identifier);
    if (idx >= 0) {
        uint8_t byte = rx.data[0];
        xQueueSend(filaConf[idx], &byte, 0);
        return;
    }

    // Rajada do ADCS: decodificada pelo byte de modo do pacote 0
    if (remontagemPertence(&remADCS, rx.identifier)) {
        if (remontagemProcessar(&remADCS, &rx)) {
            processarRajadaADCS(remADCS.buffer, remADCS.total);
        }
        return;
    }

    // Rajada da Raspberry Pi (Missão 1)
    if (remontagemPertence(&remRPi, rx.identifier)) {
        if (remontagemProcessar(&remRPi, &rx)) {
            processarTelemetriaMissao1_Para_SD(remRPi.buffer, (uint8_t)(remRPi.total * 8));
        }
        return;
    }

    // Rajada de distâncias do ESP32 de payload (Missão 3)
    if (remontagemPertence(&remPLD, rx.identifier)) {
        if (remontagemProcessar(&remPLD, &rx)) {
            processarRajadaDistancias(remPLD.buffer, remPLD.total);
        }
        return;
    }

    // Pacote de repasse recebido pelo TT&C em 433 MHz (Missão 3)
    if (remontagemPertence(&remRepasse, rx.identifier)) {
        if (remontagemProcessar(&remRepasse, &rx)) {
            PacoteRepasse p;
            memcpy(p.dados, remRepasse.buffer, sizeof(p.dados));
            if (xQueueSend(filaRepasse, &p, 0) != pdTRUE) {
                registrarLog(LOG_HK_GERAL, "M3", "REPASSE_DESCARTADO,FILA_CHEIA");
            }
        }
        return;
    }
    // Demais IDs (rajadas do próprio HK ecoadas, confirmação 0x11F, etc.): ignora
}

// ==========================================
// ENVIO DE COMANDOS E ESPERA DE CONFIRMAÇÃO
// ==========================================
static uint32_t idDoNo(no_can_t no) {
    switch (no) {
        case NO_ADCS:      return CAN_ID_TX_ADCS;
        case NO_RPI:       return CAN_ID_TX_PLD_M1;
        case NO_PLD_ESP32: return CAN_ID_TX_PLD_M23;
    }
    return CAN_ID_TX_ADCS;
}

static void transmitirFrame(uint32_t id, const uint8_t* dados, uint8_t dlc) {
    twai_message_t tx;
    memset(&tx, 0, sizeof(tx));
    tx.identifier = id;
    tx.extd = 0;
    tx.rtr = 0;
    tx.data_length_code = dlc > 8 ? 8 : dlc;
    memcpy(tx.data, dados, tx.data_length_code);
    if (twai_transmit(&tx, pdMS_TO_TICKS(10)) != ESP_OK) {
        Serial.printf("❌ [CAN ERRO] Falha ao despachar ID 0x%03X.\n", id);
    }
}

resultado_conf_t enviarEAguardar(no_can_t no, const uint8_t* dados, uint8_t dlc, uint32_t timeout_ms) {
    xQueueReset(filaConf[no]); // descarta confirmações antigas antes de enviar
    transmitirFrame(idDoNo(no), dados, dlc);

    uint8_t resposta;
    if (xQueueReceive(filaConf[no], &resposta, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        nos_sem_resposta = nos_sem_resposta & ~(1u << no);
        return (resposta == CAN_ACK) ? CONF_ACK : CONF_INV;
    }
    nos_sem_resposta = nos_sem_resposta | (1u << no);
    return CONF_TIMEOUT;
}

resultado_conf_t enviarComandoSimplesEAguardar(no_can_t no, uint8_t comando, uint32_t timeout_ms) {
    return enviarEAguardar(no, &comando, 1, timeout_ms);
}

void enviarComandoSemEspera(no_can_t no, uint8_t comando) {
    transmitirFrame(idDoNo(no), &comando, 1);
}

const char* textoResultado(resultado_conf_t r) {
    switch (r) {
        case CONF_ACK: return "ACK";
        case CONF_INV: return "INV";
        default:       return "TIMEOUT";
    }
}

// ==========================================
// RESPOSTAS AO TT&C
// ==========================================
void responderACK(uint8_t comando) {
    uint8_t b = CAN_ACK;
    transmitirFrame(CAN_ID_CONF_HK, &b, 1);
    registrarLog(LOG_HK_GERAL, "ACK", "0x%02X", comando);
}

void responderINV(uint8_t comando) {
    uint8_t b = CAN_INV;
    transmitirFrame(CAN_ID_CONF_HK, &b, 1);
    registrarLog(LOG_HK_GERAL, "INV", "0x%02X", comando);
}

// ==========================================
// RAJADAS
// ==========================================
void enfileirarRajadaHK(LoteTelemetria* lote) {
    lote->id_base = CAN_ID_TX_BASE_HK;
    lote->destino = DESTINO_NENHUM;
    if (lote->quantidade > MAX_PACOTES_RAJADA_HK) lote->quantidade = MAX_PACOTES_RAJADA_HK;
    xQueueSend(filaTransmissaoCAN, lote, 0);
}

void enfileirarRajadaRepasse(LoteTelemetria* lote, uint8_t destino) {
    lote->id_base = CAN_ID_TX_BASE_REPASSE;
    lote->destino = destino;
    xQueueSend(filaTransmissaoCAN, lote, pdMS_TO_TICKS(100));
}

void despacharRajadaCAN(const LoteTelemetria* lote) {
    uint16_t base = lote->id_base ? lote->id_base : CAN_ID_TX_BASE_HK;

    // Header: [0x02][N] ou, na faixa de repasse, [0x02][N][destino LoRa]
    uint8_t header[3] = {CAN_TIPO_RAJADA, lote->quantidade, lote->destino};
    uint8_t tam_header = (lote->destino == DESTINO_NENHUM) ? 2 : 3;
    transmitirFrame(base, header, tam_header);
    vTaskDelay(pdMS_TO_TICKS(2));

    for (uint8_t i = 0; i < lote->quantidade; i++) {
        transmitirFrame(base + 1 + i, lote->pacotes[i].dados, lote->pacotes[i].tamanho);
    }
}
