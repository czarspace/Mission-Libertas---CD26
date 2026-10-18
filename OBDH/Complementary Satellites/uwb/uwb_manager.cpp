#include "uwb_manager.h"
#include "Config.h"
#include <SPI.h>

static bool responder_ativo = false;

bool uwbIniciar() {
    SPI.begin(PIN_UWB_SCK, PIN_UWB_MISO, PIN_UWB_MOSI, PIN_UWB_CS);
    pinMode(PIN_UWB_RST, OUTPUT);
    digitalWrite(PIN_UWB_RST, HIGH);
    pinMode(PIN_UWB_IRQ, INPUT);
    // TODO DW1000: inicializar o transceptor (canal, taxa, antenna delay, endereço SAT_ID)
    return true;
}

void uwbModoResponder() {
    responder_ativo = true;
    // TODO DW1000: configurar o modo responder do two-way ranging
}

void uwbStandby() {
    responder_ativo = false;
    // TODO DW1000: colocar o transceptor em standby
}

void uwbAtenderResponder() {
    if (!responder_ativo) return;
    // TODO DW1000: atender a troca de ranging pendente (poll -> response -> final)
}

bool uwbMedir(uint8_t alvo, float* distancia_m) {
    (void)alvo;
    // TODO DW1000: two-way ranging com o nó "alvo" (DW1000 como initiator)
    *distancia_m = NAN;
    return false;
}
