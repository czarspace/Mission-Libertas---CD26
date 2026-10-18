#ifndef CONFIG_H
#define CONFIG_H
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <sys/time.h>

// ==========================================
// ESP32 DE PAYLOAD (Missões 2 e 3) — satélite Gabriella
// ==========================================
// Ligado ao CAN da Gabriella e a um DWM1000 (UWB) no VSPI. Só é energizado pelo HK
// durante as Missões 2 e 3.

// CAN (SN65HVD230)
#define PIN_CAN_RX 16
#define PIN_CAN_TX 17

// DWM1000 (VSPI)
#define PIN_UWB_CS   5
#define PIN_UWB_SCK  18
#define PIN_UWB_MISO 19
#define PIN_UWB_MOSI 23
#define PIN_UWB_IRQ  34
#define PIN_UWB_RST  27

// Identificadores da constelação (mesmos do protocolo LoRa)
#define ID_GS         0x00
#define ID_GABRIELLA  0x01
#define ID_CAROLINA   0x02
#define ID_BEATRIZ    0x03
#define ID_PROPRIO    ID_GABRIELLA

// Modos do ESP32 de payload (os códigos são os dos comandos)
typedef enum {
    PLD_IDLE = 0x01,     // DW1000 em standby
    PLD_MISSAO2 = 0x31,  // DW1000 como responder
    PLD_MISSAO3 = 0x32   // DW1000 como responder; mede a cada MEASURE
} pld_mode_t;

extern volatile pld_mode_t modoAtual;

#endif
