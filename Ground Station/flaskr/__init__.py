from flask import Flask, render_template, jsonify, request, current_app
import csv
import json
import re
import serial 
import threading
import os
import time
import math
import sqlite3
from contextlib import closing
from flask_socketio import SocketIO, emit
from datetime import datetime, timezone

CSV_FIELDS = (
    'timestamp', 'collection_timestamp', 'received_timestamp',
    'mode_code', 'tipo_dado', 'valor'
)


def _legacy_database_rows(database_path):
    """Lê o SQLite antigo para que a troca para CSV não apague o histórico."""
    if not os.path.exists(database_path):
        return []

    with closing(sqlite3.connect(database_path)) as conn:
        table = conn.execute(
            "SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'telemetria'"
        ).fetchone()
        if not table:
            return []

        columns = {
            row[1] for row in conn.execute('PRAGMA table_info(telemetria)').fetchall()
        }
        collection_column = (
            'COALESCE(collection_timestamp, timestamp)'
            if 'collection_timestamp' in columns else 'timestamp'
        )
        received_column = (
            'COALESCE(received_timestamp, timestamp)'
            if 'received_timestamp' in columns else 'timestamp'
        )
        mode_column = 'mode_code' if 'mode_code' in columns else "''"
        order_column = 'id' if 'id' in columns else 'rowid'
        return conn.execute(
            f'''
            SELECT timestamp, {collection_column}, {received_column},
                   {mode_column}, tipo_dado, valor
            FROM telemetria
            ORDER BY {order_column}
            '''
        ).fetchall()


def init_telemetry_csv(app):
    os.makedirs(app.instance_path, exist_ok=True)
    legacy_database_path = os.path.join(app.instance_path, 'historico_voo.db')

    with telemetry_lock:
        if os.path.exists(CSV_PATH) and os.path.getsize(CSV_PATH) > 0:
            return

        rows = _legacy_database_rows(legacy_database_path)
        temporary_path = f'{CSV_PATH}.tmp'
        with open(temporary_path, 'w', encoding='utf-8', newline='') as csv_file:
            writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
            writer.writeheader()
            for row in rows:
                writer.writerow(dict(zip(CSV_FIELDS, row)))
            csv_file.flush()
            os.fsync(csv_file.fileno())
        os.replace(temporary_path, CSV_PATH)






def _config_path(app):
    return os.path.join(app.instance_path, 'config.json')


def _load_modes(app):
    path = _config_path(app)
    if not os.path.exists(path):
        return []
    try:
        with open(path, 'r', encoding='utf-8') as f:
            data = json.load(f)
            return data.get('modes', [])
    except (json.JSONDecodeError, OSError):
        return []


def _save_modes(app, modes):
    path = _config_path(app)
    temporary_path = f'{path}.tmp'
    with open(temporary_path, 'w', encoding='utf-8') as f:
        json.dump({'modes': modes}, f, ensure_ascii=False, indent=2)
        f.flush()
        os.fsync(f.fileno())
    os.replace(temporary_path, path)


#PORTA = 'COM3' #Porta de entrada de dados
BAUD = 9600 
PORTA = 'COM5'
porta_serial = None
thread = None
try:
    porta_serial = serial.Serial(PORTA, BAUD) #Configura a porta serial
    porta_serial.timeout = 2
except serial.SerialException as exc:
    porta_serial = None
    print(f"Aviso: não foi possível abrir a porta serial {PORTA}: {exc}")

socketio = SocketIO()

CSV_PATH = ""
serial_connection = None
serial_lock = threading.Lock()
config_lock = threading.Lock()
telemetry_lock = threading.RLock()


def _normalize_mode_code(value):
    if isinstance(value, int) and not isinstance(value, bool):
        number = value
    elif isinstance(value, str):
        candidate = value.strip()
        if not re.fullmatch(r'(?:0[xX])?[0-9a-fA-F]+', candidate):
            raise ValueError('O código do modo deve ser hexadecimal (ex.: 0x01).')
        number = int(candidate, 16)
    else:
        raise ValueError('O código do modo é obrigatório.')
    if number < 0:
        raise ValueError('O código do modo não pode ser negativo.')
    return f'0x{number:X}'


