#ifndef SENSORES_MANAGER_H
#define SENSORES_MANAGER_H

#include "Config.h"
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_MPU6050.h>
#include <SimpleFOC.h>

#define I2C_SDA_2 32
#define I2C_SCL_2 33

extern float lux1_1; extern float lux1_2;
extern float lux2_1; extern float lux2_2;
extern float gyro_x; extern float gyro_y; extern float gyro_z;

extern BLDCMotor motor;

// Funções de Inicialização e Desativação Dinâmica
bool initSensores();
void initMotorFOC();
void desligarMotorFOC(); // Corta sinais elétricos do driver

void lerLuminosidade();
void lerGiroscopio();

#endif // SENSORES_MANAGER_H