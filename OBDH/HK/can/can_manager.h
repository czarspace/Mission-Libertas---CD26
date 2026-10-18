#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#pragma once

#include <Arduino.h>
#include "driver/twai.h"
#include "Config.h"
#include "sd_manager.h"

#define PIN_CAN_RX  17
#define PIN_CAN_TX  16

// Limite de pacotes de uma rajada (header + até 15 fatias na faixa 0x_X1-0x_XF)
#define MAX_PACOTES_POR_RAJADA 15
// Rajadas do próprio HK: no máximo 14 fatias (0x111-0x11E), para nunca usar 0x11F,
// que é o identificador da confirmação do HK.
#define MAX_PACOTES_RAJADA_HK  14

// Fila de recepção do driver TWAI (o padrão de 5 frames estoura com o ADCS a 50 Hz)
#define CAN_RX_QUEUE_LEN 32

// ==========================================
// IDENTIFICADORES CAN
// ==========================================
#define CAN_ID_RX_TTC        0x010 // TT&C -> HK (telecomando)
#define CAN_ID_TX_ADCS       0x020 // HK -> ADCS
#define CAN_ID_TX_PLD_M1     0x030 // HK -> Raspberry Pi (Missão 1)
#define CAN_ID_TX_PLD_M23    0x031 // HK -> ESP32 de payload (Missões 2 e 3)

#define CAN_ID_TX_BASE_HK      0x110 // HK -> TT&C (header + rajada)
#define CAN_ID_CONF_HK         0x11F // HK -> TT&C (ACK/INV)
#define CAN_ID_RX_BASE_ADCS    0x120 // ADCS -> HK/TT&C (rajada)
#define CAN_ID_RX_BASE_PLD_M1  0x140 // RPi -> HK/TT&C (rajada da Missão 1)
#define CAN_ID_RX_BASE_PLD_M23 0x150 // ESP32 de payload -> HK/TT&C (rajada de distâncias)
#define CAN_ID_RX_BASE_REPASSE 0x160 // TT&C -> HK (pacote de repasse recebido em 433 MHz)
#define CAN_ID_TX_BASE_REPASSE 0x170 // HK -> TT&C (pacote de repasse a transmitir em 433 MHz)

// Confirmações: fora da faixa de fatias de cada nó, para nunca serem confundidas
// com uma fatia de rajada durante a remontagem.
#define CAN_ID_CONF_ADCS     0x1AF
#define CAN_ID_CONF_PLD_M1   0x1BF
#define CAN_ID_CONF_PLD_M23  0x1CF

#define CAN_TIPO_RAJADA      0x02  // byte 0 do header: rajada de telemetria
#define CAN_ACK              0xCF
#define CAN_INV              0xFF

// Destino LoRa "nenhum" (rajadas que não são de repasse)
#define DESTINO_NENHUM       0xFF

// ==========================================
// ESTRUTURAS DE RAJADA
// ==========================================
typedef struct {
    uint8_t  tamanho;    // DLC (quantidade de bytes úteis, de 1 a 8)
    uint8_t  dados[8];
} PacoteTelemetria;

typedef struct {
    uint16_t id_base;    // faixa da rajada (0x110 ou 0x170); 0 = 0x110
    uint8_t  destino;    // byte 2 do header (só na faixa 0x170); DESTINO_NENHUM = sem byte 2
    uint8_t  quantidade;
    PacoteTelemetria pacotes[MAX_PACOTES_POR_RAJADA];
} LoteTelemetria;

// Telecomando recebido do TT&C (frame 0x010)
typedef struct {
    uint8_t dlc;
    uint8_t dados[8];
} ComandoRecebido;

// Nós que confirmam comandos do HK (uma fila de confirmação por nó)
typedef enum {
    NO_ADCS = 0,
    NO_RPI,
    NO_PLD_ESP32
} no_can_t;

typedef enum {
    CONF_ACK = 0,
    CONF_INV,
    CONF_TIMEOUT
} resultado_conf_t;

// Distâncias medidas pelo ESP32 de payload (rajada 0x150)
typedef struct {
    uint8_t origem;
    float   d[2];        // distância ao 1º e ao 2º outro satélite, por ordem de ID (m)
} MedidaDistancias;

// Pacote de repasse da Missão 3 (4 pacotes de 8 bytes)
typedef struct {
    uint8_t dados[32];
} PacoteRepasse;

// ==========================================
// FILAS
// ==========================================
extern QueueHandle_t filaTransmissaoCAN;
extern QueueHandle_t filaComandos;
extern QueueHandle_t filaDistancias;
extern QueueHandle_t filaRepasse;

// ==========================================
// ASSINATURAS
// ==========================================
bool initCanBus();
bool criarFilasCAN();

// Task de recepção: única leitora do TWAI. Roteia cada frame para a fila ou a
// remontagem correspondente.
void receberMensagensCAN();

// Envio de comandos a outro nó e espera da confirmação (lida da fila do nó, preenchida
// pela task de recepção). Limpa confirmações antigas antes de enviar.
resultado_conf_t enviarEAguardar(no_can_t no, const uint8_t* dados, uint8_t dlc, uint32_t timeout_ms);
resultado_conf_t enviarComandoSimplesEAguardar(no_can_t no, uint8_t comando, uint32_t timeout_ms);
void enviarComandoSemEspera(no_can_t no, uint8_t comando);
const char* textoResultado(resultado_conf_t r);

// Bit por nó (1 << NO_ADCS, 1 << NO_RPI, 1 << NO_PLD_ESP32): último comando sem confirmação
uint8_t nosSemResposta();

// Respostas ao TT&C (0x11F), registradas no LOG_OP.txt com o código do telecomando
void responderACK(uint8_t comando);
void responderINV(uint8_t comando);

// Rajadas
void enfileirarRajadaHK(LoteTelemetria* lote);
void enfileirarRajadaRepasse(LoteTelemetria* lote, uint8_t destino);
void despacharRajadaCAN(const LoteTelemetria* lote);

#endif
