#ifndef CONFIG_H
#define CONFIG_H
#pragma once

#include <Arduino.h>

// ==========================================
// PINOS DO CAN BUS (Comunicação com o HK)
// ==========================================
#define PIN_CAN_RX 16
#define PIN_CAN_TX 17

// ==========================================
// LORA 1: DOWNLINK (SX1276 - Transmissão para a GS, 915 MHz)
// Barramento HSPI (pinos 14, 32, 13, 26)
// ==========================================
#define LORA_DL_MISO 32
#define LORA_DL_MOSI 13
#define LORA_DL_SCK  14
#define LORA_DL_CS   26
#define LORA_DL_DIO0 33
#define LORA_DL_DIO1 25 // Pino de interrupção
#define LORA_DL_RST  27 // Reset do módulo

#define FREQ_DOWNLINK 915.0

// ==========================================
// LORA 2: UPLINK (SX1278 - Recepção da GS e canal compartilhado, 433 MHz)
// Barramento VSPI (pinos 18, 19, 23, 5)
// ==========================================
#define LORA_UP_MISO 19
#define LORA_UP_MOSI 23
#define LORA_UP_SCK  18
#define LORA_UP_CS   5
#define LORA_UP_DIO0 2  // Pino de interrupção principal
#define LORA_UP_DIO1 15 // Pino de interrupção secundário
#define LORA_UP_RST  4  // Reset do módulo

#define FREQ_UPLINK 433.0

// ==========================================
// PARÂMETROS LoRa (iguais nos dois rádios)
// ==========================================
// Têm que bater com GS/uplink/uplink.ino, GS/downlink/downlink.ino,
// SateliteComplementar/Config.h e a placa de comunicação da GS (capítulo de TT&C).
#define LORA_BW_KHZ        250.0 // largura de banda
#define LORA_SF            7     // spreading factor
#define LORA_CR            8     // coding rate 4/8 (RadioLib recebe o denominador)
#define LORA_SYNC_WORD     0x12
#define LORA_POTENCIA_DBM  17
#define LORA_PREAMBULO     8

// Intervalo entre tentativas de reiniciar um rádio que falhou
#define LORA_RETENTATIVA_MS 5000

// ==========================================
// WATCHDOG
// ==========================================
// Maior bloqueio esperado: as 5 retransmissões de 5 s de um telecomando, com o
// watchdog alimentado entre elas.
#define WDT_TIMEOUT_S 30

// ==========================================
// PROTOCOLO TT&C <-> HK
// ==========================================
#define TIMEOUT_ACK_HK_MS     5000 // janela de confirmação do HK
#define MAX_TENTATIVAS_TC     5    // retransmissões antes de desistir do telecomando
#define RESPOSTA_SEM_RESPOSTA 0xEE // enviado à GS quando o HK não responde

// ==========================================
// FILAS DO FREERTOS (Comunicação entre Cores)
// ==========================================
// Bursts e confirmações de TC para o downlink (Core 1)
extern QueueHandle_t filaTelemetriaParaGS;
// Comandos recebidos pelo LoRa (Core 0) para o envio ao HK pelo CAN
extern QueueHandle_t filaComandosParaHK;
// Resposta do HK (0xCF/0xFF) para a task de envio de comandos
extern QueueHandle_t filaAckHK;
// Pacotes de repasse (Missão 3) a transmitir em 433 MHz
extern QueueHandle_t filaRepasseTx;

void TaskLoRaRx_Routine(void *pvParameters); // Recebe da Terra (Core 0)
void TaskCanTx_Routine(void *pvParameters);  // Envia pro HK    (Core 0)
void TaskCanRx_Routine(void *pvParameters);  // Recebe do CAN   (Core 1)
void TaskLoRaTx_Routine(void *pvParameters); // Envia pra Terra (Core 1)

#endif
