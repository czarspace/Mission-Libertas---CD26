#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H
#pragma once

#include <Arduino.h>
#include "driver/twai.h"
#include "Config.h"

#define CAN_ID_RX_PLD        0x031 // HK -> ESP32 de payload
#define CAN_ID_TX_BASE_PLD   0x150 // rajada de distâncias (header + fatias)
#define CAN_ID_CONF_PLD      0x1CF // ACK/INV para o HK

// Pedido de medição (MEASURE) entregue pela recepção à task de modo
extern QueueHandle_t filaMedicoes;

bool initCanBus();
void receberComandosCAN();
void enviarConfirmacaoCAN();
void enviarInvalidoCAN();
// Rajada de distâncias: pacote 0 = ts + modo + origem; pacote 1 = d1 + d2 (float, m)
void enviarRajadaDistancias(uint8_t modo, float d1, float d2);

#endif
