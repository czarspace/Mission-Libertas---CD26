#include "mode_manager.h"
#include "sensores_manager.h"
#include <Preferences.h>

// ==========================================
// INSTANCIAÇÃO DAS VARIÁVEIS COMPARTILHADAS
// ==========================================
double Input = 0.0, Output = 0.0, Setpoint = 0.0;
double Signal = 0.0, SignalM = 0.0;
double SP = 0.0; // Ponto de Apontamento Alvo
float theta = 0.0, theta_rad = 0.0;

// Inicialização da biblioteca de controle PID. As constantes de cada modo são
// aplicadas via SetTunings() na transição de modo (ver can_manager.cpp) —
// aqui só precisamos de um valor inicial pro construtor.
PID myPID_e(&Input, &Output, &Setpoint, KP_ESTABILIZACAO_PADRAO, KI_ESTABILIZACAO_PADRAO, KD_ESTABILIZACAO_PADRAO, DIRECT);

// ==========================================
// GANHOS DO PID POR MODO
// ==========================================
GanhosPID ganhosPID[N_PID] = {
    { KP_ESTABILIZACAO_PADRAO, KI_ESTABILIZACAO_PADRAO, KD_ESTABILIZACAO_PADRAO },
    { KP_APONTAMENTO_PADRAO,   KI_APONTAMENTO_PADRAO,   KD_APONTAMENTO_PADRAO },
    { KP_CONTROLE_PADRAO,      KI_CONTROLE_PADRAO,      KD_CONTROLE_PADRAO },
};

static const char* NVS_NAMESPACE_PID = "adcs_pid";
static const char* CHAVE_PID[N_PID] = { "stab", "apo", "ctrl" };
static volatile bool ganhosPendentes = false;

int indicePID(uint8_t modoHex) {
    switch (modoHex) {
        case 0x20: return PID_ESTABILIZACAO;
        case 0x22: return PID_APONTAMENTO;
        case 0x23: return PID_CONTROLE;
        default:   return -1;
    }
}

void carregarGanhosPID() {
    Preferences nvs;
    if (nvs.begin(NVS_NAMESPACE_PID, true)) { // ainda não existe enquanto nenhum SET_PID foi recebido
        for (int i = 0; i < N_PID; i++) {
            if (nvs.getBytesLength(CHAVE_PID[i]) == sizeof(GanhosPID)) {
                nvs.getBytes(CHAVE_PID[i], &ganhosPID[i], sizeof(GanhosPID));
            }
        }
        nvs.end();
    }
    for (int i = 0; i < N_PID; i++) {
        Serial.printf("[PID] %-4s Kp = %.6f | Ki = %.6f | Kd = %.6f\n",
                      CHAVE_PID[i], ganhosPID[i].kp, ganhosPID[i].ki, ganhosPID[i].kd);
    }
}

static bool ganhoValido(float g) {
    return isfinite(g) && g >= 0.0f && g <= GANHO_PID_MAXIMO;
}

bool gravarGanhosPID(uint8_t modoHex, float kp, float ki, float kd) {
    int i = indicePID(modoHex);
    if (i < 0 || !ganhoValido(kp) || !ganhoValido(ki) || !ganhoValido(kd)) return false;

    ganhosPID[i].kp = kp;
    ganhosPID[i].ki = ki;
    ganhosPID[i].kd = kd;

    Preferences nvs;
    if (nvs.begin(NVS_NAMESPACE_PID, false)) {
        nvs.putBytes(CHAVE_PID[i], &ganhosPID[i], sizeof(GanhosPID));
        nvs.end();
    }

    // Modo ativo: a task de controle aplica no próximo ciclo (sem reiniciar o PID)
    if (obterHexModo(currentMode) == modoHex) ganhosPendentes = true;
    return true;
}

void aplicarGanhosPendentes() {
    if (!ganhosPendentes) return;
    ganhosPendentes = false;
    int i = indicePID(obterHexModo(currentMode));
    if (i < 0) return;
    myPID_e.SetTunings(ganhosPID[i].kp, ganhosPID[i].ki, ganhosPID[i].kd);
    Serial.printf("[PID] Novos ganhos aplicados ao modo ativo: Kp = %.6f | Ki = %.6f | Kd = %.6f\n",
                  ganhosPID[i].kp, ganhosPID[i].ki, ganhosPID[i].kd);
}

