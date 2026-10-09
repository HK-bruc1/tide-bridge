"""CDC file transfer v1 receiver and memory benchmark; recording GET is not implemented."""
import argparse
import json
import os
import platform
from pathlib import Path
import struct
import sys
import time
import zlib

HEADER = struct.Struct('<4sBBHIII')
MAX_PAYLOAD = 8192
MAX_SIZE = 16777216
PATTERN = bytes(range(256)) * 33


class ProtocolError(Exception):
    pass


class Reader:
    def __init__(self, port, total_timeout=120):
        self.port = port
        self.buffer = bytearray()
        self.deadline = time.monotonic() + total_timeout

    def fill(self, count, timeout=5):
        deadline = min(self.deadline, time.monotonic() + timeout)
        while len(self.buffer) < count:
            if time.monotonic() >= deadline:
                raise ProtocolError('receive timeout; close/reopen port before retry')
            data = self.port.read(min(max(self.port.in_waiting, 1), 16384))
            if data:
                self.buffer.extend(data)
                if len(self.buffer) > 32768:
                    raise ProtocolError('receive buffer overflow')
                deadline = min(self.deadline, time.monotonic() + timeout)

    def take(self, count):
        self.fill(count)
        result = bytes(self.buffer[:count])
        del self.buffer[:count]
        return result

    def line(self):
        for count in range(1, 130):
            self.fill(count, timeout=10)
            if self.buffer[count - 1] == 10:
                result = self.take(count)
                if not result.endswith(b'\r\n'):
                    raise ProtocolError('text must end with CRLF')
                try:
                    return result[:-2].decode('ascii')
                except UnicodeDecodeError as error:
                    raise ProtocolError('non-ASCII reply') from error
        raise ProtocolError('text line too long')

    def frame(self, transfer_id, offset, capture=None):
        header = self.take(HEADER.size)
        magic, version, kind, header_size, actual_id, actual_offset, size = HEADER.unpack(header)
        if (magic, version, header_size) != (b'CDF1', 1, 20):
            raise ProtocolError(
                f'invalid binary header: expected_id={transfer_id} expected_offset={offset} '
                f'header={header.hex()} buffered={len(self.buffer)} next={self.buffer[:32].hex()}')
        if actual_id != transfer_id or actual_offset != offset:
            raise ProtocolError('wrong transfer ID or noncontiguous offset')
        if not ((kind == 1 and 1 <= size <= MAX_PAYLOAD) or
                (kind == 2 and size == 8) or (kind == 3 and size == 4)):
            raise ProtocolError('invalid frame type/length')
        body = self.take(size + 4)
        if capture is not None:
            capture.extend(header)
            capture.extend(body)
        elif zlib.crc32(header + body[:-4]) != struct.unpack('<I', body[-4:])[0]:
            raise ProtocolError('frame CRC mismatch')
        return kind, body[:-4]


def send(port, command):
    data = (command + '\r\n').encode('ascii')
    if port.write(data) != len(data):
        raise ProtocolError('short command write')


def unsigned(text):
    if not text or not text.isascii() or not text.isdecimal():
        raise ProtocolError('invalid unsigned decimal field')
    value = int(text)
    if value > 0xffffffff:
        raise ProtocolError('unsigned field overflow')
    return value


def caps(port):
    send(port, '*APP#file#caps#')
    reply = Reader(port, 10).line()
    fields = reply.split('#')
    if (len(fields) != 8 or fields[:4] != ['*DEV', 'file', 'caps', '1'] or
            fields[4:6] != ['8192', '16777216'] or fields[-1] != '' or
            fields[6] not in ('IDLE', 'OPENING', 'STREAMING', 'CLOSING', 'QUARANTINED')):
        raise ProtocolError('unexpected CAPS: ' + reply)
    return reply


