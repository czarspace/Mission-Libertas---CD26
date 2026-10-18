import serial
import time
import random

PORTA = 'COM7' 
BAUD_RATE = 9600

try:
    porta_serial = serial.Serial(PORTA, BAUD_RATE)

    while True:
        temperatura = round(random.uniform(20.0, 30.0), 2)
        dado_str = f"11:11:11 0x01 {temperatura}"
    
        porta_serial.write(dado_str.encode('utf-8'))
        
        print(f"Dado injetado: {dado_str.strip()}")
        time.sleep(1)

except serial.SerialException as erro:
    print(f"\nErro de conexão: {erro}")