def _validate_modes(modes):
    if not isinstance(modes, list):
        raise ValueError('O campo "modes" deve ser uma lista.')

    normalized = []
    used_codes = set()
    for mode in modes:
        if not isinstance(mode, dict):
            raise ValueError('Cada modo deve ser um objeto.')
        item = dict(mode)
        raw_code = item.get('code', item.get('modeCode'))
        code = _normalize_mode_code(raw_code) if raw_code not in (None, '') else ''
        if code:
            if code in used_codes:
                raise ValueError(f'O código {code} está repetido.')
            used_codes.add(code)
        data_types = item.get('dataTypes', [])
        if not isinstance(data_types, list) or not all(
            isinstance(value, str) and value.strip() for value in data_types
        ):
            raise ValueError(f'Os tipos de dados do modo {code} são inválidos.')
        item['code'] = code
        item.pop('modeCode', None)
        item['dataTypes'] = [value.strip() for value in data_types]
        if len(set(item['dataTypes'])) != len(item['dataTypes']):
            raise ValueError(f'O modo {code or item.get("name", "sem código")} possui tipos repetidos.')
        normalized.append(item)
    return normalized

def create_app(test_config=None):
    app = Flask(__name__, instance_relative_config=True)
    global CSV_PATH
    CSV_PATH = os.path.join(app.instance_path, 'historico_voo.csv')
    app.config.from_mapping(
        SECRET_KEY='dev',
        TELEMETRY_CSV=CSV_PATH,
        SERIAL_PORT=os.environ.get('SERIAL_PORT', PORTA),
        SERIAL_BAUD=int(os.environ.get('SERIAL_BAUD', BAUD)),
        SERIAL_COMMAND_NEWLINE=True,
        SERIAL_READER_ENABLED=True,
        # Temporário: emite telemetria simulada enquanto não há hardware conectado.
        SYNTHETIC_TELEMETRY_ENABLED=True,
        SYNTHETIC_TELEMETRY_INTERVAL=1.0,
    )
    app.config.from_mapping(test_config)
    socketio.init_app(app)
    os.makedirs(app.instance_path, exist_ok=True)

    @app.route('/')
    def base():
        return render_template('base.html')

    @app.route('/modes', methods=['GET'])
    def get_modes():
        return jsonify({'modes': _load_modes(app)})

    @app.route('/modes', methods=['POST'])
    def save_modes_route():
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict) or 'modes' not in payload:
            return jsonify({
                'status': 'error',
                'message': 'A requisição deve conter um JSON válido com o campo "modes".'
            }), 400
        try:
            modes = _validate_modes(payload['modes'])
        except ValueError as error:
            return jsonify({'status': 'error', 'message': str(error)}), 400
        with config_lock:
            _save_modes(app, modes)
        return jsonify({'status': 'ok', 'modes': modes})

    @app.route('/telemetry-availability', methods=['GET'])
    def telemetry_availability_route():
        mode_code = request.args.get('modeCode')
        try:
            return jsonify(query_telemetry_availability(mode_code))
        except ValueError as error:
            return jsonify({'message': str(error)}), 400
        except (OSError, csv.Error):
            return jsonify({'message': 'Não foi possível consultar a disponibilidade.'}), 500


    CSV_PATH = app.config['TELEMETRY_CSV']
    init_telemetry_csv(app)

    if app.config['SYNTHETIC_TELEMETRY_ENABLED']:
        synthetic_thread = threading.Thread(
            target=emit_synthetic_data,
            args=(app,),
            name='SyntheticTelemetryThread',
            daemon=True,
        )
        synthetic_thread.start()
    elif app.config['SERIAL_READER_ENABLED']:
        read_thread = threading.Thread(target=emit_data, args=(app,), name="ReadSerialDataThread")
        read_thread.daemon = True
        read_thread.start()

    return app


