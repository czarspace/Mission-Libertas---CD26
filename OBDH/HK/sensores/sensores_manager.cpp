#include "sensores_manager.h"
#include "Config.h"
#include "sd_manager.h"

// ==========================================
// TABELA DOS DS18B20 (A PREENCHER)
// ==========================================
// Preencher o ROM de cada papel com a saída do sketch Utilitarios/ds18b20_mapeamento
// depois que os sensores estiverem soldados e fixados nos pontos finais.
// Enquanto o ROM estiver zerado, o papel recebe o próximo sensor do seu barramento
// na ordem da busca OneWire (mapeamento provisório): os dados ficam válidos, mas a
// posição física de cada um é desconhecida. O ROM de cada papel vai para o
// LOG_OP.txt a cada boot, então os dados gravados agora podem ser remapeados depois.
//
// Distribuição provisória entre os barramentos: os sensores de bateria e de
// aquecedor estão nos dois barramentos, para que a falha de um barramento não
// derrube todo um conjunto. Recomenda-se manter essa divisão na fiação final.
const SensorTemp MAPA_DS18B20[N_TEMP] = {
    // nome        barramento  ROM
    { "BAT1",      1, {0, 0, 0, 0, 0, 0, 0, 0} },
    { "BAT2",      1, {0, 0, 0, 0, 0, 0, 0, 0} },
    { "BAT3",      2, {0, 0, 0, 0, 0, 0, 0, 0} },
    { "AQ1",       1, {0, 0, 0, 0, 0, 0, 0, 0} },
    { "AQ2",       2, {0, 0, 0, 0, 0, 0, 0, 0} },
    { "AQ3",       2, {0, 0, 0, 0, 0, 0, 0, 0} },
    { "INTERNA",   1, {0, 0, 0, 0, 0, 0, 0, 0} },
    { "EXTERNA",   2, {0, 0, 0, 0, 0, 0, 0, 0} },
    { "AUX1",      1, {0, 0, 0, 0, 0, 0, 0, 0} },
    { "AUX2",      2, {0, 0, 0, 0, 0, 0, 0, 0} },
};

// ==========================================
// TABELA DOS INA3221
// ==========================================
// Mapa inicial a partir do sketch de protótipo (Testes - Protótipo/INA3221):
// 0x40: ch0 livre no protótipo (bateria), ch1 painel da Face 2, ch2 consumo do sistema;
// 0x41: ch0 gerenciador/carregador, ch1 painel da Face 1, ch2 painel da Face 4.
// Confirmar com o EPS no teste C2-03. Shunt R100 (0,1 Ω) em todos os canais.
const CanalINA MAPA_INA[N_INA] = {
    // nome           unidade  canal  shunt (Ω)  inverter
    { "BATERIA",      1,       0,     0.1f,      false },
    { "PAINEL_F1",    2,       1,     0.1f,      false },
    { "PAINEL_F2",    1,       1,     0.1f,      false },
    { "PAINEL_F4",    2,       2,     0.1f,      false },
    { "CARREGADOR",   2,       0,     0.1f,      false },
    { "CONSUMO",      1,       2,     0.1f,      false },
};

// ==========================================
// INSTÂNCIAS DE HARDWARE
// ==========================================
static OneWire oneWire1(PIN_ONEWIRE_1);
static OneWire oneWire2(PIN_ONEWIRE_2);
static DallasTemperature ds1(&oneWire1);
static DallasTemperature ds2(&oneWire2);
static Adafruit_INA3221 ina1;
static Adafruit_INA3221 ina2;
static bool ina1Ok = false, ina2Ok = false;

static DeviceAddress endereco[N_TEMP];
static bool presente[N_TEMP];
static bool provisorio = false;

#define MAX_DS_POR_BARRAMENTO 10

static DallasTemperature* barramento(uint8_t b) { return b == 1 ? &ds1 : &ds2; }

static bool romVazio(const uint8_t* r) {
    for (int i = 0; i < 8; i++) if (r[i]) return false;
    return true;
}

