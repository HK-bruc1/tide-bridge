"""Read-only smoke test for CDC=1/TEST=0. Shows full identity; never retries requests."""
import argparse
import re
import sys


def request(port, command):
    data = command.encode('ascii') + b'\r\n'
    if port.write(data) != len(data):
        raise RuntimeError('incomplete write; reconnect before continuing')
    response = port.read_until(b'\r\n', size=259)
    if not response.endswith(b'\r\n') or len(response) > 258:
        raise RuntimeError('timeout/invalid frame; session result unknown, no retry')
    try:
        text = response[:-2].decode('ascii')
    except UnicodeDecodeError:
        raise RuntimeError('non-ASCII response') from None
    if any(ord(c) < 32 or ord(c) > 126 for c in text):
        raise RuntimeError('invalid response character')
    return text


def validate_info(response):
    # Error messages deliberately omit the raw response and AUTH.
    if not response.startswith('OK '):
        raise RuntimeError('GET_INFO rejected; inspect firmware readiness/configuration')
    fields = response[3:].split(';')
    if len(fields) != 5 or any(field.count('=') != 1 for field in fields):
        raise RuntimeError('invalid GET_INFO fields')
    info = dict(field.split('=', 1) for field in fields)
    if set(info) != {'PROTO', 'NAME', 'AUTH', 'ADDR', 'FW'} or info['PROTO'] != '1':
        raise RuntimeError('unsupported GET_INFO schema/version')
    if not (1 <= len(info['NAME']) <= 24 and 1 <= len(info['FW']) <= 32):
        raise RuntimeError('invalid name/version length')
    if not re.fullmatch(r'[0-9A-Za-z]{24}', info['AUTH']):
        raise RuntimeError('invalid AUTH format')
    if not re.fullmatch(r'[0-9A-F]{12}', info['ADDR']) or info['ADDR'] in ('0'*12, 'F'*12):
        raise RuntimeError('invalid BLE address')
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    args = parser.parse_args()
    import serial
    try:
        with serial.Serial(args.port, 115200, timeout=2, write_timeout=2) as port:
            if request(port, 'PING') != 'OK PONG':
                raise RuntimeError('PING failed; requires CDC=1/TEST=0 firmware')
            print('PASS PING')
            info = validate_info(request(port, 'GET_INFO'))
            print(f"PASS GET_INFO: PROTO={info['PROTO']};NAME={info['NAME']};ADDR={info['ADDR']};FW={info['FW']};AUTH={info['AUTH']}")
            if info['AUTH'] == '0'*24:
                raise RuntimeError('identity not provisioned; transport passed, factory identity check failed')
            print('Identity format valid; manufacturing record comparison still required.')
        return 0
    except (serial.SerialException, OSError, RuntimeError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
