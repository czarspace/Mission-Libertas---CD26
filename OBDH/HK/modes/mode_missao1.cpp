#include "mode_manager.h"
#include "sd_manager.h"
#include "can_manager.h"
#include "sensores_manager.h"

// Rajada de ativação da Missão 1: só o pacote 0 (timestamp + 0x30). Enviada quando
// a RPi confirma que iniciou a missão, cerca de 1 min depois do telecomando.
void enviarStatusMissao1Confirmada() {
    LoteTelemetria lote;
    lote.quantidade = 1;

    uint32_t ts = timestampAtual();
    uint8_t modo = obterHexModo(MODE_MISSAO1);

    lote.pacotes[0].tamanho = 5;
    memcpy(lote.pacotes[0].dados, &ts, 4);
    memcpy(&lote.pacotes[0].dados[4], &modo, 1);

    enfileirarRajadaHK(&lote);
}

// Executada a cada ciclo da task de modo: conclui a ativação da Missão 1 quando os
// 60 s de boot da RPi terminam. A task de comandos já confirmou o telecomando e
// ligou o MOSFET; enquanto isso ela continua livre para outros telecomandos.
void atualizarAtivacaoMissao1() {
    if (!missao1_ativando) return;
    if (millis() - missao1_inicio < TEMPO_BOOT_RPI_MS) return;

    missao1_ativando = false;

    if (missao1_cancelada) {
        // 0x01 ou 0xC0 durante o boot: a RPi já está de pé, então recebe o aviso
        // de desligamento gracioso e o MOSFET é cortado 40 s depois.
        missao1_cancelada = false;
        enviarComandoSemEspera(NO_RPI, 0x01);
        payload_aguardando_desligamento = true;
        payload_desligando_desde = millis();
        registrarLog(LOG_HK_GERAL, "M1", "ATIVACAO_CANCELADA");
        hk_ocupado = false;
        return;
    }

    resultado_conf_t r = enviarComandoSimplesEAguardar(NO_RPI, 0x30, TIMEOUT_CONF_MS);
    registrarLog(LOG_HK_GERAL, "RPI", "0x30,%s", textoResultado(r));

    if (r == CONF_ACK) {
        transicionarModo(MODE_MISSAO1, GATILHO_TC);
        enviarStatusMissao1Confirmada();
    } else {
        digitalWrite(PIN_MOSFET_CARGA_UTIL, LOW); // reverte: a missão não confirmou
        registrarLog(LOG_HK_GERAL, "INV", "0x30,RPI_SEM_CONFIRMACAO");
    }
    hk_ocupado = false;
}

// O formato dos dados da Missão 1 é definido pelo Payload. Os bytes recebidos são
// gravados em hexadecimal no missao1.txt, sem perder nada.
void processarTelemetriaMissao1_Para_SD(const uint8_t* buffer_bruto, uint8_t tamanho) {
    char hex[3 * 15 * 8 + 1];
    int offset = 0;
    for (uint8_t i = 0; i < tamanho && offset < (int)sizeof(hex) - 3; i++) {
        offset += snprintf(&hex[offset], sizeof(hex) - offset, "%02X", buffer_bruto[i]);
    }
    hex[offset] = '\0';
    registrarLog(LOG_MODO_BLOCO, "0x30", "RAW,%s", hex);
}