def _normalize_timestamp(value):
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        seconds = value / 1000 if value > 10_000_000_000 else value
        return datetime.fromtimestamp(seconds, timezone.utc).isoformat()
    if not isinstance(value, str) or not value.strip():
        raise ValueError('A timestamp de coleta é obrigatória.')
    candidate = value.strip()
    if re.fullmatch(r'\d+(?:\.\d+)?', candidate):
        numeric_value = float(candidate)
        seconds = numeric_value / 1000 if numeric_value > 10_000_000_000 else numeric_value
        return datetime.fromtimestamp(seconds, timezone.utc).isoformat()
    parsed = datetime.fromisoformat(candidate.replace('Z', '+00:00'))
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc).isoformat()


def parse_serial_message(raw_message, modes):
    if isinstance(raw_message, bytes):
        raw_message = raw_message.decode('utf-8')
    if not isinstance(raw_message, str) or not raw_message.strip():
        raise ValueError('Mensagem serial vazia.')

    text = raw_message.strip()
    if text.startswith('{'):
        message = json.loads(text)
        collected_at = message.get('timestamp', message.get('collectedAt'))
        mode_code = message.get('modeCode', message.get('mode', message.get('code')))
        values = message.get('values', message.get('data'))
    else:
        fields = text.split()
        if len(fields) < 3:
            raise ValueError('Formato esperado: timestamp código_hex valor1 valor2 ...')
        collected_at, mode_code, *values = fields

    if not isinstance(values, list):
        raise ValueError('O campo "values" deve ser uma lista.')
    normalized_code = _normalize_mode_code(mode_code)
    mode = next((item for item in modes if item.get('code') == normalized_code), None)
    if mode is None:
        raise ValueError(f'Não existe configuração para o modo {normalized_code}.')
    data_types = mode.get('dataTypes', [])
    if len(values) != len(data_types):
        raise ValueError(
            f'O modo {normalized_code} espera {len(data_types)} valores, mas recebeu {len(values)}.'
        )
    try:
        telemetry = {name: float(value) for name, value in zip(data_types, values)}
    except (TypeError, ValueError) as error:
        raise ValueError('Todos os valores de telemetria devem ser numéricos.') from error

    return {
        'timestamp': _normalize_timestamp(collected_at),
        'receivedTimestamp': datetime.now(timezone.utc).isoformat(),
        'modeCode': normalized_code,
        'modeName': mode.get('name', normalized_code),
        'values': telemetry,
    }


def read_serial(app):
    with app.app_context():
        with serial_lock:
            connection = _get_serial_connection()
            raw_message = connection.readline()
    if not raw_message:
        return None
    with config_lock:
        modes = _validate_modes(_load_modes(app))
    return parse_serial_message(raw_message, modes)


def emit_data(app):
    while True:
        try:
            dados = read_serial(app)
            if dados is None:
                continue
            save_telemetry(dados)
            socketio.emit('new_data', dados)
        except (serial.SerialException, OSError) as error:
            app.logger.warning('Falha na leitura serial: %s', error)
            time.sleep(2)
        except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
            app.logger.warning('Mensagem serial ignorada: %s', error)


def _synthetic_value(data_type, sample_number, position):
    """Gera uma curva previsível, mas com variação suficiente para testar gráficos."""
    name = data_type.lower()
    wave = math.sin(sample_number / 6 + position) * 0.5

    if any(term in name for term in ('dist', 'alt', 'altura')):
        return round(500 + (sample_number * 1.8) + (wave * 25), 2)
    if any(term in name for term in ('temp', 'temperatura')):
        return round(24 + (wave * 6), 2)
    if any(term in name for term in ('press', 'pressao', 'pressão')):
        return round(1013.25 + (wave * 12), 2)
    if any(term in name for term in ('volt', 'bateria')):
        return round(12.4 + (wave * 0.35), 3)
    if 'icao' in name:
        return float(891000 + (sample_number % 500))

    return round(50 + (position * 10) + (wave * 20), 2)


