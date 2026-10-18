#include "telemetria.h"
#include <stdarg.h>
#include <time.h>

volatile bool modoVerboso = false;

// ==========================================
// SAÍDA
// ==========================================
static void enfileirar(const char* prefixo, const char* fmt, va_list args) {
    LinhaSaida l;
    int n = snprintf(l.texto, TAM_LINHA, "%s", prefixo);
    vsnprintf(&l.texto[n], TAM_LINHA - n, fmt, args);
    xQueueSend(filaSaida, &l, pdMS_TO_TICKS(50));
}

void saida(const char* fmt, ...) {
    va_list a; va_start(a, fmt); enfileirar("", fmt, a); va_end(a);
}

void info(const char* fmt, ...) {
    va_list a; va_start(a, fmt); enfileirar("# ", fmt, a); va_end(a);
}

// ==========================================
// LAYOUT DE CADA TM ID
// ==========================================
// Cada campo: nome (o mesmo dataType a configurar no software oficial), pacote do
// burst, byte dentro do pacote (passo fixo de 8 bytes) e tipo. Pacote 0 = timestamp
// (bytes 0-3) + modo (byte 4) [+ origem (byte 5) nas Missões 2 e 3].
// Tem que bater com os bursts de HK/modes, ADCS/modes e da RPi.
enum TipoCampo : uint8_t { F32, U8, U16, U32, U24BE };

typedef struct { const char* nome; uint8_t pacote; uint8_t pos; TipoCampo tipo; } Campo;
typedef struct { uint8_t codigo; const char* nome; const Campo* campos; uint8_t n; } Layout;

#define N_CAMPOS(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