static void romParaTexto(const uint8_t* r, char* s) {
    for (int i = 0; i < 8; i++) sprintf(&s[2 * i], "%02X", r[i]);
}

// ==========================================
// MAPEAMENTO DOS DS18B20
// ==========================================
static void mapearDS18B20() {
    DeviceAddress achados[2][MAX_DS_POR_BARRAMENTO];
    uint8_t n[2] = {0, 0};
    bool usado[2][MAX_DS_POR_BARRAMENTO] = {};
    char rom[17];

    // ROMs encontrados em cada barramento, na ordem da busca
    for (uint8_t b = 0; b < 2; b++) {
        DallasTemperature* ds = barramento(b + 1);
        ds->begin();
        ds->setResolution(DS18B20_RESOLUCAO_BITS);
        ds->setWaitForConversion(false); // a espera é feita em requisitarTodasTemperaturas()
        uint8_t total = ds->getDeviceCount();
        DeviceAddress a;
        for (uint8_t i = 0; i < total && n[b] < MAX_DS_POR_BARRAMENTO; i++) {
            if (ds->getAddress(a, i) && ds->validFamily(a)) memcpy(achados[b][n[b]++], a, 8);
        }
        Serial.printf("[SENSORES] Barramento %d: %d DS18B20\n", b + 1, n[b]);
    }

    // 1) Papéis com ROM preenchido: procura o sensor no barramento indicado
    for (uint8_t p = 0; p < N_TEMP; p++) {
        presente[p] = false;
        if (romVazio(MAPA_DS18B20[p].rom)) continue;
        uint8_t b = MAPA_DS18B20[p].barramento - 1;
        for (uint8_t i = 0; i < n[b]; i++) {
            if (!usado[b][i] && memcmp(achados[b][i], MAPA_DS18B20[p].rom, 8) == 0) {
                memcpy(endereco[p], achados[b][i], 8);
                presente[p] = usado[b][i] = true;
                break;
            }
        }
    }

    // 2) Papéis sem ROM: próximo sensor livre do barramento (provisório)
    provisorio = false;
    for (uint8_t p = 0; p < N_TEMP; p++) {
        if (!romVazio(MAPA_DS18B20[p].rom)) continue;
        provisorio = true;
        uint8_t b = MAPA_DS18B20[p].barramento - 1;
        for (uint8_t i = 0; i < n[b]; i++) {
            if (!usado[b][i]) {
                memcpy(endereco[p], achados[b][i], 8);
                presente[p] = usado[b][i] = true;
                break;
            }
        }
    }

    // 3) Registro: ROM de cada papel e sensores que sobraram
    for (uint8_t p = 0; p < N_TEMP; p++) {
        const char* situacao = !presente[p] ? "AUSENTE" : (romVazio(MAPA_DS18B20[p].rom) ? "PROVISORIO" : "MAPEADO");
        if (presente[p]) romParaTexto(endereco[p], rom); else romParaTexto(MAPA_DS18B20[p].rom, rom);
        Serial.printf("[SENSORES] T%-2d %-8s barramento %d  %s  %s\n", p + 1, MAPA_DS18B20[p].nome, MAPA_DS18B20[p].barramento, rom, situacao);
        registrarLog(LOG_HK_GERAL, "DS18B20", "T%d,%s,%d,%s,%s", p + 1, MAPA_DS18B20[p].nome, MAPA_DS18B20[p].barramento, rom, situacao);
    }
    for (uint8_t b = 0; b < 2; b++) {
        for (uint8_t i = 0; i < n[b]; i++) {
            if (usado[b][i]) continue;
            romParaTexto(achados[b][i], rom);
            Serial.printf("[SENSORES] Sensor sem papel no barramento %d: %s\n", b + 1, rom);
            registrarLog(LOG_HK_GERAL, "DS18B20", "EXTRA,%d,%s", b + 1, rom);
        }
    }
}

