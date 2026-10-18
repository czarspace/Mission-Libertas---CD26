#include "can_manager.h"
#include "mode_manager.h"

// Float de 16 bits (IEEE 754 meia precisão) -> float. Os ganhos do SET_PID viajam
// assim para caber modo + Kp + Ki + Kd em um frame CAN de 8 bytes.
static float f16ParaFloat(uint16_t h) {
    uint16_t sinal = h >> 15, expoente = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
    float v;
    if (expoente == 0)       v = ldexpf((float)mantissa, -24);                  // subnormal
    else if (expoente == 31) v = mantissa ? NAN : INFINITY;
    else                     v = ldexpf((float)(mantissa | 0x400), expoente - 25);
    return sinal ? -v : v;
}



// ==========================================
// INICIALIZAÇÃO DO BARRAMENTO
// ==========================================
bool initCanBus() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS(); 
    
    // Filtro configurado para escutar APENAS 0x020 a 0x02F
    twai_filter_config_t f_config = {
        .acceptance_code = (0x020 << 21),
        .acceptance_mask = (0x00F << 21) | 0x1FFFFF, 
        .single_filter = true
    };

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        if (twai_start() == ESP_OK) {
            Serial.println("[CAN ADCS] TWAI Iniciado. Barramento selado para 0x02X.");
            return true;
        }
    }
    Serial.println("[CAN ADCS] ERRO: Falha ao iniciar barramento.");
    return false;
}

// ==========================================
// RECEPÇÃO DE COMANDOS (Escutando o HK)
// ==========================================
void receberComandosCAN() {
    twai_message_t rx_message;
    
    // portMAX_DELAY: Não consome CPU enquanto não chegar comando
    if (twai_receive(&rx_message, portMAX_DELAY) == ESP_OK) {
        if (!(rx_message.rtr) && rx_message.data_length_code > 0) {
            
            // Verifica se é o comando principal do HK para o ADCS (0x020)
            if (rx_message.identifier == CAN_ID_RX_BASE) {
                uint8_t comando = rx_message.data[0];
                
                Serial.printf("📥 [CAN RX] ID: 0x%02X | Comando Recebido: 0x%02X\n", rx_message.identifier, comando);
                
                switch(comando) {
                    case 0x00: { // SET_TIME (no boot e em operação)
                        if (rx_message.data_length_code < 5) { enviarInvalidoCAN(); return; }
                        uint32_t unix_time = 0;
                        memcpy(&unix_time, &rx_message.data[1], sizeof(unix_time));
                        struct timeval tv; tv.tv_sec = unix_time; tv.tv_usec = 0;
                        settimeofday(&tv, NULL);
                        Serial.printf("[CAN ADCS] Relogio ajustado: %lu\n", unix_time);
                        break;
                    }

                    case 0x01:
                        currentMode = MODE_IDLE;
                        prepararNovoModo(ganhosPID[PID_ESTABILIZACAO]);
                        Serial.println("[CAN ADCS] Entrando em Modo IDLE.");
                        break;

                    case 0xB0: // STOP — do ponto de vista do ADCS é idêntico ao 0x01
                        currentMode = MODE_IDLE;
                        prepararNovoModo(ganhosPID[PID_ESTABILIZACAO]);
                        Serial.println("[CAN ADCS] STOP recebido. Entrando em Modo IDLE.");
                        break;

                    case 0x20:
                        prepararNovoModo(ganhosPID[PID_ESTABILIZACAO]);
                        currentMode = MODE_ESTABILIZACAO;
                        Serial.println("[CAN ADCS] Entrando em Modo Estabilizacao (B-Dot).");
                        break;

                    case 0x21:
                        prepararNovoModo(ganhosPID[PID_ESTABILIZACAO]);
                        currentMode = MODE_DETERMINACAO;
                        Serial.println("[CAN ADCS] Entrando em Modo Determinacao Frio.");
                        break;

                    case 0x22:
                        prepararNovoModo(ganhosPID[PID_APONTAMENTO]);
                        currentMode = MODE_APONTAMENTO;
                        Serial.println("[CAN ADCS] Entrando em Modo Apontamento Solar Fixo.");
                        break;

                    case 0x24: // teste do motor em malha aberta (bancada) — relatório 0x24
                        prepararNovoModo(ganhosPID[PID_ESTABILIZACAO]);
                        currentMode = MODE_TI;
                        Serial.println("[CAN ADCS] Entrando em teste de motor.");
                        break;

                    case 0x25: // teste dos sensores, sem mexer no motor — relatório 0x25
                        prepararNovoModo(ganhosPID[PID_ESTABILIZACAO]);
                        currentMode = MODE_TP;
                        Serial.println("[CAN ADCS] Entrando em teste de sensores.");
                        break;

                    case 0x23: // Controle de atitude para o ângulo alvo (4 bytes de float)
                        // Valida se o HK enviou o byte do comando + 4 bytes do float (SP)
                        if (rx_message.data_length_code >= 5) {
                            float novo_sp = 0.0;

                            // Copia de forma segura os 4 bytes do payload para a variável float
                            memcpy(&novo_sp, &rx_message.data[1], sizeof(float));

                            // Atualiza a variável global de Setpoint de Atitude. Um novo
                            // alvo com o modo já ativo não reinicia o controle.
                            SP = (double)novo_sp;
                            if (currentMode != MODE_CONTROLE) {
                                prepararNovoModo(ganhosPID[PID_CONTROLE]);
                            }
                            currentMode = MODE_CONTROLE;

                            Serial.printf("[CAN ADCS] Entrando em Modo Controle. Novo Alvo SP: %.2f graus\n", SP);
                        } else {
                            // Se chegou o comando mas esqueceu o payload, aborta por segurança
                            Serial.println("❌ [CAN ADCS] Erro: Comando Controle recebido sem o angulo (SP).");
                            enviarInvalidoCAN(); 
                            return;
                        }
                        break;

                    case 0x26: { // SET_PID: [0x26][modo][Kp f16][Ki f16][Kd f16]
                        uint16_t h[3];
                        if (rx_message.data_length_code < 8) { enviarInvalidoCAN(); return; }
                        memcpy(h, &rx_message.data[2], 6);
                        uint8_t modo = rx_message.data[1];
                        float kp = f16ParaFloat(h[0]), ki = f16ParaFloat(h[1]), kd = f16ParaFloat(h[2]);
                        if (!gravarGanhosPID(modo, kp, ki, kd)) {
                            Serial.printf("❌ [CAN ADCS] SET_PID inválido (modo 0x%02X).\n", modo);
                            enviarInvalidoCAN();
                            return;
                        }
                        Serial.printf("[CAN ADCS] SET_PID modo 0x%02X: Kp = %.6f | Ki = %.6f | Kd = %.6f\n", modo, kp, ki, kd);
                        break;
                    }

                    default:
                        Serial.println("❌ [CAN ADCS] Comando Desconhecido.");
                        enviarInvalidoCAN(); 
                        return; 
                }
                
                // Se tudo deu certo, responde 0xCF para o HK
                enviarConfirmacaoCAN();
            }
        }
    }
}