static const Campo C_IDLE[] = {
    {"T_bateria", 1, 0, F32}, {"T_interna", 1, 4, F32}, {"T_externa", 2, 0, F32}, {"V_bateria", 2, 4, F32},
    {"I_bateria", 3, 0, F32}, {"Modo_ativo", 4, 0, U8}, {"Estado", 4, 1, U8}, {"Falhas", 4, 2, U8},
    {"DS18B20_validos", 4, 3, U16}, {"Motivo_reset", 4, 5, U8}, {"Boots", 4, 6, U16},
};
static const Campo C_CARGA[] = {
    {"V_bateria", 1, 0, F32}, {"I_bateria", 1, 4, F32}, {"V_painel_F1", 2, 0, F32}, {"I_painel_F1", 2, 4, F32},
    {"V_painel_F2", 3, 0, F32}, {"I_painel_F2", 3, 4, F32}, {"V_painel_F4", 4, 0, F32}, {"I_painel_F4", 4, 4, F32},
    {"V_carregador", 5, 0, F32}, {"I_carregador", 5, 4, F32}, {"V_consumo", 6, 0, F32}, {"I_consumo", 6, 4, F32},
};
static const Campo C_ABERTURA[] = { {"Evento", 1, 0, U8} };
static const Campo C_TERM[] = {
    {"V_bateria", 1, 0, F32}, {"T_BAT1", 1, 4, F32}, {"T_BAT2", 2, 0, F32}, {"T_BAT3", 2, 4, F32},
    {"T_AQ1", 3, 0, F32}, {"T_AQ2", 3, 4, F32}, {"T_AQ3", 4, 0, F32}, {"T_interna", 4, 4, F32},
    {"T_externa", 5, 0, F32}, {"T_aux1", 5, 4, F32}, {"T_aux2", 6, 0, F32}, {"Duty_aquecedor", 6, 4, F32},
    {"Estado_aquecedor", 7, 0, U8}, {"Flags_aquecedor", 7, 1, U8}, {"DS18B20_validos", 7, 2, U16},
    {"T_controle", 7, 4, F32},
};
static const Campo C_TP[] = {
    {"T_BAT1", 1, 0, F32}, {"T_BAT2", 1, 4, F32}, {"T_BAT3", 2, 0, F32}, {"T_AQ1", 2, 4, F32},
    {"T_AQ2", 3, 0, F32}, {"T_AQ3", 3, 4, F32}, {"T_interna", 4, 0, F32}, {"T_externa", 4, 4, F32},
    {"T_aux1", 5, 0, F32}, {"T_aux2", 5, 4, F32},
    {"V_0x40_ch0", 6, 0, F32}, {"V_0x40_ch1", 6, 4, F32}, {"V_0x40_ch2", 7, 0, F32},
    {"I_0x40_ch0", 7, 4, F32}, {"I_0x40_ch1", 8, 0, F32}, {"I_0x40_ch2", 8, 4, F32},
    {"V_0x41_ch0", 9, 0, F32}, {"V_0x41_ch1", 9, 4, F32}, {"V_0x41_ch2", 10, 0, F32},
    {"I_0x41_ch0", 10, 4, F32}, {"I_0x41_ch1", 11, 0, F32}, {"I_0x41_ch2", 11, 4, F32},
};
static const Campo C_STAB[] = { {"Gyro_z", 1, 0, F32}, {"Comando_roda", 1, 4, F32}, {"Vel_motor", 2, 0, F32} };
static const Campo C_DET[] = {
    {"Theta", 1, 0, F32}, {"Lux_V1", 1, 4, F32}, {"Lux_V2", 2, 0, F32}, {"Lux_V3", 2, 4, F32}, {"Lux_V4", 3, 0, F32},
};
static const Campo C_APO[] = {
    {"Gyro_z", 1, 0, F32}, {"Theta", 1, 4, F32}, {"Vel_motor", 2, 0, F32}, {"Lux_V1", 2, 4, F32},
    {"Lux_V2", 3, 0, F32}, {"Lux_V3", 3, 4, F32}, {"Lux_V4", 4, 0, F32},
};
static const Campo C_MOTOR[] = { {"Vel_motor", 1, 0, F32} };
static const Campo C_SENSORES[] = {
    {"Gyro_z", 1, 0, F32}, {"Lux_V1", 1, 4, F32}, {"Lux_V2", 2, 0, F32}, {"Lux_V3", 2, 4, F32}, {"Lux_V4", 3, 0, F32},
};
static const Campo C_M1[] = {
    {"ICAO", 1, 0, U32}, {"Altitude", 1, 4, F32}, {"Longitude", 2, 0, F32}, {"Latitude", 2, 4, F32},
    {"Velocidade", 3, 0, F32},
};
static const Campo C_DIST[] = { {"Origem", 0, 5, U8}, {"D1", 1, 0, F32}, {"D2", 1, 4, F32} };

// Códigos próprios da GS (os dados vêm de um buffer montado aqui, no mesmo formato)
static const Campo C_TC[] = { {"Comando", 0, 0, U8}, {"Resposta", 0, 1, U8} };
static const Campo C_ENLACE[] = { {"RSSI", 0, 0, F32}, {"SNR", 0, 4, F32}, {"Bytes", 1, 0, U16}, {"Frequencia", 1, 2, U16} };
static const Campo C_M1_ATIVA[] = { {"Modo", 0, 4, U8} };
static const Campo C_CONF_CB[] = { {"Origem", 0, 5, U8}, {"Modo", 0, 4, U8} };
static const Campo C_REPASSE[] = {
    {"Origem", 0, 5, U8}, {"Remetente", 0, 6, U8}, {"Visitados", 0, 7, U8}, {"ICAO", 1, 0, U24BE},
    {"Sequencia", 1, 3, U8}, {"Velocidade", 1, 4, F32}, {"Latitude", 2, 0, F32}, {"Longitude", 2, 4, F32},
    {"Altitude", 3, 0, F32},
};