// ==========================================
// INICIALIZAÇÃO
// ==========================================
bool initSensores() {
    // MOSFETs desligados antes de qualquer outra coisa (o do aquecedor também;
    // depois ele passa para o PWM em aquecedorIniciar())
    const uint8_t mosfets[] = { PIN_MOSFET_AQUECEDOR, PIN_MOSFET_BURN_WIRE_1, PIN_MOSFET_ADCS,
                                PIN_MOSFET_CARGA_UTIL, PIN_MOSFET_EPS };
    for (uint8_t pino : mosfets) {
        pinMode(pino, OUTPUT);
        digitalWrite(pino, LOW);
    }

    Wire.begin();
    mapearDS18B20();

    ina1Ok = ina1.begin(INA1_ADDR);
    ina2Ok = ina2.begin(INA2_ADDR);
    for (uint8_t p = 0; p < N_INA; p++) {
        Adafruit_INA3221& ina = (MAPA_INA[p].unidade == 1) ? ina1 : ina2;
        ina.setShuntResistance(MAPA_INA[p].canal, MAPA_INA[p].shunt_ohm);
    }
    if (!ina1Ok) registrarLog(LOG_HK_GERAL, "FALHA", "INA3221,0x%02X", INA1_ADDR);
    if (!ina2Ok) registrarLog(LOG_HK_GERAL, "FALHA", "INA3221,0x%02X", INA2_ADDR);
    return ina1Ok && ina2Ok;
}

// IDLE, CARGA, TERM e a coleta dos testes leem os mesmos sensores a partir de
// tasks diferentes; o mutex evita leituras intercaladas no OneWire/I2C.
void travarSensores() {
    if (sensoresMutex) xSemaphoreTake(sensoresMutex, portMAX_DELAY);
}

void liberarSensores() {
    if (sensoresMutex) xSemaphoreGive(sensoresMutex);
}

// ==========================================
// TEMPERATURAS
// ==========================================
void requisitarTodasTemperaturas() {
    ds1.requestTemperatures();
    ds2.requestTemperatures();
    vTaskDelay(pdMS_TO_TICKS(DS18B20_CONVERSAO_MS + 10)); // cede a CPU durante a conversão
}

bool temperaturaValida(float t) {
    return !isnan(t) && t != DEVICE_DISCONNECTED_C && t != 85.0f && t >= -55.0f && t <= 125.0f;
}

float lerTemperatura(uint8_t papel) {
    if (papel >= N_TEMP || !presente[papel]) return NAN;
    float t = barramento(MAPA_DS18B20[papel].barramento)->getTempC(endereco[papel]);
    return temperaturaValida(t) ? t : NAN;
}

void lerTodasTemperaturas(float t[N_TEMP]) {
    for (uint8_t p = 0; p < N_TEMP; p++) t[p] = lerTemperatura(p);
}

uint16_t mascaraTemperaturasValidas(const float t[N_TEMP]) {
    uint16_t m = 0;
    for (uint8_t p = 0; p < N_TEMP; p++) if (!isnan(t[p])) m |= (1u << p);
    return m;
}

bool mapeamentoDS18B20Provisorio() { return provisorio; }

// ==========================================
// INA3221
// ==========================================
float lerTensaoINA1(uint8_t canal)   { return ina1.getBusVoltage(canal); }
float lerCorrenteINA1(uint8_t canal) { return ina1.getCurrentAmps(canal) * 1000.0f; }
float lerTensaoINA2(uint8_t canal)   { return ina2.getBusVoltage(canal); }
float lerCorrenteINA2(uint8_t canal) { return ina2.getCurrentAmps(canal) * 1000.0f; }

void lerCanalINA(uint8_t papel, float* tensao, float* corrente) {
    const CanalINA& c = MAPA_INA[papel];
    if (c.unidade == 1) {
        *tensao = lerTensaoINA1(c.canal);
        *corrente = lerCorrenteINA1(c.canal);
    } else {
        *tensao = lerTensaoINA2(c.canal);
        *corrente = lerCorrenteINA2(c.canal);
    }
    if (c.inverter) *corrente = -*corrente;
}

void lerBateria(float* tensao, float* corrente) {
    lerCanalINA(INA_BATERIA, tensao, corrente);
}

bool inaRespondeu(uint8_t unidade) { return unidade == 1 ? ina1Ok : ina2Ok; }
