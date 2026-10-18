#include "mode_manager.h"
#include "can_manager.h"
#include "sd_manager.h"
#include "sensores_manager.h"
#include "aquecedor.h"
#include <Preferences.h>

// TERM ativo fica gravado na NVS para ser restaurado se o HK reiniciar no meio da
// ciclagem térmica (HK.ino). Gravado ao entrar no TERM e apagado em qualquer saída
// dele, então um reset em outro modo nunca leva ao TERM.
static void persistirTerm(bool ativo) {
    Preferences nvs;
    if (!nvs.begin(NVS_NAMESPACE_HK, false)) return;
    nvs.putBool(NVS_CHAVE_TERM, ativo);
    nvs.end();
}

// TM ID = código do telecomando que ativa o modo
uint8_t obterHexModo(obdh_mode_t modo) {
    switch(modo) {
        case MODE_IDLE:      return 0x01;
        case MODE_CARGA:     return 0x10;
        case MODE_ABERTURA:  return 0x12;
        case MODE_TERM:      return 0x13;
        case MODE_TP1:       return 0x14;
        case MODE_TI:        return 0x15;
        case MODE_TP2:       return 0x16;
        case MODE_STAB:      return 0x20;
        case MODE_DET:       return 0x21;
        case MODE_APO:       return 0x22;
        case MODE_CTRL:      return 0x23;
        case MODE_MISSAO1:   return 0x30;
        case MODE_MISSAO2:   return 0x31;
        case MODE_MISSAO3:   return 0x32;
        default:             return 0xFF;
    }
}

bool modoUsaADCS(obdh_mode_t modo) {
    return modo == MODE_STAB || modo == MODE_DET || modo == MODE_APO || modo == MODE_CTRL;
}

bool modoUsaPayload(obdh_mode_t modo) {
    return modo == MODE_MISSAO1 || modo == MODE_MISSAO2 || modo == MODE_MISSAO3;
}

// Período da task de modo em cada modo
TickType_t periodoDoModo(obdh_mode_t modo) {
    switch (modo) {
        case MODE_TERM:     return pdMS_TO_TICKS(500); // ciclagem térmica: 2 Hz
        case MODE_MISSAO3:  return pdMS_TO_TICKS(100); // reação rápida ao pacote de repasse
        default:            return pdMS_TO_TICKS(1000);
    }
}

// Avisa o computador de payload ativo (0x01 = início do desligamento gracioso) e
// agenda o corte do MOSFET de carga útil para 40 s depois (TaskIdle_Routine).
void iniciarDesligamentoPayload(obdh_mode_t modo_payload) {
    no_can_t no = (modo_payload == MODE_MISSAO1) ? NO_RPI : NO_PLD_ESP32;
    enviarComandoSemEspera(no, 0x01);
    payload_aguardando_desligamento = true;
    payload_desligando_desde = millis();
    registrarLog(LOG_HK_GERAL, "ACT", "PAYLOAD,DESLIGAMENTO_GRACIOSO");
}

void transicionarModo(obdh_mode_t novo, const char* gatilho, uint8_t opcoes) {
    xSemaphoreTake(modoMutex, portMAX_DELAY);
    obdh_mode_t anterior = modo_secundario;

    // 1. Ações de saída do modo anterior
    if (anterior == MODE_TERM && novo != MODE_TERM) {
        aquecedorDesligar();
        persistirTerm(false);
    }
    if (novo == MODE_TERM && anterior != MODE_TERM) {
        persistirTerm(true);
    }

    if (modoUsaADCS(anterior) && !modoUsaADCS(novo)) {
        if (!(opcoes & TR_ADCS_JA_PARADO)) {
            resultado_conf_t r = enviarComandoSimplesEAguardar(NO_ADCS, 0x01, TIMEOUT_CONF_MS);
            registrarLog(LOG_HK_GERAL, "ADCS", "0x01,%s", textoResultado(r));
        }
        if (!(opcoes & TR_MANTER_MOSFET_ADCS)) {
            digitalWrite(PIN_MOSFET_ADCS, LOW);
        }
    }

    if (modoUsaPayload(anterior) && !modoUsaPayload(novo) && !(opcoes & TR_MANTER_PAYLOAD)) {
        iniciarDesligamentoPayload(anterior);
    }

    // 2. MOSFET do EPS: ligado só no modo de carga
    digitalWrite(PIN_MOSFET_EPS, (novo == MODE_CARGA) ? HIGH : LOW);

    // 3. Arquivos do SD e novo modo
    requisitarFechamentoLogModo();
    modo_secundario = novo;
    delay_modo_secundario = periodoDoModo(novo);
    requisitarAberturaLogModo(novo);

    // 4. Log de Eventos
    if (anterior != novo) {
        registrarEvento(anterior, novo, gatilho);
    }
    xSemaphoreGive(modoMutex);
}
