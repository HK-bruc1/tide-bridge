"""Basic CDC check: python tests/board/factory_cdc_bytes.py --port COM6.
Requires DUT and CDC TEST firmware. COM3 is the debug log port.
Closes the port on success or failure; never retries a failed session.
"""
import argparse
import time

PATTERN = bytes(range(256))


def read_exact(port, count, timeout):
    deadline = time.monotonic() + timeout
    received = bytearray()
    while len(received) < count:
        if time.monotonic() >= deadline:
            raise TimeoutError(f'read timeout: expected {count}, received {len(received)} bytes')
        received.extend(port.read(count - len(received)))
    return bytes(received)


def write_all(port, payload, timeout):
    deadline = time.monotonic() + timeout
    offset = 0
    while offset < len(payload):
        if time.monotonic() >= deadline:
            raise TimeoutError(f'write timeout: accepted {offset}/{len(payload)} bytes')
        written = port.write(payload[offset:])
        if written is None or written < 0 or written > len(payload) - offset:
            raise IOError('invalid serial write count')
        offset += written
        if not written:
            time.sleep(0.001)


def compare(expected, actual, label):
    if actual != expected:
        mismatch = next((i for i, pair in enumerate(zip(expected, actual)) if pair[0] != pair[1]),
                        min(len(expected), len(actual)))
        raise AssertionError(f'{label}: mismatch at byte {mismatch}, lengths {len(expected)}/{len(actual)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--timeout', type=float, default=3)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('timeout must be positive')
    import serial
    port = serial.Serial(port=None, baudrate=115200, bytesize=8, parity='N',
                         stopbits=1, timeout=0.1, write_timeout=args.timeout)
    port.dtr = False
    port.port = args.port
    try:
        # Initial DTR low prevents the open-time purge from losing the banner.
        with port:
            port.dtr = True
            compare(PATTERN, read_exact(port, 256, args.timeout), 'startup pattern')
            print('PASS: startup pattern (256 bytes)', flush=True)
            for label, payload in [('hello', b'hello'), ('short', b'CDC OK'),
                                   ('binary', bytes([0, 1, 127, 128, 255])),
                                   ('cross-packet', bytes(range(65)))]:
                # Separate the firmware's one-second RX/TX summary windows.
                time.sleep(1.1)
                write_all(port, payload, args.timeout)
                compare(payload, read_exact(port, len(payload), args.timeout), label)
                print(f'PASS: {label}, {len(payload)} bytes echoed', flush=True)
            if port.read(1):
                raise AssertionError('unexpected extra bytes')
            time.sleep(1.1)
            print('PASS: 4 exchanges, 81 echoed bytes', flush=True)
        return 0
    except (serial.SerialException, OSError, AssertionError) as exc:
        print(f'FAIL: {exc}; replug USB before retrying', flush=True)
        return 1
    finally:
        port.close()
        print(f'{args.port} closed', flush=True)


if __name__ == '__main__':
    raise SystemExit(main())