def build_synthetic_telemetry(app, sample_number):
    """Monta um payload idêntico ao produzido pela leitura da porta serial."""
    with config_lock:
        modes = _validate_modes(_load_modes(app))

    mode = next((item for item in modes if item.get('dataTypes')), None)
    if mode is None:
        return None

    data_types = mode['dataTypes']
    values = {
        data_type: _synthetic_value(data_type, sample_number, index)
        for index, data_type in enumerate(data_types)
    }
    now = datetime.now(timezone.utc).isoformat()
    return {
        'timestamp': now,
        'receivedTimestamp': now,
        'modeCode': mode['code'],
        'modeName': mode.get('name', mode['code']),
        'values': values,
    }


def emit_synthetic_data(app):
    """Emissor temporário para desenvolvimento da interface e dos widgets."""
    sample_number = 0
    interval = float(app.config['SYNTHETIC_TELEMETRY_INTERVAL'])

    while True:
        try:
            dados = build_synthetic_telemetry(app, sample_number)
            if dados is not None:
                save_telemetry(dados)
                socketio.emit('new_data', dados)
                sample_number += 1
        except (OSError, ValueError, csv.Error) as error:
            app.logger.warning('Falha ao gerar telemetria sintética: %s', error)

        time.sleep(max(interval, 0.1))

@socketio.on('connect')
def init_connection():
    emit('connection_status', {'connected': True})


def _get_serial_connection():
    global serial_connection

    configured_connection = current_app.config.get('SERIAL_CONNECTION')
    if configured_connection is not None:
        return configured_connection

    port = current_app.config['SERIAL_PORT']
    baud = current_app.config['SERIAL_BAUD']
    if serial_connection is None or not serial_connection.is_open:
        serial_connection = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=1,
            write_timeout=1
        )
    return serial_connection


@socketio.on('serial_command')
def handle_serial_command(payload):
    command = payload.get('command', '') if isinstance(payload, dict) else ''
    if not isinstance(command, str) or not command.strip():
        return {'ok': False, 'message': 'Digite um comando antes de enviar.'}
    if len(command) > 1024:
        return {'ok': False, 'message': 'O comando deve possuir no máximo 1024 caracteres.'}

    command = command.rstrip('\r\n')
    if current_app.config.get('SERIAL_COMMAND_NEWLINE', True):
        command += '\n'

    try:
        encoded_command = command.encode('utf-8')
        with serial_lock:
            connection = _get_serial_connection()
            bytes_written = connection.write(encoded_command)
            connection.flush()
        return {
            'ok': True,
            'message': f'Comando enviado ({bytes_written} bytes).'
        }
    except (serial.SerialException, OSError) as error:
        return {
            'ok': False,
            'message': f'Falha ao enviar pela porta serial: {error}'
        }



def save_telemetry(dados_recebidos):
    try:
        received_at = dados_recebidos.get(
            'receivedTimestamp', datetime.now(timezone.utc).isoformat()
        )
        collected_at = dados_recebidos.get('timestamp', received_at)
        mode_code = dados_recebidos.get('modeCode')
        telemetry = dados_recebidos.get('values', {})
        
        rows = []
        for tipo, valor in telemetry.items():
            try:
                valor_numerico = float(valor)
                rows.append({
                    'timestamp': collected_at,
                    'collection_timestamp': collected_at,
                    'received_timestamp': received_at,
                    'mode_code': mode_code or '',
                    'tipo_dado': tipo,
                    'valor': valor_numerico,
                })
            except (TypeError, ValueError):
                continue

        if not rows:
            return

        with telemetry_lock:
            needs_header = not os.path.exists(CSV_PATH) or os.path.getsize(CSV_PATH) == 0
            with open(CSV_PATH, 'a', encoding='utf-8', newline='') as csv_file:
                writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
                if needs_header:
                    writer.writeheader()
                writer.writerows(rows)
                csv_file.flush()
                os.fsync(csv_file.fileno())
        
    except Exception as e:
        print(f"Erro ao salvar no arquivo CSV: {e}")

