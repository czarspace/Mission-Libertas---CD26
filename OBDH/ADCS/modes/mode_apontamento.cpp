#include "mode_manager.h"
#include "sensores_manager.h"
#include "can_manager.h" // Acesso à filaTransmissaoCAN e LoteTelemetria

void executarApontamento() {
    SP = 0.0; 

    // 1. Leituras Físicas
    lerLuminosidade();
    calcularAnguloSolar();
    lerGiroscopio();

    // 2. Lógica PID: o erro de apontamento (rotação mais curta) entra como Setpoint,
    // com Input = 0, de modo que o erro do PID (Setpoint - Input) é o próprio erro angular
    if (abs(SP - theta) >= 180 && theta >= 0) { Setpoint = 360 - theta + SP; }
    else if (abs(SP - theta) >= 180 && theta < 0) { Setpoint = -360 - theta + SP; }
    else { Setpoint = SP - theta; }
    Input = 0.0;

    if (abs(Setpoint) >= abs(ParadaAngulo)) { myPID_e.Compute(); } 
    else { Output = 0; }

    double parametro_damping = (abs(gyro_z) > ParadaEst) ? gyro_z : 0.0; 
    Signal = Signal + 0.7 * parametro_damping - Output; 
    
    if (Signal <= -limitA) { Signal = -limitA; } 
    else if (Signal >= limitA) { Signal = limitA; } 

    SignalM = Signal * 0.1047; 
    motor.target = SignalM; 

    // =========================================================
    // 3. EMPACOTAMENTO ASSÍNCRONO PARA O CAN — a cada chamada (50Hz, ritmo de
    // TaskControleADCS). O HK loga tudo que conseguir (~50/s); o TT&C já faz
    // o downsample pra 2Hz sozinho (TaskInjetaAdcs_Routine em TTeC.ino).
    // =========================================================
    {
        LoteTelemetria lote;
        lote.quantidade = 5;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint32_t ts = (uint32_t)tv.tv_sec;
        uint8_t modo = obterHexModo(MODE_APONTAMENTO);

        float vel_motor = motor.shaft_velocity;

        // Pacote 0: Timestamp + Modo (mesmo padrão dos modos do HK)
        lote.pacotes[0].tamanho = 5;
        memcpy(lote.pacotes[0].dados, &ts, 4);
        memcpy(&lote.pacotes[0].dados[4], &modo, 1);

        // Pacote 1: Giroscópio Z e Ângulo Calculado (Theta)
        lote.pacotes[1].tamanho = 8;
        memcpy(&lote.pacotes[1].dados[0], &gyro_z, sizeof(float));
        memcpy(&lote.pacotes[1].dados[4], &theta, sizeof(float));

        // Pacote 2: Velocidade do Motor e Lux V1
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

        // Envia o lote inteiro para a Fila. O Core 0 vai despachar quando puder.
        xQueueSend(filaTransmissaoCAN, &lote, 0);
    }
}