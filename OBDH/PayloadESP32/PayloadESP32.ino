#include "Config.h"
#include "can_manager.h"
#include "uwb_manager.h"

volatile pld_mode_t modoAtual = PLD_IDLE;

// ==========================================
// TASKS
// ==========================================
// Core 0: comandos do HK (0x031) e confirmações (0x1CF)
void TaskCan_Routine(void *pvParameters) {
    for (;;) {
        receberComandosCAN();
    }
}

// Core 1, prioridade 3: atende as trocas de ranging como responder (Missões 2 e 3).
// Acima da task de modo porque o ranging mede tempo de voo e é sensível a atrasos.
void TaskUwb_Routine(void *pvParameters) {
    for (;;) {
        if (modoAtual == PLD_MISSAO2 || modoAtual == PLD_MISSAO3) {
            uwbAtenderResponder();
            vTaskDelay(pdMS_TO_TICKS(1));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// Core 1, prioridade 2: atende os pedidos de medição (MEASURE) da Missão 3
void TaskModo_Routine(void *pvParameters) {
    uint8_t pedido;
    for (;;) {
        if (xQueueReceive(filaMedicoes, &pedido, portMAX_DELAY) != pdTRUE) continue;

        // Mede as distâncias aos outros dois satélites (Carolina e Beatriz) como
        // initiator e volta ao modo responder
        float d_carolina, d_beatriz;
        uwbMedir(ID_CAROLINA, &d_carolina);
        uwbMedir(ID_BEATRIZ, &d_beatriz);
        uwbModoResponder();

        enviarRajadaDistancias((uint8_t)modoAtual, d_carolina, d_beatriz);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("[PLD] Inicializando ESP32 de payload...");

    filaMedicoes = xQueueCreate(2, sizeof(uint8_t));

    uwbIniciar();
    uwbStandby();
    initCanBus();

    xTaskCreatePinnedToCore(TaskCan_Routine, "CAN_PLD", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(TaskUwb_Routine, "UWB_PLD", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(TaskModo_Routine, "MODE_PLD", 4096, NULL, 2, NULL, 1);
    Serial.println("🚀 [PLD] Pronto. Aguardando comandos do HK.");
}

void loop() {
    vTaskDelete(NULL);
}
