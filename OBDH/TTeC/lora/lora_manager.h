#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H
#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "Config.h"
#include "can_manager.h" // Para acessar as Filas, Structs e tipos LoRa


// ==========================================
// DEFINIÇÕES DO PROTOCOLO DA CONSTELAÇÃO
// ==========================================
#define DESTINO_GS 0x00
#define DESTINO_GB 0x01 // Nós (satélite Gabriella)
#define DESTINO_CR 0x02 // Carolina
#define DESTINO_BT 0x03 // Beatriz

// ==========================================
// ASSINATURAS DAS FUNÇÕES DO LORA
// ==========================================
// Inicia os dois rádios de forma independente; true só se os dois subirem.
// Um rádio que falhar é reiniciado pelas tasks a cada LORA_RETENTATIVA_MS.
bool initLoRaModems();
bool downlinkPronto();
bool uplinkPronto();
void escutarComandosLoRa();
void transmitirTelemetriaLoRa();

#endif // LORA_MANAGER_H
