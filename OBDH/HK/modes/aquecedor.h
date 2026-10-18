#ifndef AQUECEDOR_H
#define AQUECEDOR_H

#pragma once

#include <Arduino.h>
#include "sensores_manager.h"

// ==========================================
// CONTROLE DOS AQUECEDORES DA BATERIA (modo TERM)
// ==========================================
// Três aquecedores, acionados juntos pelo MOSFET do aquecedor (PWM), e seis
// sensores: três na bateria (BAT1..3) e três nos aquecedores (AQ1..3). Basta um
// sensor de cada grupo funcionando para o controle continuar.
//
// Limites da tabela tab:limites_tcs do capítulo de TCS: acionamento a 15 °C,
// alvo de cerca de 35 °C nas células e corte de 50 °C nos sensores dos aquecedores.
//
// Prioridade das decisões, da mais forte para a mais fraca:
//  1. aquecedor >= 50 °C (qualquer sensor AQ)  -> desligado até todos <= 45 °C;
//  2. bateria >= 35 °C (sensor BAT mais quente) -> desligado até <= 33 °C;
//  3. bateria <= 15 °C (sensor BAT mais frio)   -> 100% (e arma o PI);
//  4. PI armado                                 -> PI leva a bateria a 35 °C;
//  5. caso contrário                            -> desligado (economiza bateria).
// O PI é armado quando a bateria chega a 15 °C e desarmado quando o sensor mais frio
// da bateria chega ao setpoint.
// Sem nenhum sensor de bateria válido, os sensores dos aquecedores fazem o papel
// dela (leem mais quente com o aquecedor ligado, então o erro é para menos calor).
// Sem nenhum sensor válido, o aquecedor fica desligado.

// Faixa de segurança (prevalece sobre o PI)
#define AQ_LIMITE_FRIO_C          15.0f  // bateria <= isso: aquecedor no máximo
#define AQ_LIMITE_QUENTE_C        35.0f  // bateria >= isso: aquecedor desligado
#define AQ_LIBERA_QUENTE_C        33.0f  // ... até a bateria voltar a <= isso
#define AQ_CORTE_AQUECEDOR_C      50.0f  // qualquer aquecedor >= isso: desligado
#define AQ_LIBERA_AQUECEDOR_C     45.0f  // ... até todos voltarem a <= isso

// PI (Kd = 0). Ganhos iniciais = sintonia IMC/SIMC da bateria (pid_tcs_heater3.ino).
// TODO (TCS): validar com o circuito completo abaixo de 0 °C (testes C1-08 e C7-12).
#define AQ_SETPOINT_C             35.0f
#define AQ_KP                     0.6574f   // % de duty por °C de erro
#define AQ_KI                     0.001096f // % de duty por (°C · s)
#define AQ_DUTY_MAX               100.0f    // % (pode ser reduzido para limitar a potência)
#define AQ_DUTY_MAX_SEM_SENSOR_AQ 50.0f     // % quando nenhum sensor de aquecedor responde

// PWM do MOSFET do aquecedor
#define AQ_PWM_FREQ_HZ            2000
#define AQ_PWM_BITS               10
#define AQ_PWM_CANAL              0      // usado só no core ESP32 < 3.x

typedef enum : uint8_t {
    AQ_DESLIGADO = 0,     // PI desarmado: sem frio, aquecedor desligado
    AQ_PI,                // PI regulando a bateria no setpoint
    AQ_FRIO,              // bateria <= 15 °C: duty máximo
    AQ_CORTE_BATERIA,     // bateria >= 35 °C: desligado
    AQ_CORTE_AQUECEDOR,   // aquecedor >= 50 °C: desligado
    AQ_FALHA_SENSORES,    // nenhum sensor de bateria nem de aquecedor válido
    AQ_FORCADO            // acionamento direto do teste TI
} estado_aquecedor_t;

// Bits de aquecedorFlags()
#define AQ_FLAG_PI_ARMADO        0x01
#define AQ_FLAG_PROXY_AQUECEDOR  0x02 // sem sensor de bateria: usando os dos aquecedores
#define AQ_FLAG_SEM_SENSOR_AQ    0x04 // sem sensor de aquecedor: duty limitado
#define AQ_FLAG_BAT_INVALIDO     0x08 // ao menos um sensor de bateria inválido
#define AQ_FLAG_AQ_INVALIDO      0x10 // ao menos um sensor de aquecedor inválido

// Configura o PWM com o aquecedor desligado (chamar uma vez no setup)
void aquecedorIniciar();

// Um ciclo do controle com as temperaturas T1..T10 (NAN = inválida). Retorna a
// fração de acionamento aplicada (0 a 1).
float aquecedorAtualizar(const float t[N_TEMP]);

// Desliga o aquecedor e zera o estado do controle (saída do TERM e fim do TI)
void aquecedorDesligar();

// Acionamento direto, sem controle (teste funcional do TI). fracao de 0 a 1.
void aquecedorForcar(float fracao);

estado_aquecedor_t aquecedorEstado();
const char* aquecedorNomeEstado(estado_aquecedor_t e);
uint8_t aquecedorFlags();
float aquecedorTemperaturaControle(); // bateria usada no último ciclo (°C, NAN se nenhuma)
bool aquecedorLigado();

#endif
