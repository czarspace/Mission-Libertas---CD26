#Rodando o server:
Rode o comando:
```
py -m flask --app flaskr run
```

## Configuração da porta serial

Por padrão, o backend envia os comandos do Terminal pela porta `COM3`, a 9600 baud.
No PowerShell, a porta e o baud rate podem ser configurados antes de iniciar o servidor:

```powershell
$env:SERIAL_PORT = 'COM3'
$env:SERIAL_BAUD = '9600'
py -m flask --app flaskr run
```

Os comandos digitados no widget Terminal são enviados em UTF-8 e recebem `\n` ao final.

## Formato da telemetria recebida

Cada linha da porta serial deve conter a timestamp de coleta, o código hexadecimal do
modo e os valores na mesma ordem dos tipos configurados, separados por espaços:

```text
2026-09-04T14:30:00Z 0x01 1013.25 24.8
```

Espaços múltiplos e tabulações também são aceitos. Como alternativa, também é
possível enviar JSON:

```json
{"timestamp":"2026-09-04T14:30:00Z","modeCode":"0x01","values":[1013.25,24.8]}
```

O código é configurado no menu de cada modo. O backend usa esse código para mapear
os valores, armazena as timestamps de coleta e recebimento e envia ao navegador a
telemetria já nomeada.

O histórico de telemetria é salvo em `instance/historico_voo.csv`. Na primeira
execução desta versão, os dados de um eventual `instance/historico_voo.db` antigo
são copiados automaticamente para o CSV.
