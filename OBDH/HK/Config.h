#ifndef CONFIG_H
#define CONFIG_H

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <sys/time.h>
#include <string.h>

// ==========================================
// PINOUT DA ESP32 (Missão Libertas — satélite Gabriella)
// ==========================================

// Cartão SD
#define PIN_SD_CS   5

// ==========================================
// MÁQUINA DE ESTADOS (Modos de Operação)
// ==========================================
typedef enum {
    MODE_IDLE = 0,
    MODE_CARGA,
    MODE_ABERTURA,
    MODE_TERM,
    MODE_TP1,
    MODE_TP2,
    MODE_TI,
    MODE_STAB,
    MODE_DET,
    MODE_APO,
    MODE_CTRL,
    MODE_MISSAO1,
    MODE_MISSAO2,
    MODE_MISSAO3
} obdh_mode_t;

// Gatilhos do Log de Eventos (campo "trigger" do registro EVT)
#define GATILHO_TC    "TC"    // transição pedida por telecomando
#define GATILHO_AUTO  "AUTO"  // auto-retorno ao IDLE (ABERTURA, TP1, TP2, TI)
#define GATILHO_RESET "RESET" // TERM restaurado depois de um reset (ver HK.ino)

// ==========================================
// TEMPOS DO PROTOCOLO
// ==========================================
#define TIMEOUT_CONF_MS          500   // confirmação de ADCS / RPi / ESP32 de payload
#define TIMEOUT_CONF_SET_TIME_MS 2000  // confirmação do ADCS no SET_TIME do boot
#define TEMPO_BOOT_RPI_MS        60000 // boot da Raspberry Pi (Missão 1)
#define TEMPO_BOOT_ESP32_PLD_MS  2000  // boot do ESP32 de payload (Missões 2 e 3)
#define TEMPO_DESLIGAMENTO_PLD_MS 40000 // desligamento gracioso do payload
#define TIMEOUT_DISTANCIAS_MS    2000  // rajada de distâncias após um MEASURE (Missão 3)

// Watchdog de tasks: maior bloqueio esperado é a sequência TI (~15 s)
#define WDT_TIMEOUT_S            30

// NVS (Preferences): namespace e chaves do HK
#define NVS_NAMESPACE_HK  "hk"
#define NVS_CHAVE_TERM    "term"   // true enquanto o TERM estiver ativo (restaura após reset)
#define NVS_CHAVE_BOOTS   "boots"  // contador de inicializações

// ==========================================
// VARIÁVEIS GLOBAIS COMPARTILHADAS
// ==========================================
extern volatile obdh_mode_t modo_secundario;
extern volatile TickType_t delay_modo_secundario;

// Desligamento gracioso do payload: ao sair de uma missão, o MOSFET de carga útil
// só é cortado 40 s depois de avisar o computador de payload ativo.
extern volatile bool payload_aguardando_desligamento;
extern volatile uint32_t payload_desligando_desde;

// HK ocupado: ABERTURA, sequências TP1/TP2/TI e ativação da Missão 1 em curso.
// Enquanto true, telecomandos de modo são respondidos com INV.
extern volatile bool hk_ocupado;

// Sequência de tiro único pedida pela task de comandos e executada pela task de modo
extern volatile obdh_mode_t sequencia_pendente;

// Ativação da Missão 1 (boot de 60 s da RPi, sem bloquear a task de comandos)
extern volatile bool missao1_ativando;
extern volatile bool missao1_cancelada;
extern volatile uint32_t missao1_inicio;

// Estado de saúde enviado no housekeeping (pacote 4 do burst IDLE)
extern volatile bool tempo_sincronizado;
extern uint8_t  motivo_reset;     // esp_reset_reason() deste boot
extern uint16_t contador_boots;   // inicializações registradas na NVS

extern SemaphoreHandle_t sdMutex;
extern SemaphoreHandle_t sensoresMutex; // DS18B20 e INA3221 (IDLE, modos e testes)
extern SemaphoreHandle_t modoMutex;     // transições de modo

// Handles do RTOS
extern TaskHandle_t TaskCanRx_Handle;
extern TaskHandle_t TaskCanTx_Handle;
extern TaskHandle_t TaskComandos_Handle;
extern TaskHandle_t TaskIdle_Handle;
extern TaskHandle_t TaskMode_Handle;

#endif
