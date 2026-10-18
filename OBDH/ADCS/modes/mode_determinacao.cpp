#include "mode_manager.h"
#include "sensores_manager.h"
#include "can_manager.h"

void executarDeterminacao() {
    // 1. Leitura Fria e Cálculo
    lerLuminosidade();
    calcularAnguloSolar();

    // =========================================================
    // 2. EMPACOTAMENTO PARA CAN — a cada chamada (50Hz, ritmo de
    // TaskControleADCS). O HK loga tudo que conseguir (~50/s); o TT&C já faz
    // o downsample pra 2Hz sozinho (TaskInjetaAdcs_Routine em TTeC.ino).
    // =========================================================
    {
        // Decoder próprio de DET no HK: timestamp, modo, theta e os 4 lux
        // (sem gyro/motor, que esse modo não usa)
        LoteTelemetria lote;
        lote.quantidade = 4;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint32_t ts = (uint32_t)tv.tv_sec;
        uint8_t modo = obterHexModo(MODE_DETERMINACAO);

        // Pacote 0: Timestamp + Modo
        lote.pacotes[0].tamanho = 5;
        memcpy(lote.pacotes[0].dados, &ts, 4);
        memcpy(&lote.pacotes[0].dados[4], &modo, 1);

        // Pacote 1: Ângulo e Lux V1
        lote.pacotes[1].tamanho = 8;
        memcpy(&lote.pacotes[1].dados[0], &theta, sizeof(float));
        memcpy(&lote.pacotes[1].dados[4], &lux2_2, sizeof(float));

        // Pacote 2: Lux V2 e V3
        lote.pacotes[2].tamanho = 8;
        memcpy(&lote.pacotes[2].dados[0], &lux1_1, sizeof(float));
        memcpy(&lote.pacotes[2].dados[4], &lux1_2, sizeof(float));

        // Pacote 3: Lux V4
        lote.pacotes[3].tamanho = 4;
        memcpy(&lote.pacotes[3].dados[0], &lux2_1, sizeof(float));

        xQueueSend(filaTransmissaoCAN, &lote, 0);
    }
}
