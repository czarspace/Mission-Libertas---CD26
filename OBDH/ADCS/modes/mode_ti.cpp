#include "mode_manager.h"
#include "sensores_manager.h"
#include "can_manager.h"

static uint32_t ultimo_envio_teste_motor = 0;

// MODE_TI: gira o motor em malha aberta, a uma velocidade fixa, pra testar o
// circuito do driver/motor em bancada (parado, sem o que o B-dot reagir). A
// duração é controlada pelo HK (manda 0x24, espera um tempo curto, manda 0x01
// de volta) — este modo não tem timer próprio.
void executarTesteMotor() {
    motor.target = VELOCIDADE_TESTE_TI;

    if (millis() - ultimo_envio_teste_motor >= 1000) {
        ultimo_envio_teste_motor = millis();

        LoteTelemetria lote;
        lote.quantidade = 2;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint32_t ts = (uint32_t)tv.tv_sec;
        uint8_t modo = obterHexModo(MODE_TI);
        float vel_motor = motor.shaft_velocity;

        // Pacote 0: Timestamp + Modo
        lote.pacotes[0].tamanho = 5;
        memcpy(lote.pacotes[0].dados, &ts, 4);
        memcpy(&lote.pacotes[0].dados[4], &modo, 1);

        // Pacote 1: Velocidade do Motor (confirma que girou)
        lote.pacotes[1].tamanho = 4;
        memcpy(&lote.pacotes[1].dados[0], &vel_motor, sizeof(float));

        xQueueSend(filaTransmissaoCAN, &lote, 0);
    }
}
