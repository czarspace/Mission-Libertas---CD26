#include "aquecedor.h"
#include "sd_manager.h"
#include <esp_arduino_version.h>

// Estado do controle (só a task de modo e a task de comandos mexem nele, nunca ao mesmo tempo:
// o TERM roda na task de modo e o TI também; a transição de saída do TERM o desliga)
static estado_aquecedor_t estado = AQ_DESLIGADO;
static uint8_t flags = 0;
static float dutyPct = 0.0f;
static float integral = 0.0f;
static float tControle = NAN;
static uint32_t ultimoCiclo = 0;

// Travas com histerese
static bool corteAquecedor = false;
static bool corteBateria = false;
static bool frioBateria = false;

static const char* NOMES[] = { "DESLIGADO", "PI", "FRIO", "CORTE_BATERIA", "CORTE_AQUECEDOR", "FALHA_SENSORES", "FORCADO" };

const char* aquecedorNomeEstado(estado_aquecedor_t e) { return e <= AQ_FORCADO ? NOMES[e] : "?"; }
estado_aquecedor_t aquecedorEstado() { return estado; }
uint8_t aquecedorFlags() { return flags; }
float aquecedorTemperaturaControle() { return tControle; }
bool aquecedorLigado() { return dutyPct > 0.0f; }

// ==========================================
// PWM
// ==========================================
static void escreverDuty(float pct) {
    dutyPct = constrain(pct, 0.0f, 100.0f);
    const uint32_t maximo = (1UL << AQ_PWM_BITS) - 1;
    uint32_t valor = (uint32_t)lroundf(dutyPct / 100.0f * maximo);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PIN_MOSFET_AQUECEDOR, valor);
#else
    ledcWrite(AQ_PWM_CANAL, valor);
#endif
}

void aquecedorIniciar() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_MOSFET_AQUECEDOR, AQ_PWM_FREQ_HZ, AQ_PWM_BITS);
#else
    ledcSetup(AQ_PWM_CANAL, AQ_PWM_FREQ_HZ, AQ_PWM_BITS);
    ledcAttachPin(PIN_MOSFET_AQUECEDOR, AQ_PWM_CANAL);
#endif
    escreverDuty(0);
}

// ==========================================
// PI (mesma lei do pid_tcs_heater3.ino)
// ==========================================
// Anti-windup por integração condicional: com a saída no limite e o erro empurrando
// para além dele, a integral para de acumular.
static float calcularPI(float setpoint, float medida, float dt, float dutyMax) {
    float erro = setpoint - medida;
    float p = AQ_KP * erro;
    float integralNova = integral + AQ_KI * erro * dt;
    float saida = p + integralNova;

    bool empurrandoAcima  = (saida > dutyMax) && (erro > 0);
    bool empurrandoAbaixo = (saida < 0.0f) && (erro < 0);
    if (!empurrandoAcima && !empurrandoAbaixo) integral = integralNova;

    return constrain(p + integral, 0.0f, dutyMax);
}

// ==========================================
// CICLO DE CONTROLE
// ==========================================
float aquecedorAtualizar(const float t[N_TEMP]) {
    uint32_t agora = millis();
    float dt = ultimoCiclo ? (agora - ultimoCiclo) / 1000.0f : 0.5f;
    if (dt > 5.0f) dt = 5.0f; // pausa longa (ex.: reentrada no modo) não vira salto na integral
    ultimoCiclo = agora;

    // 1) Leituras válidas de cada grupo
    float bMin = NAN, bMax = NAN, aMin = NAN, aMax = NAN; // fminf/fmaxf ignoram NAN
    uint8_t nBat = 0, nAq = 0;
    for (uint8_t p = T_BAT1; p <= T_BAT3; p++) {
        if (isnan(t[p])) continue;
        nBat++; bMin = fminf(bMin, t[p]); bMax = fmaxf(bMax, t[p]);
    }
    for (uint8_t p = T_AQ1; p <= T_AQ3; p++) {
        if (isnan(t[p])) continue;
        nAq++; aMin = fminf(aMin, t[p]); aMax = fmaxf(aMax, t[p]);
    }

    flags = 0;
    if (nBat < 3) flags |= AQ_FLAG_BAT_INVALIDO;
    if (nAq < 3)  flags |= AQ_FLAG_AQ_INVALIDO;
    if (nAq == 0) flags |= AQ_FLAG_SEM_SENSOR_AQ;
    if (nBat == 0 && nAq > 0) {
        bMin = aMin; bMax = aMax;           // modo degradado
        flags |= AQ_FLAG_PROXY_AQUECEDOR;
    }
    tControle = bMin;

    // 2) Travas com histerese
    if (nAq > 0) {
        if (!corteAquecedor && aMax >= AQ_CORTE_AQUECEDOR_C) corteAquecedor = true;
        else if (corteAquecedor && aMax <= AQ_LIBERA_AQUECEDOR_C) corteAquecedor = false;
    }
    if (!isnan(bMax)) {
        if (!corteBateria && bMax >= AQ_LIMITE_QUENTE_C) corteBateria = true;
        else if (corteBateria && bMax <= AQ_LIBERA_QUENTE_C) corteBateria = false;
    }
    if (!isnan(bMin)) {
        if (bMin <= AQ_LIMITE_FRIO_C) frioBateria = true;
        else if (bMin >= AQ_SETPOINT_C) frioBateria = false;
    }
    bool armado = frioBateria;
    if (armado) flags |= AQ_FLAG_PI_ARMADO;

    // 3) Decisão: as condições de segurança vencem o PI
    estado_aquecedor_t novo;
    if (isnan(bMin))                    novo = AQ_FALHA_SENSORES;
    else if (corteAquecedor)            novo = AQ_CORTE_AQUECEDOR;
    else if (corteBateria)              novo = AQ_CORTE_BATERIA;
    else if (bMin <= AQ_LIMITE_FRIO_C)  novo = AQ_FRIO;
    else if (armado)                    novo = AQ_PI;
    else                                novo = AQ_DESLIGADO;

    float dutyMax = (nAq == 0) ? AQ_DUTY_MAX_SEM_SENSOR_AQ : AQ_DUTY_MAX;
    float duty;
    if (novo == AQ_PI) {
        duty = calcularPI(AQ_SETPOINT_C, bMin, dt, dutyMax);
    } else {
        duty = (novo == AQ_FRIO) ? dutyMax : 0.0f;
        integral = 0.0f; // fora do PI não acumula: ao voltar, recomeça do zero
    }
    escreverDuty(duty);

    if (novo != estado) {
        registrarLog(LOG_HK_GERAL, "AQUEC", "%s,%s,%.2f", aquecedorNomeEstado(estado), aquecedorNomeEstado(novo), bMin);
        estado = novo;
    }
    return dutyPct / 100.0f;
}

void aquecedorDesligar() {
    escreverDuty(0);
    integral = 0.0f;
    ultimoCiclo = 0;
    corteAquecedor = corteBateria = frioBateria = false;
    estado = AQ_DESLIGADO;
    flags = 0;
    tControle = NAN;
}

void aquecedorForcar(float fracao) {
    estado = AQ_FORCADO;
    escreverDuty(fracao * 100.0f);
}
