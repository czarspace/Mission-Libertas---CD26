#ifndef CONFIG_H
#define CONFIG_H
#pragma once

// =====================================================================
// Placa de comunicação da estação solo (ESP32 + SX1278 433 MHz + SX1276 915 MHz)
// Junta GS/uplink/uplink.ino e GS/downlink/downlink.ino num único firmware com
// FreeRTOS, ligado ao software oficial da GS (Mission-Libertas---CD26/Ground Station).
// =====================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// ==========================================
// SERIAL (software oficial da GS)
// ==========================================
// No software oficial: $env:SERIAL_BAUD = '115200' antes de iniciar o servidor.
// 9600 não comporta a Missão 1 (até 13 registros de aeronave por segundo).
#define SERIAL_BAUD 115200

// ==========================================
// LoRa 433 MHz: SX1278 no VSPI (SPI3) — telecomandos e canal compartilhado
// ==========================================
#define L433_SCK   18
#define L433_MISO  19
#define L433_MOSI  23
#define L433_CS    5
#define L433_DIO0  2
#define L433_DIO1  15
#define L433_RST   4
#define FREQ_433   433.0

// ==========================================
// LoRa 915 MHz: SX1276 no HSPI (SPI2) — telemetria e confirmações da Gabriella
// ==========================================
#define L915_SCK   14
#define L915_MISO  32
#define L915_MOSI  13
#define L915_CS    26
#define L915_DIO0  33
#define L915_DIO1  25
#define L915_RST   27
#define FREQ_915   915.0

// Parâmetros LoRa — têm que bater com TTeC/Config.h e SateliteComplementar/Config.h
#define LORA_BW_KHZ        250.0
#define LORA_SF            7
#define LORA_CR            8     // coding rate 4/8
#define LORA_SYNC_WORD     0x12
#define LORA_POTENCIA_DBM  17
#define LORA_PREAMBULO     8
#define LORA_RETENTATIVA_MS 5000 // intervalo entre tentativas de reiniciar um rádio

// ==========================================
// PROTOCOLO DA CONSTELAÇÃO
// ==========================================
#define DESTINO_GS 0x00
#define DESTINO_GB 0x01 // Gabriella
#define DESTINO_CR 0x02 // Carolina
#define DESTINO_BT 0x03 // Beatriz

#define TIPO_CMD   0x0C
#define TIPO_TEL   0x0D
#define TIPO_PLD   0x0E
#define TIPO_ACK   0x0F

// ==========================================
// CÓDIGOS PRÓPRIOS DA GS (linhas para o software oficial)
// ==========================================
// O software oficial só exibe e grava linhas de telemetria; estes códigos levam
// para ele o que não é burst de modo. Ficam fora da faixa dos TM IDs do satélite.
#define COD_CONFIRMACAO_TC  0xF0 // confirmação de telecomando da Gabriella
#define COD_ENLACE          0xF1 // RSSI/SNR de cada pacote recebido
#define COD_M1_ATIVA        0xF2 // burst de ativação da Missão 1
#define COD_CONFIRMACAO_CB  0xF3 // confirmação de Carolina ou Beatriz (433 MHz)
#define COD_REPASSE         0xF4 // pacote de repasse que voltou à GS (Missão 3)

// ==========================================
// FILAS
// ==========================================
#define TAM_LINHA      360  // maior linha: TP1/TP2/TI, 22 valores
#define TAM_FILA_SAIDA 32

typedef struct {
    char texto[TAM_LINHA];
} LinhaSaida;

// Pacote a transmitir em 433 MHz: [destino][tipo][dados]
typedef struct {
    uint8_t tamanho;
    uint8_t dados[64];
} PacoteTx;

extern QueueHandle_t filaSaida; // linhas para a serial (uma única task escreve)
extern QueueHandle_t filaTx433; // telecomandos e repasses a transmitir

#endif
