#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#pragma once

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "Config.h"


enum TipoLogSD {
    LOG_HK_GERAL,   // vai pro LOG_OP.txt geral
    LOG_MODO_BLOCO, // vai pro arquivo do modo atualmente aberto (stab.txt, tp1.txt, missao1.txt...)
    // Controles de abertura/fechamento de arquivo — viajam pela MESMA fila que
    // os dados (filaGravacaoSD) de propósito: como a escrita é assíncrona, um
    // fechamento feito na hora poderia acontecer antes de dados já enfileirados
    // serem gravados. Na mesma fila FIFO, todo dado enfileirado antes da troca
    // de modo é gravado no arquivo antigo antes dele fechar.
    LOG_ABRIR_MODO,
    LOG_FECHAR_MODO
};

#define MAX_LOG_SIZE 550

typedef struct {
    TipoLogSD tipo;
    obdh_mode_t modo_alvo; // só usado por LOG_ABRIR_MODO
    char texto[MAX_LOG_SIZE];
} MensagemLog;

// Fila global do SD
extern QueueHandle_t filaGravacaoSD;


// Inicializa o barramento SPI e o Cartão SD (grava o registro BOOT no LOG_OP.txt,
// com o motivo do reset e o número do boot)
bool initSD(const char* motivo_reset, uint16_t boots);

// Cartão montado e gravando (vai para as falhas do housekeeping)
bool sdOk();

// Registro CSV com timestamp Unix: "<ts>,<tag>,<campos>". É o formato de todo
// registro do cartão (Log de Eventos, telecomandos, confirmações e dados dos modos).
// tag: "EVT", "TC", "ACK", "INV", "ACT", "TIME", "OPEN"... ou o TM ID do modo
// ("0x01", "0x20"...) nos registros de dados.
void registrarLog(TipoLogSD tipo, const char* tag, const char* fmt, ...);
// Mesmo formato, com o timestamp informado (rajadas do ADCS trazem o próprio)
void registrarLogTs(TipoLogSD tipo, uint32_t ts, const char* tag, const char* fmt, ...);

// Log de Eventos: "<ts>,EVT,<modo anterior>,<modo novo>,<gatilho>"
void registrarEvento(obdh_mode_t anterior, obdh_mode_t novo, const char* gatilho);

// Grava diretamente (usadas só pela TaskSdWrite_Routine)
void gravarLogHousekeeping(const char* dados);
void gravarLogModo(const char* dados);

// Função interna/auxiliar para mapear o modo para o nome do arquivo
// (retorna false para os modos sem arquivo: IDLE e MISSAO2)
bool getNomeArquivoModo(obdh_mode_t modo, char* buffer);

// Abre/fecha o arquivo do modo DE FATO. Só chamadas pela TaskSdWrite_Routine.
void abrirLogModo(obdh_mode_t modo);
void fecharLogModo();

// Pontos de entrada usados pelo resto do firmware para PEDIR a abertura/fechamento
// do arquivo do modo — apenas enfileiram a solicitação, preservando a ordem.
void requisitarAberturaLogModo(obdh_mode_t modo);
void requisitarFechamentoLogModo();

// Timestamp Unix atual
uint32_t timestampAtual();


#endif
