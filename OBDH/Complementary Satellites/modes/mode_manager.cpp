#include "mode_manager.h"
#include "lora_manager.h"
#include "uwb_manager.h"

// Os outros dois satélites, em ordem de identificador
#if SAT_ID == ID_CAROLINA
static const uint8_t OUTROS[2] = {ID_GABRIELLA, ID_BEATRIZ};
#else
static const uint8_t OUTROS[2] = {ID_GABRIELLA, ID_CAROLINA};
#endif

static uint32_t timestampAtual() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec;
}

// Pacote 0 comum: timestamp(4) + modo + origem
static void montarPacote0(uint8_t* p0, uint8_t modo) {
    memset(p0, 0, 8);
    uint32_t ts = timestampAtual();
    memcpy(p0, &ts, 4);
    p0[4] = modo;
    p0[5] = SAT_ID;
}

// Confirmação de comando de modo: rajada de 1 pacote
static void enviarConfirmacao() {
    uint8_t pacotes[8];
    montarPacote0(pacotes, (uint8_t)modoAtual);
    enviarRajadaParaGS(pacotes, 1);
}

// Mede as distâncias aos outros dois satélites (DW1000 como initiator)
static void medirDistancias(float d[2]) {
    for (int k = 0; k < 2; k++) {
        uwbMedir(OUTROS[k], &d[k]);
    }
    uwbModoResponder();
}

// MEASURE (Missão 2): rajada de distâncias de 2 pacotes para a GS
static void responderMeasure() {
    float d[2];
    medirDistancias(d);

    uint8_t pacotes[16];
    montarPacote0(&pacotes[0], (uint8_t)modoAtual);
    memcpy(&pacotes[8], &d[0], 4);
    memcpy(&pacotes[12], &d[1], 4);
    enviarRajadaParaGS(pacotes, 2);
}

// Repasse (Missão 3): marca-se como visitado, mede, escolhe o próximo salto e retransmite
static void processarRepasse(const uint8_t* recebido) {
    uint8_t d[32];
    memcpy(d, recebido, 32);

    // O primeiro satélite a receber vira a origem e registra o instante da detecção
    // (a GS envia o timestamp zerado)
    if (d[7] == 0) {
        d[5] = SAT_ID;
        uint32_t ts = timestampAtual();
        memcpy(&d[0], &ts, 4);
    }
    d[7] |= BIT_SATELITE(SAT_ID);

    float dist[2];
    medirDistancias(dist);

    // Próximo salto: mais próximo ainda não visitado; se não houver, a GS
    uint8_t destino = ID_GS;
    float melhor = INFINITY;
    for (int k = 0; k < 2; k++) {
        if (d[7] & BIT_SATELITE(OUTROS[k])) continue;
        float dk = isnan(dist[k]) ? INFINITY : dist[k];
        if (destino == ID_GS || dk < melhor) {
            destino = OUTROS[k];
            melhor = dk;
        }
    }

    d[6] = SAT_ID; // remetente deste salto
    enviarRepasse(destino, d);
    Serial.printf("[SAT] Repasse enviado ao destino 0x%02X.\n", destino);
}

static void processarComando(const PacoteRecebido* p) {
    uint8_t comando = p->dados[0];

    switch (comando) {
        case 0x00: { // SET_TIME
            if (p->tamanho < 5) return;
            uint32_t unix_time;
            memcpy(&unix_time, &p->dados[1], 4);
            struct timeval tv; tv.tv_sec = unix_time; tv.tv_usec = 0;
            settimeofday(&tv, NULL);
            enviarConfirmacao();
            break;
        }

        case 0x01:
            uwbStandby();
            modoAtual = SAT_IDLE;
            enviarConfirmacao();
            break;

        case 0x31:
            uwbModoResponder();
            modoAtual = SAT_MISSAO2;
            enviarConfirmacao();
            break;

        case 0x32:
            uwbModoResponder();
            modoAtual = SAT_MISSAO3;
            enviarConfirmacao();
            break;

        case 0x33: // MEASURE: válido só na Missão 2
            if (modoAtual == SAT_MISSAO2) responderMeasure();
            break;

        default:
            break; // comando desconhecido: ignorado
    }
}

void processarPacote(const PacoteRecebido* p) {
    if (p->tipo == TIPO_CMD && p->tamanho >= 1) {
        processarComando(p);
    } else if (p->tipo == TIPO_PLD && p->tamanho >= 32 && modoAtual == SAT_MISSAO3) {
        processarRepasse(p->dados);
    }
}
