#ifndef REMONTAGEM_H
#define REMONTAGEM_H

#pragma once

#include <Arduino.h>
#include "driver/twai.h"

// Remontagem genérica de uma rajada: header no primeiro ID da faixa ([0x02][N]) e
// N fatias nos IDs seguintes. O buffer usa passo fixo de 8 bytes por pacote, então
// o pacote k sempre começa no byte 8*k, mesmo que tenha menos de 8 bytes úteis.
#define REMONTAGEM_MAX_PACOTES 15

typedef struct {
    uint16_t base;        // ID do header (ex.: 0x120)
    uint8_t  esperados;   // N do header; 0 = nenhuma rajada em andamento
    uint8_t  recebidos;
    uint8_t  total;       // N da última rajada completa
    uint8_t  destino;     // byte 2 do header, quando existe (faixa 0x170)
    uint8_t  buffer[REMONTAGEM_MAX_PACOTES * 8];
} Remontagem;

static inline void remontagemIniciar(Remontagem* r, uint16_t base) {
    memset(r, 0, sizeof(Remontagem));
    r->base = base;
}

// true se o frame pertence à faixa desta remontagem (header ou fatia)
static inline bool remontagemPertence(const Remontagem* r, uint32_t id) {
    return id >= r->base && id <= (uint32_t)(r->base + 0x0F);
}

// Processa um frame da faixa. Retorna true quando a rajada acabou de completar;
// nesse caso, r->buffer contém os r->total pacotes.
static inline bool remontagemProcessar(Remontagem* r, const twai_message_t* msg) {
    if (msg->identifier == r->base) {
        if (msg->data_length_code >= 2 && msg->data[0] == 0x02 &&
            msg->data[1] > 0 && msg->data[1] <= REMONTAGEM_MAX_PACOTES) {
            r->esperados = msg->data[1];
            r->recebidos = 0;
            r->destino = (msg->data_length_code >= 3) ? msg->data[2] : 0xFF;
            memset(r->buffer, 0, sizeof(r->buffer));
        } else {
            r->esperados = 0; // header de tipo desconhecido: ignora a rajada
        }
        return false;
    }

    if (r->esperados == 0) return false; // fatia sem header: descarta

    int indice = (int)msg->identifier - (int)r->base - 1;
    if (indice < 0 || indice >= r->esperados) return false;

    uint8_t n = msg->data_length_code > 8 ? 8 : msg->data_length_code;
    memcpy(&r->buffer[indice * 8], msg->data, n);
    r->recebidos++;

    if (r->recebidos >= r->esperados) {
        r->total = r->esperados;
        r->esperados = 0;
        return true;
    }
    return false;
}

#endif