static const Layout LAYOUTS[] = {
    {0x01, "IDLE (housekeeping)",       C_IDLE,     N_CAMPOS(C_IDLE)},
    {0x10, "CARGA",                     C_CARGA,    N_CAMPOS(C_CARGA)},
    {0x12, "ABERTURA",                  C_ABERTURA, N_CAMPOS(C_ABERTURA)},
    {0x13, "TERM",                      C_TERM,     N_CAMPOS(C_TERM)},
    {0x14, "TP1",                       C_TP,       N_CAMPOS(C_TP)},
    {0x15, "TI",                        C_TP,       N_CAMPOS(C_TP)},
    {0x16, "TP2",                       C_TP,       N_CAMPOS(C_TP)},
    {0x20, "STAB",                      C_STAB,     N_CAMPOS(C_STAB)},
    {0x21, "DET",                       C_DET,      N_CAMPOS(C_DET)},
    {0x22, "APO",                       C_APO,      N_CAMPOS(C_APO)},
    {0x23, "CTRL",                      C_APO,      N_CAMPOS(C_APO)},
    {0x24, "Motor (testes)",            C_MOTOR,    N_CAMPOS(C_MOTOR)},
    {0x25, "Sensores ADCS (testes)",    C_SENSORES, N_CAMPOS(C_SENSORES)},
    {0x30, "Missao 1 (aeronave)",       C_M1,       N_CAMPOS(C_M1)},
    {0x31, "Distancias M2",             C_DIST,     N_CAMPOS(C_DIST)},
    {0x32, "Distancias M3",             C_DIST,     N_CAMPOS(C_DIST)},
    {COD_CONFIRMACAO_TC, "Confirmacao de TC",   C_TC,        N_CAMPOS(C_TC)},
    {COD_ENLACE,         "Enlace RF",           C_ENLACE,    N_CAMPOS(C_ENLACE)},
    {COD_M1_ATIVA,       "Missao 1 ativada",    C_M1_ATIVA,  N_CAMPOS(C_M1_ATIVA)},
    {COD_CONFIRMACAO_CB, "Confirmacao C/B",     C_CONF_CB,   N_CAMPOS(C_CONF_CB)},
    {COD_REPASSE,        "Repasse M3",          C_REPASSE,   N_CAMPOS(C_REPASSE)},
};
static const uint8_t N_LAYOUTS = sizeof(LAYOUTS) / sizeof(LAYOUTS[0]);

static const Layout* layoutDe(uint8_t codigo) {
    for (uint8_t i = 0; i < N_LAYOUTS; i++) if (LAYOUTS[i].codigo == codigo) return &LAYOUTS[i];
    return nullptr;
}

// Valor de um campo; NAN se o pacote não veio (burst mais curto que o layout)
static double lerCampo(const uint8_t* d, uint8_t nPacotes, const Campo& c) {
    if (c.pacote >= nPacotes) return NAN;
    const uint8_t* p = d + c.pacote * 8 + c.pos;
    switch (c.tipo) {
        case F32:   { float f; memcpy(&f, p, 4); return f; }
        case U8:    return p[0];
        case U16:   { uint16_t v; memcpy(&v, p, 2); return v; }
        case U32:   { uint32_t v; memcpy(&v, p, 4); return v; }
        case U24BE: return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    }
    return NAN;
}

// ==========================================
// NOMES PARA O MODO VERBOSO (mesmos bits do HK)
// ==========================================
static const char* const ESTADO_HK[8] = { "sincronizado", "HK ocupado", "MOSFET payload", "MOSFET ADCS",
                                          "MOSFET EPS", "aquecedor ligado", "mapa DS18B20 provisorio", "desligando payload" };
static const char* const FALHAS_HK[8] = { "cartao SD", "INA3221 0x40", "INA3221 0x41", "DS18B20 invalido",
                                          "sem sensor de bateria", "ADCS sem resposta", "RPi sem resposta", "ESP32 payload sem resposta" };
