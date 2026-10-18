#include "mode_manager.h"
#include "sensores_manager.h"
#include "can_manager.h"
#include "sd_manager.h"

// Carregamento: tensão e corrente dos seis canais dos INA3221 (bateria, painéis das
// Faces 1, 2 e 4, saída do carregador e consumo do sistema). Coleta a 1 Hz (todo
// ciclo vai pro carga.txt) e rajada a cada 2 s (0,5 Hz). A potência de cada
// elemento é calculada no solo (V × I).
void executarModoCarga() {
    static uint8_t contador_envio_can = 0;
    static LoteTelemetria lote;

    // 1. Coleta, na ordem do mapa (papel_ina_t)
    float v[N_INA], i[N_INA];
    travarSensores();
    for (uint8_t p = 0; p < N_INA; p++) lerCanalINA(p, &v[p], &i[p]);
    liberarSensores();

    // 2. Registro no SD: ts,0x10,Vb,Ib,Vf1,If1,Vf2,If2,Vf4,If4,Vcarreg,Icarreg,Vcons,Icons
    registrarLog(LOG_MODO_BLOCO, "0x10", "%.3f,%.1f,%.3f,%.1f,%.3f,%.1f,%.3f,%.1f,%.3f,%.1f,%.3f,%.1f",
                 v[INA_BATERIA], i[INA_BATERIA], v[INA_PAINEL_F1], i[INA_PAINEL_F1],
                 v[INA_PAINEL_F2], i[INA_PAINEL_F2], v[INA_PAINEL_F4], i[INA_PAINEL_F4],
                 v[INA_CARREGADOR], i[INA_CARREGADOR], v[INA_CONSUMO], i[INA_CONSUMO]);

    // 3. Rajada para o solo
    contador_envio_can++;
    if (contador_envio_can >= 2) {
        contador_envio_can = 0;

        lote.quantidade = 1 + N_INA;
        uint8_t modo = obterHexModo(MODE_CARGA);
        uint32_t ts = timestampAtual();

        // Pacote 0: timestamp + modo
        lote.pacotes[0].tamanho = 5;
        memcpy(lote.pacotes[0].dados, &ts, 4);
        memcpy(&lote.pacotes[0].dados[4], &modo, 1);

        // Pacotes 1 a 6 (tensão + corrente): bateria, painel F1, painel F2, painel F4,
        // saída do carregador, consumo do sistema. Os pacotes 1 a 4 mantêm o layout
        // anterior; 5 e 6 foram acrescentados no fim.
        for (uint8_t p = 0; p < N_INA; p++) {
            lote.pacotes[1 + p].tamanho = 8;
            memcpy(&lote.pacotes[1 + p].dados[0], &v[p], 4);
            memcpy(&lote.pacotes[1 + p].dados[4], &i[p], 4);
        }

        enfileirarRajadaHK(&lote);
    }
}
