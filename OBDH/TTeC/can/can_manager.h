#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H
#pragma once

#include <Arduino.h>
#include "driver/twai.h"
#include "Config.h"

// ==========================================
// ESTRUTURAS DE DADOS (Espelho do HK)
// ==========================================
#define MAX_PACOTES_POR_RAJADA 15
#define CAN_RX_QUEUE_LEN 32

// Tipos do pacote LoRa (byte 1)
#define TIPO_CMD 0x0C // telecomando
#define TIPO_TEL 0x0D // telemetria
#define TIPO_PLD 0x0E // dados de payload (repasse da Missão 3)
#define TIPO_ACK 0x0F // confirmação de telecomando para a GS

typedef struct {
    uint8_t tamanho;
    uint8_t dados[8];
} PacoteTelemetria;

// Item da fila do downlink: uma rajada (TIPO_TEL) ou uma confirmação de TC (TIPO_ACK)
typedef struct {
    uint8_t tipo_lora;
    uint8_t quantidade;
    PacoteTelemetria pacotes[MAX_PACOTES_POR_RAJADA];
    uint8_t ack_comando;   // só em TIPO_ACK
    uint8_t ack_resposta;  // 0xCF, 0xFF ou 0xEE (sem resposta)
} LoteTelemetria;

// Comando vindo do LoRa (Terra) para o HK
typedef struct {
    uint32_t id_comando; // Ex: 0x00 para SET_TIME
    uint8_t tamanho;
    uint8_t dados[8];
} ComandoSat;

// Pacote de repasse da Missão 3 a transmitir em 433 MHz
typedef struct {
    uint8_t destino;
    uint8_t dados[32];
} RepasseTx;

extern LoteTelemetria ultimo_lote_adcs;
extern volatile bool tem_dado_novo_adcs;

// ==========================================
// ASSINATURAS DAS FUNÇÕES
// ==========================================
bool initCanBus();
void receberMensagensCAN();
void enviarComandoCAN();
// Pacote de repasse recebido em 433 MHz -> HK (rajada 0x160 + 4 fatias)
void enviarRajadaRepasseCAN(const uint8_t* dados32);

#endif // CAN_MANAGER_H
