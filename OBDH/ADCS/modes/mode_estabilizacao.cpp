#include "mode_manager.h"
#include "sensores_manager.h"
#include "can_manager.h"

void executarEstabilizacao() {
    // 1. Leitura Dinâmica
    lerGiroscopio();
    
    // 2. Lógica B-Dot Pura (Desofuscação): erro do PID = Setpoint - Input = gyro_z
    Setpoint = 0.0;
    Input = -gyro_z;
    double Parada_e = 0.2; 

    if (abs(Input) >= abs(Parada_e)) {
        myPID_e.Compute();
    } else {
        Output = 0;
    }

    Signal = Signal + Output;
    if (Signal <= -limitA) Signal = -limitA;
    else if (Signal >= limitA) Signal = limitA;

    SignalM = Signal * 0.1047; // mesma conversão usada em apontamento/controle
    motor.target = SignalM;

    // =========================================================
    // 3. EMPACOTAMENTO PARA CAN — a cada chamada (50Hz, ritmo de
    // TaskControleADCS). O HK loga tudo que conseguir (~50/s); o TT&C já faz
    // o downsample pra 2Hz sozinho antes de mandar pro solo
    // (TaskInjetaAdcs_Routine em TTeC.ino), então não precisa throttle aqui.
    // =========================================================
    {
        LoteTelemetria lote;
        lote.quantidade = 3;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint32_t ts = (uint32_t)tv.tv_sec;
        uint8_t modo = obterHexModo(MODE_ESTABILIZACAO);

        float vel_motor = motor.shaft_velocity;
        float sinal_atual = (float)Signal;

        // Pacote 0: Timestamp + Modo (mesmo padrão dos modos do HK)
        lote.pacotes[0].tamanho = 5;
        memcpy(lote.pacotes[0].dados, &ts, 4);
        memcpy(&lote.pacotes[0].dados[4], &modo, 1);

        // Pacote 1: Giroscópio Z e Acumulador do B-dot (Signal)
        // (decoder próprio de STAB no HK — não é ângulo solar, esse modo não calcula theta)
        lote.pacotes[1].tamanho = 8;
        memcpy(&lote.pacotes[1].dados[0], &gyro_z, sizeof(float));
        memcpy(&lote.pacotes[1].dados[4], &sinal_atual, sizeof(float));

        // Pacote 2: Velocidade do Motor
        lote.pacotes[2].tamanho = 4;
        memcpy(&lote.pacotes[2].dados[0], &vel_motor, sizeof(float));

        xQueueSend(filaTransmissaoCAN, &lote, 0);
    }
}