static const char* const ESTADO_AQ[7] = { "DESLIGADO", "PI", "FRIO", "CORTE_BATERIA", "CORTE_AQUECEDOR", "FALHA_SENSORES", "FORCADO" };
static const char* const NOMES_NO[4] = { "GS", "Gabriella", "Carolina", "Beatriz" };

static void bitsLigados(char* s, size_t tam, uint8_t mascara, const char* const nomes[8]) {
    size_t n = 0;
    s[0] = '\0';
    for (int i = 0; i < 8; i++) {
        if (mascara & (1 << i)) n += snprintf(s + n, tam - n, "%s%s", n ? ", " : "", nomes[i]);
        if (n >= tam) break;
    }
    if (!n) snprintf(s, tam, "nenhum");
}

// ==========================================
// EMISSÃO
// ==========================================
// Linha do software oficial: "<timestamp> 0x<código> v1 v2 ..." (todos os campos do
// layout, nan para os ausentes) e, no modo verboso, a mesma informação legível.
static void emitirLayout(uint32_t ts, const Layout* l, const uint8_t* d, uint8_t nPacotes) {
    LinhaSaida linha;
    int n = snprintf(linha.texto, TAM_LINHA, "%lu 0x%02X", (unsigned long)ts, l->codigo);
    for (uint8_t i = 0; i < l->n && n < TAM_LINHA - 1; i++) {
        double v = lerCampo(d, nPacotes, l->campos[i]);
        if (isnan(v))                    n += snprintf(&linha.texto[n], TAM_LINHA - n, " nan");
        else if (l->campos[i].tipo == F32) n += snprintf(&linha.texto[n], TAM_LINHA - n, " %.7g", v);
        else                             n += snprintf(&linha.texto[n], TAM_LINHA - n, " %lu", (unsigned long)v);
    }
    xQueueSend(filaSaida, &linha, pdMS_TO_TICKS(50));

    if (!modoVerboso) return;
    n = snprintf(linha.texto, TAM_LINHA, "# [0x%02X %s]", l->codigo, l->nome);
    for (uint8_t i = 0; i < l->n && n < TAM_LINHA - 1; i++) {
        double v = lerCampo(d, nPacotes, l->campos[i]);
        if (l->campos[i].tipo == F32) n += snprintf(&linha.texto[n], TAM_LINHA - n, " %s=%.4g", l->campos[i].nome, v);
        else if (isnan(v))            n += snprintf(&linha.texto[n], TAM_LINHA - n, " %s=nan", l->campos[i].nome);
        else                          n += snprintf(&linha.texto[n], TAM_LINHA - n, " %s=%lu", l->campos[i].nome, (unsigned long)v);
    }
    xQueueSend(filaSaida, &linha, pdMS_TO_TICKS(50));

    // Campos de bits do HK, por extenso
    char s[200];
    if (l->codigo == 0x01 && nPacotes >= 5) {
        bitsLigados(s, sizeof(s), d[4 * 8 + 1], ESTADO_HK); info("  estado: %s", s);
        bitsLigados(s, sizeof(s), d[4 * 8 + 2], FALHAS_HK); info("  falhas: %s", s);
    } else if (l->codigo == 0x13 && nPacotes >= 8) {
        uint8_t e = d[7 * 8];
        info("  aquecedor: %s", e < 7 ? ESTADO_AQ[e] : "?");
    }
}

static uint32_t agoraGS() { return (uint32_t)time(nullptr); }

