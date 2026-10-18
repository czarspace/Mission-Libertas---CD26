#ifndef CONFIG_H
#define CONFIG_H
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <sys/time.h>

// ==========================================
// SATÉLITE COMPLEMENTAR (Carolina ou Beatriz)
// ==========================================
// Um único firmware para os dois satélites: escolha o identificador antes de gravar.
#define ID_GS         0x00
#define ID_GABRIELLA  0x01
#define ID_CAROLINA   0x02
#define ID_BEATRIZ    0x03

#define SAT_ID        ID_CAROLINA   // ID_CAROLINA ou ID_BEATRIZ

// Bit de cada satélite na máscara de visitados do repasse (Missão 3)
#define BIT_SATELITE(id) (1 << ((id) - 1))

// ==========================================
// DWM1000 (VSPI)
// ==========================================
#define PIN_UWB_CS   5
#define PIN_UWB_SCK  18
#define PIN_UWB_MISO 19
#define PIN_UWB_MOSI 23
#define PIN_UWB_IRQ  34
// Reset do DWM1000 no GPIO 4 nos dois satélites (esquemáticos BEATRIZ e CAROLINA do EPS):
// o GPIO 27 é o reset do SX1278
#define PIN_UWB_RST  4

// ==========================================
// SX1278 (HSPI, 433 MHz)
// ==========================================
// Pinos do mapa de conexões da Carolina e dos esquemáticos do EPS (iguais na Beatriz).
// MISO no GPIO 32, e não no 12 padrão do HSPI, que é pino de strapping no boot.
#define LORA_SCK   14
#define LORA_MISO  32
#define LORA_MOSI  13
#define LORA_CS    26
#define LORA_DIO0  33
#define LORA_DIO1  25
#define LORA_RST   27

#define FREQ_LORA  433.0

// Parâmetros do enlace da constelação — têm que bater com TTeC/Config.h e a GS
#define LORA_BW_KHZ        250.0
#define LORA_SF            7
#define LORA_CR            8     // coding rate 4/8
#define LORA_SYNC_WORD     0x12
#define LORA_POTENCIA_DBM  17
#define LORA_PREAMBULO     8

// Tipos do pacote LoRa (byte 1)
#define TIPO_CMD 0x0C
#define TIPO_TEL 0x0D
#define TIPO_PLD 0x0E

// Modos (os códigos são os dos comandos)
typedef enum {
    SAT_IDLE = 0x01,     // LoRa em recepção, DW1000 em standby
    SAT_MISSAO2 = 0x31,  // DW1000 como responder; mede a cada MEASURE da GS
    SAT_MISSAO3 = 0x32   // DW1000 como responder; mede e repassa a cada pacote de repasse
} sat_mode_t;

extern volatile sat_mode_t modoAtual;

// Pacote recebido pelo rádio (sem os 2 bytes de destino e tipo)
typedef struct {
    uint8_t tipo;
    uint8_t tamanho;
    uint8_t dados[64];
} PacoteRecebido;

// Pacote a transmitir: [destino][tipo][dados]
typedef struct {
    uint8_t destino;
    uint8_t tipo;
    uint8_t tamanho;
    uint8_t dados[64];
} PacoteEnvio;

extern QueueHandle_t filaRecebidos;
extern QueueHandle_t filaEnvio;

#endif
