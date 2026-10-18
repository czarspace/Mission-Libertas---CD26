#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <sys/time.h>

// ==========================================
// DEFINIÇÕES DE PINOS (Motor e Sensores)
// ==========================================
#define I2C_SDA 21
#define I2C_SCL 22

// Pinos do Motor BLDC (Driver e Sensor SPI)
#define MOT_PWM_A 13
#define MOT_PWM_B 12
#define MOT_PWM_C 14
#define MOT_EN    27
#define MOT_PIN   11
#define MAG_CS    5

// ==========================================
// VARIÁVEIS DE CONTROLE E PID
// ==========================================
// Ganhos PADRÃO de cada modo que usa o motor. Cada modo tem os seus, guardados
// separadamente na NVS: o telecomando SET_PID (0x26) grava novos ganhos para um
// modo, e eles valem também depois de um reset. Estes valores só são usados
// enquanto a NVS ainda não tiver ganhos gravados para o modo.
constexpr float KP_ESTABILIZACAO_PADRAO = 0.022;
constexpr float KI_ESTABILIZACAO_PADRAO = 0.001;
constexpr float KD_ESTABILIZACAO_PADRAO = 0.003;

constexpr float KP_APONTAMENTO_PADRAO = 0.022;
constexpr float KI_APONTAMENTO_PADRAO = 0.001;
constexpr float KD_APONTAMENTO_PADRAO = 0.003;

constexpr float KP_CONTROLE_PADRAO = 0.022;
constexpr float KI_CONTROLE_PADRAO = 0.001;
constexpr float KD_CONTROLE_PADRAO = 0.003;

// Faixa aceita para cada ganho no SET_PID
constexpr float GANHO_PID_MAXIMO = 1000.0;

// Velocidade fixa (rad/s) do teste de motor em malha aberta do modo TI — não dá
// pra usar Estabilização pra isso porque em bancada o satélite fica parado e o
// B-dot não teria do que reagir. Valor conservador, calibrar com o hardware real.
constexpr float VELOCIDADE_TESTE_TI = 5.0;

extern double ParadaAngulo;
extern double ParadaEst;
extern int limitA;

// ==========================================
// GESTÃO DINÂMICA DE ENERGIA
// ==========================================
extern bool motorInicializado; // Controla se o FOC está calibrado e ligado

// ==========================================
// MODOS DA MISSÃO (State Machine)
// ==========================================
enum MissionMode {
    MODE_IDLE,
    MODE_ESTABILIZACAO,
    MODE_DETERMINACAO,
    MODE_APONTAMENTO,
    MODE_CONTROLE,
    MODE_TP, // Teste Parcial: reporta os sensores do ADCS, sem mexer no motor
    MODE_TI  // Teste Integral: gira o motor em malha aberta por um tempo curto
};
extern MissionMode currentMode;

// ==========================================
// RTOS: FILAS, SEMÁFOROS E MUTEX
// ==========================================

// ==========================================
// ASSINATURAS DAS TAREFAS RTOS
// ==========================================
void TaskMotorADCS(void *pvParameters);
void TaskControleADCS(void *pvParameters);
void TaskCanRx_Routine(void *pvParameters);
void TaskCanTx_Routine(void *pvParameters);
void TaskInitCanCore0(void *pvParameters);

#endif // CONFIG_H