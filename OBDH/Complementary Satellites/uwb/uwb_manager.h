#ifndef UWB_MANAGER_H
#define UWB_MANAGER_H
#pragma once

#include <Arduino.h>

// Interface do DWM1000. As rotinas de ranging (two-way ranging) ainda serão
// integradas; por enquanto as funções só mantêm o estado e retornam "sem medida".

bool uwbIniciar();               // inicializa o SPI e o DWM1000
void uwbModoResponder();         // passa a responder às trocas de ranging dos outros nós
void uwbStandby();               // coloca o DWM1000 em standby
void uwbAtenderResponder();      // atende uma troca de ranging pendente (chamada em loop)

// Mede a distância até o nó "alvo" (DW1000 como initiator). Retorna false e NAN
// enquanto o ranging não estiver integrado.
bool uwbMedir(uint8_t alvo, float* distancia_m);

#endif