@socketio.on('time_series')
def handle_time_series(dados_recebidos):
    dados_recebidos = dados_recebidos if isinstance(dados_recebidos, dict) else {}
    request_id = dados_recebidos.get('requestId')
    try:
        start_utc = dados_recebidos.get('start')
        end_utc = dados_recebidos.get('end')
        mode_code = dados_recebidos.get('modeCode')
        result = query_timeframe(start_utc, end_utc, mode_code)
        result['requestId'] = request_id
        emit('historical_data', result)
    except (TypeError, ValueError) as error:
        emit('historical_error', {'message': str(error), 'requestId': request_id})
    except (OSError, csv.Error):
        emit('historical_error', {
            'message': 'Não foi possível consultar o arquivo de telemetria.',
            'requestId': request_id
        })

def _parse_iso_datetime(value, field_name):
    if not isinstance(value, str) or not value:
        raise ValueError(f'O campo "{field_name}" é obrigatório.')

    try:
        parsed = datetime.fromisoformat(value.replace('Z', '+00:00'))
    except ValueError as error:
        raise ValueError(f'O campo "{field_name}" possui uma data inválida.') from error

    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)


def query_timeframe(start_utc, end_utc, mode_code=None):
    start = _parse_iso_datetime(start_utc, 'start')
    end = _parse_iso_datetime(end_utc, 'end')
    if start > end:
        raise ValueError('A data inicial deve ser anterior à data final.')

    start_iso = start.isoformat()
    end_iso = end.isoformat()

    mode_code = _normalize_mode_code(mode_code) if mode_code else None
    rows = []
    with telemetry_lock:
        with open(CSV_PATH, 'r', encoding='utf-8', newline='') as csv_file:
            for row_number, row in enumerate(csv.DictReader(csv_file)):
                collection_timestamp = row.get('collection_timestamp') or row.get('timestamp')
                try:
                    collected_at = _parse_iso_datetime(collection_timestamp, 'timestamp')
                    value = float(row['valor'])
                except (KeyError, TypeError, ValueError):
                    continue
                row_mode_code = row.get('mode_code') or None
                if start <= collected_at <= end and (not mode_code or row_mode_code == mode_code):
                    rows.append((
                        collected_at,
                        row_number,
                        collection_timestamp,
                        row.get('received_timestamp') or collection_timestamp,
                        row_mode_code,
                        row.get('tipo_dado'),
                        value,
                    ))

    rows.sort(key=lambda row: (row[0], row[1]))

    samples_by_timestamp = {}
    for _, _, timestamp, received_timestamp, mode_code, data_type, value in rows:
        sample_key = (timestamp, received_timestamp, mode_code)
        sample = samples_by_timestamp.setdefault(sample_key, {
            'timestamp': timestamp,
            'collectionTimestamp': timestamp,
            'receivedTimestamp': received_timestamp,
            'modeCode': mode_code,
        })
        sample[str(data_type)] = value

    samples = list(samples_by_timestamp.values())
    return {
        'start': start_iso,
        'end': end_iso,
        'count': len(samples),
        'samples': samples
    }


def query_telemetry_availability(mode_code=None):
    mode_code = _normalize_mode_code(mode_code) if mode_code else None
    timestamps = set()
    with telemetry_lock:
        with open(CSV_PATH, 'r', encoding='utf-8', newline='') as csv_file:
            for row in csv.DictReader(csv_file):
                timestamp = row.get('collection_timestamp') or row.get('timestamp')
                if timestamp and (not mode_code or row.get('mode_code') == mode_code):
                    try:
                        _parse_iso_datetime(timestamp, 'timestamp')
                    except (TypeError, ValueError):
                        continue
                    timestamps.add(timestamp)

    ordered_timestamps = sorted(timestamps, key=lambda value: _parse_iso_datetime(value, 'timestamp'))
    samples_by_day = {}
    for timestamp in ordered_timestamps:
        day = _parse_iso_datetime(timestamp, 'timestamp').date().isoformat()
        samples_by_day[day] = samples_by_day.get(day, 0) + 1

    return {
        'modeCode': mode_code,
        'start': ordered_timestamps[0] if ordered_timestamps else None,
        'end': ordered_timestamps[-1] if ordered_timestamps else None,
        'days': [
            {'date': day, 'count': count}
            for day, count in samples_by_day.items()
        ]
    }
    

if __name__ == '__main__':
    app = create_app()
