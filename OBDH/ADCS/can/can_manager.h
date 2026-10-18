#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#pragma once

#include <Arduino.h>
#include "driver/twai.h"
#include "Config.h"

#define PIN_CAN_RX  16
#define PIN_CAN_TX  17

// Limite seguro para não estourar a memória da Fila
#define MAX_PACOTES_POR_RAJADA 15

// ==========================================
// CONFIGURAÇÕES DO NÓ CAN (ADCS)
// ==========================================
#define CAN_ID_RX_BASE      0x020 // ADCS recebe comandos de Voo (0x02X) do HK
#define CAN_ID_TX_BASE      0x120 // ADCS envia Rajada de Telemetria (0x12X)
// ADCS responde CONF/INV para o HK. Fora de 0x120-0x12F: essa faixa é toda
// ocupada pelo header + até 15 fatias de rajada (MAX_PACOTES_POR_RAJADA).
#define CAN_ID_CONF_ADCS    0x1AF

// ==========================================
// ESTRUTURA DE PACOTE GENÉRICO (AGNOSTICA)
// ==========================================
typedef struct __attribute__((packed)) {
    uint8_t  tamanho;    // DLC (quantidade de bytes úteis, de 1 a 8)
    uint8_t  dados[8];   // O payload bruto empacotado via memcpy
} PacoteTelemetria;

typedef struct __attribute__((packed)) {
    uint8_t quantidade;
    PacoteTelemetria pacotes[MAX_PACOTES_POR_RAJADA];
} LoteTelemetria;

// ==========================================
// VARIÁVEIS GLOBAIS DE COMUNICAÇÃO
// ==========================================
extern QueueHandle_t filaTransmissaoCAN;

// ==========================================
// ASSINATURAS DAS FUNÇÕES
// ==========================================
bool initCanBus();
void receberComandosCAN();
void enviarConfirmacaoCAN();
void enviarInvalidoCAN();
void despacharRajadaCAN(PacoteTelemetria* pacotes, uint8_t quantidade);

#endif // CAN_MANAGER_H