#include "sd_manager.h"
#include "mode_manager.h"
#include <stdarg.h>

// Arquivos abertos globalmente (dentro do escopo deste módulo)
File arquivoHK;
File arquivoModo;

// false se o cartão não montou, se o LOG_OP.txt não abriu ou se uma escrita falhou
static volatile bool sd_ok = false;
bool sdOk() { return sd_ok; }

uint32_t timestampAtual() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec;
}

bool initSD(const char* motivo_reset, uint16_t boots) {
    Serial.print("[SD] Inicializando cartao MicroSD...");

    if (!SD.begin(PIN_SD_CS)) {
        Serial.println(" FALHA! Verifique as conexoes.");
        sd_ok = false;
        return false;
    }

    Serial.println(" OK!");

    // Registro de início: ainda sem sincronização de tempo (o relógio conta do boot).
    // ts,BOOT,OBDH,<motivo do reset>,<nº do boot>
    arquivoHK = SD.open("/LOG_OP.txt", FILE_APPEND);
    if (arquivoHK) {
        arquivoHK.printf("%lu,BOOT,OBDH,%s,%u\n", (unsigned long)timestampAtual(), motivo_reset, boots);
        arquivoHK.flush();
        sd_ok = true;
    } else {
        Serial.println("[SD] ERRO: Nao foi possivel criar LOG_OP.txt");
        sd_ok = false;
    }
    return sd_ok;
}

// ==========================================
// REGISTRO CSV COM TIMESTAMP
// ==========================================
static void enfileirarRegistro(TipoLogSD tipo, uint32_t ts, const char* tag, const char* fmt, va_list args) {
    MensagemLog msg;
    msg.tipo = tipo;
    int n = snprintf(msg.texto, MAX_LOG_SIZE, "%lu,%s", (unsigned long)ts, tag);
    if (fmt && fmt[0] != '\0' && n > 0 && n < MAX_LOG_SIZE - 1) {
        msg.texto[n++] = ',';
        vsnprintf(&msg.texto[n], MAX_LOG_SIZE - n, fmt, args);
    }
    xQueueSend(filaGravacaoSD, &msg, pdMS_TO_TICKS(50));
}

void registrarLog(TipoLogSD tipo, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    enfileirarRegistro(tipo, timestampAtual(), tag, fmt, args);
    va_end(args);
}

void registrarLogTs(TipoLogSD tipo, uint32_t ts, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    enfileirarRegistro(tipo, ts, tag, fmt, args);
    va_end(args);
}

void registrarEvento(obdh_mode_t anterior, obdh_mode_t novo, const char* gatilho) {
    registrarLog(LOG_HK_GERAL, "EVT", "0x%02X,0x%02X,%s",
                 obterHexModo(anterior), obterHexModo(novo), gatilho);
}

// ==========================================
// ARQUIVOS
// ==========================================
bool getNomeArquivoModo(obdh_mode_t modo, char* buffer) {
    switch(modo) {
        case MODE_CARGA:       sprintf(buffer, "/carga.txt"); return true;
        case MODE_ABERTURA:    sprintf(buffer, "/abertura.txt"); return true;
        case MODE_TERM:        sprintf(buffer, "/term.txt"); return true;
        case MODE_TP1:         sprintf(buffer, "/tp1.txt"); return true;
        case MODE_TP2:         sprintf(buffer, "/tp2.txt"); return true;
        case MODE_TI:          sprintf(buffer, "/ti.txt"); return true;
        case MODE_STAB:        sprintf(buffer, "/stab.txt"); return true;
        case MODE_DET:         sprintf(buffer, "/det.txt"); return true;
        case MODE_APO:         sprintf(buffer, "/apo.txt"); return true;
        case MODE_CTRL:        sprintf(buffer, "/ctrl.txt"); return true;
        case MODE_MISSAO1:     sprintf(buffer, "/missao1.txt"); return true;
        case MODE_MISSAO3:     sprintf(buffer, "/missao3.txt"); return true;
        // IDLE grava no LOG_OP.txt; na Missão 2 a Gabriella não gera dados
        default:               buffer[0] = '\0'; return false;
    }
}

void gravarLogHousekeeping(const char* dados) {
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        if (arquivoHK) {
            if (arquivoHK.println(dados) == 0) sd_ok = false; // cartão removido ou com falha
            arquivoHK.flush();
        }
        xSemaphoreGive(sdMutex);
    }
}

// abrirLogModo/fecharLogModo/gravarLogModo mexem todos no mesmo File
// (arquivoModo) e tomam o mesmo mutex. Só são chamadas de dentro da
// TaskSdWrite_Routine; o mutex fica como cinto de segurança. Nenhuma delas chama
// a outra por dentro da seção travada — sdMutex não é recursivo.

void abrirLogModo(obdh_mode_t modo) {
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        if (arquivoModo) {
            arquivoModo.close();
        }

        char nome[32];
        if (getNomeArquivoModo(modo, nome)) {
            arquivoModo = SD.open(nome, FILE_APPEND);
            if (arquivoModo) {
                arquivoModo.printf("%lu,OPEN,0x%02X\n", (unsigned long)timestampAtual(), obterHexModo(modo));
                arquivoModo.flush();
            }
        }
        xSemaphoreGive(sdMutex);
    }
}

void fecharLogModo() {
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        if (arquivoModo) {
            arquivoModo.close();
        }
        xSemaphoreGive(sdMutex);
    }
}

void gravarLogModo(const char* dados) {
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        if (arquivoModo) {
            arquivoModo.println(dados);
            arquivoModo.flush();
        }
        xSemaphoreGive(sdMutex);
    }
}

// portMAX_DELAY (não um timeout curto): estas duas são as únicas mensagens da
// fila cuja perda corrompe a atribuição de TODO o resto da sessão — se o pedido
// de abrir/fechar for descartado (fila cheia), os dados do modo novo cairiam no
// arquivo do modo anterior. Bloquear aqui é seguro: troca de modo é evento raro.
void requisitarAberturaLogModo(obdh_mode_t modo) {
    MensagemLog msg;
    msg.tipo = LOG_ABRIR_MODO;
    msg.modo_alvo = modo;
    msg.texto[0] = '\0';
    xQueueSend(filaGravacaoSD, &msg, portMAX_DELAY);
}

void requisitarFechamentoLogModo() {
    MensagemLog msg;
    msg.tipo = LOG_FECHAR_MODO;
    msg.texto[0] = '\0';
    xQueueSend(filaGravacaoSD, &msg, portMAX_DELAY);
}