def receive_bench(port, size, block, output=None, cancel_after=None, report=None,
                  receive_mode='inline'):
    if receive_mode not in ('inline', 'deferred') or not 1 <= size <= MAX_SIZE or block not in (1024, 2048, 4096, 8192):
        raise ProtocolError('invalid receive mode/size/block')
    if receive_mode == 'deferred' and cancel_after is not None:
        raise ProtocolError('deferred verification does not support cancellation')
    report = report if report is not None else {}
    report.update(size_requested=size, block=block, bytes=0, complete=False, cancelled=False,
                  passes_600KBps=False, crc_ok=False, pattern_ok=False,
                  receive_mode=receive_mode)
    destination = Path(output) if output else None
    temporary = destination.with_name(destination.name + '.part') if destination else None
    if destination and destination.exists():
        raise ProtocolError('output exists; choose a new output name')
    stream = None
    offset = crc = 0
    started = time.monotonic()
    reader = Reader(port)
    first_data = last_data = None
    first_data_bytes = 0
    cancel_sent = None
    last_progress = started
    received_at = None
    try:
        if temporary:
            stream = temporary.open('xb')
        send(port, f'*APP#file#bench#{size}#{block}#')
        fields = reader.line().split('#')
        if len(fields) != 6 or fields[:3] != ['*DEV', 'file', 'accepted'] or fields[4:] != ['1', '']:
            raise ProtocolError('bench rejected: ' + '#'.join(fields))
        transfer_id = unsigned(fields[3])
        if not transfer_id:
            raise ProtocolError('zero transfer ID')
        report['transfer_id'] = transfer_id
        if receive_mode == 'deferred':
            captured = bytearray()
            captured_offset = 0
            while True:
                kind, payload = reader.frame(transfer_id, captured_offset, capture=captured)
                if kind != 1:
                    break
                if len(payload) != min(block, size - captured_offset):
                    raise ProtocolError('unexpected captured DATA length')
                captured_offset += len(payload)
                report['captured_bytes'] = captured_offset
            received_at = time.monotonic()
            report['capture_elapsed_s'] = received_at - started
            report['capture_wire_bytes'] = len(captured)
            reader.buffer = captured
            reader.deadline = time.monotonic() + 120
        while True:
            if cancel_after is not None and offset >= cancel_after and cancel_sent is None:
                send(port, f'*APP#file#cancel#{transfer_id}#')
                cancel_sent = time.monotonic()
            kind, payload = reader.frame(transfer_id, offset)
            if kind == 1:
                if offset + len(payload) > size or len(payload) != min(block, size - offset):
                    raise ProtocolError('unexpected DATA length')
                if payload != PATTERN[offset % 256:offset % 256 + len(payload)]:
                    raise ProtocolError('memory-source pattern mismatch')
                crc = zlib.crc32(payload, crc)
                if stream:
                    stream.write(payload)
                offset += len(payload)
                now = time.monotonic()
                if first_data is None:
                    first_data = now
                    first_data_bytes = offset
                last_data = now
                report['bytes'] = offset
                if received_at is None and now - last_progress >= 1:
                    print(f'{offset}/{size} bytes, {offset / (now - started) / 1000:.1f} KB/s', file=sys.stderr)
                    last_progress = now
                continue
            if kind == 3:
                error = struct.unpack('<I', payload)[0]
                report['device_error'] = error
                if error != 6 or cancel_sent is None:
                    raise ProtocolError(f'device ERROR {error}')
                report['cancelled'] = True
                report['cancel_to_terminal_s'] = time.monotonic() - cancel_sent
            else:
                total, expected_crc = struct.unpack('<II', payload)
                if total != size or total != offset or expected_crc != crc:
                    raise ProtocolError('END length/file CRC mismatch')
                if cancel_sent is not None:
                    raise ProtocolError('END after cancellation request; retry cancellation earlier')
                report['complete'] = True
                report['crc_ok'] = True
                report['pattern_ok'] = True
            if stream:
                stream.close()
                stream = None
            if report['complete'] and temporary:
                os.rename(temporary, destination)
            elapsed = time.monotonic() - started
            if received_at is not None:
                report.update(receive_elapsed_s=received_at - started,
                              receive_KBps=offset / (received_at - started) / 1000,
                              verification_s=elapsed - (received_at - started))
            report.update(elapsed_s=elapsed, bytes_per_s=offset / elapsed,
                          KBps=offset / elapsed / 1000, crc32=f'{crc:08x}',
                          steady_s=None if first_data is None else last_data - first_data,
                          steady_bytes_per_s=None if first_data is None or last_data == first_data else
                          (offset - first_data_bytes) / (last_data - first_data),
                          passes_600KBps=report['complete'] and offset >= 10000000 and offset / elapsed >= 600000)
            if receive_mode == 'deferred':
                report.update(steady_s=None, steady_bytes_per_s=None)
            return report
    except Exception as error:
        report.update(complete=False, error=str(error), elapsed_s=time.monotonic() - started, bytes=offset)
        raise
    finally:
        if stream:
            stream.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--firmware', default='unknown')
    parser.add_argument('--usb-speed', default='unverified')
    parser.add_argument('--topology', default='unreported')
    parser.add_argument('--driver', default='unreported')
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('caps')
    bench = commands.add_parser('bench')
    bench.add_argument('--size', type=int, default=10000000)
    bench.add_argument('--block', type=int, choices=(1024, 2048, 4096, 8192), default=4096)
    bench.add_argument('--repeat', type=int, default=1)
    bench.add_argument('--output', type=Path)
    bench.add_argument('--cancel-after', type=int)
    bench.add_argument('--receive-mode', choices=('inline', 'deferred'), default='inline')
    args = parser.parse_args(argv)
    if args.command == 'bench' and (not 1 <= args.size <= MAX_SIZE or not 1 <= args.repeat <= 100 or
            (args.cancel_after is not None and not 0 <= args.cancel_after < args.size)):
        parser.error('invalid size/repeat/cancel-after')
    if args.command == 'bench' and args.receive_mode == 'deferred' and args.cancel_after is not None:
        parser.error('deferred verification does not support cancellation')
    report = dict(protocol=1, stage='A-memory-only', os=platform.platform(), port=args.port,
                  baud=args.baud, firmware=args.firmware, usb_speed=args.usb_speed,
                  topology=args.topology, driver=args.driver, runs=[], fsync=False,
                  real_file_acceptance=False)
    try:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        report_output = args.report.open('x', encoding='utf-8')
    except OSError as error:
        print(f'Cannot create new report; serial port not opened: {error}', file=sys.stderr)
        return 1
    status = 0
    try:
        import serial
        report['pyserial'] = serial.__version__
        with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=2) as port:
            report['caps'] = caps(port)
            print(report['caps'])
            if args.command == 'bench':
                for index in range(args.repeat):
                    run = dict(iteration=index + 1)
                    report['runs'].append(run)
                    output = args.output
                    if output and args.repeat > 1:
                        output = output.with_name(f'{output.stem}-{index + 1}{output.suffix}')
                    receive_bench(port, args.size, args.block, output, args.cancel_after, run, args.receive_mode)
                    if args.receive_mode == 'deferred':
                        print(f"接收阶段 {run['receive_KBps']:.2f} KB/s；后置完整校验 {run['verification_s']:.3f} 秒")
                    if run['cancelled']:
                        print(f"[{index + 1}/{args.repeat}] 取消已确认，收到 {run['bytes']} 字节，"
                              f"请求至终止帧 {run['cancel_to_terminal_s']:.3f} 秒")
                    else:
                        verdict = '达到目标' if run['passes_600KBps'] else '未达到目标'
                        print(f"[{index + 1}/{args.repeat}] {run['bytes']} 字节，{run['elapsed_s']:.3f} 秒，"
                              f"{run['KBps']:.2f} KB/s，CRC/模式校验通过；{verdict}")
                if args.cancel_after is None and not all(run['passes_600KBps'] for run in report['runs']):
                    status = 2
    except Exception as error:
        report['error'] = str(error)
        print(str(error), file=sys.stderr)
        status = 1
    finally:
        with report_output:
            report_output.write(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    return status


if __name__ == '__main__':
    raise SystemExit(main())
