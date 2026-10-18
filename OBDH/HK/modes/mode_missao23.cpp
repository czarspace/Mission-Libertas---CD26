#include "mode_manager.h"
#include "sd_manager.h"
#include "can_manager.h"
#include "sensores_manager.h"

// Identificadores da constelação (mesmos do protocolo LoRa)
#define ID_GS         0x00
#define ID_GABRIELLA  0x01
#define ID_CAROLINA   0x02
#define ID_BEATRIZ    0x03
#define BIT_GABRIELLA 0x01
#define BIT_CAROLINA  0x02
#define BIT_BEATRIZ   0x04

// ==========================================
// ATIVAÇÃO DO ESP32 DE PAYLOAD (Missões 2 e 3)
// ==========================================
// Chamada pela task de comandos. Se o ESP32 ainda estiver desligado: liga o MOSFET,
// espera o boot e sincroniza o relógio. Depois repassa o comando da missão.
// Retorna true se o ESP32 confirmou tudo.
bool ativarPayloadESP32(uint8_t comando) {
    bool ja_ligado = (modo_secundario == MODE_MISSAO2 || modo_secundario == MODE_MISSAO3);

    if (!ja_ligado) {
        payload_aguardando_desligamento = false; // cancela um desligamento pendente
        digitalWrite(PIN_MOSFET_CARGA_UTIL, HIGH);
        vTaskDelay(pdMS_TO_TICKS(TEMPO_BOOT_ESP32_PLD_MS));

        uint8_t set_time[5];
        uint32_t ts = timestampAtual();
        set_time[0] = 0x00;
        memcpy(&set_time[1], &ts, 4);
        resultado_conf_t r = enviarEAguardar(NO_PLD_ESP32, set_time, 5, TIMEOUT_CONF_MS);
        registrarLog(LOG_HK_GERAL, "PLD", "0x00,%s", textoResultado(r));
        if (r != CONF_ACK) {
            digitalWrite(PIN_MOSFET_CARGA_UTIL, LOW);
            return false;
        }
    }

    resultado_conf_t r = enviarComandoSimplesEAguardar(NO_PLD_ESP32, comando, TIMEOUT_CONF_MS);
    registrarLog(LOG_HK_GERAL, "PLD", "0x%02X,%s", comando, textoResultado(r));
    if (r != CONF_ACK) {
        if (!ja_ligado) digitalWrite(PIN_MOSFET_CARGA_UTIL, LOW);
        return false;
    }
    return true;
}

// ==========================================
// RAJADA DE DISTÂNCIAS (0x150)
// ==========================================
// Pacote 0: timestamp + modo + origem; pacote 1: d(1º outro satélite) + d(2º),
// por ordem de identificador. Chamada pela task de recepção.
void processarRajadaDistancias(const uint8_t* buffer, uint8_t total) {
    if (total < 2) return;

    MedidaDistancias m;
    uint8_t modo = buffer[4];
    m.origem = buffer[5];
    memcpy(&m.d[0], &buffer[8], 4);
    memcpy(&m.d[1], &buffer[12], 4);

    char tag[5];
    snprintf(tag, sizeof(tag), "0x%02X", modo);
    registrarLog(LOG_MODO_BLOCO, tag, "DIST,0x%02X,%.3f,%.3f", m.origem, m.d[0], m.d[1]);

    xQueueOverwrite(filaDistancias, &m); // só interessa a medida mais recente
}

// ==========================================
// REPASSE (Missão 3)
// ==========================================
// Layout do pacote de repasse (Tabela de bursts das Missões 2 e 3):
//  pacote 0: ts(4) | modo 0x32 | origem | remetente | máscara de visitados
//  pacote 1: ICAO(3) | sequência | velocidade (float)
//  pacote 2: latitude | longitude (float)
//  pacote 3: altitude (float)
static void registrarRepasse(const char* sentido, const uint8_t* d, uint8_t destino) {
    uint32_t icao = ((uint32_t)d[8] << 16) | ((uint32_t)d[9] << 8) | d[10];
    float vel, lat, lon, alt;
    memcpy(&vel, &d[12], 4);
    memcpy(&lat, &d[16], 4);
    memcpy(&lon, &d[20], 4);
    memcpy(&alt, &d[24], 4);
    registrarLog(LOG_MODO_BLOCO, "0x32", "%s,0x%02X,0x%02X,0x%02X,0x%02X,%06lX,%u,%.2f,%.6f,%.6f,%.1f",
                 sentido, d[5], d[6], d[7], destino, (unsigned long)icao, d[11], vel, lat, lon, alt);
}

static void processarRepasse(PacoteRepasse* p) {
    uint8_t* d = p->dados;
    registrarRepasse("RELAY_RX", d, ID_GABRIELLA);

    // 1. Marca a Gabriella como visitada. O primeiro satélite da cadeia vira a origem
    // e registra o instante da detecção (a GS envia o timestamp zerado).
    if (d[7] == 0) {
        d[5] = ID_GABRIELLA;
        uint32_t ts = timestampAtual();
        memcpy(&d[0], &ts, 4);
    }
    d[7] |= BIT_GABRIELLA;

    // 2. Pede uma medição ao ESP32 de payload e espera a rajada de distâncias
    MedidaDistancias m;
    xQueueReset(filaDistancias);
    resultado_conf_t r = enviarComandoSimplesEAguardar(NO_PLD_ESP32, 0x33, TIMEOUT_CONF_MS);
    bool tem_medida = (r == CONF_ACK) &&
                      (xQueueReceive(filaDistancias, &m, pdMS_TO_TICKS(TIMEOUT_DISTANCIAS_MS)) == pdTRUE);
    if (!tem_medida) {
        registrarLog(LOG_MODO_BLOCO, "0x32", "MEASURE,%s", textoResultado(r));
    }

    // 3. Próximo salto: satélite mais próximo ainda não visitado; se não houver, a GS.
    // Distâncias da Gabriella: d[0] = Carolina, d[1] = Beatriz (ordem de identificador).
    float dist_c = (tem_medida && !isnan(m.d[0])) ? m.d[0] : INFINITY;
    float dist_b = (tem_medida && !isnan(m.d[1])) ? m.d[1] : INFINITY;
    bool c_livre = !(d[7] & BIT_CAROLINA);
    bool b_livre = !(d[7] & BIT_BEATRIZ);

    uint8_t destino = ID_GS;
    if (c_livre && b_livre)  destino = (dist_b < dist_c) ? ID_BEATRIZ : ID_CAROLINA;
    else if (c_livre)        destino = ID_CAROLINA;
    else if (b_livre)        destino = ID_BEATRIZ;

    // 4. Remetente = Gabriella; envia ao TT&C na faixa 0x170 com o destino no header
    d[6] = ID_GABRIELLA;
    registrarRepasse("RELAY_TX", d, destino);

    LoteTelemetria lote;
    lote.quantidade = 4;
    for (int k = 0; k < 4; k++) {
        lote.pacotes[k].tamanho = 8;
        memcpy(lote.pacotes[k].dados, &d[8 * k], 8);
    }
    enfileirarRajadaRepasse(&lote, destino);
}

// Executada pela task de modo (a cada 100 ms) enquanto a Missão 3 está ativa
void executarModoMissao3() {
    PacoteRepasse p;
    if (xQueueReceive(filaRepasse, &p, 0) == pdTRUE) {
        processarRepasse(&p);
    }
}
