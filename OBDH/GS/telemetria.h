#ifndef TELEMETRIA_H
#define TELEMETRIA_H
#pragma once

#include "config.h"

// Linha para a serial (via filaSaida; só a task de saída escreve na Serial)
void saida(const char* fmt, ...);

// Mensagem para o operador: "# ...". O software oficial ignora estas linhas.
void info(const char* fmt, ...);

// true = além das linhas do software oficial, imprime cada burst decodificado
// campo a campo (como o antigo downlink.ino). Alternado pelo comando V.
extern volatile bool modoVerboso;

// Decodifica um pacote LoRa completo ([destino][tipo][dados]) recebido em
// 433 ou 915 MHz e emite as linhas correspondentes.
void decodificarPacote(const uint8_t* buf, int tamanho, float rssi, float snr, uint16_t freqMHz);

// Imprime o config.json de modos para o software oficial (comando CONFIG)
void imprimirConfigGS();

#endif
