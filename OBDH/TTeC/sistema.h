#ifndef SISTEMA_H
#define SISTEMA_H
#pragma once

// Watchdog de tasks e motivo do último reset (mesmo arquivo em HK/ e TTeC/).

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_arduino_version.h>

// Configura o watchdog de tasks: com timeout_s sem alimentar, a task registrada
// gera um pânico e a placa reinicia. Chamar no setup(), antes de criar as tasks.
inline void wdtConfigurar(uint32_t timeout_s) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t cfg = {};
    cfg.timeout_ms = timeout_s * 1000;
    cfg.idle_core_mask = 0;     // vigia só as tasks registradas
    cfg.trigger_panic = true;
    if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
#else
    esp_task_wdt_init(timeout_s, true);
#endif
}

// Chamada uma vez no início de cada task vigiada
inline void wdtRegistrar() { esp_task_wdt_add(NULL); }

// Chamada a cada volta do laço da task (e dentro de esperas longas)
inline void wdtAlimentar() { esp_task_wdt_reset(); }

inline const char* textoMotivoReset(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";   // ligado pela chave / energia
        case ESP_RST_EXT:       return "EXTERNO";   // pino EN
        case ESP_RST_SW:        return "SOFTWARE";  // esp_restart()
        case ESP_RST_PANIC:     return "PANICO";    // exceção ou abort
        case ESP_RST_INT_WDT:   return "WDT_INT";   // watchdog de interrupção
        case ESP_RST_TASK_WDT:  return "WDT_TASK";  // watchdog de tasks
        case ESP_RST_WDT:       return "WDT";       // outros watchdogs
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";  // queda de tensão
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "DESCONHECIDO";
    }
}

#endif
