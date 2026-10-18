#include "mode_manager.h"
#include "can_manager.h"
#include "sd_manager.h"
#include "sensores_manager.h"

// ==========================================
// TELECOMANDOS (executados pela task de comandos)
// ==========================================
// A task de recepção só entrega o telecomando nesta fila; aqui ele é validado,
// executado e respondido ao TT&C (ACK 0xCF / INV 0xFF em 0x11F). As esperas por
// confirmação de outros nós leem as filas de confirmação, sem tocar no TWAI.

// SET_TIME (0x00 + Unix time de 4 bytes): ajusta o relógio do HK e repassa ao ADCS
// e, se estiver ligado, ao ESP32 de payload.
static void comandoSetTime(const ComandoRecebido* tc) {
    if (tc->dlc < 5) { responderINV(0x00); return; }

    uint32_t antigo = timestampAtual();
    uint32_t novo;
    memcpy(&novo, &tc->dados[1], 4);

    struct timeval tv;
    tv.tv_sec = novo;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    tempo_sincronizado = true;
    registrarLog(LOG_HK_GERAL, "TIME", "%lu,%lu", (unsigned long)antigo, (unsigned long)novo);

    resultado_conf_t r = enviarEAguardar(NO_ADCS, tc->dados, 5, TIMEOUT_CONF_MS);
    registrarLog(LOG_HK_GERAL, "ADCS", "0x00,%s", textoResultado(r));

    if (modo_secundario == MODE_MISSAO2 || modo_secundario == MODE_MISSAO3) {
        resultado_conf_t rp = enviarEAguardar(NO_PLD_ESP32, tc->dados, 5, TIMEOUT_CONF_MS);
        registrarLog(LOG_HK_GERAL, "PLD", "0x00,%s", textoResultado(rp));
    }

    if (r == CONF_ACK) responderACK(0x00);
    else responderINV(0x00);
}

// Modos que dependem do ADCS (STAB, DET, APO, CTRL)
static void comandoModoADCS(const ComandoRecebido* tc, obdh_mode_t novo, bool usa_motor) {
    uint8_t cmd = tc->dados[0];
    if (cmd == 0x23 && tc->dlc < 5) { responderINV(cmd); return; } // CTRL sem o alvo

    int mosfet_antes = digitalRead(PIN_MOSFET_ADCS);
    if (usa_motor) digitalWrite(PIN_MOSFET_ADCS, HIGH); // liga ANTES de falar com o ADCS

    resultado_conf_t r = enviarEAguardar(NO_ADCS, tc->dados, tc->dlc, TIMEOUT_CONF_MS);
    registrarLog(LOG_HK_GERAL, "ADCS", "0x%02X,%s", cmd, textoResultado(r));

    if (r != CONF_ACK) {
        digitalWrite(PIN_MOSFET_ADCS, mosfet_antes); // reverte: não confirmou
        responderINV(cmd);
        return;
    }

    if (!usa_motor) digitalWrite(PIN_MOSFET_ADCS, LOW); // DET não usa o motor
    if (modo_secundario != novo) {
        transicionarModo(novo, GATILHO_TC);
    }
    // CTRL -> CTRL: só atualiza o alvo (o ADCS já confirmou), sem transição
    responderACK(cmd);
}

// IDLE (0x01) e STOP ADCS (0xB0)
static void comandoIdle(uint8_t cmd) {
    // Durante o boot da RPi, 0x01 cancela a ativação da Missão 1
    if (missao1_ativando) {
        missao1_cancelada = true;
        registrarLog(LOG_HK_GERAL, "M1", "CANCELAMENTO_PEDIDO");
        responderACK(cmd);
        return;
    }

    // A confirmação do ADCS só é exigida quando ele está em uso (modos do ADCS ou
    // 0xB0); fora deles o ADCS já está em idle e o IDLE continua alcançável mesmo
    // que o ADCS não responda.
    if (modoUsaADCS(modo_secundario) || cmd == 0xB0) {
        resultado_conf_t r = enviarComandoSimplesEAguardar(NO_ADCS, cmd, TIMEOUT_CONF_MS);
        registrarLog(LOG_HK_GERAL, "ADCS", "0x%02X,%s", cmd, textoResultado(r));
        if (r != CONF_ACK) { responderINV(cmd); return; }
    }

    uint8_t opcoes = TR_ADCS_JA_PARADO;
    if (cmd == 0xB0) opcoes |= TR_MANTER_MOSFET_ADCS;
    else             digitalWrite(PIN_MOSFET_ADCS, LOW);

    transicionarModo(MODE_IDLE, GATILHO_TC, opcoes);
    responderACK(cmd);
}

// STOP Payload (0xC0): para a missão ativa sem cortar o MOSFET de carga útil
static void comandoStopPayload() {
    if (missao1_ativando) {
        missao1_cancelada = true;
        registrarLog(LOG_HK_GERAL, "M1", "CANCELAMENTO_PEDIDO");
        responderACK(0xC0);
        return;
    }
    if (!modoUsaPayload(modo_secundario)) { responderINV(0xC0); return; }

    no_can_t no = (modo_secundario == MODE_MISSAO1) ? NO_RPI : NO_PLD_ESP32;
    resultado_conf_t r = enviarComandoSimplesEAguardar(no, 0xC0, TIMEOUT_CONF_MS);
    registrarLog(LOG_HK_GERAL, "PLD", "0xC0,%s", textoResultado(r));
    if (r != CONF_ACK) { responderINV(0xC0); return; }

    transicionarModo(MODE_IDLE, GATILHO_TC, TR_MANTER_PAYLOAD);
    responderACK(0xC0);
}

