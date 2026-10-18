#include "sd_manager.h"
#include "can_manager.h"
#include "mode_manager.h"

// Telemetria do ADCS. Todo pacote 0 é timestamp(4) + modo(1); como a remontagem
// usa passo fixo de 8 bytes por pacote, os dados começam sempre no byte 8.
// A decodificação usa o byte de modo do próprio pacote 0, então a rajada é
// interpretada corretamente mesmo durante uma troca de modo.
//
// Byte de modo (TM ID):
//   0x20 STAB, 0x21 DET, 0x22 APO, 0x23 CTRL
//   0x25 relatório de sensores (comando interno 0x25, testes)
//   0x24 relatório do motor    (comando interno 0x24, testes)
//
// Ordem dos sensores de luz nas rajadas: V1 (lux2_2), V2 (lux1_1), V3 (lux1_2), V4 (lux2_1).

static float lerFloat(const uint8_t* buffer, int offset) {
    float v;
    memcpy(&v, &buffer[offset], 4);
    return v;
}

void processarRajadaADCS(const uint8_t* buffer, uint8_t total) {
    uint32_t ts;
    memcpy(&ts, &buffer[0], 4);
    uint8_t modo = buffer[4];

    char tag[5];
    snprintf(tag, sizeof(tag), "0x%02X", modo);

    switch (modo) {
        case 0x20: // STAB: gyro_z, comando acumulado da roda, velocidade do motor
            if (total < 3) return;
            registrarLogTs(LOG_MODO_BLOCO, ts, tag, "%.4f,%.2f,%.2f",
                           lerFloat(buffer, 8), lerFloat(buffer, 12), lerFloat(buffer, 16));
            break;

        case 0x21: // DET: theta, V1..V4
            if (total < 4) return;
            registrarLogTs(LOG_MODO_BLOCO, ts, tag, "%.2f,%.0f,%.0f,%.0f,%.0f",
                           lerFloat(buffer, 8), lerFloat(buffer, 12), lerFloat(buffer, 16),
                           lerFloat(buffer, 20), lerFloat(buffer, 24));
            break;

        case 0x22: // APO
        case 0x23: // CTRL: gyro_z, theta, velocidade do motor, V1..V4
            if (total < 5) return;
            registrarLogTs(LOG_MODO_BLOCO, ts, tag, "%.4f,%.2f,%.2f,%.0f,%.0f,%.0f,%.0f",
                           lerFloat(buffer, 8), lerFloat(buffer, 12), lerFloat(buffer, 16),
                           lerFloat(buffer, 20), lerFloat(buffer, 24), lerFloat(buffer, 28),
                           lerFloat(buffer, 32));
            break;

        case 0x25: // relatório de sensores: gyro_z, V1..V4
            if (total < 4) return;
            registrarLogTs(LOG_MODO_BLOCO, ts, tag, "%.4f,%.0f,%.0f,%.0f,%.0f",
                           lerFloat(buffer, 8), lerFloat(buffer, 12), lerFloat(buffer, 16),
                           lerFloat(buffer, 20), lerFloat(buffer, 24));
            break;

        case 0x24: // relatório do motor: velocidade medida
            if (total < 2) return;
            registrarLogTs(LOG_MODO_BLOCO, ts, tag, "%.2f", lerFloat(buffer, 8));
            break;

        default:
            break; // modo desconhecido: ignora
    }
}
