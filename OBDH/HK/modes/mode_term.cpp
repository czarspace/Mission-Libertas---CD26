#include "mode_manager.h"
#include "can_manager.h"
#include "sd_manager.h"
#include "sensores_manager.h"
#include "aquecedor.h"

// Ciclagem térmica: coleta a 2 Hz (todo ciclo vai pro term.txt), controle dos
// aquecedores a cada ciclo e rajada a cada 5 s (0,2 Hz).
void executarModoTerm() {
    static uint8_t contador_envio_can = 0;
    static LoteTelemetria lote;
    float t[N_TEMP];
    float v_bat, i_bat;

    // Conversão dos dois barramentos ao mesmo tempo (o atraso ocorre só aqui)
    travarSensores();
    requisitarTodasTemperaturas();
    lerTodasTemperaturas(t);
    lerBateria(&v_bat, &i_bat);
    liberarSensores();

    float duty = aquecedorAtualizar(t);
    estado_aquecedor_t estado = aquecedorEstado();
    uint8_t flags = aquecedorFlags();
    uint16_t validos = mascaraTemperaturasValidas(t);
    float t_controle = aquecedorTemperaturaControle();

    // Registro no SD: ts,0x13,VBat,T1..T10,duty,estado,flags,T_controle
    // (T1..T3 bateria, T4..T6 aquecedores, T7 interna, T8 externa, T9..T10 auxiliares; nan = inválida)
    registrarLog(LOG_MODO_BLOCO, "0x13",
        "%.3f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%s,0x%02X,%.2f",
        v_bat, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9], duty,
        aquecedorNomeEstado(estado), flags, t_controle);

    // Rajada para o solo (a cada 10 ciclos de 0,5 s = 5 s)
    contador_envio_can++;
    if (contador_envio_can >= 10) {
        contador_envio_can = 0;

        lote.quantidade = 8;
        uint32_t ts = timestampAtual();
        uint8_t modo = obterHexModo(MODE_TERM);

        // Pacote 0: timestamp + modo
        lote.pacotes[0].tamanho = 5;
        memcpy(lote.pacotes[0].dados, &ts, 4);
        memcpy(&lote.pacotes[0].dados[4], &modo, 1);

        // Pacote 1: V_bat + T1
        lote.pacotes[1].tamanho = 8;
        memcpy(&lote.pacotes[1].dados[0], &v_bat, 4);
        memcpy(&lote.pacotes[1].dados[4], &t[0], 4);

        // Pacotes 2 a 5: T2..T9, aos pares
        for (uint8_t k = 0; k < 4; k++) {
            lote.pacotes[2 + k].tamanho = 8;
            memcpy(&lote.pacotes[2 + k].dados[0], &t[1 + 2 * k], 4);
            memcpy(&lote.pacotes[2 + k].dados[4], &t[2 + 2 * k], 4);
        }

        // Pacote 6: T10 + fração de acionamento do aquecedor
        lote.pacotes[6].tamanho = 8;
        memcpy(&lote.pacotes[6].dados[0], &t[9], 4);
        memcpy(&lote.pacotes[6].dados[4], &duty, 4);

        // Pacote 7: estado do controle (1 B) + flags (1 B) + sensores válidos (2 B) +
        // temperatura de controle da bateria (float)
        uint8_t est = (uint8_t)estado;
        lote.pacotes[7].tamanho = 8;
        lote.pacotes[7].dados[0] = est;
        lote.pacotes[7].dados[1] = flags;
        memcpy(&lote.pacotes[7].dados[2], &validos, 2);
        memcpy(&lote.pacotes[7].dados[4], &t_controle, 4);

        enfileirarRajadaHK(&lote);
    }
}