// Missão 1 (0x30): confirma já e liga a RPi; a task de modo conclui a ativação
// depois do boot de 60 s, sem bloquear a recepção de novos telecomandos.
static void comandoMissao1() {
    if (modo_secundario == MODE_MISSAO2 || modo_secundario == MODE_MISSAO3) {
        responderINV(0x30); // ESP32 de payload ativo: voltar ao IDLE antes
        return;
    }
    if (modo_secundario == MODE_MISSAO1) { responderACK(0x30); return; }

    payload_aguardando_desligamento = false; // cancela um desligamento pendente
    digitalWrite(PIN_MOSFET_CARGA_UTIL, HIGH);
    missao1_cancelada = false;
    missao1_inicio = millis();
    missao1_ativando = true;
    hk_ocupado = true;
    registrarLog(LOG_HK_GERAL, "M1", "BOOT_RPI");
    responderACK(0x30);
}

// Missões 2 (0x31) e 3 (0x32): ativação do ESP32 de payload (< 5 s)
static void comandoMissao23(uint8_t cmd) {
    if (modo_secundario == MODE_MISSAO1) { responderINV(cmd); return; } // RPi ativa

    obdh_mode_t novo = (cmd == 0x31) ? MODE_MISSAO2 : MODE_MISSAO3;
    if (!ativarPayloadESP32(cmd)) { responderINV(cmd); return; }

    // A transição não desliga o payload (o modo novo também é de payload)
    transicionarModo(novo, GATILHO_TC);
    responderACK(cmd);
}

// Float de 16 bits (IEEE 754 meia precisão) -> float. Os ganhos do SET_PID viajam
// assim para caber modo + Kp + Ki + Kd em um frame CAN de 8 bytes.
static float f16ParaFloat(uint16_t h) {
    uint16_t sinal = h >> 15, expoente = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
    float v;
    if (expoente == 0)       v = ldexpf((float)mantissa, -24);                  // subnormal
    else if (expoente == 31) v = mantissa ? NAN : INFINITY;
    else                     v = ldexpf((float)(mantissa | 0x400), expoente - 25);
    return sinal ? -v : v;
}

// SET_PID (0x26): [0x26][modo][Kp f16][Ki f16][Kd f16]. Ajusta os ganhos do PID de um
// modo que usa o motor (0x20 STAB, 0x22 APO, 0x23 CTRL). O HK repassa ao ADCS, que
// valida, grava e aplica; não há transição de modo.
static void comandoSetPid(const ComandoRecebido* tc) {
    uint8_t modo = (tc->dlc >= 2) ? tc->dados[1] : 0;
    if (tc->dlc < 8 || (modo != 0x20 && modo != 0x22 && modo != 0x23)) { responderINV(0x26); return; }

    uint16_t h[3];
    memcpy(h, &tc->dados[2], 6);
    float kp = f16ParaFloat(h[0]), ki = f16ParaFloat(h[1]), kd = f16ParaFloat(h[2]);

    resultado_conf_t r = enviarEAguardar(NO_ADCS, tc->dados, 8, TIMEOUT_CONF_MS);
    registrarLog(LOG_HK_GERAL, "PID", "0x%02X,%.6f,%.6f,%.6f,%s", modo, kp, ki, kd, textoResultado(r));

    if (r == CONF_ACK) responderACK(0x26);
    else responderINV(0x26);
}

// Sequências de tiro único (TP1, TP2, TI): a task de modo executa e volta ao IDLE
static void comandoSequencia(uint8_t cmd, obdh_mode_t seq) {
    transicionarModo(seq, GATILHO_TC);
    hk_ocupado = true;
    sequencia_pendente = seq;
    responderACK(cmd);
}

void processarTelecomando(const ComandoRecebido* tc) {
    if (tc->dlc == 0) return;
    uint8_t cmd = tc->dados[0];
    registrarLog(LOG_HK_GERAL, "TC", "0x%02X", cmd);

    // HK ocupado (ABERTURA, TP1/TP2/TI ou ativação da Missão 1): só o SET_TIME e,
    // durante a ativação da Missão 1, o cancelamento (0x01 / 0xC0) são aceitos.
    if (hk_ocupado) {
        bool permitido = (cmd == 0x00) || (missao1_ativando && (cmd == 0x01 || cmd == 0xC0));
        if (!permitido) { responderINV(cmd); return; }
    }

    switch (cmd) {
        case 0x00: comandoSetTime(tc); break;
        case 0x01: comandoIdle(0x01); break;
        case 0xB0: comandoIdle(0xB0); break;
        case 0xC0: comandoStopPayload(); break;

        // Modos executados só pelo HK
        case 0x10: transicionarModo(MODE_CARGA, GATILHO_TC);     responderACK(cmd); break;
        case 0x12: transicionarModo(MODE_ABERTURA, GATILHO_TC);  hk_ocupado = true; responderACK(cmd); break;
        case 0x13: transicionarModo(MODE_TERM, GATILHO_TC);      responderACK(cmd); break;

        // Sequências de teste
        case 0x14: comandoSequencia(cmd, MODE_TP1); break;
        case 0x16: comandoSequencia(cmd, MODE_TP2); break;
        case 0x15: comandoSequencia(cmd, MODE_TI);  break;

        // Modos do ADCS
        case 0x20: comandoModoADCS(tc, MODE_STAB, true);  break;
        case 0x21: comandoModoADCS(tc, MODE_DET,  false); break;
        case 0x22: comandoModoADCS(tc, MODE_APO,  true);  break;
        case 0x23: comandoModoADCS(tc, MODE_CTRL, true);  break;
        case 0x26: comandoSetPid(tc); break;

        // Missões
        case 0x30: comandoMissao1(); break;
        case 0x31:
        case 0x32: comandoMissao23(cmd); break;

        default: responderINV(cmd); break;
    }
}
