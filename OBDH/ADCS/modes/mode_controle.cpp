#include "mode_manager.h"
#include "sensores_manager.h"
#include "can_manager.h"

void executarControle() {
    // O Valor de SP (Target) permanecerá o que a rotina receberComandosCAN do Core 0 definir.
    
    // 1. Leituras Completas Agregadas
    lerLuminosidade();
    calcularAnguloSolar();
    lerGiroscopio();

    // 2. Transformação de Ângulo Esférico e PID
    if (abs(SP - theta) >= 180 && theta >= 0) { Setpoint = 360 - theta + SP; } 
    else if (abs(SP - theta) >= 180 && theta < 0) { Setpoint = -360 - theta + SP; } 
    else { Setpoint = SP - theta; }
    Input = 0.0; // erro do PID = Setpoint - Input = erro angular

    if (abs(Setpoint) >= abs(ParadaAngulo)) { myPID_e.Compute(); }
    else { Output = 0; }

    // 3. Amortecimento Ativo e Injeção no Driver
    double parametro_damping = (abs(gyro_z) > ParadaEst) ? gyro_z : 0.0; 
    Signal = Signal + 0.7 * parametro_damping - Output; 
    
    if (Signal <= -limitA) { Signal = -limitA; } 
    else if (Signal >= limitA) { Signal = limitA; } 

    SignalM = Signal * 0.1047; 
    motor.target = SignalM; 

    // =========================================================
    // 4. EMPACOTAMENTO TOTAL DE VOO PARA CAN — a cada chamada (50Hz, ritmo de
    // TaskControleADCS). O HK loga tudo que conseguir (~50/s); o TT&C já faz
    // o downsample pra 2Hz sozinho (TaskInjetaAdcs_Routine em TTeC.ino).
    // =========================================================
    {
        LoteTelemetria lote;
        lote.quantidade = 5; // Rajada completa

        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint32_t ts = (uint32_t)tv.tv_sec;
        uint8_t modo = obterHexModo(MODE_CONTROLE);

        float vel_motor = motor.shaft_velocity;

        // Pacote 0: Timestamp + Modo (mesmo padrão dos modos do HK)
        lote.pacotes[0].tamanho = 5;
        memcpy(lote.pacotes[0].dados, &ts, 4);
        memcpy(&lote.pacotes[0].dados[4], &modo, 1);

        // Pacote 1: Giro_Z e Angulo Calculado
        lote.pacotes[1].tamanho = 8;
        memcpy(&lote.pacotes[1].dados[0], &gyro_z, sizeof(float));
        memcpy(&lote.pacotes[1].dados[4], &theta, sizeof(float));

        // Pacote 2: Velocidade Motor e Lux V1
        lote.pacotes[2].tamanho = 8;
        memcpy(&lote.pacotes[2].dados[0], &vel_motor, sizeof(float));
        memcpy(&lote.pacotes[2].dados[4], &lux2_2, sizeof(float));

        // Pacote 3: Lux V2 e V3
        lote.pacotes[3].tamanho = 8;
        memcpy(&lote.pacotes[3].dados[0], &lux1_1, sizeof(float));
        memcpy(&lote.pacotes[3].dados[4], &lux1_2, sizeof(float));

        // Pacote 4: Lux V4
        lote.pacotes[4].tamanho = 4;
        memcpy(&lote.pacotes[4].dados[0], &lux2_1, sizeof(float));

        xQueueSend(filaTransmissaoCAN, &lote, 0);
    }
}