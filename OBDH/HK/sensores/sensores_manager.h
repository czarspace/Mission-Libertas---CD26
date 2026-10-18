#ifndef SENSORES_MANAGER_H
#define SENSORES_MANAGER_H

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_INA3221.h>

// ==========================================
// MAPEAMENTO DE PINOS E ENDEREÇOS
// ==========================================
#define PIN_ONEWIRE_1   14
#define PIN_ONEWIRE_2   26

// 10 bits: 0,25 °C de resolução e 188 ms de conversão (os dois barramentos convertem juntos)
#define DS18B20_RESOLUCAO_BITS 10
#define DS18B20_CONVERSAO_MS   (750 >> (12 - DS18B20_RESOLUCAO_BITS))

#define INA1_ADDR    0x40 // INA3221 #1 (A0 no GND)
#define INA2_ADDR    0x41 // INA3221 #2 (A0 no VCC)

// ==========================================
// ATUADORES (MOSFETS CRÍTICOS)
// ==========================================
#define PIN_MOSFET_AQUECEDOR     27 // controlado só pelo módulo aquecedor (PWM)
#define PIN_MOSFET_BURN_WIRE_1   13
#define PIN_MOSFET_ADCS          25
#define PIN_MOSFET_CARGA_UTIL    33
#define PIN_MOSFET_EPS           32

// ==========================================
// MAPA DOS SENSORES DE TEMPERATURA (DS18B20)
// ==========================================
// Cada sensor tem um papel lógico. A ordem do enum é a ordem T1..T10 da
// telemetria e dos registros do SD (T1 = BAT1, ..., T10 = AUX2).
// A tabela MAPA_DS18B20 (sensores_manager.cpp) associa cada papel ao endereço
// ROM do sensor físico; ela é preenchida com a saída do sketch
// Utilitarios/ds18b20_mapeamento depois da soldagem nas PCBs oficiais.
typedef enum {
    T_BAT1 = 0,  // bateria (3 sensores, um ao lado de cada aquecedor)
    T_BAT2,
    T_BAT3,
    T_AQ1,       // aquecedores (proteção contra sobreaquecimento)
    T_AQ2,
    T_AQ3,
    T_INTERNA,   // interior do satélite (PCB do OBDH)
    T_EXTERNA,   // estrutura externa
    T_AUX1,      // pontos a definir pelo TCS
    T_AUX2,
    N_TEMP
} papel_temp_t;

typedef struct {
    const char* nome;
    uint8_t barramento;  // 1 = pino 14, 2 = pino 26
    uint8_t rom[8];      // endereço ROM; tudo zero = ainda não mapeado
} SensorTemp;

extern const SensorTemp MAPA_DS18B20[N_TEMP];

// ==========================================
// MAPA DOS CANAIS DOS INA3221
// ==========================================
// Corrente em mA; com 'inverter' = false, o sinal é o medido pelo INA3221.
// Convenção a confirmar no teste C2-04: corrente da bateria positiva = carregando.
typedef enum {
    INA_BATERIA = 0,
    INA_PAINEL_F1,
    INA_PAINEL_F2,
    INA_PAINEL_F4,
    INA_CARREGADOR,  // saída do carregador (CN3722)
    INA_CONSUMO,     // consumo do sistema
    N_INA
} papel_ina_t;

typedef struct {
    const char* nome;
    uint8_t unidade;   // 1 = INA3221 #1 (0x40), 2 = INA3221 #2 (0x41)
    uint8_t canal;     // 0, 1 ou 2
    float shunt_ohm;   // resistor shunt do canal na placa
    bool inverter;     // inverte o sinal da corrente
} CanalINA;

extern const CanalINA MAPA_INA[N_INA];

// ==========================================
// FUNÇÕES
// ==========================================
// Inicializa I2C, OneWire, MOSFETs (desligados) e os mapas. Retorna true se os
// dois INA3221 responderem. Registra no LOG_OP.txt o ROM de cada papel.
bool initSensores();

// Acesso exclusivo aos sensores (DS18B20 e INA3221 são lidos por várias tasks)
void travarSensores();
void liberarSensores();

// Dispara a conversão dos dois barramentos ao mesmo tempo e espera o fim dela
void requisitarTodasTemperaturas();

// Temperatura de um papel (°C), ou NAN se o sensor estiver ausente ou a leitura
// for inválida (−127 = desconectado, 85 = conversão não ocorreu, fora da faixa)
float lerTemperatura(uint8_t papel);
void  lerTodasTemperaturas(float t[N_TEMP]);
bool  temperaturaValida(float t);
uint16_t mascaraTemperaturasValidas(const float t[N_TEMP]); // bit i = T(i+1) válido
bool  mapeamentoDS18B20Provisorio(); // algum papel usou a ordem da busca no lugar do ROM

// Leitura bruta de cada INA3221 (usada pelos testes TP1, TP2 e TI)
float lerTensaoINA1(uint8_t canal);     // V
float lerCorrenteINA1(uint8_t canal);   // mA
float lerTensaoINA2(uint8_t canal);     // V
float lerCorrenteINA2(uint8_t canal);   // mA

// Leitura por papel (aplica o sinal da tabela): tensão (V) e corrente (mA)
void lerCanalINA(uint8_t papel, float* tensao, float* corrente);
void lerBateria(float* tensao, float* corrente);
bool inaRespondeu(uint8_t unidade);

#endif
