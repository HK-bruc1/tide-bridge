"""CDF1 receiver, firmware producers and transport regressions (no USB hardware)."""
import ctypes
import contextlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock
import zlib
from llvmlite import binding as llvm

from host_c_test_lib import ROOT, run_c_checks

VECTORS = json.loads((ROOT / 'tests/host/fixtures/cdc_file_v1.json').read_text())
launcher = types.ModuleType('cdc_launcher')
launcher.__file__ = str(ROOT / 'CDC-test.bat')
exec(compile((ROOT / 'CDC-test.bat').read_text(encoding='utf-8').split('\n', 1)[1],
             launcher.__file__, 'exec'), launcher.__dict__)
tool = launcher


class OpusExportTests(unittest.TestCase):
    def test_matches_existing_factory_container_without_changing_raw(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / 'recording.raw'
            payload = bytes(range(40)) * 3
            raw.write_bytes(payload)
            expected = Path(directory) / 'expected.opus'
            launcher.write_opus(payload, expected)
            result = tool.export_opus(raw)
            self.assertEqual(result.name, 'recording.raw.opus')
            self.assertEqual(result.read_bytes(), expected.read_bytes())
            self.assertEqual(raw.read_bytes(), payload)
            self.assertFalse(Path(str(result) + '.part').exists())
            with self.assertRaises(tool.ProtocolError):
                tool.export_opus(raw)
            self.assertEqual(result.read_bytes(), expected.read_bytes())

    def test_invalid_raw_never_publishes_opus(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / 'recording.raw'
            for payload in (b'', b'x' * 41):
                raw.write_bytes(payload)
                with self.assertRaises(tool.ProtocolError):
                    tool.export_opus(raw)
                self.assertFalse(Path(str(raw) + '.opus').exists())


class MenuTests(unittest.TestCase):
    def test_standalone_file_cli_help(self):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'CDC-test.bat'
            script.write_bytes((ROOT / 'CDC-test.bat').read_bytes())
            result = subprocess.run(
                [sys.executable, '-X', 'utf8', '-x', str(script), '--file-cli', '--help'],
                cwd=directory, capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('download', result.stdout)
            self.assertIn('bench', result.stdout)

    def test_four_daily_paths(self):
        for kind, mode in (('1', 'file-download'), ('2', 'file-bench')):
            for choice, repeat in (('1', '1'), ('2', '10')):
                with self.subTest(kind=kind, choice=choice), contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(launcher.file_test_menu(mock.Mock(side_effect=[kind, choice])),
                                     (mode, ['--bench-repeat', repeat]))

    def test_defaults_back_and_invalid_input(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(launcher.file_test_menu(mock.Mock(side_effect=['', ''])),
                             ('file-download', ['--bench-repeat', '1']))
            self.assertIsNone(launcher.file_test_menu(mock.Mock(side_effect=['1', 'q', 'q'])))
            self.assertEqual(launcher.file_test_menu(mock.Mock(side_effect=['bad', '2', 'bad', '2'])),
                             ('file-bench', ['--bench-repeat', '10']))


def frame(kind, offset, payload, transfer_id=1):
    header = tool.HEADER.pack(b'CDF1', 1, kind, 20, transfer_id, offset, len(payload))
    return header + payload + struct.pack('<I', zlib.crc32(header + payload))


class Port:
    def __init__(self, data=b'', chunk=16384, responses=None):
        self.data = bytearray(data)
        self.chunk = chunk
        self.writes = []
        self.responses = responses

    @property
    def in_waiting(self):
        return len(self.data)

    def read(self, count):
        count = min(count, self.chunk)
        data = bytes(self.data[:count])
        del self.data[:count]
        if not data:
            raise tool.ProtocolError('mock EOF')
        return data

    def write(self, data):
        if self.responses is not None:
            self.data.extend(self.responses[data])
        self.writes.append(data)
        return len(data)

    def serial_module(self):
        return types.SimpleNamespace(
            __version__='mock', Serial=mock.Mock(return_value=contextlib.nullcontext(self)))


class ReceiverTests(unittest.TestCase):
    def entry(self, **fields):
        return dict(dict(sn=7, name='abc123.raw', size=4000, start_time=100,
                         end_time=102, frame_size=40, opus=1084752896), **fields)

    def test_list_stream_and_validation(self):
        for entries in ([], [self.entry()]):
            payload = json.dumps(entries).encode()
            port = Port(self.recording_wire(payload), chunk=1)
            self.assertEqual(tool.receive_list(port), entries)
            self.assertEqual(port.writes, [b'*APP#file#list#\r\n'])
        for entries in ({}, [self.entry(), self.entry()], [self.entry(name='../abc.raw')],
                        [self.entry(size=-1)], [self.entry(sn=True)], [None]):
            with self.subTest(entries=entries), self.assertRaises(tool.ProtocolError):
                tool.receive_list(Port(self.recording_wire(json.dumps(entries).encode())))
        wire = self.recording_wire(b'[]')
        with self.assertRaises(tool.ProtocolError):
            tool.receive_list(Port(wire[:-1] + bytes([wire[-1] ^ 1])))

    def test_list_selection_refresh_and_unavailable(self):
        entries = [self.entry(size=tool.MAX_SIZE + 40), self.entry(sn=8, name='def.raw')]
        report = {}
        with mock.patch.object(tool, 'receive_list', side_effect=[[], entries]) as listing, \
                mock.patch('builtins.input', side_effect=['r', 'invalid', '1', '2']), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tool.select_recording(None, report), entries[1])
        self.assertEqual(listing.call_count, 2)
        with mock.patch.object(tool, 'receive_list', return_value=[]), \
                mock.patch('builtins.input', return_value='q'), contextlib.redirect_stdout(io.StringIO()):
            self.assertIsNone(tool.select_recording(None, {}))

    def test_large_recording_selection_and_legacy_caps(self):
        entry = self.entry(size=143340000, end_time=71770)
        self.assertEqual(tool.recording_unavailable(entry), '')
        self.assertIn('16,777,216', tool.recording_unavailable(entry, 16777216))
        self.assertNotIn('16,777,216', tool.recording_unavailable(self.entry(size=0), 16777216))
        with mock.patch.object(tool, 'receive_list', return_value=[entry]), \
                mock.patch('builtins.input', return_value='1'), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tool.select_recording(None, {}, tool.MAX_SIZE), entry)
        with mock.patch.object(tool, 'receive_list', return_value=[entry]), \
                mock.patch('builtins.input', side_effect=['1', 'q']), contextlib.redirect_stdout(io.StringIO()):
            self.assertIsNone(tool.select_recording(None, {}, 16777216))

    def test_interactive_download_on_same_port(self):
        payload = b'x' * 4000
        listing = json.dumps([self.entry()]).encode()
        for selection in ('1', 'q'):
            port = Port(chunk=63, responses={
                b'*APP#file#caps#\r\n': b'*DEV#file#caps#1#8192#16777216#IDLE#\r\n',
                b'*APP#file#list#\r\n': self.recording_wire(listing),
                b'*APP#file#get#7#abc123.raw#\r\n': self.recording_wire(payload),
            })
            serial = port.serial_module()
            with tempfile.TemporaryDirectory() as directory, \
                    mock.patch.dict('sys.modules', {'serial': serial}), \
                    mock.patch('builtins.input', return_value=selection), \
                    contextlib.redirect_stdout(io.StringIO()):
                output = Path(directory) / 'record.raw'
                report_path = Path(directory) / 'report.json'
                result = tool.file_transfer_main(['--port', 'COM6', '--report', str(report_path),
                                    'download', '--output', str(output)])
                report = json.loads(report_path.read_text(encoding='utf-8'))
                serial.Serial.assert_called_once()
                self.assertTrue(report['list_requests'][0]['crc_ok'])
                if selection == '1':
                    self.assertEqual(result, 0)
                    self.assertEqual(output.read_bytes(), payload)
                    self.assertEqual(report['selected_recording']['sn'], 7)
                    self.assertTrue(report['runs'][0]['complete'])
                    self.assertEqual(len(port.writes), 3)
                else:
                    self.assertEqual(result, 3)
                    self.assertFalse(output.exists())
                    self.assertTrue(report['selection_cancelled'])
                    self.assertEqual(len(port.writes), 2)

    def recording_wire(self, payload, block=4096):
        wire = b'*DEV#file#accepted#1#1#\r\n'
        for offset in range(0, len(payload), block):
            wire += frame(1, offset, payload[offset:offset + block])
        return wire + frame(2, len(payload), struct.pack('<II', len(payload), zlib.crc32(payload)))

    def test_recording_fragmentation_and_reference(self):
        payload = bytes((index * 19 + 7) & 255 for index in range(10000))
        with tempfile.TemporaryDirectory() as directory:
            reference = Path(directory) / 'reference.raw'
            reference.write_bytes(payload)
            for block, chunk in ((4096, 1), (1024, 63), (8192, 16384)):
                with self.subTest(block=block, chunk=chunk):
                    output = Path(directory) / f'{block}-{chunk}.raw'
                    port = Port(self.recording_wire(payload, block), chunk)
                    report = tool.receive_download(port, 7, 'abc123.raw', output, reference=reference)
                    self.assertEqual(port.writes, [b'*APP#file#get#7#abc123.raw#\r\n'])
                    self.assertEqual(output.read_bytes(), payload)
                    self.assertEqual(report['sha256'], tool.hashlib.sha256(payload).hexdigest())
                    self.assertTrue(report['complete'] and report['crc_ok'] and report['reference_match'])
                    self.assertIsNone(report['pattern_ok'])
                    self.assertFalse(report['passes_600KBps'])
                    self.assertEqual(report['max_payload'], block)

    def test_large_recording_stream_past_old_size_and_time_limits(self):
        total = 143340000
        clock = [0.0]
        expected_hash = tool.hashlib.sha256()

        def wire():
            yield b'*DEV#file#accepted#1#1#\r\n'
            crc = 0
            for offset in range(0, total, 4096):
                payload = bytes(range(256)) * 16
                payload = payload[:min(4096, total - offset)]
                expected_hash.update(payload)
                crc = zlib.crc32(payload, crc)
                yield frame(1, offset, payload)
            yield frame(2, total, struct.pack('<II', total, crc))

        class StreamingPort(Port):
            def __init__(self):
                super().__init__()
                self.frames = wire()
                self.peak_buffer = 0

            def read(self, count):
                if not self.data:
                    self.data.extend(next(self.frames))
                    self.peak_buffer = max(self.peak_buffer, len(self.data))
                clock[0] += 0.01
                return super().read(count)

        port = StreamingPort()
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(tool.time, 'monotonic', side_effect=lambda: clock[0]), \
                contextlib.redirect_stderr(io.StringIO()):
            output = Path(directory) / 'large.raw'
            result = tool.receive_download(port, 7, 'abc123.raw', output, max_size=0xffffffff)
            self.assertTrue(result['complete'] and result['crc_ok'])
            self.assertEqual(result['bytes'], total)
            self.assertGreater(result['elapsed_s'], 120)
            self.assertEqual(result['sha256'], expected_hash.hexdigest())
            self.assertEqual(output.stat().st_size, total)
            actual_hash = tool.hashlib.sha256()
            with output.open('rb') as stream:
                for block in iter(lambda: stream.read(65536), b''):
                    actual_hash.update(block)
            self.assertEqual(actual_hash.hexdigest(), expected_hash.hexdigest())
            self.assertLessEqual(port.peak_buffer, 4096 + 24)
            self.assertFalse(output.with_suffix('.raw.part').exists())

    def test_stalled_recording_keeps_idle_timeout(self):
        clock = [0.0]

        class StalledPort(Port):
            def read(self, count):
                if self.data:
                    return super().read(count)
                clock[0] += 1
                return b''

        port = StalledPort(b'*DEV#file#accepted#1#1#\r\n' + frame(1, 0, b'x' * 40))
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(tool.time, 'monotonic', side_effect=lambda: clock[0]):
            output = Path(directory) / 'stalled.raw'
            report = {}
            with self.assertRaisesRegex(tool.ProtocolError, 'receive timeout'):
                tool.receive_download(port, 7, 'abc123.raw', output, report=report)
            self.assertEqual(clock[0], 5)
            self.assertEqual(report['bytes'], 40)
            self.assertFalse(output.exists() or report['complete'])

    def test_negotiated_limit_and_u32_offsets(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'over-caps.raw'
            with self.assertRaisesRegex(tool.ProtocolError, 'size limit'):
                tool.receive_download(Port(self.recording_wire(b'x' * 80)), 7, 'abc123.raw',
                                      output, max_size=40)
            self.assertFalse(output.exists())
        for offset in (16777216, 0x80000000, 0xfffffff0):
            kind, payload = tool.Reader(Port(frame(2, offset, struct.pack('<II', offset, 0)))).frame(1, offset)
            self.assertEqual((kind, struct.unpack('<II', payload)[0]), (2, offset))
        with self.assertRaises(tool.ProtocolError):
            tool.receive_bench(Port(), tool.BENCH_MAX_SIZE + 1, 4096)

    def test_recording_identity_rejected_before_send(self):
        for sn, name in ((0, 'abc.raw'), (0x7fffffff, 'abc.raw'), (7, '../abc.raw'),
                         (7, 'abc.raw#'), (7, 'ab.raw'), (7, 'abcdef0.raw'),
                         (7, 'abc.RAW'), (7, 'abc.raw\r\n*APP#file#bench#1#1024#')):
            port = Port(b'')
            with self.assertRaises(tool.ProtocolError):
                tool.receive_download(port, sn, name, 'unused.raw')
            self.assertEqual(port.writes, [])

    def test_recording_failures_do_not_publish(self):
        payload = b'x' * 80
        prefix = b'*DEV#file#accepted#1#1#\r\n'
        good = frame(1, 0, payload)
        cases = [b'*DEV#file#error#8#\r\n', self.recording_wire(payload)[:-1],
                 self.recording_wire(b'x' * 41), self.recording_wire(b''),
                 prefix + good + frame(2, 80, struct.pack('<II', 40, zlib.crc32(payload))),
                 prefix + good + frame(2, 80, struct.pack('<II', 80, 123)),
                 prefix + frame(1, 0, payload, transfer_id=2),
                 prefix + good + frame(3, 80, struct.pack('<I', 5)),
                 prefix + good[:-1] + bytes([good[-1] ^ 1])]
        with tempfile.TemporaryDirectory() as directory:
            for index, wire in enumerate(cases):
                output = Path(directory) / f'{index}.raw'
                report = {}
                with self.assertRaises(tool.ProtocolError):
                    tool.receive_download(Port(wire), 7, 'abc123.raw', output, report=report)
                self.assertFalse(output.exists())
                self.assertFalse(report['complete'] or report['passes_600KBps'])
            output = Path(directory) / 'overflow.raw'
            with mock.patch.object(tool, 'MAX_SIZE', 40), self.assertRaises(tool.ProtocolError):
                tool.receive_download(Port(self.recording_wire(payload)), 7, 'abc123.raw', output)
            self.assertFalse(output.exists())

    def test_recording_reference_mismatch_and_cancel(self):
        payload = b'x' * 80
        with tempfile.TemporaryDirectory() as directory:
            reference = Path(directory) / 'reference.raw'
            for index, content in enumerate((b'y' * 80, payload[:-1], payload + b'x')):
                reference.write_bytes(content)
                output = Path(directory) / f'mismatch-{index}.raw'
                report = {}
                with self.assertRaises(tool.ProtocolError):
                    tool.receive_download(Port(self.recording_wire(payload)), 7, 'abc123.raw',
                                          output, report=report, reference=reference)
                self.assertFalse(report['reference_match'])
                self.assertFalse(output.exists())
            output = Path(directory) / 'cancel.raw'
            port = Port(b'*DEV#file#accepted#1#1#\r\n' + frame(3, 0, struct.pack('<I', 6)))
            report = tool.receive_download(port, 7, 'abc123.raw', output, cancel_after=0)
            self.assertTrue(report['cancelled'])
            self.assertFalse(report['complete'] or output.exists())
            self.assertEqual(port.writes[-1], b'*APP#file#cancel#1#\r\n')
            output = Path(directory) / 'cancel-too-late.raw'
            with self.assertRaises(tool.ProtocolError):
                tool.receive_download(Port(self.recording_wire(payload)), 7, 'abc123.raw',
                                      output, cancel_after=1024)
            self.assertFalse(output.exists())

    def test_recording_existing_output_and_close_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'existing.raw'
            output.write_bytes(b'keep')
            port = Port(b'')
            with self.assertRaises(tool.ProtocolError):
                tool.receive_download(port, 7, 'abc123.raw', output)
            self.assertEqual(port.writes, [])
            self.assertEqual(output.read_bytes(), b'keep')
            failing = mock.Mock()
            failing.write.return_value = 40
            failing.close.side_effect = OSError('close failed')
            report = {}
            with mock.patch.object(Path, 'open', return_value=failing), self.assertRaises(OSError):
                tool.receive_download(Port(self.recording_wire(b'x' * 40)), 7, 'abc123.raw',
                                      Path(directory) / 'close.raw', report=report)
            self.assertFalse(report['complete'] or report['passes_600KBps'])
            self.assertFalse((Path(directory) / 'close.raw').exists())

    def test_recording_cli_one_port_and_repeat(self):
        payload = b'x' * 40
        port = Port(responses={
            b'*APP#file#caps#\r\n': b'*DEV#file#caps#1#8192#4294967295#IDLE#\r\n',
            b'*APP#file#get#7#abc123.raw#\r\n': self.recording_wire(payload),
        })
        serial = port.serial_module()
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict('sys.modules', {'serial': serial}), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            path = Path(directory) / 'report.json'
            output = Path(directory) / 'record.raw'
            reference = Path(directory) / 'reference.raw'
            reference.write_bytes(payload)
            status = tool.file_transfer_main(['--port', 'COM6', '--report', str(path), 'download', '--sn', '7',
                                '--name', 'abc123.raw', '--output', str(output), '--repeat', '2',
                                '--reference', str(reference)])
            self.assertEqual(status, 0)
            serial.Serial.assert_called_once()
            report = json.loads(path.read_text(encoding='utf-8'))
            self.assertEqual(report['max_file_size'], 0xffffffff)
            self.assertTrue(report['independent_content_verified'])
            self.assertFalse(report['real_file_acceptance'] or report['ten_run_throughput_pass'])
            self.assertEqual(len(report['runs']), 2)
            self.assertEqual(report['summary']['validated'], 2)
            self.assertEqual(report['summary']['failed'], 0)
            self.assertEqual(report['summary']['not_started'], 0)
            self.assertIn('mean_KBps', report['summary'])
            for index in (1, 2):
                self.assertEqual(output.with_name(f'record-{index}.raw').read_bytes(), payload)

    def test_summary_stops_on_changed_content(self):
        port = Port(responses={
            b'*APP#file#caps#\r\n': b'*DEV#file#caps#1#8192#16777216#IDLE#\r\n',
        })
        def receive(port, sn, name, output, cancel_after, run, reference, **kwargs):
            run.update(complete=True, cancelled=False, bytes=40, elapsed_s=1, KBps=0.04,
                       passes_600KBps=False, sha256=str(run['iteration']))
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.dict('sys.modules', {'serial': port.serial_module()}), \
                mock.patch.object(tool, 'receive_download', side_effect=receive), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            path = Path(directory) / 'report.json'
            self.assertEqual(tool.file_transfer_main(['--port', 'COM6', '--report', str(path), 'download',
                                       '--sn', '7', '--name', 'abc123.raw',
                                       '--output', str(Path(directory) / 'file.raw'), '--repeat', '10']), 1)
            summary = json.loads(path.read_text(encoding='utf-8'))['summary']
            self.assertEqual((summary['validated'], summary['failed'], summary['not_started']), (1, 1, 8))
            self.assertEqual(summary['mean_KBps'], 0.04)

    def test_deferred_verification(self):
        payload = (bytes(range(256)) * 40)[:10001]
        for block in (1024, 2048, 4096, 8192):
            wire = b'*DEV#file#accepted#1#1#\r\n'
            for offset in range(0, len(payload), block):
                wire += frame(1, offset, payload[offset:offset + block])
            wire += frame(2, len(payload), struct.pack('<II', len(payload), zlib.crc32(payload)))
            for chunk in (63, 16384):
                port = Port(wire, chunk)
                result = tool.receive_bench(port, len(payload), block, receive_mode='deferred')
                self.assertTrue(result['complete'] and result['crc_ok'] and result['pattern_ok'])
                self.assertEqual(result['bytes'], len(payload))
                self.assertEqual(len(port.writes), 1)
                self.assertLessEqual(result['receive_elapsed_s'], result['elapsed_s'])
                self.assertGreaterEqual(result['receive_KBps'], result['KBps'])
                self.assertIsNone(result['steady_s'])
            with self.assertRaises(tool.ProtocolError):
                tool.receive_bench(Port(wire[:-1]), len(payload), block, receive_mode='deferred')

    def test_deferred_still_rejects_corruption(self):
        prefix = b'*DEV#file#accepted#1#1#\r\n'
        good_data = bytes.fromhex(VECTORS['data'])
        good_end = bytes.fromhex(VECTORS['end'])
        cases = (good_data[:-1] + bytes([good_data[-1] ^ 1]) + good_end,
                 frame(1, 0, b'X') + good_end,
                 good_data + frame(2, 1, struct.pack('<II', 1, 999)),
                 frame(1, 0, b'XX') + good_end,
                 frame(1, 0, b'\0', transfer_id=2) + good_end,
                 b'X' * 20, bytes.fromhex(VECTORS['error']))
        for wire in cases:
            report = {}
            with self.assertRaises(tool.ProtocolError):
                tool.receive_bench(Port(prefix + wire), 1, 1024, report=report, receive_mode='deferred')
            self.assertFalse(report['complete'])
            self.assertFalse(report['passes_600KBps'])
        port = Port(b'')
        with self.assertRaises(tool.ProtocolError):
            tool.receive_bench(port, 10000, 1024, cancel_after=0, receive_mode='deferred')
        self.assertEqual(port.writes, [])

    def test_deferred_output_only_published_after_validation(self):
        prefix = b'*DEV#file#accepted#1#1#\r\n' + bytes.fromhex(VECTORS['data'])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'good.bin'
            tool.receive_bench(Port(prefix + bytes.fromhex(VECTORS['end'])), 1, 1024,
                               output=output, receive_mode='deferred')
            self.assertEqual(output.read_bytes(), b'\0')
            output = Path(directory) / 'bad.bin'
            with self.assertRaises(tool.ProtocolError):
                tool.receive_bench(Port(prefix + frame(2, 1, struct.pack('<II', 1, 999))), 1, 1024,
                                   output=output, receive_mode='deferred')
            self.assertFalse(output.exists())

    def test_entry_point_owns_one_port_and_reserves_report(self):
        port = Port(responses={
            b'*APP#file#caps#\r\n': b'*DEV#file#caps#1#8192#16777216#IDLE#\r\n',
            b'*APP#file#bench#1#1024#\r\n':
                b'*DEV#file#accepted#1#1#\r\n' + bytes.fromhex(VECTORS['data'] + VECTORS['end']),
        })
        serial = port.serial_module()
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict('sys.modules', {'serial': serial}), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            path = Path(directory) / 'nested/report.json'
            arguments = ['--port', 'COM6', '--report', str(path), 'bench', '--size', '1', '--block', '1024']
            self.assertEqual(tool.file_transfer_main(arguments), 0)
            serial.Serial.assert_called_once()
            original = path.read_bytes()
            self.assertTrue(json.loads(original)['runs'][0]['complete'])
            self.assertEqual(tool.file_transfer_main(arguments), 1)
            self.assertEqual(path.read_bytes(), original)
            serial.Serial.assert_called_once()

    def test_golden(self):
        self.assertEqual(zlib.crc32(b''), int(VECTORS['crc_empty'], 16))
        self.assertEqual(zlib.crc32(b'123456789'), int(VECTORS['crc_123456789'], 16))
        self.assertEqual(frame(1, 0, b'\0').hex(), VECTORS['data'])
        self.assertEqual(frame(2, 1, struct.pack('<II', 1, zlib.crc32(b'\0'))).hex(), VECTORS['end'])
        self.assertEqual(frame(3, 0, struct.pack('<I', 6)).hex(), VECTORS['error'])

    def test_fragmentation_and_coalescing(self):
        wire = b'*DEV#file#accepted#1#1#\r\n' + bytes.fromhex(VECTORS['data'] + VECTORS['end'])
        for chunk in (1, 2, 19, 20, 24, 64, 16384):
            with self.subTest(chunk=chunk):
                report = tool.receive_bench(Port(wire, chunk), 1, 1024)
                self.assertTrue(report['complete'] and report['crc_ok'])
                self.assertFalse(report['passes_600KBps'])

    def test_bad_headers_crc_offset_and_id(self):
        golden = bytes.fromhex(VECTORS['data'])
        for index in (0, 4, 5, 6, 8, 12, 16, 20, 24):
            bad = bytearray(golden)
            bad[index] ^= 0x80
            with self.subTest(index=index), self.assertRaises(tool.ProtocolError):
                tool.Reader(Port(bad)).frame(1, 0)
        for kind, payload in ((1, b''), (2, b'1234'), (3, b'12345678')):
            with self.assertRaises(tool.ProtocolError):
                tool.Reader(Port(frame(kind, 0, payload))).frame(1, 0)

    def test_bad_header_preserves_bounded_evidence(self):
        bad = b'X' * 20
        report = {}
        with self.assertRaisesRegex(tool.ProtocolError, 'invalid binary header'):
            tool.receive_bench(Port(b'*DEV#file#accepted#1#1#\r\n' + bad + b'Y' * 80),
                               1, 1024, report=report)
        self.assertIn('header=' + bad.hex(), report['error'])
        self.assertIn('expected_id=1 expected_offset=0', report['error'])
        self.assertIn('buffered=80 next=' + (b'Y' * 32).hex(), report['error'])

    def test_caps_and_text_bounds(self):
        for maximum in (16777216, 0xffffffff):
            reply = f'*DEV#file#caps#1#8192#{maximum}#IDLE#'
            self.assertEqual(tool.caps(Port((reply + '\r\n').encode())), reply)
            self.assertEqual(tool.caps_max_size(reply), maximum)
        for maximum in ('0', '-1', '4294967296', '1.5', ''):
            with self.assertRaises(tool.ProtocolError):
                tool.caps_max_size(f'*DEV#file#caps#1#8192#{maximum}#IDLE#')
        for data in (b'x' * 130, b'x\n', b'\xff\r\n'):
            with self.assertRaises(tool.ProtocolError):
                tool.Reader(Port(data)).line()

    def test_cancellation(self):
        port = Port(b'*DEV#file#accepted#1#1#\r\n' + bytes.fromhex(VECTORS['error']))
        report = tool.receive_bench(port, 10000, 1024, cancel_after=0)
        self.assertTrue(report['cancelled'])
        self.assertFalse(report['complete'])
        self.assertEqual(port.writes[-1], b'*APP#file#cancel#1#\r\n')

    def test_multiple_blocks_and_truncated_tail(self):
        payload = (bytes(range(256)) * 40)[:10001]
        for block in (1024, 2048, 4096, 8192):
            wire = b'*DEV#file#accepted#1#1#\r\n'
            for offset in range(0, len(payload), block):
                wire += frame(1, offset, payload[offset:offset + block])
            wire += frame(2, len(payload), struct.pack('<II', len(payload), zlib.crc32(payload)))
            self.assertTrue(tool.receive_bench(Port(wire, 63), len(payload), block)['complete'])
            with self.assertRaises(tool.ProtocolError):
                tool.receive_bench(Port(wire[:-1]), len(payload), block)

    def test_output_and_failed_crc(self):
        prefix = b'*DEV#file#accepted#1#1#\r\n' + bytes.fromhex(VECTORS['data'])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'test.bin'
            tool.receive_bench(Port(prefix + bytes.fromhex(VECTORS['end'])), 1, 1024, output)
            self.assertEqual(output.read_bytes(), b'\0')
            self.assertFalse(output.with_suffix('.bin.part').exists())
            output = Path(directory) / 'bad.bin'
            with self.assertRaises(tool.ProtocolError):
                tool.receive_bench(Port(prefix + frame(2, 1, struct.pack('<II', 1, 0))), 1, 1024, output)
            self.assertFalse(output.exists())
            self.assertEqual(output.with_suffix('.bin.part').read_bytes(), b'\0')


SOURCE_STUBS = r'''
static int source_mock_busy, source_mock_cancel;
static u32 source_mock_offset, source_mock_size = 10000;
static int source_mock_wait, source_mock_error, source_mock_close = 1;
int rdx_cdc_source_begin(u32 sn, const char *name) {
    if (source_mock_busy) return 2;
    source_mock_busy=1; source_mock_cancel=source_mock_offset=0;
    return 0;
}
int rdx_cdc_source_list_begin(void) { return rdx_cdc_source_begin(0,""); }
int rdx_cdc_source_busy(void) { return source_mock_busy; }
void rdx_cdc_source_cancel(void) { source_mock_cancel=1; }
void rdx_cdc_source_release(void) { source_mock_busy=0; }
void rdx_cdc_source_service(void) {}
int rdx_cdc_source_closed(void) { return source_mock_close; }
int rdx_cdc_source_take(u8 *data,u32 *length,u32 *size) {
    *size=source_mock_size; *length=0;
    if(source_mock_error) return -source_mock_error;
    if(source_mock_wait || source_mock_offset==source_mock_size) return 0;
    *length=source_mock_size-source_mock_offset; if(*length>4096) *length=4096;
    for(u32 index=0;index<*length;++index) data[index]=(u8)((source_mock_offset+index)*19+7);
    source_mock_offset+=*length; return 1;
}
'''

STUBS = r'''
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long size_t;
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
size_t strlen(const char *);
int snprintf(char *, size_t, const char *, ...);
#define TCFG_T2620_CDC_FILE_ENABLE 1
#define FACTORY_CDC_FULL -2
#define FACTORY_CDC_OK 0
static u32 now, generation=1;
static int credit=1000, occupied, aborted, terminals;
static u8 wire[40000];
static unsigned wire_length;
u32 sys_timer_get_ms(void) { return now; }
int rdx_dut_is_in_mode(void) { return occupied; }
int rdx_factory_audio_owned(void) { return 0; }
int rdx_factory_audio_file_busy(void) { return 0; }
int rdx_factory_audio_tx_pending(u32 gen) { return 0; }
void factory_cdc_abort(u32 gen) { if (gen==generation) { generation=0; ++aborted; } }
int factory_cdc_stream_send(u32 gen, const u8 *bytes, u32 length) {
    if (!gen || gen!=generation) return -1;
    if (!credit) return FACTORY_CDC_FULL;
    --credit;
    if (length>256 || wire_length+length>sizeof(wire)) return -3;
    if ((length==32 && bytes[5]==2) || (length==28 && bytes[5]==3)) ++terminals;
    memcpy(wire+wire_length, bytes, length); wire_length+=length; return 0;
}
int factory_cdc_send(u32 gen, const u8 *bytes, u32 length) { return -3; }
#define printf(...) ((void)0)
'''
TESTS = r'''
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static char reply[258];
static void boot(void) {
    source_mock_busy=source_mock_cancel=source_mock_offset=source_mock_wait=source_mock_error=0;
    source_mock_close=1;
    source_mock_size=10000;
    memset(&file_stream,0,sizeof(file_stream)); file_next_id=0;
    now=occupied=wire_length=aborted=terminals=0; credit=1000; generation=1;
}
static int command(const char *line) { return rdx_cdc_file_command(generation,line,0,reply,sizeof(reply)); }
static void drain(void) { for(int attempt=0; attempt<100 && rdx_cdc_file_active(); ++attempt) rdx_cdc_file_service(generation); }
int test_vectors(void) {
    boot();
    CHECK(rdx_cdc_file_crc(0,(const u8 *)"",0)==0);
    CHECK(rdx_cdc_file_crc(0,(const u8 *)"123456789",9)==0xcbf43926u);
    CHECK(rdx_cdc_file_crc(rdx_cdc_file_crc(0,(const u8 *)"1234",4),(const u8 *)"56789",5)==0xcbf43926u);
    CHECK(command("*APP#file#bench#1#1024#"));
    CHECK(!strcmp(reply,"*DEV#file#accepted#1#1#\r\n"));
    drain(); CHECK(!rdx_cdc_file_active() && wire_length==sizeof(golden_data)+sizeof(golden_end));
    CHECK(terminals==1);
    CHECK(!memcmp(wire,golden_data,sizeof(golden_data)));
    CHECK(!memcmp(wire+sizeof(golden_data),golden_end,sizeof(golden_end)));
    boot(); command("*APP#file#bench#1#1024#"); command("*APP#file#cancel#1#"); drain();
    CHECK(wire_length==sizeof(golden_error) && !memcmp(wire,golden_error,wire_length));
    return 0;
}
int test_cancel_and_backpressure(void) {
    boot(); command("*APP#file#bench#16384#8192#"); credit=1;
    rdx_cdc_file_service(generation); CHECK(wire_length==256 && file_stream.sent==256);
    command("*APP#file#cancel#2#"); CHECK(!file_stream.error && file_stream.deferred==9);
    command("*APP#file#cancel#1#"); command("*APP#file#cancel#1#");
    CHECK(file_stream.error==6 && file_stream.offset==0);
    credit=1000; drain(); CHECK(wire_length==8216+28 && file_stream.offset==8192);
    CHECK(wire[8216+5]==3 && wire[8216+20]==6);
    CHECK(rdx_cdc_file_reply(reply,sizeof(reply)) && !strcmp(reply,"*DEV#file#error#9#\r\n"));
    CHECK(!rdx_cdc_file_reply(reply,sizeof(reply)));
    boot(); command("*APP#file#bench#4096#4096#"); credit=0;
    rdx_cdc_file_service(generation); CHECK(!wire_length && file_stream.length);
    command("*APP#file#cancel#1#"); credit=1000; drain();
    CHECK(wire_length==28 && !file_stream.offset);
    return 0;
}
int test_admission_and_sessions(void) {
    const char *invalid[]={"*APP#file#bench#0#1024#","*APP#file#bench#16777217#1024#",
        "*APP#file#bench#42949672960#1024#","*APP#file#bench#1#512#","*APP#file#bench#1#1024#extra",
        "*APP#file#bench#-1#1024#","*APP#file#bench##1024#"};
    boot();
    for(unsigned index=0;index<sizeof(invalid)/sizeof(invalid[0]);++index) {
        CHECK(command(invalid[index]) && !strcmp(reply,"*DEV#file#error#1#\r\n") && !rdx_cdc_file_active());
    }
    command("*APP#file#get#1#abc#"); CHECK(!strcmp(reply,"*DEV#file#error#1#\r\n"));
    occupied=1; command("*APP#file#bench#1#1024#"); CHECK(!strcmp(reply,"*DEV#file#error#2#\r\n"));
    occupied=0; command("*APP#file#bench#10000#4096#");
    credit=1; rdx_cdc_file_service(1); CHECK(file_next_id==1);
    generation=2; rdx_cdc_file_session(2); CHECK(!rdx_cdc_file_active());
    wire_length=0; credit=1000; command("*APP#file#bench#1#1024#"); drain(); CHECK(wire[8]==2);
    file_next_id=0xffffffffu; command("*APP#file#bench#1#1024#"); CHECK(!strcmp(reply,"*DEV#file#error#8#\r\n"));
    boot(); command("*APP#file#bench#1#1024#"); now=10000; drain(); CHECK(wire_length==28 && wire[20]==7);
    boot(); command("*APP#file#bench#1#1024#"); generation=0; rdx_cdc_file_service(1);
    CHECK(!rdx_cdc_file_active() && !wire_length);
    boot(); command("*APP#file#list#");
    CHECK(!strcmp(reply,"*DEV#file#accepted#1#1#\r\n") && source_mock_busy);
    drain(); CHECK(!rdx_cdc_file_active() && !source_mock_busy && file_stream.offset==10000);
    command("*APP#file#list#extra"); CHECK(!strcmp(reply,"*DEV#file#error#1#\r\n"));
    source_mock_busy=1; command("*APP#file#list#"); CHECK(!strcmp(reply,"*DEV#file#error#2#\r\n"));
    return 0;
}
int test_all_blocks(void) {
    unsigned blocks[]={1024,2048,4096,8192};
    for(unsigned option=0;option<4;++option) {
        boot(); char line[80];
        snprintf(line,sizeof(line),"*APP#file#bench#10001#%u#",blocks[option]);
        command(line); drain(); CHECK(!rdx_cdc_file_active() && file_stream.offset==10001);
        unsigned position=0, offset=0;
        while (offset<10001) {
            unsigned length=10001-offset; if(length>blocks[option]) length=blocks[option];
            CHECK(wire[position+5]==1);
            CHECK(wire[position+12]==(u8)offset && wire[position+13]==(u8)(offset>>8));
            for(unsigned index=0;index<length;++index) CHECK(wire[position+20+index]==(u8)(offset+index));
            u32 expected=rdx_cdc_file_crc(0,wire+position,length+20);
            for(unsigned index=0;index<4;++index) CHECK(wire[position+20+length+index]==(u8)(expected>>(index*8)));
            position+=length+24; offset+=length;
        }
        CHECK(wire_length==position+32 && wire[position+5]==2);
        CHECK(file_stream.crc==GOLDEN_LARGE_CRC);
    }
    return 0;
}
int test_recording_producer(void) {
    boot(); command("*APP#file#get#7#abc123.raw#");
    CHECK(file_stream.recording && source_mock_busy);
    CHECK(!strcmp(reply,"*DEV#file#accepted#1#1#\r\n"));
    source_mock_wait=1; drain(); CHECK(!wire_length && file_stream.active);
    source_mock_wait=0; source_mock_close=0; drain();
    CHECK(file_stream.offset==10000 && file_stream.active && !terminals);
    source_mock_close=1; drain(); CHECK(!file_stream.active && !source_mock_busy && terminals==1);
    unsigned position=0, offset=0;
    while(offset<10000) {
        unsigned length=10000-offset; if(length>4096) length=4096;
        CHECK(wire[position+5]==1);
        for(unsigned index=0;index<length;++index) CHECK(wire[position+20+index]==(u8)((offset+index)*19+7));
        u32 crc=rdx_cdc_file_crc(0,wire+position,length+20);
        for(unsigned index=0;index<4;++index) CHECK(wire[position+20+length+index]==(u8)(crc>>(index*8)));
        offset+=length; position+=length+24;
    }
    CHECK(wire_length==position+32 && wire[position+5]==2);
    boot(); command("*APP#file#get#7#abc.raw#"); credit=1;
    rdx_cdc_file_service(1); command("*APP#file#cancel#1#");
    CHECK(source_mock_cancel); credit=1000; drain();
    CHECK(wire_length==4096+24+28 && wire[4096+24+20]==6);
    boot(); command("*APP#file#get#7#abc.raw#"); source_mock_error=5; drain();
    CHECK(wire_length==28 && wire[20]==5 && !source_mock_busy);
    boot(); command("*APP#file#get#7#abc.raw#"); rdx_cdc_file_session(2);
    CHECK(!source_mock_busy && !file_stream.active);
    const char *bad[]={"*APP#file#get#0#abc.raw#","*APP#file#get#2147483647#abc.raw#",
        "*APP#file#get#1#../abc.raw#","*APP#file#get#1#abcdef0.raw#","*APP#file#get#1#ab.raw#",
        "*APP#file#get#1#abc.raw##","*APP#file#get#1#abc.RAW#"};
    for(unsigned index=0;index<sizeof(bad)/sizeof(bad[0]);++index) {
        boot(); command(bad[index]); CHECK(!strcmp(reply,"*DEV#file#error#1#\r\n"));
    }
    return 0;
}
int test_progress_timeout(void) {
    for(unsigned wrap=0;wrap<2;++wrap) {
        boot(); if(wrap) now=0xfffff000u;
        command("*APP#file#get#7#abc.raw#");
        u32 started=now;
        for(unsigned attempt=0;attempt<100 && file_stream.active;++attempt) {
            now+=9000; credit=1;
            rdx_cdc_file_service(generation);
            CHECK(!file_stream.error);
        }
        CHECK(!file_stream.active && file_stream.offset==10000 && terminals==1);
        CHECK((u32)(now-started)>120000);
        boot(); if(wrap) now=0xfffff000u;
        command("*APP#file#get#7#abc.raw#"); source_mock_wait=1;
        now+=9999; rdx_cdc_file_service(generation);
        CHECK(file_stream.active && !file_stream.error);
        command("*APP#file#caps#");
        now+=1; drain();
        CHECK(!file_stream.active && source_mock_cancel && !source_mock_busy);
        CHECK(wire_length==28 && wire[20]==7);
    }
    return 0;
}
int test_recording_large_offsets(void) {
    u32 sizes[]={16777240u,143340000u,0x80000000u,0xfffffff0u};
    for(unsigned index=0;index<sizeof(sizes)/sizeof(sizes[0]);++index) {
        boot(); command("*APP#file#get#7#abc.raw#");
        source_mock_size=sizes[index];
        file_stream.offset=source_mock_offset=source_mock_size-4096;
        u32 offset=file_stream.offset;
        drain();
        CHECK(!file_stream.active && !source_mock_busy && file_stream.offset==sizes[index]);
        CHECK(wire_length==4096+24+32 && wire[5]==1 && wire[4120+5]==2);
        for(unsigned byte=0;byte<4;++byte) {
            CHECK(wire[12+byte]==(u8)(offset>>(byte*8)));
            CHECK(wire[4120+12+byte]==(u8)(sizes[index]>>(byte*8)));
            CHECK(wire[4120+20+byte]==(u8)(sizes[index]>>(byte*8)));
        }
    }
    boot(); command("*APP#file#get#7#abc.raw#");
    source_mock_size=143340000; file_stream.offset=source_mock_offset=20000000;
    credit=1; rdx_cdc_file_service(generation);
    command("*APP#file#cancel#1#"); credit=1000; drain();
    CHECK(source_mock_cancel && !source_mock_busy && !file_stream.active);
    CHECK(file_stream.offset==20004096 && wire_length==4096+24+28 && wire[4120+20]==6);
    return 0;
}
'''


def check_firmware():
    config = (ROOT / 'SDK/apps/earphone/include/t2620_project_config.h').read_text(encoding='utf-8')
    assert re.search(r'#define\s+TCFG_T2620_CDC_FILE_BENCH_ENABLE\s+[01]\b', config)
    runtime = ctypes.CDLL('msvcrt')
    for name in ('snprintf', 'strncmp', 'strcmp', 'strlen', 'memcpy', 'memset', 'memcmp'):
        symbol = '_snprintf' if name == 'snprintf' else name
        llvm.add_symbol(name, ctypes.cast(getattr(runtime, symbol), ctypes.c_void_p).value)
    source = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_cdc_file.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include.*$', '', source, flags=re.M)
    arrays = '\n'.join('static const u8 golden_' + name + '[]={' +
                       ','.join(str(value) for value in bytes.fromhex(VECTORS[name])) + '};'
                       for name in ('data', 'end', 'error'))
    with tempfile.TemporaryDirectory(prefix='cdc_file_') as directory:
        path = Path(directory) / 'file.c'
        tests = TESTS.replace('GOLDEN_LARGE_CRC', str(zlib.crc32((bytes(range(256)) * 40)[:10001])) + 'u')
        path.write_text('#define TCFG_T2620_CDC_FILE_BENCH_ENABLE 1\n' +
                        STUBS + SOURCE_STUBS + source + arrays + tests, encoding='utf-8')
        run_c_checks(path, ('test_vectors', 'test_cancel_and_backpressure', 'test_admission_and_sessions',
                           'test_all_blocks'), native=True)
        gate_tests = r'''
int test_bench_disabled(void) {
    boot(); command("*APP#file#bench#10000000#4096#");
    CHECK(!strcmp(reply,"*DEV#file#error#1#\r\n"));
    CHECK(!file_stream.active && !file_next_id && !source_mock_busy);
    command("*APP#file#caps#");
    CHECK(!strncmp(reply,"*DEV#file#caps#",15));
    command("*APP#file#list#");
    CHECK(file_stream.active && file_stream.recording);
    command("*APP#file#bench#10000000#4096#");
    CHECK(!reply[0] && file_stream.active && file_stream.deferred==2);
    drain(); CHECK(!file_stream.active);
    return 0;
}
'''
        path.write_text('#define TCFG_T2620_CDC_FILE_BENCH_ENABLE 0\n' +
                        STUBS + SOURCE_STUBS + source + arrays + tests + gate_tests, encoding='utf-8')
        run_c_checks(path, ('test_bench_disabled', 'test_recording_producer',
                           'test_recording_large_offsets', 'test_progress_timeout'), native=True)
    print('PASS: actual CDC file producer: vectors, CRC, bounds, partial cancel, stale sessions, timeout')




def check_transport():
    from test_factory_usb import CDC_STUBS, CDC_TESTS
    folder = ROOT / 'SDK/apps/common/device/usb/device'
    strip = lambda text: re.sub(r'^#include.*$', '', text, flags=re.M)
    headers = strip((folder / 'usb_factory_cdc.h').read_text()) + strip((folder / 'usb_factory_cdc_internal.h').read_text())
    source = strip((folder / 'usb_factory_cdc.c').read_text())
    tests = r'''
#if TCFG_T2620_CDC_TX_STATS_ENABLE
#define CHECK_STATS(value) CHECK(value)
#else
#define CHECK_STATS(value) ((void)0)
#endif
static int notifications, received;
static void notify(void) { ++notifications; if (irq_depth) violation=11; }
static int accept_rx(u32 gen,const u8 *bytes,u32 length) { ++received; return 0; }
int test_file_timing(void) {
#if TCFG_T2620_CDC_TX_TIMING_ENABLE
    u8 data[192]={0};
    boot(); u32 gen=factory_cdc_generation();
    CHECK(!factory_cdc_stream_send(gen,data,sizeof(data)));
    --queued; factory_cdc_process();
    test_clock_invalid=1; busy=0; factory_cdc_tx_irq(); test_clock_invalid=0;
    CHECK_STATS(cdc_tx_stats.submit_clock_invalid==1 && !cdc_tx_stats.submit_samples);
    CHECK_STATS(!cdc_tx_stats.submit_us_sum && !cdc_tx_stats.submit_us_max);
    CHECK(wire_len==128 && !violation); CHECK_STATS(cdc_tx_stats.irq_fast==1);
#endif
    return 0;
}
int test_file_stream_boundaries(void) {
    u8 data[256];
    for (unsigned index=0;index<sizeof(data);++index) data[index]=index;
    boot(); u32 gen=factory_cdc_generation();
    CHECK(!factory_cdc_stream_send(gen,data,24));
    CHECK(!factory_cdc_stream_send(gen,data+24,40));
    CHECK(!factory_cdc_stream_send(gen,data+64,32));
    --queued; factory_cdc_process();
    CHECK(wire_len==24 && transport.count==2);
    busy=0; factory_cdc_tx_irq();
    CHECK(wire_len==64 && transport.count==1);
    busy=0; factory_cdc_tx_irq();
    CHECK(wire_len==96 && !transport.count);
    pump(3);
    CHECK(!memcmp(wire,data,96) && !transport.tx_wait && !zlp_count);
    CHECK_STATS(cdc_tx_stats.bytes==96 && cdc_tx_stats.packets==3);
    CHECK(!violation && !irq_depth);
    boot(); gen=factory_cdc_generation();
    CHECK(!factory_cdc_stream_send(gen,data,24));
    zero_write=1; --queued; factory_cdc_process();
    CHECK(!wire_len && transport.count==1 && !transport.tx[transport.head].offset);
    zero_write=0; pump(3);
    CHECK(wire_len==24 && !memcmp(wire,data,24));
    return 0;
}
int test_file_irq_stream(void) {
    u8 data[256];
    for (unsigned index=0;index<sizeof(data);++index) data[index]=index;
    boot(); u32 gen=factory_cdc_generation(); notifications=0;
    factory_cdc_set_tx_handler(notify);
    CHECK(factory_cdc_stream_send(gen,data,256)==0);
    CHECK(factory_cdc_stream_send(gen,data,65)==0);
    --queued; factory_cdc_process();
    CHECK(wire_len==64 && !queued);
    factory_cdc_tx_irq();
    CHECK(wire_len==64); CHECK_STATS(!cdc_tx_stats.bytes);
    --queued; factory_cdc_process();
    for (unsigned index=0;index<4;++index) {
        busy=0; factory_cdc_tx_irq();
        CHECK(wire_len==128+index*64 && !queued && !notifications);
    }
    busy=0; factory_cdc_tx_irq();
    CHECK(wire_len==321 && queued==1 && !notifications);
    CHECK_STATS(cdc_tx_stats.irq_fast==5 && cdc_tx_stats.bytes==320);
#if TCFG_T2620_CDC_TX_TIMING_ENABLE
    CHECK_STATS(cdc_tx_stats.submit_samples==1 && cdc_tx_stats.submit_us_sum==3);
    CHECK_STATS(cdc_tx_stats.submit_us_max==3 && !cdc_tx_stats.submit_clock_invalid);
#endif
    CHECK(!memcmp(wire,data,256) && !memcmp(wire+256,data,65));
    --queued; factory_cdc_process();
    CHECK(!notifications); CHECK_STATS(cdc_tx_stats.bytes==320);
    busy=0; factory_cdc_tx_irq(); --queued; factory_cdc_process();
    CHECK(notifications==1 && !zlp_count); CHECK_STATS(cdc_tx_stats.bytes==321);
    CHECK(!transport.tx_wait && !violation && !irq_depth);

    boot(); gen=factory_cdc_generation(); notifications=0;
    factory_cdc_set_tx_handler(notify);
    for (unsigned index=0;index<FACTORY_CDC_TX_DEPTH;++index)
        CHECK(factory_cdc_stream_send(gen,data,256)==0);
    --queued; factory_cdc_process();
    for (unsigned index=0;index<63;++index) { busy=0; factory_cdc_tx_irq(); }
    CHECK(queued==1 && !notifications && transport.refill_armed);
    --queued; factory_cdc_process();
    CHECK(notifications==1 && !transport.refill_armed);
    factory_cdc_set_rx_handler(accept_rx); received=0;
    in_len=1; incoming[0]='C'; factory_cdc_rx_irq();
    --queued; factory_cdc_process(); CHECK(received==1);
    CHECK(!violation && !irq_depth);

    boot(); gen=factory_cdc_generation();
    CHECK(factory_cdc_send(gen,data,64)==0);
    CHECK(factory_cdc_stream_send(gen,data,128)==0);
    --queued; factory_cdc_process(); busy=0; factory_cdc_tx_irq();
    CHECK(wire_len==64); CHECK_STATS(!cdc_tx_stats.irq_fast);
    --queued; factory_cdc_process(); CHECK(zlp_count==1);
    pump(5); CHECK(wire_len==192 && zlp_count==1);

    for (int reason=FACTORY_CDC_RESET;reason<=FACTORY_CDC_TIMEOUT;++reason) {
        boot(); gen=factory_cdc_generation();
        CHECK(factory_cdc_stream_send(gen,data,256)==0);
        --queued; factory_cdc_process(); busy=0; factory_cdc_tx_irq();
        CHECK(wire_len==128);
        factory_cdc_invalidate(reason); busy=0; factory_cdc_tx_irq();
        pump(5); CHECK(wire_len==128 && !transport.tx_pending_length);
    }
    boot(); gen=factory_cdc_generation();
    CHECK(factory_cdc_stream_send(gen,data,256)==0);
    --queued; factory_cdc_process(); factory_cdc_quiesce();
    busy=0; factory_cdc_tx_irq(); CHECK(wire_len==64);

    boot(); gen=factory_cdc_generation();
    CHECK(factory_cdc_stream_send(gen,data,256)==0);
    --queued; factory_cdc_process(); now=3000; busy=0; factory_cdc_tx_irq();
    CHECK(wire_len==64); --queued; factory_cdc_process();
    CHECK(factory_cdc_needs_restart());

    boot(); gen=factory_cdc_generation();
    CHECK(factory_cdc_stream_send(gen,data,128)==0);
    --queued; factory_cdc_process(); post_fail=1; busy=0; factory_cdc_tx_irq();
    CHECK(wire_len==128 && !queued);
    post_fail=0; pump(2); CHECK(!transport.tx_wait); CHECK_STATS(cdc_tx_stats.bytes==128);
    CHECK(!violation && !irq_depth);
    return 0;
}
int test_file_wake_edges(void) {
    u8 data[256]; memset(data,0xa5,sizeof(data));
    boot(); u32 gen=factory_cdc_generation(); notifications=0;
    factory_cdc_set_tx_handler(notify);
    CHECK(factory_cdc_stream_send(gen,data,256)==0 && queued==1);
    --queued; factory_cdc_process();
    CHECK(busy && !queued && !notifications);
    CHECK_STATS(!cdc_tx_stats.bytes && !cdc_tx_stats.packets);
    CHECK(factory_cdc_stream_send(gen,data,256)==0 && !queued);
    factory_cdc_rx_irq(); CHECK(queued==1);
    --queued; factory_cdc_process();
    CHECK(!queued && !notifications);
    for(unsigned index=2;index<FACTORY_CDC_TX_DEPTH;++index) {
        CHECK(factory_cdc_stream_send(gen,data,256)==0);
        CHECK(!queued);
    }
    CHECK(transport.refill_armed);
    pump(63);
    CHECK(transport.count==FACTORY_CDC_TX_DEPTH/2 && notifications==1);
    CHECK_STATS(cdc_tx_stats.bytes==63*64 && cdc_tx_stats.packets==63);
    CHECK(!transport.refill_armed);
    pump(1); CHECK(notifications==1);
    for(unsigned index=0;index<4;++index) tick();
    CHECK(notifications==1);
    CHECK_STATS(cdc_tx_stats.busy && cdc_tx_stats.refill);
    CHECK(factory_cdc_stream_send(gen,data,256)==0 && !queued);
    CHECK(transport.refill_armed);
    pump(3); CHECK(notifications==2 && !transport.refill_armed);
    pump(100);
    CHECK(wire_len==33*256 && notifications==3 && !zlp_count);
    for(unsigned index=0;index<33*256;++index) CHECK(wire[index]==0xa5);
    pump(4); CHECK(notifications==3);
    CHECK(factory_cdc_stream_send(gen,data,1)==0 && queued==1);
    pump(2); CHECK(wire_len==33*256+1 && notifications==4);
    CHECK_STATS(cdc_tx_stats.refill && cdc_tx_stats.drained);
    factory_cdc_invalidate(FACTORY_CDC_RESET);
    CHECK(!transport.refill_armed); CHECK_STATS(!cdc_tx_stats.bytes && !cdc_tx_stats.dispatch);
    CHECK_STATS(cdc_tx_stats.generation==transport.generation);
    CHECK(!violation && !irq_depth);
    boot(); gen=factory_cdc_generation(); post_fail=1;
    CHECK(factory_cdc_stream_send(gen,data,256)==0 && !queued);
    CHECK(!transport.posted);
    post_fail=0; pump(5); CHECK(wire_len==256);
    CHECK(!violation && !irq_depth);
    return 0;
}
int test_file_transport(void) {
    u8 data[256]; memset(data,0xa5,sizeof(data));
    boot(); u32 gen=factory_cdc_generation();
    factory_cdc_set_tx_handler(notify);
    CHECK(factory_cdc_stream_send(gen,data,sizeof(data))==0);
    CHECK(queued==1); factory_cdc_rx_irq(); factory_cdc_rx_irq(); CHECK(queued==1);
    pump(6); CHECK(wire_len==256 && !zlp_count && notifications && !violation && !irq_depth);
    boot(); gen=factory_cdc_generation();
    CHECK(factory_cdc_send(gen,data,sizeof(data))==0); pump(6); CHECK(zlp_count==1);
    boot(); gen=factory_cdc_generation(); factory_cdc_set_rx_handler(accept_rx);
    for(unsigned index=0;index<FACTORY_CDC_TX_DEPTH;++index) CHECK(factory_cdc_stream_send(gen,data,256)==0);
    CHECK(factory_cdc_stream_send(gen,data,1)==FACTORY_CDC_FULL);
    busy=1; in_len=1; incoming[0]='C'; received=0; tick();
    CHECK(received==1 && !transport.rx_length && transport.count==FACTORY_CDC_TX_DEPTH);
    now=3000; tick(); CHECK(!factory_cdc_generation() && factory_cdc_needs_restart());
    CHECK(factory_cdc_stream_send(gen,data,1)==FACTORY_CDC_STALE);
    CHECK(!violation && !irq_depth);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='cdc_stream_') as directory:
        path = Path(directory) / 'transport.c'
        for stats, timing, log_level in ((0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 0, 0), (1, 1, 0)):
            timing_stubs = '''
static u32 test_clock_us = 0xfffffffe;
static int test_clock_invalid;
u32 jiffies_usec(void) { test_clock_us += 3; return test_clock_us; }
int jiffies_usec2offset(u32 begin, u32 end) { return test_clock_invalid ? -1 : (int)(end - begin); }
'''
            path.write_text(CDC_STUBS.replace('#define TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL 1',
                                              f'#define TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL {log_level}') +
                            '\n#define TCFG_T2620_CDC_FILE_ENABLE 1\n' +
                            f'#define TCFG_T2620_CDC_TX_STATS_ENABLE {stats}\n#define TCFG_T2620_CDC_TX_TIMING_ENABLE {timing}\n' + timing_stubs +
                            headers + source + CDC_TESTS + tests, encoding='utf-8')
            run_c_checks(path, ('test_file_transport', 'test_file_wake_edges', 'test_file_irq_stream',
                                'test_file_stream_boundaries', 'test_file_timing'))
    print('PASS: file transport (diagnostics off / stats / timing): wake edges/refill rearm, completion statistics, full-TX RX, stream/ZLP, stale generation, DMA timeout')


def check_irq_driver():
    from test_factory_usb import CDC_DRIVER_STUBS, CDC_DRIVER_TESTS
    folder = ROOT / 'SDK/apps/common/device/usb/device'
    headers = '\n'.join((folder / name).read_text(encoding='utf-8') for name in
                        ('usb_factory_cdc.h', 'usb_factory_cdc_internal.h'))
    headers = re.sub(r'^#include.*$', '', headers, flags=re.M)
    driver = (folder / 'cdc.c').read_text(encoding='utf-8')
    callback = driver[driver.index('static void cdc_tx_wakeup('):driver.index('\n#endif', driver.index('static void cdc_tx_wakeup('))]
    driver = driver[driver.index('void cdc_factory_configuration('):driver.index('\nu32 cdc_read_data(')]
    driver = driver[:driver.rindex('#endif')]
    stubs = r'''
#define TCFG_T2620_CDC_FILE_ENABLE 1
static int tx_enabled, irq_called, mask_seen;
void usb_set_intr_txe(int id,int ep) { tx_enabled=1; }
void usb_clr_intr_txe(int id,int ep) { tx_enabled=0; }
void factory_cdc_tx_irq(void) {
    ++irq_called;
    mask_seen=!tx_enabled;
    cdc_factory_write_packet(tx_dma,64);
}
'''
    tests = r'''
int test_irq_driver(void) {
    CHECK(!test_driver());
    device.bDeviceStates=USB_CONFIGURED;
    handle.bmTransceiver=BIT(0)|BIT(4);
    txcsr=0; tx_enabled=1;
    int before=committed;
    cdc_tx_wakeup(&device,CDC_DATA_EP_IN);
    CHECK(irq_called==1 && mask_seen && tx_enabled && committed==before+1);
    CHECK(txcsr & TXCSRP_TxPktRdy);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='cdc_irq_driver_') as directory:
        path = Path(directory) / 'driver.c'
        path.write_text(CDC_DRIVER_STUBS + headers + stubs + callback + driver +
                        CDC_DRIVER_TESTS + tests, encoding='utf-8')
        run_c_checks(path, ('test_irq_driver',))
    print('PASS: actual CDC IN callback: mask, bounded commit, rearm; DMA busy guard')


def check_parser_integration():
    from test_factory_protocol import STUBS as parser_stubs, TESTS as parser_tests
    parser_stubs = re.sub(r'int printf\(.*?\n}',
                         'int printf(const char *format, ...) { if (irq) log_in_irq=1; return 0; }',
                         parser_stubs, flags=re.S)
    folder = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    source = '\n'.join(re.sub(r'^#include.*$', '', (folder / name).read_text(encoding='utf-8'), flags=re.M)
                       for name in ('rdx_cdc_file.c', 'rdx_factory_cdc.c'))
    stubs = r'''
#define TCFG_T2620_CDC_FILE_ENABLE 1
void factory_cdc_set_tx_handler(void (*handler)(void)) {}
int rdx_dut_is_in_mode(void) { return 0; }
int rdx_factory_audio_owned(void) { return 0; }
int rdx_factory_audio_file_busy(void) { return 0; }
int rdx_factory_audio_tx_pending(u32 gen) { return 0; }
int factory_cdc_stream_send(u32 gen,const u8 *bytes,u32 length) { return factory_cdc_send(gen,bytes,length); }
'''
    tests = r'''
int test_file_parser(void) {
    boot(); rdx_cdc_file_session(0); file_next_id=0;
    CHECK(!input("*APP#file#caps#\r\n"));
    CHECK(!strcmp(wire,"*DEV#file#caps#1#8192#4294967295#IDLE#\r\n"));
    wire_length=0;
    CHECK(!input("*APP#file#bench#1#1024#\r\n"));
    unsigned prefix=strlen("*DEV#file#accepted#1#1#\r\n");
    CHECK(wire_length==prefix+57 && !memcmp(wire+prefix,"CDF1",4));
    boot(); rdx_cdc_file_session(0); file_next_id=0;
    CHECK(!input("*APP#file#bench#8192#8192#\r\n*APP#file#cancel#1#\r\n"));
    CHECK(wire_length==prefix+28 && wire[prefix+5]==3 && wire[prefix+20]==6);
    boot(); rdx_cdc_file_session(0); file_next_id=0;
    CHECK(!input("*APP#file#bench#8192#8192#\r\n*APP#file#cancel#1#\r\nPING\r\n"));
    CHECK(!strcmp(wire+prefix+28,"*DEV#file#error#2#\r\n"));
    CHECK(!rdx_cdc_file_active() && !log_in_irq && !irq);
    return 0;
}
'''
    runtime = ctypes.CDLL('msvcrt')
    for name in ('strchr', 'strcpy'):
        llvm.add_symbol(name, ctypes.cast(getattr(runtime, name), ctypes.c_void_p).value)
    with tempfile.TemporaryDirectory(prefix='cdc_parser_') as directory:
        path = Path(directory) / 'parser.c'
        path.write_text('#define TCFG_T2620_CDC_FILE_BENCH_ENABLE 1\n' +
                        parser_stubs + stubs + SOURCE_STUBS + source + parser_tests + tests, encoding='utf-8')
        run_c_checks(path, ('test_file_parser',), native=True)
    print('PASS: actual common parser: CAPS/accepted/binary order, coalesced cancel, deferred BUSY')


if __name__ == '__main__':
    from cdc_conflict_checks import run_conflict_checks
    from cdc_source_checks import run_source_checks
    run_conflict_checks()
    run_source_checks()
    check_firmware()
    check_transport()
    check_irq_driver()
    check_parser_integration()
    unittest.main()
