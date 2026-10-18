#include "mode_manager.h"
#include "sensores_manager.h"
#include "can_manager.h"
#include "sd_manager.h"
#include "aquecedor.h"

#define TEMPO_ASSENTAMENTO_MOSFET_MS 200
#define TEMPO_RELATORIO_ADCS_MS      1200 // o ADCS reporta a cada 1 s
#define TEMPO_TESTE_MOTOR_MS         5000

// Registra um atuador do teste no LOG_OP.txt e no arquivo da sequência
static void registrarAtuador(const char* atuador, const char* estado) {
    registrarLog(LOG_HK_GERAL, "ACT", "%s,%s", atuador, estado);
    registrarLog(LOG_MODO_BLOCO, "ACT", "%s,%s", atuador, estado);
}

// Envia um comando interno ao ADCS e registra o resultado no arquivo da sequência
static resultado_conf_t comandoADCSTeste(uint8_t comando) {
    resultado_conf_t r = enviarComandoSimplesEAguardar(NO_ADCS, comando, TIMEOUT_CONF_MS);
    registrarLog(LOG_MODO_BLOCO, "ADCS", "0x%02X,%s", comando, textoResultado(r));
    return r;
}

// Coleta todos os sensores do satélite (HK + ADCS) e despacha a rajada de
// telemetria. Usada pela fase de coleta do TP1, do TP2 e do TI.
void coletarTodosSensores() {
    float t[N_TEMP];
    float v_ina1[3], i_ina1[3], v_ina2[3], i_ina2[3];

    travarSensores();

    // Temperatura PRIMEIRO, com os MOSFETs ainda desligados: o chaveamento dos
    // MOSFETs introduzia ruído no OneWire e os DS18B20 retornavam -127 °C.
    requisitarTodasTemperaturas();
    lerTodasTemperaturas(t);

    // Só agora liga os MOSFETs que alimentam os rails monitorados pelos INA3221
    digitalWrite(PIN_MOSFET_ADCS, HIGH);
    digitalWrite(PIN_MOSFET_EPS, HIGH);
    vTaskDelay(pdMS_TO_TICKS(TEMPO_ASSENTAMENTO_MOSFET_MS));

    for (int c = 0; c < 3; c++) {
        v_ina1[c] = lerTensaoINA1(c);
        i_ina1[c] = lerCorrenteINA1(c);
        v_ina2[c] = lerTensaoINA2(c);
        i_ina2[c] = lerCorrenteINA2(c);
    }

    digitalWrite(PIN_MOSFET_ADCS, LOW);
    digitalWrite(PIN_MOSFET_EPS, LOW);
    liberarSensores();

    uint8_t modo = obterHexModo(modo_secundario);
    char tag[5];
    snprintf(tag, sizeof(tag), "0x%02X", modo);

    // Registro: ts,0x14|0x15|0x16,T1..T10,V1..V3,I1..I3,V4..V6,I4..I6
    registrarLog(LOG_MODO_BLOCO, tag,
        "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
        "%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f",
        t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9],
        v_ina1[0], v_ina1[1], v_ina1[2], i_ina1[0], i_ina1[1], i_ina1[2],
        v_ina2[0], v_ina2[1], v_ina2[2], i_ina2[0], i_ina2[1], i_ina2[2]);

    // Rajada CAN: header (timestamp+modo) + 10 temperaturas + 12 valores de
    // tensão/corrente = 12 pacotes
    LoteTelemetria lote;
    lote.quantidade = 12;
    uint32_t ts = timestampAtual();

    lote.pacotes[0].tamanho = 5;
    memcpy(lote.pacotes[0].dados, &ts, 4);
    memcpy(&lote.pacotes[0].dados[4], &modo, 1);

    for (int k = 0; k < 5; k++) {
        lote.pacotes[1 + k].tamanho = 8;
        memcpy(&lote.pacotes[1 + k].dados[0], &t[2 * k], 4);
        memcpy(&lote.pacotes[1 + k].dados[4], &t[2 * k + 1], 4);
    }

    float valores[12] = {
        v_ina1[0], v_ina1[1], v_ina1[2], i_ina1[0], i_ina1[1], i_ina1[2],
        v_ina2[0], v_ina2[1], v_ina2[2], i_ina2[0], i_ina2[1], i_ina2[2]
    };
    for (int k = 0; k < 6; k++) {
        lote.pacotes[6 + k].tamanho = 8;
        memcpy(&lote.pacotes[6 + k].dados[0], &valores[2 * k], 4);
        memcpy(&lote.pacotes[6 + k].dados[4], &valores[2 * k + 1], 4);
    }

    enfileirarRajadaHK(&lote);

    // Relatório de sensores do ADCS (0x25): a rajada chega pela task de recepção
    // e é gravada no arquivo da sequência, que está aberto.
    comandoADCSTeste(0x25);
    vTaskDelay(pdMS_TO_TICKS(TEMPO_RELATORIO_ADCS_MS));
    comandoADCSTeste(0x01);
}

// Giro da roda de reação em malha aberta (TP2 e TI)
static void testarMotor() {
    digitalWrite(PIN_MOSFET_ADCS, HIGH);
    registrarAtuador("DRIVER_MOTOR", "ON");
    comandoADCSTeste(0x24);
    vTaskDelay(pdMS_TO_TICKS(TEMPO_TESTE_MOTOR_MS));
    comandoADCSTeste(0x01);
    digitalWrite(PIN_MOSFET_ADCS, LOW);
    registrarAtuador("DRIVER_MOTOR", "OFF");
}

// TP1: coleta de todos os sensores, sem atuadores móveis
void executarSequenciaTP1() {
    coletarTodosSensores();
}

// TP2: coleta do TP1 + giro da roda de reação
void executarSequenciaTP2() {
    coletarTodosSensores();
    testarMotor();
}

// TI: coleta do TP1 + abertura dos painéis + aquecedor + roda de reação
#define TEMPO_ABERTURA_PAINEL_MS  1000
#define TEMPO_TESTE_AQUECEDOR_MS  3000

void executarSequenciaTI() {
    coletarTodosSensores();

    digitalWrite(PIN_MOSFET_BURN_WIRE_1, HIGH);
    registrarAtuador("BURN", "ON");
    vTaskDelay(pdMS_TO_TICKS(TEMPO_ABERTURA_PAINEL_MS));
    digitalWrite(PIN_MOSFET_BURN_WIRE_1, LOW);
    registrarAtuador("BURN", "OFF");

    // Teste funcional do circuito do aquecedor, independente da temperatura
    // (o pino está no PWM do módulo aquecedor: digitalWrite não teria efeito)
    aquecedorForcar(1.0f);
    registrarAtuador("AQUECEDOR", "ON");
    vTaskDelay(pdMS_TO_TICKS(TEMPO_TESTE_AQUECEDOR_MS));
    aquecedorDesligar();
    registrarAtuador("AQUECEDOR", "OFF");

    testarMotor();
}

// Executada pela task de modo: roda a sequência pedida pela task de comandos e
// volta ao IDLE sozinha ao terminar.
void executarSequenciaPendente() {
    obdh_mode_t seq = sequencia_pendente;
    if (seq == MODE_IDLE) return;

    switch (seq) {
        case MODE_TP1: executarSequenciaTP1(); break;
        case MODE_TP2: executarSequenciaTP2(); break;
        case MODE_TI:  executarSequenciaTI();  break;
        default: break;
    }

    sequencia_pendente = MODE_IDLE;
    transicionarModo(MODE_IDLE, GATILHO_AUTO);
    hk_ocupado = false;
}
