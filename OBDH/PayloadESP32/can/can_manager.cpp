#include "can_manager.h"
#include "uwb_manager.h"

QueueHandle_t filaMedicoes;

bool initCanBus() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    // Filtro: aceita apenas 0x031 (todos os bits do identificador precisam bater)
    twai_filter_config_t f_config = {
        .acceptance_code = (CAN_ID_RX_PLD << 21),
        .acceptance_mask = (0x000 << 21) | 0x1FFFFF,
        .single_filter = true
    };

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        if (twai_start() == ESP_OK) {
            Serial.println("[CAN PLD] TWAI iniciado. Escutando 0x031.");
            return true;
        }
    }
    Serial.println("[CAN PLD] ERRO: Falha ao iniciar barramento.");
    return false;
}

static void transmitir(uint32_t id, const uint8_t* dados, uint8_t dlc) {
    twai_message_t tx;
    memset(&tx, 0, sizeof(tx));
    tx.identifier = id;
    tx.data_length_code = dlc;
    memcpy(tx.data, dados, dlc);
    twai_transmit(&tx, pdMS_TO_TICKS(10));
}

void enviarConfirmacaoCAN() {
    uint8_t b = 0xCF;
    transmitir(CAN_ID_CONF_PLD, &b, 1);
}

void enviarInvalidoCAN() {
    uint8_t b = 0xFF;
    transmitir(CAN_ID_CONF_PLD, &b, 1);
}

// Comandos do HK em 0x031
void receberComandosCAN() {
    twai_message_t rx;
    if (twai_receive(&rx, portMAX_DELAY) != ESP_OK) return;
    if (rx.rtr || rx.identifier != CAN_ID_RX_PLD || rx.data_length_code == 0) return;

    uint8_t comando = rx.data[0];
    switch (comando) {
        case 0x00: { // SET_TIME
            if (rx.data_length_code < 5) { enviarInvalidoCAN(); return; }
            uint32_t unix_time;
            memcpy(&unix_time, &rx.data[1], 4);
            struct timeval tv; tv.tv_sec = unix_time; tv.tv_usec = 0;
            settimeofday(&tv, NULL);
            break;
        }

        case 0x01: // IDLE (também é o aviso de desligamento gracioso)
        case 0xC0: // STOP
            uwbStandby();
            modoAtual = PLD_IDLE;
            break;

        case 0x31: // Missão 2: só responde às medições dos outros nós
            uwbModoResponder();
            modoAtual = PLD_MISSAO2;
            break;

        case 0x32: // Missão 3: responde e mede a cada MEASURE
            uwbModoResponder();
            modoAtual = PLD_MISSAO3;
            break;

        case 0x33: { // MEASURE: válido só na Missão 3
            if (modoAtual != PLD_MISSAO3) { enviarInvalidoCAN(); return; }
            uint8_t pedido = 0x33;
            if (xQueueSend(filaMedicoes, &pedido, 0) != pdTRUE) { enviarInvalidoCAN(); return; }
            break;
        }

        default:
            enviarInvalidoCAN();
            return;
    }
    enviarConfirmacaoCAN();
}

void enviarRajadaDistancias(uint8_t modo, float d1, float d2) {
    // Header: [0x02][2]
    uint8_t header[2] = {0x02, 2};
    transmitir(CAN_ID_TX_BASE_PLD, header, 2);
    vTaskDelay(pdMS_TO_TICKS(2));

    // Pacote 0: timestamp + modo + origem
    uint8_t p0[8] = {0};
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t ts = (uint32_t)tv.tv_sec;
    memcpy(p0, &ts, 4);
    p0[4] = modo;
    p0[5] = ID_PROPRIO;
    transmitir(CAN_ID_TX_BASE_PLD + 1, p0, 6);

    // Pacote 1: distâncias aos outros dois satélites, por ordem de identificador
    uint8_t p1[8];
    memcpy(&p1[0], &d1, 4);
    memcpy(&p1[4], &d2, 4);
    transmitir(CAN_ID_TX_BASE_PLD + 2, p1, 8);
}
