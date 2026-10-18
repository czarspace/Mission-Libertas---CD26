#include "Config.h"
#include "can_manager.h"
#include "lora_manager.h"
#include "sistema.h"

// ==========================================
// INSTÂNCIAS DAS FILAS
// ==========================================
QueueHandle_t filaTelemetriaParaGS;
QueueHandle_t filaComandosParaHK;
QueueHandle_t filaAckHK;
QueueHandle_t filaRepasseTx;

void TaskInjetaAdcs_Routine(void *pvParameters);

void setup() {
  Serial.begin(115200);
  Serial.println("[TT&C] Inicializando Sistema de Comunicacao Assincrono...");
  Serial.printf("[TT&C] Motivo do reset: %s\n", textoMotivoReset(esp_reset_reason()));

  wdtConfigurar(WDT_TIMEOUT_S);

  // 1. Criação das filas
  filaTelemetriaParaGS = xQueueCreate(6, sizeof(LoteTelemetria));
  filaComandosParaHK = xQueueCreate(10, sizeof(ComandoSat));
  filaAckHK = xQueueCreate(1, sizeof(uint8_t)); // xQueueOverwrite exige tamanho 1
  filaRepasseTx = xQueueCreate(2, sizeof(RepasseTx));

  if (filaTelemetriaParaGS == NULL || filaComandosParaHK == NULL || filaAckHK == NULL || filaRepasseTx == NULL) {
    Serial.println("❌ [ERRO] Falha ao criar objetos do FreeRTOS!");
    while(1);
  }

  // 2. Inicialização dos hardwares. O CAN tem algumas tentativas; os rádios são
  // independentes e quem falhar é reiniciado depois pelas tasks.
  bool canOk = false;
  for (int tentativa = 1; tentativa <= 5 && !canOk; tentativa++) {
    canOk = initCanBus();
    if (!canOk) delay(1000);
  }
  if (!canOk) Serial.println("❌ [TT&C] CAN não iniciou após 5 tentativas. Seguindo só com os rádios.");

  if (!initLoRaModems()) {
    Serial.println("⚠️ [TT&C] Algum rádio não iniciou; nova tentativa a cada 5 s.");
  }

  Serial.println("[TT&C] Hardwares inicializados. Subindo Tasks...");

  // ==========================================
  // CORE 0: PIPELINE DE UPLINK (Terra -> Satélite) e repasse em 433 MHz
  // ==========================================
  xTaskCreatePinnedToCore(TaskLoRaRx_Routine, "TaskLoRaRx", 4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(TaskCanTx_Routine, "TaskCanTx", 4096, NULL, 2, NULL, 0);

  // ==========================================
  // CORE 1: PIPELINE DE DOWNLINK (Satélite -> Terra)
  // ==========================================
  xTaskCreatePinnedToCore(TaskCanRx_Routine, "TaskCanRx", 4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(TaskLoRaTx_Routine, "TaskLoRaTx", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskInjetaAdcs_Routine, "TaskInjetaAdcs", 4096, NULL, 1, NULL, 1);
  Serial.println("✅ [TT&C] RTOS e Isolamento de Tarefas configurados.");
}

void loop() {
  vTaskDelete(NULL);
}

// ==========================================
// IMPLEMENTAÇÃO DAS TASKS
// ==========================================
// Todas são vigiadas pelo watchdog: nenhuma espera fica bloqueada por mais de 1 s
// sem voltar ao laço e alimentá-lo.

void TaskLoRaRx_Routine(void *pvParameters) {
  wdtRegistrar();
  for (;;) {
    wdtAlimentar();
    escutarComandosLoRa();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void TaskCanTx_Routine(void *pvParameters) {
  wdtRegistrar();
  for (;;) {
    wdtAlimentar();
    enviarComandoCAN();
  }
}

// Sem vTaskDelay entre chamadas: receberMensagensCAN() processa uma mensagem TWAI
// por chamada, e o ADCS envia telemetria a 50 Hz (até 300 frames/s). Um atraso aqui
// limitaria a vazão e estouraria a fila de recepção do driver. twai_receive() já
// bloqueia (cede a CPU) quando o barramento está quieto.
void TaskCanRx_Routine(void *pvParameters) {
  wdtRegistrar();
  for (;;) {
    wdtAlimentar();
    receberMensagensCAN();
  }
}

void TaskLoRaTx_Routine(void *pvParameters) {
  wdtRegistrar();
  for (;;) {
    wdtAlimentar();
    transmitirTelemetriaLoRa();
  }
}

// Downsampling da telemetria do ADCS: a cada 500 ms (2 Hz), envia a rajada mais
// recente, se tiver chegado alguma nova nesse intervalo.
void TaskInjetaAdcs_Routine(void *pvParameters) {
  wdtRegistrar();
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for(;;) {
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));
    wdtAlimentar();
    if (tem_dado_novo_adcs) {
      xQueueSend(filaTelemetriaParaGS, &ultimo_lote_adcs, pdMS_TO_TICKS(10));
      tem_dado_novo_adcs = false;
    }
  }
}
