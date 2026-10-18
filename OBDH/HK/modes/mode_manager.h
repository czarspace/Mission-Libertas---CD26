#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#pragma once

#include <Arduino.h>
#include "Config.h"
#include "can_manager.h"

// ==========================================
// TRANSIÇÕES DE MODO
// ==========================================
// Opções de transicionarModo()
#define TR_PADRAO              0x00
#define TR_ADCS_JA_PARADO      0x01 // o handler já mandou 0x01/0xB0 ao ADCS e recebeu o ACK
#define TR_MANTER_MOSFET_ADCS  0x02 // 0xB0: ADCS parado, driver do motor continua energizado
#define TR_MANTER_PAYLOAD      0x04 // 0xC0: payload parado, MOSFET de carga útil continua ligado

// Ponto único de troca de modo. Executa as ações de saída do modo anterior
// (aquecedor desligado, ADCS parado, desligamento gracioso do payload), ajusta o
// MOSFET do EPS, troca os arquivos do SD e grava o registro EVT do Log de Eventos.
void transicionarModo(obdh_mode_t novo, const char* gatilho, uint8_t opcoes = TR_PADRAO);
bool modoUsaADCS(obdh_mode_t modo);
bool modoUsaPayload(obdh_mode_t modo);
void iniciarDesligamentoPayload(obdh_mode_t modo_payload);
TickType_t periodoDoModo(obdh_mode_t modo);

// Função para armazenar os HEX (TM ID = código do telecomando do modo)
uint8_t obterHexModo(obdh_mode_t modo);

// ==========================================
// TELECOMANDOS
// ==========================================
void processarTelecomando(const ComandoRecebido* tc);

// ==========================================
// MODOS DO HK
// ==========================================
void executarModoIdle();
void executarModoAbertura();
void executarModoCarga();
void executarModoTerm();

// Testes de bancada (tiro único, executados pela task de modo, auto-retorno ao IDLE)
void coletarTodosSensores();
void executarSequenciaTP1();
void executarSequenciaTP2();
void executarSequenciaTI();
void executarSequenciaPendente();

// Missão 1 (Raspberry Pi)
void atualizarAtivacaoMissao1();
void processarTelemetriaMissao1_Para_SD(const uint8_t* buffer_bruto, uint8_t tamanho);
void enviarStatusMissao1Confirmada();

// Missões 2 e 3 (ESP32 de payload)
bool ativarPayloadESP32(uint8_t comando);
void processarRajadaDistancias(const uint8_t* buffer, uint8_t total);
void executarModoMissao3();

// Telemetria do ADCS: decodificada pelo byte de modo do pacote 0
void processarRajadaADCS(const uint8_t* buffer, uint8_t total);

#endif
