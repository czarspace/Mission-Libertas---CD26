#include "Config.h"
#include "lora_manager.h"
#include "uwb_manager.h"
#include "mode_manager.h"

volatile sat_mode_t modoAtual = SAT_IDLE;
QueueHandle_t filaRecebidos;
QueueHandle_t filaEnvio;

// ==========================================
// TASKS
// ==========================================
// Core 0, prioridade 3: rádio LoRa (recepção por padrão, transmissão a partir da fila)
void TaskLoRa_Routine(void *pvParameters) {
    for (;;) {
        atenderLoRa();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// Core 1, prioridade 3: responder UWB (trocas de ranging dos outros nós)
void TaskUwb_Routine(void *pvParameters) {
    for (;;) {
        if (modoAtual == SAT_MISSAO2 || modoAtual == SAT_MISSAO3) {
            uwbAtenderResponder();
            vTaskDelay(pdMS_TO_TICKS(1));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// Core 1, prioridade 2: modos e decisão de repasse
void TaskModo_Routine(void *pvParameters) {
    PacoteRecebido p;
    for (;;) {
        if (xQueueReceive(filaRecebidos, &p, portMAX_DELAY) == pdTRUE) {
            processarPacote(&p);
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.printf("[SAT] Inicializando satelite complementar 0x%02X...\n", SAT_ID);

    filaRecebidos = xQueueCreate(4, sizeof(PacoteRecebido));
    filaEnvio = xQueueCreate(4, sizeof(PacoteEnvio));

    uwbIniciar();
    uwbStandby();
    initLoRa();

    xTaskCreatePinnedToCore(TaskLoRa_Routine, "LORA", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(TaskUwb_Routine, "UWB", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(TaskModo_Routine, "MODE", 4096, NULL, 2, NULL, 1);
    Serial.println("🚀 [SAT] Pronto. Radio em recepcao.");
}

void loop() {
    vTaskDelete(NULL);
}
