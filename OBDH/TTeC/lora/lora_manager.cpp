#include "lora_manager.h"

// ==========================================
// INSTÂNCIAS DOS BARRAMENTOS SPI INDEPENDENTES
// ==========================================
SPIClass spiUplink(VSPI);   // Uplink (SX1278): VSPI, pinos 18, 19, 23, 5
SPIClass spiDownlink(HSPI); // Downlink (SX1276): HSPI, pinos 14, 32, 13, 26

// ==========================================
// INSTÂNCIAS DOS MÓDULOS (RadioLib)
// ==========================================
// Módulo SX1276 (Downlink para a Terra, 915 MHz) ligado ao HSPI
Module modDownlink(LORA_DL_CS, LORA_DL_DIO0, LORA_DL_RST, LORA_DL_DIO1, spiDownlink);
SX1276 loraDownlink(&modDownlink);

// Módulo SX1278 (Uplink da Terra e canal compartilhado, 433 MHz) ligado ao VSPI
Module modUplink(LORA_UP_CS, LORA_UP_DIO0, LORA_UP_RST, LORA_UP_DIO1, spiUplink);
SX1278 loraUplink(&modUplink);

// Estado de cada rádio. Cada um é iniciado e reiniciado de forma independente:
// uma falha no downlink não pode deixar o satélite surdo aos telecomandos.
static bool downlinkOk = false;
static bool uplinkOk = false;
static uint32_t ultimaTentativaDownlink = 0;
static uint32_t ultimaTentativaUplink = 0;

// ==========================================
// INICIALIZAÇÃO DOS RÁDIOS
// ==========================================
static bool iniciarDownlink() {
    ultimaTentativaDownlink = millis();
    int estado = loraDownlink.begin(FREQ_DOWNLINK, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
                                    LORA_POTENCIA_DBM, LORA_PREAMBULO, 0);
    if (estado != RADIOLIB_ERR_NONE) {
        Serial.printf("❌ [LORA 915] Falha ao iniciar o SX1276 (Downlink TX): %d\n", estado);
        return false;
    }
    Serial.println("✅ [LORA 915] SX1276 pronto.");
    return true;
}

static bool iniciarUplink() {
    ultimaTentativaUplink = millis();
    int estado = loraUplink.begin(FREQ_UPLINK, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
                                  LORA_POTENCIA_DBM, LORA_PREAMBULO, 0);
    if (estado != RADIOLIB_ERR_NONE) {
        Serial.printf("❌ [LORA 433] Falha ao iniciar o SX1278 (Uplink RX): %d\n", estado);
        return false;
    }
    // O rádio de 433 MHz fica em recepção contínua por padrão
    loraUplink.startReceive();
    Serial.println("✅ [LORA 433] SX1278 em recepção.");
    return true;
}

bool initLoRaModems() {
    Serial.println("[LORA] Inicializando rádios (Dual SPI)...");

    spiDownlink.begin(LORA_DL_SCK, LORA_DL_MISO, LORA_DL_MOSI, LORA_DL_CS);
    spiUplink.begin(LORA_UP_SCK, LORA_UP_MISO, LORA_UP_MOSI, LORA_UP_CS);

    downlinkOk = iniciarDownlink();
    uplinkOk = iniciarUplink();
    return downlinkOk && uplinkOk;
}

bool downlinkPronto() { return downlinkOk; }
bool uplinkPronto() { return uplinkOk; }

// ==========================================
// REPASSE (Missão 3): transmissão em 433 MHz
// ==========================================
// Feita pela própria task de recepção LoRa, para que o rádio de 433 MHz só seja
// usado por uma task: sai da recepção, transmite e volta a receber.
static void transmitirRepassesPendentes() {
    RepasseTx tx;
    while (xQueueReceive(filaRepasseTx, &tx, 0) == pdTRUE) {
        uint8_t pacote[2 + 32];
        pacote[0] = tx.destino;
        pacote[1] = TIPO_PLD;
        memcpy(&pacote[2], tx.dados, 32);

        int estado = loraUplink.transmit(pacote, sizeof(pacote));
        if (estado == RADIOLIB_ERR_NONE) {
            Serial.printf("📤 [LORA 433] Repasse enviado ao destino 0x%02X.\n", tx.destino);
        } else {
            Serial.printf("❌ [LORA 433] Falha no repasse (%d).\n", estado);
        }
        loraUplink.startReceive();
    }
}

