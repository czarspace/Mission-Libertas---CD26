#include "mode_manager.h"
#include "sensores_manager.h"
#include "can_manager.h"
#include "sd_manager.h"
#include "aquecedor.h"

// ==========================================
// PACOTE 4 DO BURST IDLE: ESTADO E FALHAS
// ==========================================
// O burst IDLE continua com TM ID 0x01 e o pacote 0 inalterado; o pacote 4 diz
// qual modo está ativo e quais falhas existem. Layout (8 bytes):
//   byte 0    modo ativo (TM ID, ex.: 0x20 em STAB)
//   byte 1    estado:  bit0 tempo sincronizado | bit1 HK ocupado | bit2 MOSFET payload
//                      bit3 MOSFET ADCS | bit4 MOSFET EPS | bit5 aquecedor ligado
//                      bit6 mapa DS18B20 provisório | bit7 desligamento do payload pendente
//   byte 2    falhas:  bit0 cartão SD | bit1 INA3221 0x40 | bit2 INA3221 0x41
//                      bit3 algum DS18B20 inválido | bit4 nenhum sensor de bateria válido
//                      bit5 ADCS sem resposta | bit6 RPi sem resposta | bit7 ESP32 payload sem resposta
//   bytes 3-4 DS18B20 válidos (uint16, bit i = T(i+1))
//   byte 5    motivo do último reset (esp_reset_reason_t)
//   bytes 6-7 contador de boots (uint16)
static uint8_t montarEstado() {
    uint8_t e = 0;
    if (tempo_sincronizado)                     e |= 0x01;
    if (hk_ocupado)                             e |= 0x02;
    if (digitalRead(PIN_MOSFET_CARGA_UTIL))     e |= 0x04;
    if (digitalRead(PIN_MOSFET_ADCS))           e |= 0x08;
    if (digitalRead(PIN_MOSFET_EPS))            e |= 0x10;
    if (aquecedorLigado())                      e |= 0x20;
    if (mapeamentoDS18B20Provisorio())          e |= 0x40;
    if (payload_aguardando_desligamento)        e |= 0x80;
    return e;
}

static uint8_t montarFalhas(uint16_t validos, uint8_t nBat) {
    uint8_t f = 0;
    if (!sdOk())                       f |= 0x01;
    if (!inaRespondeu(1))              f |= 0x02;
    if (!inaRespondeu(2))              f |= 0x04;
    if (validos != (1u << N_TEMP) - 1) f |= 0x08;
    if (nBat == 0)                     f |= 0x10;
    uint8_t nos = nosSemResposta();    // bit NO_ADCS, NO_RPI, NO_PLD_ESP32
    f |= (nos & 0x07) << 5;
    return f;
}

// Housekeeping: coleta a 1 Hz, registro no LOG_OP.txt a cada ciclo e rajada a cada 10 s
void executarModoIdle() {
    static uint8_t contador_envio_can = 0;
    static uint8_t falhas_anteriores = 0;
    static LoteTelemetria lote;

    // 1. Coleta física
    float t[N_TEMP];
    float v_bat, i_bat;
    travarSensores();
    requisitarTodasTemperaturas();
    lerTodasTemperaturas(t);
    lerBateria(&v_bat, &i_bat);
    liberarSensores();

    // Temperatura da bateria = o mais frio dos sensores de bateria válidos (o mesmo
    // critério do controle térmico); NAN se nenhum responder
    float t_bat = NAN;
    uint8_t nBat = 0;
    for (uint8_t p = T_BAT1; p <= T_BAT3; p++) {
        if (!isnan(t[p])) { t_bat = fminf(t_bat, t[p]); nBat++; }
    }
    float t_int = t[T_INTERNA];
    float t_ext = t[T_EXTERNA];
    uint16_t validos = mascaraTemperaturasValidas(t);
    uint8_t estado = montarEstado();
    uint8_t falhas = montarFalhas(validos, nBat);
    uint8_t modo_ativo = obterHexModo(modo_secundario);

    // 2. Registro no SD: ts,0x01,TBat,TInt,TExt,VBat,IBat,modo,estado,falhas,validos
    registrarLog(LOG_HK_GERAL, "0x01", "%.2f,%.2f,%.2f,%.3f,%.1f,0x%02X,0x%02X,0x%02X,0x%03X",
                 t_bat, t_int, t_ext, v_bat, i_bat, modo_ativo, estado, falhas, validos);

    // Mudança no conjunto de falhas vai para o Log de Eventos
    if (falhas != falhas_anteriores) {
        registrarLog(LOG_HK_GERAL, "FALHA", "0x%02X,0x%02X,0x%03X", falhas_anteriores, falhas, validos);
        falhas_anteriores = falhas;
    }

    // 3. Rajada para o solo (a cada 10 s / 0,1 Hz)
    contador_envio_can++;
    if (contador_envio_can >= 10) {
        contador_envio_can = 0;

        lote.quantidade = 5;
        uint8_t modo = obterHexModo(MODE_IDLE);
        uint32_t ts = timestampAtual();

        // Pacote 0: timestamp + modo (TM ID 0x01: o burst é sempre de housekeeping)
        lote.pacotes[0].tamanho = 5;
        memcpy(lote.pacotes[0].dados, &ts, 4);
        memcpy(&lote.pacotes[0].dados[4], &modo, 1);

        // Pacote 1: temperatura da bateria + temperatura interna
        lote.pacotes[1].tamanho = 8;
        memcpy(&lote.pacotes[1].dados[0], &t_bat, 4);
        memcpy(&lote.pacotes[1].dados[4], &t_int, 4);

        // Pacote 2: temperatura externa + tensão da bateria
        lote.pacotes[2].tamanho = 8;
        memcpy(&lote.pacotes[2].dados[0], &t_ext, 4);
        memcpy(&lote.pacotes[2].dados[4], &v_bat, 4);

        // Pacote 3: corrente da bateria
        lote.pacotes[3].tamanho = 4;
        memcpy(&lote.pacotes[3].dados[0], &i_bat, 4);

        // Pacote 4: estado e falhas (layout no topo do arquivo)
        lote.pacotes[4].tamanho = 8;
        lote.pacotes[4].dados[0] = modo_ativo;
        lote.pacotes[4].dados[1] = estado;
        lote.pacotes[4].dados[2] = falhas;
        memcpy(&lote.pacotes[4].dados[3], &validos, 2);
        lote.pacotes[4].dados[5] = motivo_reset;
        memcpy(&lote.pacotes[4].dados[6], &contador_boots, 2);

        enfileirarRajadaHK(&lote);
    }
}