// ==========================================
// RESPOSTAS DE TELECOMANDO 
// ==========================================
void enviarConfirmacaoCAN() {
    twai_message_t tx_message;
    tx_message.identifier = CAN_ID_CONF_ADCS;
    tx_message.extd = 0;
    tx_message.rtr = 0;
    tx_message.data_length_code = 1; 
    tx_message.data[0] = 0xCF; 

    if (twai_transmit(&tx_message, pdMS_TO_TICKS(10)) == ESP_OK) {
        Serial.println("📤 [CAN TX] Confirmacao (0xCF) enviada ao HK.");
    }
}

void enviarInvalidoCAN() {
    twai_message_t tx_message;
    tx_message.identifier = CAN_ID_CONF_ADCS;
    tx_message.extd = 0;
    tx_message.rtr = 0;
    tx_message.data_length_code = 1; 
    tx_message.data[0] = 0xFF; 

    if (twai_transmit(&tx_message, pdMS_TO_TICKS(10)) == ESP_OK) {
        Serial.println("📤 [CAN TX] Comando Invalido (0xFF) relatado ao HK.");
    }
}

// ==========================================
// FUNÇÃO AUXILIAR PRIVADA
// ==========================================
static void enviarPacoteTWAI(uint32_t id_mensagem, uint8_t tamanho, uint8_t* dados) {
    twai_message_t tx_message;
    tx_message.identifier = id_mensagem;
    tx_message.extd = 0;
    tx_message.rtr = 0;
    tx_message.data_length_code = tamanho;

    for (int i = 0; i < tamanho; i++) {
        tx_message.data[i] = dados[i];
    }

    twai_transmit(&tx_message, pdMS_TO_TICKS(10));
}

// ==========================================
// ENVIO DE TELEMETRIA (Rajada Agnóstica)
// ==========================================
void despacharRajadaCAN(PacoteTelemetria* pacotes, uint8_t quantidade) {
    
    // -------------------------------------------------------------
    // 1. ENVIO DO CABEÇALHO (HEADER DO ADCS -> 0x120)
    // -------------------------------------------------------------
    // Byte 0: Tipo da Mensagem (0x02 = Telemetria Dinâmica)
    // Byte 1: Quantidade de pacotes que o HK/TT&C deve esperar
    uint8_t payload_header[2] = {0x02, quantidade};
    
    enviarPacoteTWAI(CAN_ID_TX_BASE, 2, payload_header);
    vTaskDelay(pdMS_TO_TICKS(2)); 

    // -------------------------------------------------------------
    // 2. ENVIO DA RAJADA SEQUENCIAL (A partir de 0x121)
    // -------------------------------------------------------------
    uint32_t id_base = CAN_ID_TX_BASE + 1; 
    
    for (uint8_t i = 0; i < quantidade; i++) {
        uint32_t id_atual = id_base + i;
        enviarPacoteTWAI(id_atual, pacotes[i].tamanho, pacotes[i].dados);
    }
    
    Serial.println("📤 [CAN TX] Rajada de Atitude/Dinâmica concluída.");
}