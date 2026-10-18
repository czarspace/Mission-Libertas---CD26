#include "mode_manager.h"
#include "sensores_manager.h"
#include "can_manager.h"

static uint32_t ultimo_envio_teste_sensores = 0;

// MODE_TP: reporta os sensores do ADCS (luminosidade + giroscópio) sem mexer no
// motor. O HK aciona esse modo, espera a rajada chegar e comanda a volta pro IDLE.
void executarTesteSensores() {
    lerLuminosidade();
    lerGiroscopio();
    calcularAnguloSolar();

    if (millis() - ultimo_envio_teste_sensores >= 1000) {
        ultimo_envio_teste_sensores = millis();

        // Mesmo shape do decoder de DET no HK: timestamp, modo, gyro_z e os 4 lux
        LoteTelemetria lote;
        lote.quantidade = 4;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint32_t ts = (uint32_t)tv.tv_sec;
        uint8_t modo = obterHexModo(MODE_TP);

        lote.pacotes[0].tamanho = 5;
        memcpy(lote.pacotes[0].dados, &ts, 4);
        memcpy(&lote.pacotes[0].dados[4], &modo, 1);

        lote.pacotes[1].tamanho = 8;
        memcpy(&lote.pacotes[1].dados[0], &gyro_z, sizeof(float));
        memcpy(&lote.pacotes[1].dados[4], &lux2_2, sizeof(float));

        lote.pacotes[2].tamanho = 8;
        memcpy(&lote.pacotes[2].dados[0], &lux1_1, sizeof(float));
        memcpy(&lote.pacotes[2].dados[4], &lux1_2, sizeof(float));

        lote.pacotes[3].tamanho = 4;
        memcpy(&lote.pacotes[3].dados[0], &lux2_1, sizeof(float));

        xQueueSend(filaTransmissaoCAN, &lote, 0);
    }
}
