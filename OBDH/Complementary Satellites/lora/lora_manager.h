#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H
#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "Config.h"

// Rádio SX1278 em 433 MHz, compartilhado com a GS, a Gabriella e o outro satélite:
// em recepção por padrão, transmite o que estiver na fila de envio e volta a receber.
bool initLoRa();
void atenderLoRa(); // uma passada: transmite a fila e recebe um pacote, se houver

// Monta [0x00][0x0D] + pacotes de 8 bytes e coloca na fila de envio
void enviarRajadaParaGS(const uint8_t* pacotes, uint8_t quantidade);
// Monta [destino][0x0E] + 32 bytes do pacote de repasse e coloca na fila de envio
void enviarRepasse(uint8_t destino, const uint8_t* dados32);

#endif