// ==========================================
// RX: ESCUTA A TERRA E O CANAL COMPARTILHADO (CORE 0)
// ==========================================
void escutarComandosLoRa() {
    // Rádio de 433 MHz fora do ar: tenta reiniciar a cada LORA_RETENTATIVA_MS
    if (!uplinkOk) {
        if (millis() - ultimaTentativaUplink >= LORA_RETENTATIVA_MS) uplinkOk = iniciarUplink();
        // Repasses pendentes não têm como sair sem o rádio: descarta
        RepasseTx descartado;
        while (xQueueReceive(filaRepasseTx, &descartado, 0) == pdTRUE) {
            Serial.println("⚠️ [LORA 433] Repasse descartado: rádio fora do ar.");
        }
        return;
    }

    transmitirRepassesPendentes();

    if (digitalRead(LORA_UP_DIO0) != HIGH) return;

    uint8_t buffer_rx[256];
    int estado = loraUplink.readData(buffer_rx, sizeof(buffer_rx));

    if (estado == RADIOLIB_ERR_NONE) {
        size_t tamanho_total = loraUplink.getPacketLength();

        // Mínimo: [DESTINO] [TIPO] [ID_CMD]
        if (tamanho_total >= 3) {
            uint8_t destino = buffer_rx[0];
            uint8_t tipo = buffer_rx[1];

            if (destino != DESTINO_GB) {
                // Pacote de outro nó do canal compartilhado: ignora
                Serial.printf("⚠️ [LORA RX] Pacote ignorado. Destino: 0x%02X\n", destino);
            }
            else if (tipo == TIPO_CMD) {
                ComandoSat cmd;
                cmd.id_comando = buffer_rx[2];
                cmd.tamanho = tamanho_total - 3; // parâmetros após [DESTINO][TIPO][ID]

                // Os parâmetros precisam caber em um frame CAN (8 bytes com o comando)
                if (cmd.tamanho <= 7) {
                    if (cmd.tamanho > 0) memcpy(cmd.dados, &buffer_rx[3], cmd.tamanho);
                    // Espera curta: com o HK sem responder, cada comando ocupa a fila por até
                    // 25 s, e esperar sem limite aqui travaria a recepção (e o watchdog reiniciaria)
                    if (xQueueSend(filaComandosParaHK, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
                        Serial.printf("📥 [LORA RX] Comando 0x%02X recebido (%d bytes de parâmetros).\n",
                                      cmd.id_comando, cmd.tamanho);
                    } else {
                        Serial.printf("⚠️ [LORA RX] Fila de comandos cheia: 0x%02X descartado.\n", cmd.id_comando);
                    }
                } else {
                    Serial.println("⚠️ [LORA RX] Parâmetros excedem o limite de um frame CAN.");
                }
            }
            else if (tipo == TIPO_PLD && tamanho_total >= 2 + 32) {
                // Pacote de repasse da Missão 3 endereçado à Gabriella -> HK (0x160)
                enviarRajadaRepasseCAN(&buffer_rx[2]);
                Serial.println("📥 [LORA RX] Pacote de repasse encaminhado ao HK.");
            }
        } else {
            Serial.println("⚠️ [LORA RX] Pacote corrompido: menos de 3 bytes recebidos.");
        }
    }

    // Religa o rádio para continuar escutando
    loraUplink.startReceive();
}

// ==========================================
// TX: PUXA DA FILA E TRANSMITE PRA TERRA (CORE 1)
// ==========================================
void transmitirTelemetriaLoRa() {
    static LoteTelemetria lote;

    // Espera limitada para a task poder alimentar o watchdog
    if (xQueueReceive(filaTelemetriaParaGS, &lote, pdMS_TO_TICKS(1000)) != pdTRUE) {
        if (!downlinkOk && millis() - ultimaTentativaDownlink >= LORA_RETENTATIVA_MS) downlinkOk = iniciarDownlink();
        return;
    }

    if (!downlinkOk) {
        if (millis() - ultimaTentativaDownlink >= LORA_RETENTATIVA_MS) downlinkOk = iniciarDownlink();
        if (!downlinkOk) {
            Serial.println("⚠️ [LORA 915] Pacote descartado: rádio fora do ar.");
            return;
        }
    }

    uint8_t buffer_tx[2 + MAX_PACOTES_POR_RAJADA * 8];
    int tamanho_lora;

    if (lote.tipo_lora == TIPO_ACK) {
        // Confirmação de telecomando: [0x00][0x0F][código do TC][0xCF | 0xFF | 0xEE]
        buffer_tx[0] = DESTINO_GS;
        buffer_tx[1] = TIPO_ACK;
        buffer_tx[2] = lote.ack_comando;
        buffer_tx[3] = lote.ack_resposta;
        tamanho_lora = 4;
    } else {
        // Rajada: [0x00][0x0D] + 8 bytes por pacote, na ordem em que chegaram do CAN
        buffer_tx[0] = DESTINO_GS;
        buffer_tx[1] = TIPO_TEL;
        int offset = 2;
        for (int i = 0; i < lote.quantidade; i++) {
            memcpy(&buffer_tx[offset], lote.pacotes[i].dados, 8);
            offset += 8;
        }
        tamanho_lora = offset;
    }

    int estado = loraDownlink.transmit(buffer_tx, tamanho_lora);
    if (estado == RADIOLIB_ERR_NONE) {
        Serial.printf("✅ [LORA TX] Downlink entregue (%d bytes).\n", tamanho_lora);
    } else {
        // Reinicia o rádio antes da próxima transmissão
        Serial.printf("❌ [LORA TX] Falha na transmissão de rádio (%d). Reiniciando o SX1276.\n", estado);
        downlinkOk = false;
    }
}
