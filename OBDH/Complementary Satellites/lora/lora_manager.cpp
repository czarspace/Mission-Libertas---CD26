#include "lora_manager.h"

SPIClass spiLoRa(HSPI);
Module modLoRa(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1, spiLoRa);
SX1278 lora(&modLoRa);

bool initLoRa() {
    spiLoRa.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    // Mesmos parâmetros do enlace da constelação (Config.h)
    if (lora.begin(FREQ_LORA, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
                   LORA_POTENCIA_DBM, LORA_PREAMBULO, 0) != RADIOLIB_ERR_NONE) {
        Serial.println("❌ [LORA] Falha ao iniciar o SX1278!");
        return false;
    }
    lora.startReceive();
    Serial.println("✅ [LORA] SX1278 em recepção (433 MHz).");
    return true;
}

static void transmitirFila() {
    PacoteEnvio p;
    while (xQueueReceive(filaEnvio, &p, 0) == pdTRUE) {
        uint8_t buffer[2 + 64];
        buffer[0] = p.destino;
        buffer[1] = p.tipo;
        memcpy(&buffer[2], p.dados, p.tamanho);
        int estado = lora.transmit(buffer, 2 + p.tamanho);
        if (estado != RADIOLIB_ERR_NONE) {
            Serial.printf("❌ [LORA] Falha na transmissão (%d).\n", estado);
        }
        lora.startReceive(); // volta a escutar logo após cada transmissão
    }
}

void atenderLoRa() {
    transmitirFila();

    if (digitalRead(LORA_DIO0) != HIGH) return;

    uint8_t buffer[256];
    int estado = lora.readData(buffer, sizeof(buffer));
    if (estado == RADIOLIB_ERR_NONE) {
        size_t tamanho = lora.getPacketLength();
        // [DESTINO][TIPO][...]: só processa o que é endereçado a este satélite
        if (tamanho >= 3 && buffer[0] == SAT_ID && tamanho - 2 <= sizeof(((PacoteRecebido*)0)->dados)) {
            PacoteRecebido r;
            r.tipo = buffer[1];
            r.tamanho = tamanho - 2;
            memcpy(r.dados, &buffer[2], r.tamanho);
            xQueueSend(filaRecebidos, &r, 0);
        }
    }
    lora.startReceive();
}

void enviarRajadaParaGS(const uint8_t* pacotes, uint8_t quantidade) {
    PacoteEnvio p;
    p.destino = ID_GS;
    p.tipo = TIPO_TEL;
    p.tamanho = quantidade * 8;
    memcpy(p.dados, pacotes, p.tamanho);
    xQueueSend(filaEnvio, &p, pdMS_TO_TICKS(50));
}

void enviarRepasse(uint8_t destino, const uint8_t* dados32) {
    PacoteEnvio p;
    p.destino = destino;
    p.tipo = TIPO_PLD;
    p.tamanho = 32;
    memcpy(p.dados, dados32, 32);
    xQueueSend(filaEnvio, &p, pdMS_TO_TICKS(50));
}