void decodificarPacote(const uint8_t* buf, int tamanho, float rssi, float snr, uint16_t freqMHz) {
    if (tamanho < 2 || buf[0] != DESTINO_GS) {
        if (modoVerboso) info("[%u MHz] Pacote ignorado (destino 0x%02X).", freqMHz, tamanho ? buf[0] : 0);
        return;
    }

    // Qualidade do enlace de cada pacote (Communication Test)
    uint8_t enlace[16] = {0};
    uint16_t bytes = tamanho;
    memcpy(&enlace[0], &rssi, 4);
    memcpy(&enlace[4], &snr, 4);
    memcpy(&enlace[8], &bytes, 2);
    memcpy(&enlace[10], &freqMHz, 2);
    emitirLayout(agoraGS(), layoutDe(COD_ENLACE), enlace, 2);

    uint8_t tipo = buf[1];
    const uint8_t* d = buf + 2;
    uint8_t nPacotes = (tamanho - 2) / 8;

    if (tipo == TIPO_ACK && tamanho >= 4) {
        // Confirmação de telecomando da Gabriella: [0x00][0x0F][código][0xCF | 0xFF | 0xEE]
        uint8_t conf[8] = { buf[2], buf[3] };
        emitirLayout(agoraGS(), layoutDe(COD_CONFIRMACAO_TC), conf, 1);
        const char* texto = (buf[3] == 0xCF) ? "ACK (executado)" : (buf[3] == 0xFF) ? "INV (rejeitado)" :
                            (buf[3] == 0xEE) ? "SEM RESPOSTA do HK" : "?";
        info("Telecomando 0x%02X: %s", buf[2], texto);
        return;
    }

    if (tipo == TIPO_PLD && tamanho >= 2 + 32) {
        // Repasse da Missão 3 que voltou à GS no fim da cadeia
        uint32_t ts; memcpy(&ts, d, 4);
        emitirLayout(ts, layoutDe(COD_REPASSE), d, 4);
        info("Repasse recebido: origem %s, último salto %s, visitados 0x%02X",
             d[5] < 4 ? NOMES_NO[d[5]] : "?", d[6] < 4 ? NOMES_NO[d[6]] : "?", d[7]);
        return;
    }

    if (tipo != TIPO_TEL || nPacotes < 1) {
        if (modoVerboso) info("[%u MHz] Pacote de tipo 0x%02X ignorado.", freqMHz, tipo);
        return;
    }

    uint32_t ts; memcpy(&ts, d, 4);
    uint8_t modo = d[4];
    const Layout* l;
    if (modo == 0x30 && nPacotes == 1)        l = layoutDe(COD_M1_ATIVA);       // ativação da Missão 1
    else if (freqMHz == 433 && nPacotes == 1) l = layoutDe(COD_CONFIRMACAO_CB); // confirmação de C/B
    else                                      l = layoutDe(modo);

    if (!l) {
        info("Burst de modo 0x%02X sem layout (%u pacotes).", modo, nPacotes);
        return;
    }
    emitirLayout(ts, l, d, nPacotes);
    if (l->codigo == COD_M1_ATIVA) info("Missao 1: payload confirmado e ativo.");
    if (l->codigo == COD_CONFIRMACAO_CB) info("Confirmacao de %s: modo 0x%02X", d[5] < 4 ? NOMES_NO[d[5]] : "?", modo);
}

// ==========================================
// CONFIG.JSON DO SOFTWARE OFICIAL
// ==========================================
void imprimirConfigGS() {
    info("Copie as linhas abaixo para instance/config.json do software oficial:");
    saida("{\"modes\": [");
    for (uint8_t i = 0; i < N_LAYOUTS; i++) {
        LinhaSaida l;
        const Layout& m = LAYOUTS[i];
        int n = snprintf(l.texto, TAM_LINHA, "  {\"name\": \"%s\", \"code\": \"0x%02X\", \"dataTypes\": [", m.nome, m.codigo);
        for (uint8_t c = 0; c < m.n && n < TAM_LINHA - 8; c++) {
            n += snprintf(&l.texto[n], TAM_LINHA - n, "%s\"%s\"", c ? ", " : "", m.campos[c].nome);
        }
        snprintf(&l.texto[n], TAM_LINHA - n, "]}%s", i + 1 < N_LAYOUTS ? "," : "");
        xQueueSend(filaSaida, &l, pdMS_TO_TICKS(100));
    }
    saida("]}");
}
