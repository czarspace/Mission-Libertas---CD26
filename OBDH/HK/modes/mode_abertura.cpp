#include "mode_manager.h"
#include "sensores_manager.h"
#include "can_manager.h"
#include "sd_manager.h"

#define EVENTO_BURN_LIGADO     0x01
#define EVENTO_BURN_DESLIGADO  0x02
#define TEMPO_BURN_WIRE_MS     1000

// Rajada de evento da abertura: pacote 0 (timestamp + 0x12) + pacote 1 (código do
// evento). No solo, 0x01 indica que o processo começou e 0x02 que terminou.
static void enviarEventoAbertura(uint8_t evento) {
    LoteTelemetria lote;
    lote.quantidade = 2;
    uint32_t ts = timestampAtual();
    uint8_t modo = obterHexModo(MODE_ABERTURA);

    lote.pacotes[0].tamanho = 5;
    memcpy(lote.pacotes[0].dados, &ts, 4);
    memcpy(&lote.pacotes[0].dados[4], &modo, 1);

    lote.pacotes[1].tamanho = 1;
    memset(lote.pacotes[1].dados, 0, 8);
    lote.pacotes[1].dados[0] = evento;

    enfileirarRajadaHK(&lote);
}

static void registrarBurnWire(bool ligado) {
    const char* estado = ligado ? "ON" : "OFF";
    registrarLog(LOG_HK_GERAL, "ACT", "BURN,%s", estado);
    registrarLog(LOG_MODO_BLOCO, "0x12", "BURN,%s", estado);
}

// Executada a cada 1 s pela task de modo enquanto o modo ABERTURA está ativo
void executarModoAbertura() {
    static uint32_t tempo_inicio = 0;
    static bool cortador_ligado = false;

    // 1. Primeira execução: liga o burn wire
    if (!cortador_ligado) {
        digitalWrite(PIN_MOSFET_BURN_WIRE_1, HIGH);
        tempo_inicio = millis();
        cortador_ligado = true;
        registrarBurnWire(true);
        enviarEventoAbertura(EVENTO_BURN_LIGADO);
        return;
    }

    // 2. Após 1 s: desliga, avisa o solo e volta ao IDLE sozinho
    if (millis() - tempo_inicio >= TEMPO_BURN_WIRE_MS) {
        digitalWrite(PIN_MOSFET_BURN_WIRE_1, LOW);
        cortador_ligado = false;
        registrarBurnWire(false);
        enviarEventoAbertura(EVENTO_BURN_DESLIGADO);

        transicionarModo(MODE_IDLE, GATILHO_AUTO);
        hk_ocupado = false;
    }
}
