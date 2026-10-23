"""Station fault tests, no serial device or pyserial required."""
import contextlib
import io
import json
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

from host_c_test_lib import load_cdc_tool

_cdc = load_cdc_tool()
Station, StationError = _cdc.Station, _cdc.StationError


class Port:
    def __init__(self, replies):
        self.replies = list(replies)
        self.writes = []

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def read_until(self, *args, **kwargs):
        return self.replies.pop(0)


def status(n, command, state='DONE', active='NONE', dut='1'):
    return (f'OK DUT={dut};ACTIVE={active};LAST_ID={n};LAST_CMD={command};'
            f'LAST_STATE={state};LAST_ERR=NONE\r\n').encode()


class StationTests(unittest.TestCase):
    def test_file_menu_routing_without_opening_port(self):
        serial = types.ModuleType('serial')
        serial.tools = types.ModuleType('serial.tools')
        serial.tools.list_ports = types.ModuleType('serial.tools.list_ports')
        serial.tools.list_ports.comports = lambda: [types.SimpleNamespace(device='COM6', description='CDC')]
        serial.Serial = mock.Mock(side_effect=AssertionError('launcher must not open port'))
        modules = {'serial': serial, 'serial.tools': serial.tools, 'serial.tools.list_ports': serial.tools.list_ports}
        for kind, selection, mode, repeat in (
                ('', '', 'file-download', '1'), ('1', '2', 'file-download', '10'),
                ('2', '1', 'file-bench', '1'), ('2', '2', 'file-bench', '10')):
            with self.subTest(selection=selection), mock.patch.dict('sys.modules', modules), \
                    mock.patch('builtins.input', side_effect=['3', kind, selection, '6']), \
                    mock.patch.object(_cdc, 'main', return_value=0) as entry:
                self.assertEqual(_cdc.launcher([]), 0)
                entry.assert_called_once_with(['--port', 'COM6', '--mode', mode, '--bench-repeat', repeat])
        serial.Serial.assert_not_called()
        for answers in (['3', 'q', 'q'], ['3', '1', 'q', 'q', 'q']):
            with mock.patch.dict('sys.modules', modules), \
                    mock.patch('builtins.input', side_effect=answers), \
                    mock.patch.object(_cdc, 'main', return_value=0) as entry:
                self.assertEqual(_cdc.launcher([]), 0)
                entry.assert_not_called()
        serial.Serial.assert_not_called()

    def test_file_mode_uses_shared_receiver(self):
        module = types.SimpleNamespace(main=mock.Mock(return_value=2))
        with mock.patch.object(_cdc, 'file_transfer_main', module.main), \
                mock.patch.object(_cdc, 'Station', side_effect=AssertionError('must not enter DUT')):
            result = _cdc.main(['--port', 'COM6', '--mode', 'file-bench', '--bench-block', '8192',
                                '--bench-repeat', '10', '--report', 'bench.json', '--bench-receive-mode', 'deferred'])
        self.assertEqual(result, 1)
        module.main.assert_called_once_with(['--port', 'COM6', '--report', 'bench.json', 'bench',
                                             '--size', '10000000', '--block', '8192', '--repeat', '10',
                                             '--receive-mode', 'deferred'])
        module.main.reset_mock(return_value=True)
        module.main.return_value = 0
        with mock.patch.object(_cdc, 'file_transfer_main', module.main):
            self.assertEqual(_cdc.main(['--port', 'COM6', '--mode', 'file-cancel']), 0)
        self.assertEqual(module.main.call_args.args[0][-2:], ['--cancel-after', '1048576'])

    def test_key_dut_enable_menu_routing(self):
        serial = types.ModuleType('serial')
        serial.tools = types.ModuleType('serial.tools')
        serial.tools.list_ports = types.ModuleType('serial.tools.list_ports')
        serial.tools.list_ports.comports = lambda: [types.SimpleNamespace(device='COM6', description='CDC')]
        modules = {'serial': serial, 'serial.tools': serial.tools, 'serial.tools.list_ports': serial.tools.list_ports}
        with mock.patch.dict('sys.modules', modules), \
                mock.patch('builtins.input', side_effect=['4', '6']), \
                mock.patch.object(_cdc, 'main', return_value=0) as entry:
            self.assertEqual(_cdc.launcher([]), 0)
        entry.assert_called_once_with(['--port', 'COM6', '--mode', 'key-dut-enable'])

    def test_key_dut_enable_cli_report_scope(self):
        serial = types.ModuleType('serial')
        serial.tools = types.ModuleType('serial.tools')
        serial.tools.list_ports = types.ModuleType('serial.tools.list_ports')
        serial.SerialException = OSError
        serial.Serial = mock.MagicMock()
        modules = {'serial': serial, 'serial.tools': serial.tools, 'serial.tools.list_ports': serial.tools.list_ports}
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict('sys.modules', modules), \
                mock.patch.object(_cdc, 'Station') as factory, mock.patch.object(_cdc, 'prepare') as audio:
            station = factory.return_value
            station.audio = None
            path = Path(directory) / 'enable.json'
            self.assertEqual(_cdc.main(['--port', 'COM6', '--mode', 'key-dut-enable',
                                        '--key-timeout', '60', '--report', str(path)]), 0)
            station.foundation.assert_called_once_with(None, None)
            station.key_dut_enable.assert_called_once_with()
            station.controls.assert_not_called()
            station.packaging.assert_not_called()
            audio.assert_not_called()
            report = json.loads(path.read_text(encoding='utf-8'))
            self.assertEqual((report['scope'], report['outcome']), ('key-dut-enable', 'PASS'))
            self.assertFalse(report['overall_factory_pass'])
            self.assertIn('实体按键及重启持久化未验证', report['message'])
            self.assertNotIn('已启用并验证', report['message'])

    def test_recording_mode_uses_shared_receiver(self):
        module = types.SimpleNamespace(main=mock.Mock(return_value=0))
        with mock.patch.object(_cdc, 'file_transfer_main', module.main), \
                mock.patch.object(_cdc, 'Station', side_effect=AssertionError('must not enter DUT')):
            self.assertEqual(_cdc.main(['--port', 'COM6', '--mode', 'file-download', '--file-sn', '7',
                                        '--file-name', 'abc123.raw', '--file-output', 'record.raw',
                                        '--report', 'download.json', '--file-reference', 'baseline.raw',
                                        '--file-cancel-after', '4096']), 0)
        module.main.assert_called_once_with(['--port', 'COM6', '--report', 'download.json', 'download',
                                             '--output', 'record.raw', '--repeat', '1',
                                             '--sn', '7', '--name', 'abc123.raw', '--reference', 'baseline.raw',
                                             '--cancel-after', '4096'])
        module.main.reset_mock()
        with mock.patch.object(_cdc, 'file_transfer_main', module.main):
            self.assertEqual(_cdc.main(['--port', 'COM6', '--mode', 'file-download',
                                       '--file-output', 'record.raw', '--report', 'download.json']), 0)
        module.main.assert_called_once_with(['--port', 'COM6', '--report', 'download.json', 'download',
                                             '--output', 'record.raw', '--repeat', '1'])
        with mock.patch.object(_cdc, 'file_transfer_test') as transfer, \
                contextlib.redirect_stderr(io.StringIO()):
            for arguments in (['--file-sn', '7'], ['--file-name', 'abc123.raw'],
                              ['--file-sn', '7', '--file-name', '../abc.raw'],
                              ['--file-sn', '7', '--file-name', 'abc123.raw', '--bench-receive-mode', 'deferred']):
                with self.assertRaises(SystemExit):
                    _cdc.main(['--port', 'COM6', '--mode', 'file-download'] + arguments)
            transfer.assert_not_called()

    def test_single_file_without_repository_modules(self):
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(_cdc, '__file__', str(Path(directory) / 'CDC-test.bat')), \
                mock.patch.object(_cdc, 'file_transfer_main', return_value=0) as receiver:
            self.assertEqual(_cdc.main(['--port', 'COM6', '--mode', 'file-bench']), 0)
            receiver.assert_called_once()

    def station(self, replies, **kwargs):
        port = Port(replies)
        return Station(port, {'steps': []}, **kwargs)

    def test_old_firmware_never_receives_action(self):
        s = self.station([b'ERR UNSUPPORTED\r\n'])
        with self.assertRaises(StationError) as caught:
            s.controls()
        self.assertEqual(caught.exception.outcome, 'BLOCKED')
        self.assertEqual(s.port.writes, [b'GET_STATUS\r\n'])

    def test_timeout_does_not_retry(self):
        s = self.station([b''])
        with self.assertRaises(StationError) as caught:
            s.action('ft_dut', 1, 'NONE', '1')
        self.assertEqual(caught.exception.outcome, 'UNKNOWN')
        self.assertEqual(len(s.port.writes), 1)

    def test_overwritten_last_is_unknown(self):
        s = self.station([b'OK ID=1;STATE=RUNNING\r\n', status(2, 'ft_dut')])
        with self.assertRaises(StationError) as caught:
            s.action('ft_dut', 1, 'NONE', '1')
        self.assertEqual(caught.exception.outcome, 'UNKNOWN')
        self.assertEqual(len(s.port.writes), 2)

    def test_full_identity_in_console_and_report(self):
        secret = 'S' * 24
        s = self.station([b'OK PONG\r\n',
                          f'OK PROTO=1;NAME=T2620;AUTH={secret};ADDR=123456789ABC;FW=1.0\r\n'.encode()])
        console = io.StringIO()
        with contextlib.redirect_stdout(console):
            s.foundation('123456789ABC', '1.0')
        self.assertIn('设备名称: T2620', console.getvalue())
        self.assertIn('BLE 地址: 123456789ABC', console.getvalue())
        self.assertIn('固件版本: 1.0', console.getvalue())
        self.assertIn('协议版本: 1', console.getvalue())
        self.assertIn('AUTH: ' + secret, console.getvalue())
        self.assertEqual(s.report['device']['AUTH'], secret)
        self.assertIn(secret, json.dumps(s.report))

    def test_control_sequence(self):
        replies = [status(0, 'NONE', 'NONE', dut='0')]
        commands = [('ft_dut', 1, 'NONE', '1'), ('ft_led_cycle', 1, 'OLED', '1'),
                    ('ft_led_cycle', 0, 'NONE', '1'), ('ft_speaker', 1, 'SPEAKER', '1'),
                    ('ft_speaker', 0, 'NONE', '1'), ('ft_key_layout', 1, 'KEY', '1'),
                    ('ft_key', 0, 'NONE', '1'), ('ft_dut', 0, 'NONE', '0')]
        for n, (cmd, value, active, dut) in enumerate(commands, 1):
            replies += [f'OK ID={n};STATE=DONE\r\n'.encode(), status(n, cmd, active=active, dut=dut)]
            if cmd == 'ft_key_layout':
                replies += [f'OK TEST_ID=6;LAYOUT=A,B&C,D&E;SEEN={seen};ACTIVE=1\r\n'.encode()
                            for seen in ('00000', '10000', '10000', '11111')]
        replies += [b'OK PONG\r\n']
        answers = iter(['', 'y', 'y'])
        s = self.station(replies, ask=lambda _: next(answers), sleep=lambda _: None)
        console = io.StringIO()
        with contextlib.redirect_stdout(console):
            s.controls()
        output = console.getvalue()
        self.assertEqual(output.count('A: 0   B: 0   C: 0   D: 0   E: 0'), 1)
        self.assertEqual(output.count('A: 1   B: 0   C: 0   D: 0   E: 0'), 1)
        self.assertIn('A: 1   B: 1   C: 1   D: 1   E: 1', output)
        self.assertNotIn('ft_key_layout', output)
        self.assertTrue(any(step['step'] == 'ft_key_layout 1' for step in s.report['steps']))
        actions = [data for data in s.port.writes if data.startswith(b'*APP#custom#')]
        expected = [f'*APP#custom#{c}#{v}#\r\n'.encode() for c, v, _, _ in commands]
        expected[5] = b'*APP#custom#ft_key_layout#\r\n'
        self.assertEqual(actions, expected)
        self.assertFalse(s.port.replies)

    def test_record_chat_uses_canonical_status_name(self):
        station = self.station([b'OK ID=1;STATE=DONE\r\n', status(1, 'ft_rec_chat', active='REC'),
                                b'OK ID=2;STATE=DONE\r\n', status(2, 'ft_rec_chat')])
        self.assertEqual(station.action('ft_rec_chat', 1, 'REC', '1'), 1)
        self.assertEqual(station.action('ft_rec_chat', 0, 'NONE', '1'), 2)
        self.assertEqual(station.port.writes, [b'*APP#custom#ft_rec_chat#1#\r\n', b'GET_STATUS\r\n',
                                             b'*APP#custom#ft_rec_chat#0#\r\n', b'GET_STATUS\r\n'])

    def test_record_chat_rejects_legacy_status(self):
        station = self.station([b'OK ID=1;STATE=DONE\r\n', status(1, 'ft_rec', active='REC')])
        with self.assertRaises(StationError) as caught:
            station.action('ft_rec_chat', 1, 'REC', '1')
        self.assertEqual(caught.exception.outcome, 'UNKNOWN')

    def test_packaging_sequence_and_decline(self):
        idle = status(0, 'NONE', 'NONE', dut='0')
        s = self.station([idle], ask=lambda _: 'n')
        with self.assertRaises(StationError):
            s.packaging()
        self.assertEqual(s.port.writes, [b'GET_STATUS\r\n'])
        s = self.station([idle, b'OK ID=1;STATE=DONE\r\n', status(1, 'ft_dut'),
                          b'OK ID=2;STATE=RUNNING\r\n',
                          status(2, 'ft_finalpack_end', 'RUNNING', 'FINALPACK'),
                          status(2, 'ft_finalpack_end', active='FINALPACK'), b'OK PONG\r\n'],
                         ask=lambda _: 'y', sleep=lambda _: None)
        s.packaging()
        self.assertEqual([w for w in s.port.writes if w.startswith(b'*APP#custom#')],
                         [b'*APP#custom#ft_dut#1#\r\n', b'*APP#custom#ft_finalpack_end#1#\r\n'])
        self.assertFalse(s.port.replies)

    def enable_replies(self):
        replies = [status(0, 'NONE', 'NONE', dut='0')]
        for n, command, dut in ((1, 'ft_dut', '1'), (2, 'ft_key_dut_enable', '1'), (3, 'ft_dut', '0')):
            replies += [f'OK ID={n};STATE=DONE\r\n'.encode(), status(n, command, dut=dut)]
        return replies

    def test_key_dut_enable_sequence_without_physical_entry(self):
        ask = mock.Mock(return_value='y')
        sleep = mock.Mock(side_effect=AssertionError('must not wait for physical entry'))
        s = self.station(self.enable_replies() + [b'OK PONG\r\n'], ask=ask, sleep=sleep)
        console = io.StringIO()
        with contextlib.redirect_stdout(console):
            s.key_dut_enable()
        self.assertEqual(s.port.writes, [b'GET_STATUS\r\n', b'*APP#custom#ft_dut#1#\r\n',
                         b'GET_STATUS\r\n', b'*APP#custom#ft_key_dut_enable#\r\n',
                         b'GET_STATUS\r\n', b'*APP#custom#ft_dut#0#\r\n',
                         b'GET_STATUS\r\n', b'PING\r\n'])
        ask.assert_called_once()
        sleep.assert_not_called()
        self.assertIn('[2/3]', console.getvalue())
        self.assertIn('[3/3]', console.getvalue())
        self.assertNotIn('八击', console.getvalue())
        self.assertFalse(any(step.get('trigger') for step in s.report['steps']))
        self.assertFalse(s.port.replies)

    def test_key_dut_enable_waits_for_vm_completion_before_exit(self):
        replies = self.enable_replies()[:3] + [b'OK ID=2;STATE=RUNNING\r\n',
                  status(2, 'ft_key_dut_enable', 'RUNNING'), status(2, 'ft_key_dut_enable')]
        replies += self.enable_replies()[5:] + [b'OK PONG\r\n']
        sleep = mock.Mock()
        s = self.station(replies, ask=lambda _: 'y', sleep=sleep)
        s.key_dut_enable()
        sleep.assert_called_once_with(0.5)
        self.assertEqual(s.port.writes[3:8], [b'*APP#custom#ft_key_dut_enable#\r\n',
                         b'GET_STATUS\r\n', b'GET_STATUS\r\n',
                         b'*APP#custom#ft_dut#0#\r\n', b'GET_STATUS\r\n'])
        self.assertFalse(s.port.replies)

    def test_key_dut_enable_decline_busy_and_old_firmware(self):
        for initial, expected, confirm in ((status(0, 'NONE', 'NONE', dut='0'), 'BLOCKED', 'n'),
                (status(2, 'ft_finalpack_end', active='FINALPACK'), 'BLOCKED', None),
                (status(2, 'ft_dut', 'RUNNING', dut='0'), 'BLOCKED', None),
                (b'ERR UNSUPPORTED\r\n', 'BLOCKED', None)):
            with self.subTest(initial=initial):
                ask = mock.Mock(return_value=confirm)
                s = self.station([initial], ask=ask)
                with self.assertRaises(StationError) as caught:
                    s.key_dut_enable()
                self.assertEqual(caught.exception.outcome, expected)
                self.assertEqual(s.port.writes, [b'GET_STATUS\r\n'])
                if confirm is None:
                    ask.assert_not_called()

    def test_key_dut_enable_unknown_never_retries_or_adds_cleanup(self):
        prefix = self.enable_replies()[:3]
        for replies, actions in (
                (prefix + [b''], 2),
                (prefix + [b'OK ID=1;STATE=DONE\r\n'], 2),
                (prefix + [b'OK ID=2;STATE=DONE\r\n', status(1, 'ft_dut')], 2),
                (prefix + [b'OK ID=2;STATE=DONE\r\n', status(2, 'ft_led_cycle', active='OLED')], 2),
                (self.enable_replies()[:5] + [b''], 3),
                (self.enable_replies()[:-1] + [status(4, 'ft_dut', dut='0')], 3),
                (self.enable_replies() + [b''], 3),
                (self.enable_replies() + [b'ERR STATE\r\n'], 3)):
            with self.subTest(replies=replies):
                s = self.station(replies, ask=lambda _: 'y')
                with self.assertRaises(StationError) as caught:
                    s.key_dut_enable()
                self.assertEqual(caught.exception.outcome, 'UNKNOWN')
                self.assertEqual(len([w for w in s.port.writes if w.startswith(b'*APP#custom#')]), actions)
                self.assertFalse(s.port.replies)
        s = self.station(prefix + [b'OK ID=2;STATE=RUNNING\r\n',
                         status(2, 'ft_key_dut_enable', 'RUNNING')],
                         ask=lambda _: 'y', clock=mock.Mock(side_effect=[0, 0, 61]))
        with self.assertRaises(StationError) as caught:
            s.key_dut_enable()
        self.assertEqual(caught.exception.outcome, 'UNKNOWN')
        self.assertEqual([w for w in s.port.writes if w.startswith(b'*APP#custom#')],
                         [b'*APP#custom#ft_dut#1#\r\n', b'*APP#custom#ft_key_dut_enable#\r\n'])

    def test_key_dut_enable_rejection_or_vm_failure_stops(self):
        prefix = self.enable_replies()[:3]
        for replies, outcome in (([b'ERR UNSUPPORTED\r\n'], 'BLOCKED'),
                ([b'OK ID=2;STATE=RUNNING\r\n',
                  status(2, 'ft_key_dut_enable', 'FAILED').replace(b'LAST_ERR=NONE', b'LAST_ERR=IO')], 'FAIL')):
            s = self.station(prefix + replies, ask=lambda _: 'y')
            with self.assertRaises(StationError) as caught:
                s.key_dut_enable()
            self.assertEqual(caught.exception.outcome, outcome)
            self.assertEqual([w for w in s.port.writes if w.startswith(b'*APP#custom#')],
                             [b'*APP#custom#ft_dut#1#\r\n', b'*APP#custom#ft_key_dut_enable#\r\n'])


def check_station():
    with contextlib.redirect_stdout(io.StringIO()):
        result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(StationTests))
    if not result.wasSuccessful():
        raise RuntimeError('factory station tests failed')
    print('PASS: CDC terminal station: gating, unknown results, timeout, full identity, control sequence')


if __name__ == '__main__':
    check_station()
