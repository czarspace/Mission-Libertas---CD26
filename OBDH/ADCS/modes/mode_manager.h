#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include "Config.h"
#include <PID_v1.h> 

// ==========================================
// VARIÁVEIS COMPARTILHADAS DE CONTROLE E ALGORITMOS
// ==========================================
extern double Input, Output, Setpoint;
extern double Signal, SignalM;
extern double SP;
extern float theta, theta_rad;

extern PID myPID_e;

// ==========================================
// ASSINATURAS DOS MODOS DE MISSÃO
// ==========================================
void executarModoIdle();
void executarDeterminacao();
void executarEstabilizacao();
void executarApontamento();
void executarControle();
void executarTesteSensores(); // MODE_TP
void executarTesteMotor();    // MODE_TI

// Funções utilitárias internas da física
void calcularAnguloSolar();

//Função para armazenar os HEX
uint8_t obterHexModo(MissionMode modo);

// ==========================================
// GANHOS DO PID POR MODO (STAB, APO, CTRL)
// ==========================================
typedef struct {
    float kp, ki, kd;
} GanhosPID;

// Índice de cada modo que usa o motor no vetor de ganhos
enum { PID_ESTABILIZACAO = 0, PID_APONTAMENTO, PID_CONTROLE, N_PID };
extern GanhosPID ganhosPID[N_PID];

// Carrega da NVS os ganhos de cada modo (ou os padrões do Config.h). Chamar no setup.
void carregarGanhosPID();

// Índice do vetor para um código de modo (0x20, 0x22, 0x23); -1 se o modo não usa PID
int indicePID(uint8_t modoHex);

// Grava novos ganhos para um modo (NVS). Se o modo estiver ativo, os ganhos são
// aplicados pela task de controle no próximo ciclo, sem reiniciar o PID.
bool gravarGanhosPID(uint8_t modoHex, float kp, float ki, float kd);

// Aplica ganhos pendentes do SET_PID (chamada pela task de controle)
void aplicarGanhosPendentes();

// Zera o comando acumulado da roda e reinicia o PID com os ganhos do modo
void prepararNovoModo(const GanhosPID& g);

#endif // MODE_MANAGER_H