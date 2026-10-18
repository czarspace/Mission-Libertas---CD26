// =====================================================================
// PLACA DE COMUNICAÇÃO DA ESTAÇÃO SOLO — Missão Libertas
//
// Um ESP32 com os dois rádios da GS e FreeRTOS (antes: GS/uplink + GS/downlink):
//   SX1278 433 MHz (VSPI): telecomandos para Gabriella, Carolina e Beatriz e
//                          canal compartilhado das Missões 2 e 3 (recepção)
//   SX1276 915 MHz (HSPI / SPI2): telemetria e confirmações da Gabriella
//
// Tasks:
//   Core 0  RADIO_433 (prio 3)  transmite a fila de comandos e recebe o canal de 433 MHz
//           SERIAL_RX (prio 2)  lê e interpreta os comandos digitados
//   Core 1  RADIO_915 (prio 3)  recebe a telemetria da Gabriella
//           SAIDA     (prio 1)  única task que escreve na Serial
//
// SAÍDA (software oficial da GS, Mission-Libertas---CD26/Ground Station):
//   Cada burst vira uma linha "<timestamp unix> 0x<TM ID> v1 v2 ...", na ordem dos
//   campos de telemetria.cpp. Mande CONFIG para imprimir o config.json de modos.
//   Linhas que começam com "#" são mensagens ao operador (o software as ignora).
//   Códigos próprios da GS: 0xF0 confirmação de TC, 0xF1 RSSI/SNR, 0xF2 Missão 1
//   ativada, 0xF3 confirmação de Carolina/Beatriz, 0xF4 repasse da Missão 3.
//
// COMANDOS (um por linha):
//   [G|C|B] <código> [parâmetros]   G = Gabriella (padrão), C = Carolina, B = Beatriz
//     00 <unix>                     SET_TIME (também acerta o relógio da GS)
//     23 <graus>                    MODE_CTRL
//     26 <modo> <Kp> <Ki> <Kd>      SET_PID (modo 20, 22 ou 23)
//     sem o parâmetro, a GS pede numa segunda linha (como no antigo uplink.ino)
//   R <G|C|B> <icao_hex> <seq> <vel> <lat> <lon> <alt>   detecção simulada (Missão 3)
//   HORA <unix>   acerta só o relógio da GS     V        liga/desliga o modo verboso
//   CONFIG        imprime o config.json         MENU     lista os comandos
// =====================================================================

#include <RadioLib.h>
#include <SPI.h>
#include <sys/time.h>
#include "config.h"
#include "telemetria.h"

QueueHandle_t filaSaida;
QueueHandle_t filaTx433;

SPIClass spi433(VSPI);
SPIClass spi915(HSPI);
Module mod433(L433_CS, L433_DIO0, L433_RST, L433_DIO1, spi433);
Module mod915(L915_CS, L915_DIO0, L915_RST, L915_DIO1, spi915);
SX1278 lora433(&mod433);
SX1276 lora915(&mod915);

static bool ok433 = false, ok915 = false;
static uint32_t tentativa433 = 0, tentativa915 = 0;

// ==========================================
// TABELA DE TELECOMANDOS
// ==========================================
// Tem que bater com HK/comandos/comandos.cpp e, para Carolina/Beatriz, com
// SateliteComplementar/modes/mode_manager.cpp.
enum TipoParametro : uint8_t { SEM_PARAM, PARAM_TIMESTAMP, PARAM_ANGULO, PARAM_PID };

struct ComandoMenu {
    const char* codigo;  // como o operador digita, em hex, sem "0x"
    uint8_t id;
    const char* descricao;
    TipoParametro parametro;
};