// ==========================================
// IMPLEMENTAÇÃO DE FUNÇÕES COMPARTILHADAS
// ==========================================
void executarModoIdle() {
    // No Modo IDLE, o motor é cortado pela TaskMotorADCS.
    // Aqui não executamos nada matematicamente.
}

void calcularAnguloSolar() {
    // Mapeamento dos barramentos físicos I2C para a lógica matemática
    float V2 = lux1_1; // Bus 1 - 0x23
    float V3 = lux1_2; // Bus 1 - 0x5C
    float V4 = lux2_1; // Bus 2 - 0x23
    float V1 = lux2_2; // Bus 2 - 0x5C

    // Matemática extraída do algoritmo original
    float s12 = sqrt(V2*V2 + V1*V1);
    float s23 = sqrt(V2*V2 + V3*V3);
    float s34 = sqrt(V4*V4 + V3*V3);
    float s41 = sqrt(V4*V4 + V1*V1);
    
    float soma = s12; 
    float V_calc = V2 - V4; 
    float V1_sr = V1 - V3; 
    float V2_sr = V2 - V4; 
    float V3_sr = 0; 
    float V4_sr = 0; 
    float offset = 0.0;

    if (s23 > soma) { soma = s23; V3_sr = V3 - V1; V2_sr = V2 - V4; V1_sr = 0; V4_sr = 0; }
    if (s34 > soma) { soma = s34; V3_sr = V3 - V1; V4_sr = V4 - V2; V1_sr = 0; V2_sr = 0; }
    if (s41 > soma) { soma = s41; V1_sr = V1 - V3; V4_sr = V4 - V2; V3_sr = 0; V2_sr = 0; }

    if (soma == s12) { soma = sqrt(V2_sr*V2_sr + V1_sr*V1_sr); V_calc = V2_sr; offset = 0.0; }
    if (soma == s23) { soma = sqrt(V2_sr*V2_sr + V3_sr*V3_sr); V_calc = V3_sr; offset = PI/2.0; }
    if (soma == s34) { soma = sqrt(V3_sr*V3_sr + V4_sr*V4_sr); V_calc = V4_sr; offset = PI; }
    if (soma == s41) { soma = sqrt(V4_sr*V4_sr + V1_sr*V1_sr); V_calc = V1_sr; offset = 3.0*PI/2.0; }
    
    if (soma > 0.0) {
        float frac = V_calc / soma; 
        theta_rad = offset + asin(frac); 
        
        if (theta_rad >= PI) { theta_rad -= 2.0 * PI; } 
        theta = theta_rad * 180.0 / PI; 
    }
}

// TM ID = código do comando que ativou o modo. Nos testes, o relatório de sensores
// leva 0x25 e o do motor 0x24 (os comandos internos que os geram).
uint8_t obterHexModo(MissionMode modo) {

    switch(modo) {
        case MODE_IDLE:          return 0x01;
        case MODE_ESTABILIZACAO: return 0x20;
        case MODE_DETERMINACAO:  return 0x21;
        case MODE_APONTAMENTO:   return 0x22;
        case MODE_CONTROLE:      return 0x23;
        case MODE_TP:            return 0x25;
        case MODE_TI:            return 0x24;
        default:                 return 0xFF;
    }
}

// Estado limpo a cada entrada de modo: zera o comando acumulado da roda e reinicia
// o PID (MANUAL -> AUTOMATIC reinicializa o termo integral a partir de Output = 0).
// TODO (ADCS): validar o efeito em bancada; antes desta rotina o PID nunca era
// colocado em AUTOMATIC, então Compute() não alterava a saída.
void prepararNovoModo(const GanhosPID& g) {
    Signal = 0.0;
    SignalM = 0.0;
    Input = 0.0;
    Output = 0.0;
    myPID_e.SetTunings(g.kp, g.ki, g.kd);
    myPID_e.SetMode(MANUAL);
    myPID_e.SetMode(AUTOMATIC);
}