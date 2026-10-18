#include "sensores_manager.h"

TwoWire I2C_Bus2 = TwoWire(1); 
BH1750 bh1_0x23(0x23); BH1750 bh1_0x5C(0x5C);
BH1750 bh2_0x23(0x23); BH1750 bh2_0x5C(0x5C);
Adafruit_MPU6050 mpu;

BLDCDriver3PWM driver = BLDCDriver3PWM(MOT_PWM_A, MOT_PWM_B, MOT_PWM_C, MOT_EN);
BLDCMotor motor = BLDCMotor(11); 
MagneticSensorSPI sensor_motor = MagneticSensorSPI(AS5048_SPI, MAG_CS);

float lux1_1 = 0.0; float lux1_2 = 0.0; float lux2_1 = 0.0; float lux2_2 = 0.0;
float gyro_x = 0.0; float gyro_y = 0.0; float gyro_z = 0.0;

bool initSensores() {
    I2C_Bus2.begin(I2C_SDA_2, I2C_SCL_2, 400000);
    bool sucesso = true;
    if (!bh1_0x23.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire)) sucesso = false;
    if (!bh1_0x5C.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C, &Wire)) sucesso = false;
    if (!bh2_0x23.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &I2C_Bus2)) sucesso = false;
    if (!bh2_0x5C.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C, &I2C_Bus2)) sucesso = false;
    if (!mpu.begin(0x68, &Wire)) sucesso = false;
    return sucesso;
}

// ATIVAÇÃO DINÂMICA: Só roda quando o barramento elétrico já tem energia do HK
void initMotorFOC() {
    Serial.println("[MOTOR] Inicializando SimpleFOC em tempo de voo...");
    
    sensor_motor.init();
    motor.linkSensor(&sensor_motor);

    driver.voltage_power_supply = 12; 
    driver.init();
    motor.linkDriver(&driver);

    motor.voltage_limit = 8;
    motor.controller = MotionControlType::velocity;

    motor.init();
    motor.initFOC(); // Alinha eletricamente o motor (leva cerca de 1 a 2 segundos)
    
    Serial.println("✅ [MOTOR] SimpleFOC ativo e calibrado!");
}

// DESATIVAÇÃO DINÂMICA: Coloca os pinos em estado seguro
void desligarMotorFOC() {
    Serial.println("[MOTOR] Desativando driver por segurança elétrica...");
    motor.target = 0;
    motor.disable();          // Desliga pontes inversoras do SimpleFOC
    digitalWrite(MOT_EN, LOW); // Garante pino Enable em nível lógico lógico baixo
}

void lerLuminosidade() {

    lux1_1 = bh1_0x23.readLightLevel(); lux1_2 = bh1_0x5C.readLightLevel();
    lux2_1 = bh2_0x23.readLightLevel(); lux2_2 = bh2_0x5C.readLightLevel();

}

void lerGiroscopio() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    gyro_x = g.gyro.x; gyro_y = g.gyro.y; gyro_z = g.gyro.z;

}