const ComandoMenu MENU[] = {
    {"00", 0x00, "SET_TIME (sincroniza o relogio do satelite)", PARAM_TIMESTAMP},
    {"01", 0x01, "MODE_IDLE",                                    SEM_PARAM},
    {"10", 0x10, "MODE_CARGA",                                   SEM_PARAM},
    {"12", 0x12, "MODE_ABERTURA (abre os paineis solares)",      SEM_PARAM},
    {"13", 0x13, "MODE_TERM (ciclagem termica)",                 SEM_PARAM},
    {"14", 0x14, "MODE_TP1 (teste parcial 1)",                   SEM_PARAM},
    {"16", 0x16, "MODE_TP2 (teste parcial 2)",                   SEM_PARAM},
    {"15", 0x15, "MODE_TI (teste integral)",                     SEM_PARAM},
    {"20", 0x20, "MODE_STAB (estabilizacao B-dot)",              SEM_PARAM},
    {"21", 0x21, "MODE_DET (determinacao solar)",                SEM_PARAM},
    {"22", 0x22, "MODE_APO (apontamento solar fixo)",            SEM_PARAM},
    {"23", 0x23, "MODE_CTRL <graus>",                            PARAM_ANGULO},
    {"26", 0x26, "SET_PID <modo 20|22|23> <Kp> <Ki> <Kd>",       PARAM_PID},
    {"30", 0x30, "MODE_MISSAO1 (payload RPi)",                   SEM_PARAM},
    {"31", 0x31, "MODE_MISSAO2 (formacao)",                      SEM_PARAM},
    {"32", 0x32, "MODE_MISSAO3 (repasse)",                       SEM_PARAM},
    {"33", 0x33, "MEASURE (so Carolina/Beatriz, Missao 2)",      SEM_PARAM},
    {"b0", 0xB0, "STOP ADCS (IDLE sem desligar o MOSFET do ADCS)", SEM_PARAM},
    {"c0", 0xC0, "STOP Payload (aborta a missao sem desligar o MOSFET)", SEM_PARAM},
};
const int NUM_COMANDOS = sizeof(MENU) / sizeof(MENU[0]);

const char* nomeNo(uint8_t id) {
    switch (id) {
        case DESTINO_GS: return "GS";
        case DESTINO_GB: return "Gabriella";
        case DESTINO_CR: return "Carolina";
        case DESTINO_BT: return "Beatriz";
        default:         return "?";
    }
}

int destinoPorLetra(const char* s) {
    if (strcasecmp(s, "G") == 0) return DESTINO_GB;
    if (strcasecmp(s, "C") == 0) return DESTINO_CR;
    if (strcasecmp(s, "B") == 0) return DESTINO_BT;
    return -1;
}

void imprimirMenu() {
    info("===== Telecomandos =====");
    info("Formato: [G|C|B] <codigo> [parametros]  (G = Gabriella, padrao; C = Carolina; B = Beatriz)");
    for (int i = 0; i < NUM_COMANDOS; i++) info("  %-2s -> %s", MENU[i].codigo, MENU[i].descricao);
    info("Repasse (Missao 3): R <G|C|B> <icao_hex> <seq> <vel> <lat> <lon> <alt>");
    info("GS: HORA <unix> | V (verboso %s) | CONFIG | MENU", modoVerboso ? "ligado" : "desligado");
}

// ==========================================
// FLOAT DE 16 BITS (ganhos do SET_PID)
// ==========================================
// Os ganhos viajam em meia precisão para caber modo + Kp + Ki + Kd em um frame
// CAN de 8 bytes (~3 algarismos significativos).
uint16_t floatParaF16(float f) {
    uint32_t x;
    memcpy(&x, &f, 4);
    uint16_t sinal = (x >> 16) & 0x8000;
    int32_t expoente = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mantissa = x & 0x7FFFFF;
    if (((x >> 23) & 0xFF) == 0xFF) return sinal | 0x7C00 | (mantissa ? 0x200 : 0);
    if (expoente >= 31) return sinal | 0x7C00;
    if (expoente <= 0) {
        if (expoente < -10) return sinal;
        mantissa |= 0x800000;
        uint32_t desloc = 14 - expoente;
        uint16_t r = mantissa >> desloc;
        if ((mantissa >> (desloc - 1)) & 1) r++;
        return sinal | r;
    }
    uint16_t r = sinal | (expoente << 10) | (mantissa >> 13);
    if (mantissa & 0x1000) r++;
    return r;
}

float f16ParaFloat(uint16_t h) {
    uint16_t s = h >> 15, e = (h >> 10) & 0x1F, m = h & 0x3FF;
    float v;
    if (e == 0)       v = ldexpf((float)m, -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else              v = ldexpf((float)(m | 0x400), e - 25);
    return s ? -v : v;
}

// ==========================================
// ENVIO (433 MHz)
// ==========================================
// Coloca [destino][tipo][dados] na fila; a task do rádio de 433 MHz transmite.
bool enfileirarTx(uint8_t destino, uint8_t tipo, const uint8_t* dados, uint8_t tamanho) {
    PacoteTx p;
    p.tamanho = 2 + tamanho;
    p.dados[0] = destino;
    p.dados[1] = tipo;
    if (tamanho) memcpy(&p.dados[2], dados, tamanho);
    if (xQueueSend(filaTx433, &p, pdMS_TO_TICKS(100)) != pdTRUE) {
        info("[UPLINK] Fila de transmissao cheia: pacote descartado.");
        return false;
    }
    return true;
}

// [destino][TIPO_CMD][id][params...]; a Gabriella aceita no máximo 7 bytes de parâmetro
void enviarComando(uint8_t destino, uint8_t id, const uint8_t* params, uint8_t tamParams) {
    if (tamParams > 7) { info("[UPLINK] Parametro grande demais (max 7 bytes)."); return; }
    uint8_t d[8];
    d[0] = id;
    if (tamParams) memcpy(&d[1], params, tamParams);
    if (enfileirarTx(destino, TIPO_CMD, d, 1 + tamParams)) {
        info("[UPLINK] Comando 0x%02X para %s (%d bytes de parametro).", id, nomeNo(destino), tamParams);
    }
}

void acertarRelogioGS(uint32_t unix) {
    struct timeval tv = { (time_t)unix, 0 };
    settimeofday(&tv, NULL);
}

void enviarSetTime(uint8_t destino, const char* texto) {
    char* fim;
    unsigned long unix = strtoul(texto, &fim, 10);
    if (fim == texto || unix < 1000000000UL) { info("[UPLINK] Timestamp invalido: SET_TIME cancelado."); return; }
    uint32_t t = (uint32_t)unix;
    acertarRelogioGS(t); // as linhas geradas pela GS passam a usar o mesmo relógio do satélite
    enviarComando(destino, 0x00, (uint8_t*)&t, 4);
}

void enviarCtrl(uint8_t destino, const char* texto) {
    char* fim;
    float angulo = strtof(texto, &fim);
    if (fim == texto) { info("[UPLINK] Angulo invalido: MODE_CTRL cancelado."); return; }
    enviarComando(destino, 0x23, (uint8_t*)&angulo, 4);
}

// "<modo> <Kp> <Ki> <Kd>", ex.: "22 0.022 0.001 0.003"
void enviarSetPid(uint8_t destino, const char* texto) {
    unsigned int modo;
    float kp, ki, kd;
    if (sscanf(texto, "%x %f %f %f", &modo, &kp, &ki, &kd) != 4 ||
        (modo != 0x20 && modo != 0x22 && modo != 0x23) || kp < 0 || ki < 0 || kd < 0) {
        info("[UPLINK] Uso: 26 <modo 20|22|23> <Kp> <Ki> <Kd> (ganhos >= 0). SET_PID cancelado.");
        return;
    }
    uint16_t h[3] = { floatParaF16(kp), floatParaF16(ki), floatParaF16(kd) };
    uint8_t params[7];
    params[0] = (uint8_t)modo;
    memcpy(&params[1], h, 6);
    info("[UPLINK] SET_PID modo 0x%02X: Kp = %.6f | Ki = %.6f | Kd = %.6f (apos a conversao)",
         modo, f16ParaFloat(h[0]), f16ParaFloat(h[1]), f16ParaFloat(h[2]));
    enviarComando(destino, 0x26, params, 7);
}

// Detecção simulada da Missão 3: pacote de repasse com remetente GS e nenhum
// satélite visitado (o primeiro satélite registra o timestamp).
void enviarRepasse(uint8_t destino, uint32_t icao, uint8_t seq, float vel, float lat, float lon, float alt) {
    uint8_t d[32] = {0};
    d[4] = 0x32;        // modo
    d[5] = 0x00;        // origem (preenchida pelo primeiro satélite)
    d[6] = DESTINO_GS;  // remetente deste salto
    d[7] = 0x00;        // máscara de visitados
    d[8] = (icao >> 16) & 0xFF;
    d[9] = (icao >> 8) & 0xFF;
    d[10] = icao & 0xFF;
    d[11] = seq;
    memcpy(&d[12], &vel, 4);
    memcpy(&d[16], &lat, 4);
    memcpy(&d[20], &lon, 4);
    memcpy(&d[24], &alt, 4);
    if (enfileirarTx(destino, TIPO_PLD, d, sizeof(d))) {
        info("[UPLINK] Deteccao simulada enviada para %s.", nomeNo(destino));
    }
}

// ==========================================
// INTERPRETAÇÃO DOS COMANDOS
// ==========================================
// Parâmetro pedido numa segunda linha (fluxo do antigo uplink.ino e do painel de testes)
static struct {
    TipoParametro tipo = SEM_PARAM;
    uint8_t destino = DESTINO_GB;
    uint32_t prazo = 0;
} pendente;

static void pedirParametro(TipoParametro tipo, uint8_t destino) {
    pendente.tipo = tipo;
    pendente.destino = destino;
    pendente.prazo = millis() + 15000;
    if (tipo == PARAM_TIMESTAMP) info("Digite o timestamp UNIX em segundos:");
    else if (tipo == PARAM_ANGULO) info("Digite o angulo alvo em graus (ex: 90.0):");
    else info("Digite o modo (20, 22 ou 23) e os ganhos Kp Ki Kd (ex: 22 0.022 0.001 0.003):");
}

static void executarComParametro(TipoParametro tipo, uint8_t destino, const char* texto) {
    if (tipo == PARAM_TIMESTAMP) enviarSetTime(destino, texto);
    else if (tipo == PARAM_ANGULO) enviarCtrl(destino, texto);
    else if (tipo == PARAM_PID) enviarSetPid(destino, texto);
}

void processarLinha(char* linha) {
    // Resposta a um parâmetro pedido
    if (pendente.tipo != SEM_PARAM) {
        TipoParametro tipo = pendente.tipo;
        pendente.tipo = SEM_PARAM;
        if ((int32_t)(millis() - pendente.prazo) < 0) {
            executarComParametro(tipo, pendente.destino, linha);
            return;
        }
        info("Tempo esgotado esperando o parametro; comando anterior cancelado.");
    }

    // Separa em tokens (o texto original fica em 'resto' para os parâmetros)
    char copia[128];
    strncpy(copia, linha, sizeof(copia) - 1);
    copia[sizeof(copia) - 1] = '\0';
    char* tokens[10];
    int n = 0;
    for (char* t = strtok(copia, " \t"); t && n < 10; t = strtok(NULL, " \t")) tokens[n++] = t;
    if (n == 0) return;

    if (strcasecmp(tokens[0], "MENU") == 0) { imprimirMenu(); return; }
    if (strcasecmp(tokens[0], "CONFIG") == 0) { imprimirConfigGS(); return; }
    if (strcasecmp(tokens[0], "V") == 0) {
        modoVerboso = !modoVerboso;
        info("Modo verboso %s.", modoVerboso ? "ligado" : "desligado");
        return;
    }
    if (strcasecmp(tokens[0], "HORA") == 0) {
        unsigned long unix = (n >= 2) ? strtoul(tokens[1], NULL, 10) : 0;
        if (unix < 1000000000UL) { info("Uso: HORA <unix em segundos>"); return; }
        acertarRelogioGS((uint32_t)unix);
        info("Relogio da GS acertado: %lu", unix);
        return;
    }

    // Repasse da Missão 3
    if (strcasecmp(tokens[0], "R") == 0) {
        int dest = (n >= 2) ? destinoPorLetra(tokens[1]) : -1;
        if (dest < 0 || n < 8) { info("Uso: R <G|C|B> <icao_hex> <seq> <vel> <lat> <lon> <alt>"); return; }
        enviarRepasse((uint8_t)dest, strtoul(tokens[2], NULL, 16), (uint8_t)atoi(tokens[3]), atof(tokens[4]),
                      atof(tokens[5]), atof(tokens[6]), atof(tokens[7]));
        return;
    }

    // [destino] código [parâmetros]
    uint8_t destino = DESTINO_GB;
    int idx = 0;
    int d = destinoPorLetra(tokens[0]);
    if (d >= 0) {
        destino = (uint8_t)d;
        idx = 1;
        if (n < 2) { info("Falta o codigo do comando."); return; }
    }

    for (int i = 0; i < NUM_COMANDOS; i++) {
        if (strcasecmp(tokens[idx], MENU[i].codigo) != 0) continue;

        if (MENU[i].parametro == SEM_PARAM) {
            enviarComando(destino, MENU[i].id, nullptr, 0);
        } else if (n > idx + 1) {
            // Parâmetros na mesma linha: o texto a partir do token seguinte ao código
            const char* params = linha + (tokens[idx + 1] - copia);
            executarComParametro(MENU[i].parametro, destino, params);
        } else {
            pedirParametro(MENU[i].parametro, destino);
        }
        return;
    }
    info("Comando \"%s\" nao reconhecido. Digite MENU para ver a lista.", linha);
}

// ==========================================
// RÁDIOS
// ==========================================
bool iniciar433() {
    tentativa433 = millis();
    int e = lora433.begin(FREQ_433, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD, LORA_POTENCIA_DBM, LORA_PREAMBULO, 0);
    if (e != RADIOLIB_ERR_NONE) { info("[433] Falha ao iniciar o SX1278 (%d). Nova tentativa em 5 s.", e); return false; }
    lora433.startReceive();
    info("[433] SX1278 pronto (recepcao por padrao).");
    return true;
}

bool iniciar915() {
    tentativa915 = millis();
    int e = lora915.begin(FREQ_915, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD, LORA_POTENCIA_DBM, LORA_PREAMBULO, 0);
    if (e != RADIOLIB_ERR_NONE) { info("[915] Falha ao iniciar o SX1276 (%d). Nova tentativa em 5 s.", e); return false; }
    lora915.startReceive();
    info("[915] SX1276 pronto (recepcao).");
    return true;
}

// ==========================================
// TASKS
// ==========================================
// Core 0: único dono do SX1278. Transmite o que estiver na fila e volta a receber.
void TaskRadio433(void* pv) {
    PacoteTx p;
    static uint8_t buf[256];
    for (;;) {
        if (!ok433) {
            if (millis() - tentativa433 >= LORA_RETENTATIVA_MS) ok433 = iniciar433();
            while (xQueueReceive(filaTx433, &p, 0) == pdTRUE) info("[433] Radio fora do ar: pacote descartado.");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        while (xQueueReceive(filaTx433, &p, 0) == pdTRUE) {
            int e = lora433.transmit(p.dados, p.tamanho);
            if (e == RADIOLIB_ERR_NONE) info("[433] Transmitido para %s (%u bytes).", nomeNo(p.dados[0]), p.tamanho);
            else { info("[433] Erro na transmissao (%d). Reiniciando o radio.", e); ok433 = false; }
            lora433.startReceive();
        }

        if (ok433 && digitalRead(L433_DIO0) == HIGH) {
            int e = lora433.readData(buf, sizeof(buf));
            int tam = lora433.getPacketLength();
            float rssi = lora433.getRSSI(), snr = lora433.getSNR();
            lora433.startReceive();
            if (e == RADIOLIB_ERR_NONE) decodificarPacote(buf, tam, rssi, snr, 433);
            else if (modoVerboso) info("[433] Pacote com erro (%d) | RSSI %.1f dBm | SNR %.2f dB", e, rssi, snr);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// Core 1: único dono do SX1276 (só recepção)
void TaskRadio915(void* pv) {
    static uint8_t buf[256];
    for (;;) {
        if (!ok915) {
            if (millis() - tentativa915 >= LORA_RETENTATIVA_MS) ok915 = iniciar915();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (digitalRead(L915_DIO0) == HIGH) {
            int e = lora915.readData(buf, sizeof(buf));
            int tam = lora915.getPacketLength();
            float rssi = lora915.getRSSI(), snr = lora915.getSNR();
            lora915.startReceive();
            if (e == RADIOLIB_ERR_NONE) decodificarPacote(buf, tam, rssi, snr, 915);
            else info("[915] Pacote com erro (%d) | RSSI %.1f dBm | SNR %.2f dB", e, rssi, snr);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// Core 0: lê a serial linha a linha
void TaskSerialRx(void* pv) {
    static char linha[128];
    uint8_t tam = 0;
    for (;;) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (tam) {
                    linha[tam] = '\0';
                    processarLinha(linha);
                    tam = 0;
                }
            } else if (tam < sizeof(linha) - 1) {
                linha[tam++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Core 1: única task que escreve na Serial (as linhas das outras tasks não se misturam)
void TaskSaida(void* pv) {
    LinhaSaida l;
    for (;;) {
        if (xQueueReceive(filaSaida, &l, portMAX_DELAY) == pdTRUE) Serial.println(l.texto);
    }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);

    filaSaida = xQueueCreate(TAM_FILA_SAIDA, sizeof(LinhaSaida));
    filaTx433 = xQueueCreate(8, sizeof(PacoteTx));
    if (!filaSaida || !filaTx433) {
        Serial.println("# ERRO CRITICO: falha ao criar as filas.");
        while (true) delay(1000);
    }

    xTaskCreatePinnedToCore(TaskSaida, "SAIDA", 4096, NULL, 1, NULL, 1);
    info("Placa de comunicacao da GS (Missao Libertas) iniciando...");

    spi433.begin(L433_SCK, L433_MISO, L433_MOSI, L433_CS);
    spi915.begin(L915_SCK, L915_MISO, L915_MOSI, L915_CS);
    ok433 = iniciar433();
    ok915 = iniciar915();

    xTaskCreatePinnedToCore(TaskRadio433, "RADIO_433", 6144, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(TaskSerialRx, "SERIAL_RX", 6144, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskRadio915, "RADIO_915", 6144, NULL, 3, NULL, 1);

    imprimirMenu();
}

void loop() {
    vTaskDelete(NULL);
}
