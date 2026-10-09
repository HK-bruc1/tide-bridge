@echo off & setlocal EnableDelayedExpansion & title T2620 CDC Test & chcp 65001 >nul & (py -3 -c "import sys" >nul 2>nul && set "PY=py -3") || (python -c "import sys" >nul 2>nul && set "PY=python") || (echo [X] 未找到 Python 3，请先安装 Python 并勾选 Add Python to PATH。 & echo. & pause & exit /b 1) & !PY! -X utf8 -x "%~f0" %* & echo. & pause & exit /b
"""T2620 CDC 产测工具（单文件版）。

本文件包含完整产测脚本，普通产测和包装发给工站电脑只需要这一个文件。
开发文件测速另需同目录下的 tools/cdc_file_transfer.py，复用其二进制接收器。
安装 Python 3（勾选 Add Python to PATH）
后双击即可；首次运行会自动联网安装 pyserial 与 imageio-ffmpeg。报告保存
在本文件同目录的 output/factory-cdc-reports/ 下。

命令行方式（可选）：
    python -X utf8 -x CDC-test.bat --list-ports
    python -X utf8 -x CDC-test.bat --port COM6 --mode controls
支持 --mode foundation/controls/packaging/full/file-bench/file-cancel/file-download、--expect-addr、--expect-fw、
--key-timeout、--report。tests/board/ 仅保留 TEST=1 的 factory_cdc_bytes.py。
"""
import argparse
import importlib.util
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import queue
import re
import struct
import subprocess
import sys
import threading
import time
import wave
import zlib


# ---------- CDC 串口协议（对应 tests/board/factory_cdc_protocol.py）----------


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


# ---------- 终端测试流程（对应 tests/board/factory_cdc_flow.py）----------


class StationError(Exception):
    def __init__(self, outcome, message):
        super().__init__(message)
        self.outcome = outcome


def fields(reply, names):
    if not reply.startswith('OK '):
        if reply in ('ERR UNSUPPORTED', 'ERR CMD'):
            raise StationError('BLOCKED', '固件尚不支持所需接口')
        raise StationError('FAIL', '设备拒绝请求（原始响应不写入报告）')
    items = reply[3:].split(';')
    if any(item.count('=') != 1 for item in items):
        raise StationError('UNKNOWN', '响应字段格式非法')
    result = dict(item.split('=', 1) for item in items)
    if len(items) != len(names) or set(result) != set(names):
        raise StationError('UNKNOWN', '响应字段不符合协议')
    return result


def number(value, nonzero=False):
    if not re.fullmatch(r'0|[1-9][0-9]{0,9}', value):
        raise StationError('UNKNOWN', '动作编号格式非法')
    result = int(value)
    if result > 0xffffffff or (nonzero and result == 0):
        raise StationError('UNKNOWN', '动作编号越界')
    return result


class Station:
    def __init__(self, port, report, ask=input, clock=time.monotonic, sleep=time.sleep):
        self.port, self.report, self.ask = port, report, ask
        self.clock, self.sleep = clock, sleep
        self.last_id = None
        self.audio = None
        self.audio_dir = None
        self.ffmpeg = None

    def note(self, step, outcome='PASS', visible=True, **details):
        self.report['steps'].append(dict(step=step, outcome=outcome, **details))
        if visible:
            label = {'PASS': '通过', 'FAIL': '失败'}.get(outcome, outcome)
            print(f'  [{label}] {step}', flush=True)

    def query(self, command):
        # Record the request here; validated identity fields are stored by foundation.
        self.report['last_request'] = command
        try:
            reply = self.audio.request(command) if self.audio else request(self.port, command)
            if reply.startswith('ERR '):
                self.report['device_error'] = dict(command=command, response=reply)
            return reply
        except (OSError, RuntimeError) as error:
            raise StationError('UNKNOWN', '通信中断/超时，结果未知；不重发，请检查设备并重建会话') from error

    def foundation(self, expected_addr=None, expected_fw=None):
        total = 3 if self.report.get('scope') == 'packaging' else 7 if self.report.get('scope') == 'full' else 5
        print(f'\n[1/{total}] 基础信息', flush=True)
        if self.query('PING') != 'OK PONG':
            raise StationError('FAIL', 'PING 失败，检查 CDC=1 / TEST=0 固件')
        self.note('PING', visible=False)
        try:
            info = validate_info(self.query('GET_INFO'))
        except RuntimeError as error:
            raise StationError('FAIL', str(error)) from error
        provisioned = info['AUTH'] != '0' * 24
        self.report['device'] = info
        print(f"  设备名称: {info['NAME']}\n"
              f"  BLE 地址: {info['ADDR']}\n"
              f"  固件版本: {info['FW']}\n"
              f"  协议版本: {info['PROTO']}\n"
              f"  AUTH: {info['AUTH']}\n"
              f"  烧录状态: {'已烧录' if provisioned else '未烧录'}", flush=True)
        if not provisioned:
            raise StationError('FAIL', 'AUTH 未烧录')
        if expected_addr and info['ADDR'] != expected_addr:
            raise StationError('FAIL', 'BLE 地址与预期不一致')
        if expected_fw and info['FW'] != expected_fw:
            raise StationError('FAIL', '固件版本与预期不一致')
        self.note('GET_INFO / 身份格式', visible=False, address_compared=bool(expected_addr),
                  firmware_compared=bool(expected_fw))

    def status(self):
        result = fields(self.query('GET_STATUS'),
                        ('DUT', 'ACTIVE', 'LAST_ID', 'LAST_CMD', 'LAST_STATE', 'LAST_ERR'))
        number(result['LAST_ID'])
        if (result['DUT'] not in ('0', '1') or
                result['ACTIVE'] not in ('NONE', 'OLED', 'MOTOR', 'KEY', 'SPEAKER',
                                         'REC', 'REC_CALL', 'MIC', 'WIFI', 'FORMAT', 'FINALPACK') or
                result['LAST_STATE'] not in ('NONE', 'RUNNING', 'DONE', 'FAILED') or
                result['LAST_ERR'] not in ('NONE', 'STATE', 'BUSY', 'IO', 'UNSUPPORTED')):
            raise StationError('UNKNOWN', '状态值非法')
        return result

    def action(self, name, value, active, dut, timeout=60):
        parameter = '' if name == 'ft_key_layout' and value == 1 else f'{value}#'
        command = f'*APP#custom#{name}#{parameter}'
        reply = fields(self.query(command), ('ID', 'STATE'))
        action_id = number(reply['ID'], nonzero=True)
        if self.last_id is not None and action_id <= self.last_id:
            raise StationError('UNKNOWN', '编号未递增，可能发生复位或收到旧响应')
        self.last_id = action_id
        if reply['STATE'] not in ('DONE', 'RUNNING'):
            raise StationError('UNKNOWN', '接受状态非法')
        deadline = self.clock() + timeout
        while True:
            result = self.status()
            if number(result['LAST_ID']) != action_id or result['LAST_CMD'] != name:
                raise StationError('UNKNOWN', 'LAST 被覆盖，无法确认本次动作结果')
            if result['LAST_STATE'] == 'FAILED':
                raise StationError('FAIL', '动作执行失败：' + result['LAST_ERR'])
            if result['LAST_STATE'] == 'DONE':
                if result['LAST_ERR'] != 'NONE' or result['ACTIVE'] != active or result['DUT'] != dut:
                    raise StationError('UNKNOWN', '动作完成状态与预期不一致')
                self.note(f'{name} {value}', visible=False, id=action_id)
                return action_id
            if result['LAST_STATE'] != 'RUNNING':
                raise StationError('UNKNOWN', '动作终态缺失')
            if self.clock() >= deadline:
                raise StationError('UNKNOWN', '动作等待超时；动作未被取消，不自动重发或退出')
            self.sleep(0.5)

    def observe(self, prompt):
        while True:
            answer = self.ask(prompt + ' [y=通过 / n=失败]：').strip().lower()
            if answer in ('y', 'n'):
                return answer == 'y'
            print('请输入 y 或 n，再按回车。')

    def show_keys(self, seen):
        line = '  ' + '   '.join(f'{key}: {bit}' for key, bit in zip('ABCDE', seen))
        line += f"   已完成 {seen.count('1')}/5"
        print(('\r' if sys.stdout.isatty() else '') + line,
              end='' if sys.stdout.isatty() else '\n', flush=True)

    def controls(self, key_timeout=30):
        current = self.status()  # Old firmware stops here, before any DUT command.
        if current['DUT'] != '0' or current['ACTIVE'] != 'NONE' or current['LAST_STATE'] in ('RUNNING', 'FAILED'):
            raise StationError('BLOCKED', '设备不是空闲正常模式，请先检查其他工站或故障状态')
        self.last_id = number(current['LAST_ID'])
        answer = self.ask('\n请核对以上信息。回车开始灯、喇叭和五键测试，输入 q 退出：').strip().lower()
        while answer not in ('', 'q'):
            answer = self.ask('按回车开始，或输入 q 退出：').strip().lower()
        if answer == 'q':
            raise StationError('BLOCKED', '操作员未开始测试')
        self.action('ft_dut', 1, 'NONE', '1')
        for stage, command, active, prompt in (
                ('[2/5] 灯光测试', 'ft_led_cycle', 'OLED', '观察 LED：绿灯持续常亮（确认后发送停止命令），颜色、亮度是否正常'),
                ('[3/5] 喇叭测试', 'ft_speaker', 'SPEAKER', '试听喇叭：是否有声且无明显杂音')):
            print('\n' + stage.replace('/5]', '/7]' if self.audio else '/5]'), flush=True)
            self.action(command, 1, active, '1')
            passed = self.observe(prompt)
            self.action(command, 0, 'NONE', '1')
            self.note('灯光测试' if command == 'ft_led_cycle' else '喇叭测试',
                      'PASS' if passed else 'FAIL', judgement='operator')
            if not passed:
                self.action('ft_dut', 0, 'NONE', '0')
                raise StationError('FAIL', '人工测试未通过，已停止测试并退出 DUT')
        print('\n[4/7] 五键测试' if self.audio else '\n[4/5] 五键测试', flush=True)
        test_id = self.action('ft_key_layout', 1, 'KEY', '1')
        print(f'请在 {key_timeout} 秒内逐个按下 A～E 五个键，顺序不限。0=未检测，1=已检测。', flush=True)
        deadline = self.clock() + key_timeout
        seen, layout = '00000', None
        self.show_keys(seen)
        try:
            while True:
                result = fields(self.query('GET_KEY_STATUS'), ('TEST_ID', 'LAYOUT', 'SEEN', 'ACTIVE'))
                if (number(result['TEST_ID']) != test_id or result['ACTIVE'] != '1' or
                        not re.fullmatch('[01]{5}', result['SEEN']) or
                        not re.fullmatch('[A-E]+(?:[,&][A-E]+)*', result['LAYOUT']) or
                        sorted(re.findall('[A-E]', result['LAYOUT'])) != list('ABCDE')):
                    raise StationError('UNKNOWN', '按键实例或状态不匹配')
                if layout is not None and layout != result['LAYOUT']:
                    raise StationError('UNKNOWN', '按键布局发生变化')
                if any(a == '1' and b != '1' for a, b in zip(seen, result['SEEN'])):
                    raise StationError('UNKNOWN', '按键累计状态回退')
                if result['SEEN'] != seen:
                    self.show_keys(result['SEEN'])
                layout, seen = result['LAYOUT'], result['SEEN']
                if seen == '11111' or self.clock() >= deadline:
                    break
                self.sleep(0.5)
        finally:
            if sys.stdout.isatty():
                print(flush=True)
        self.action('ft_key', 0, 'NONE', '1')
        self.note('五键测试', 'PASS' if seen == '11111' else 'FAIL', seen=seen, layout=layout)
        if self.audio and seen == '11111':
            run(self)
        else:
            print('\n[5/5] 结束测试', flush=True)
            self.action('ft_dut', 0, 'NONE', '0')
        if seen != '11111':
            raise StationError('FAIL', '未检测到按键：' + '、'.join(k for k, bit in zip('ABCDE', seen) if bit == '0') + '；已停止测试并退出产测')
        if self.query('PING') != 'OK PONG':
            raise StationError('UNKNOWN', '退出 DUT 后通信检查失败')
        self.note('已退出产测，通信正常')

    def packaging(self):
        current = self.status()
        if current['DUT'] != '0' or current['ACTIVE'] != 'NONE' or current['LAST_STATE'] in ('RUNNING', 'FAILED'):
            raise StationError('BLOCKED', '设备不是空闲正常模式，请先检查其他工站或故障状态')
        self.last_id = number(current['LAST_ID'])
        print('\n[2/3] 包装结束\n将清空存储中的录音等文件、恢复默认参数、解绑，并禁用实体按键进入产测。')
        if not self.observe('已核对设备信息，确认执行包装结束'):
            raise StationError('BLOCKED', '操作员未开始包装测试')
        self.action('ft_dut', 1, 'NONE', '1')
        print('正在执行包装结束，请等待设备完成……', flush=True)
        self.action('ft_finalpack_end', 1, 'FINALPACK', '1')
        self.note('包装结束：设备已确认存储清理和参数提交完成')
        print('\n[3/3] 包装灯效', flush=True)
        passed = self.observe('LED 是否持续红灯常亮')
        self.note('包装红灯', 'PASS' if passed else 'FAIL', judgement='operator')
        if not passed:
            print('包装已提交，请拨码 OFF 后检查灯效。', flush=True)
            raise StationError('FAIL', '包装提交已完成，但红灯人工检查未通过')
        if self.query('PING') != 'OK PONG':
            raise StationError('UNKNOWN', '包装完成后通信检查失败')
        self.note('包装后通信正常；拨码 OFF 待人工执行')
        print('设备保持包装结束状态，请拨码 OFF 关机；本轮不自动退出 DUT。', flush=True)


def file_transfer_test(args):
    module_path = Path(__file__).resolve().parent / 'tools/cdc_file_transfer.py'
    if not module_path.is_file():
        print('缺少 tools/cdc_file_transfer.py，请保留仓库目录结构；原普通产测和包装不依赖此文件。')
        return 2
    spec = importlib.util.spec_from_file_location('cdc_file_transfer', module_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    path = args.report or Path(__file__).resolve().parent / 'output/cdc-file-reports' / (
        datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + args.mode + '.json')
    print('需要已启用 TCFG_T2620_CDC_FILE_ENABLE=1 的固件；保持正常 ON，不进入 DUT。')
    if args.mode == 'file-download':
        print('\nCDC 真实录音下载；正常开机连接 CDC 即可，不需要 BLE 登录或进入产测。')
        print('自动读取设备录音列表，输入序号即可下载；接收 RAW 内容并校验长度、CRC。')
        print('小文件仅验证功能；CRC 不替代独立内容对照、音频解码和异常验收。')
        output = args.file_output or path.with_suffix('.raw')
        arguments = ['--port', args.port, '--report', str(path), 'download',
                     '--output', str(output), '--repeat', str(args.bench_repeat)]
        if args.file_sn is not None:
            arguments += ['--sn', str(args.file_sn), '--name', args.file_name]
        if args.file_reference:
            arguments += ['--reference', str(args.file_reference)]
        if args.file_cancel_after is not None:
            arguments += ['--cancel-after', str(args.file_cancel_after)]
    else:
        print('\nCDC 文件传输链路验证：10 MB 内存模式，校验数据和 CRC，不读取真实录音。')
        print('开发专用：固件需开启 TCFG_T2620_CDC_FILE_BENCH_ENABLE；默认关闭时返回 file#error#1。')
        print('这是链路实验，不代表真实录音文件业务已经达到 600 KB/s。')
        arguments = ['--port', args.port, '--report', str(path), 'bench', '--size', '10000000',
                     '--block', str(args.bench_block), '--repeat', str(args.bench_repeat),
                     '--receive-mode', args.bench_receive_mode]
        if args.mode == 'file-cancel':
            arguments += ['--cancel-after', '1048576']
    result = module.main(arguments)
    if result == 3:
        print('已退出文件选择，未下载。')
        return 0
    message = ('取消实验完成，终止帧已验证' if args.mode == 'file-cancel' else
               '本轮所有内存源测速达到 600 KB/s（不代表真实文件验收通过）') if result == 0 else (
               '数据传输完整，但至少一轮未达到 600 KB/s' if result == 2 else
               '实验未完成，请检查开发固件、CDC 端口及报告；不会自动重试')
    if args.mode == 'file-download' and result in (0, 2):
        message = ('下载取消已确认' if args.file_cancel_after is not None else
                   '下载完整性校验通过；单轮性能见报告，不等同完整业务验收通过')
    print(f'\n{message}\n报告路径：{path.resolve()}（仅成功创建时写入）')
    return 0 if result == 0 else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description='T2620 CDC 终端测试（不等同整机产测验收）')
    parser.add_argument('--list-ports', action='store_true', help='列举串口，不打开设备')
    parser.add_argument('--port', help='例如 COM6')
    parser.add_argument('--mode', choices=('foundation', 'controls', 'packaging', 'full', 'file-bench', 'file-cancel', 'file-download'), default='full',
                        help='foundation=基础；controls=控制；packaging=包装；full=完整普通测试；file-bench=开发链路测速；file-cancel=开发取消测试')
    parser.add_argument('--bench-block', type=int, choices=(1024, 2048, 4096, 8192), default=4096)
    parser.add_argument('--bench-repeat', type=int, default=1, help='开发测速轮数，1~100')
    parser.add_argument('--bench-receive-mode', choices=('inline', 'deferred'), default='inline',
                        help='inline=边收边校验；deferred=先接收后完整校验（不支持取消）')
    parser.add_argument('--file-sn', type=int)
    parser.add_argument('--file-name')
    parser.add_argument('--file-output', type=Path)
    parser.add_argument('--file-reference', type=Path, help='独立取得的 RAW，不含 BLE 报文封装')
    parser.add_argument('--file-cancel-after', type=int)
    parser.add_argument('--expect-addr', type=str.upper, help='烧录记录的 12 位 BLE 地址')
    parser.add_argument('--expect-fw', help='预期产品版本')
    parser.add_argument('--key-timeout', type=int, default=30, help='五键测试等待秒数，1~300')
    parser.add_argument('--report', type=Path, help='JSON 报告路径（不覆盖已有文件）')
    args = parser.parse_args(argv)
    if args.expect_addr and not re.fullmatch('[0-9A-F]{12}', args.expect_addr):
        parser.error('--expect-addr 必须为 12 位 HEX')
    if not 1 <= args.key_timeout <= 300:
        parser.error('--key-timeout 必须为 1~300')
    if not args.list_ports and not args.port:
        parser.error('请指定 --port COMx 或 --list-ports')
    if not 1 <= args.bench_repeat <= 100:
        parser.error('--bench-repeat 必须为 1~100')
    if args.mode == 'file-download' and not args.list_ports:
        if args.file_sn is not None or args.file_name is not None:
            if (args.file_sn is None or not 1 <= args.file_sn <= 0x7ffffffe or
                    not args.file_name or not re.fullmatch(r'[0-9a-fA-F]{3,6}\.raw', args.file_name)):
                parser.error('--file-sn 和 --file-name 必须同时提供；省略则从设备列表选择')
        if args.bench_receive_mode != 'inline':
            parser.error('真实下载只支持在线校验')
        if args.file_cancel_after is not None and not 0 <= args.file_cancel_after < 16777216:
            parser.error('--file-cancel-after 必须为 0~16777215')
    if args.mode in ('file-bench', 'file-cancel', 'file-download') and not args.list_ports:
        if args.expect_addr or args.expect_fw:
            parser.error('开发测速不核验身份，请先单独运行 --mode foundation')
        return file_transfer_test(args)
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        print('请先安装依赖: python -m pip install pyserial', file=sys.stderr)
        return 2
    if args.list_ports:
        for port in list_ports.comports():
            print(f'{port.device}: {port.description}')
        return 0
    report = dict(schema=1, started=datetime.now(timezone.utc).isoformat(),
                  port=args.port, scope=args.mode, overall_factory_pass=False, steps=[])
    path = args.report or Path('output/factory-cdc-reports') / (datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.json')
    # Reserve report before changing device state; fail without touching the port if unwritable.
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        output = path.open('x', encoding='utf-8')
    except OSError:
        print('无法创建报告（文件已存在或目录不可写），未打开串口。', file=sys.stderr)
        return 2
    try:
        with serial.Serial(args.port, 115200, timeout=2, write_timeout=2) as port:
            station = Station(port, report)
            try:
                station.foundation(args.expect_addr, args.expect_fw)
                if args.mode == 'full':
                    prepare(station, path.parent / (path.stem + '-audio'))
                if args.mode == 'packaging':
                    station.packaging()
                elif args.mode != 'foundation':
                    station.controls(args.key_timeout)
                message = ('包装提交及红灯检查通过，请拨码 OFF；不代表普通测试或完整产测通过'
                           if args.mode == 'packaging' else '普通测试通过，含实时流与文件下载回放；包装需单独执行'
                           if args.mode == 'full' else '本轮控制测试通过；未覆盖录音、文件下载及最终包装')
                report.update(outcome='PASS', message=message)
            finally:
                if station.audio:
                    station.audio.close()
                    partial = station.audio_dir / 'stream.raw'
                    if not partial.exists():
                        partial.write_bytes(station.audio.frames)
                    report['audio']['stream'].update(frames=station.audio.frame_count,
                        bytes=len(station.audio.frames), raw=str(partial.resolve()),
                        receiver_error=station.audio.error)
    except StationError as error:
        report.update(outcome=error.outcome, message=str(error))
    except RuntimeError as error:
        report.update(outcome='FAIL', message=str(error))
    except (KeyboardInterrupt, EOFError):
        report.update(outcome='UNKNOWN', message='操作中断；未发送自动清理命令，请检查设备状态')
    except (OSError, serial.SerialException):
        report.update(outcome='UNKNOWN', message='串口打开/通信/关闭失败，请检查设备状态')
    finally:
        report['finished'] = datetime.now(timezone.utc).isoformat()
        with output:
            json.dump(report, output, ensure_ascii=False, indent=2)
            output.write('\n')
    label = {'PASS': '通过', 'FAIL': '失败', 'BLOCKED': '未开始或未完成', 'UNKNOWN': '结果未知'}[report['outcome']]
    print(f"\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n测试结果：{label}\n{report['message']}\n报告: {path.resolve()}")
    if report['outcome'] == 'UNKNOWN':
        print('不要根据超时重发动作。检查设备；必要时拨码 OFF 安全关机，再恢复连接。')
    return {'PASS': 0, 'FAIL': 1, 'BLOCKED': 2, 'UNKNOWN': 3}[report['outcome']]


# ---------- 音频链路（对应 tests/board/factory_cdc_audio.py）----------


class AudioLink:
    def __init__(self, port):
        self.port = port
        self.replies = queue.Queue(maxsize=8)
        self.files = queue.Queue(maxsize=1)
        self.error = None
        self.stop = threading.Event()
        self.end = threading.Event()
        self.record_id = None
        self.frames = bytearray()
        self.frame_count = 0
        self.end_info = None
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def check(self):
        if self.error:
            raise RuntimeError(self.error)

    def accept(self, text):
        if not text.startswith('@'):
            if not text.startswith(('OK ', 'ERR ')):
                raise RuntimeError('invalid control response')
            self.replies.put_nowait(text)
            return
        prefix, checksum = text.rsplit('*', 1)
        if len(checksum) != 8 or int(checksum, 16) != zlib.crc32(prefix.encode('ascii')):
            raise RuntimeError('data frame CRC mismatch')
        parts = prefix.split(',')
        if len(parts) not in (5, 6):
            raise RuntimeError('invalid data frame fields')
        numeric = parts[1:] if parts[0] == '@E' else parts[1:4]
        if any(not re.fullmatch(r'0|[1-9][0-9]{0,9}', value) or int(value) > 0xffffffff
               for value in numeric):
            raise RuntimeError('invalid numeric field')
        kind, record_id = parts[0], int(parts[1])
        if record_id <= 0 or (self.record_id is not None and record_id != self.record_id):
            raise RuntimeError('record instance mismatch')
        self.record_id = record_id
        if kind == '@A' and len(parts) == 5:
            data = bytes.fromhex(parts[4])
            if self.end.is_set() or int(parts[2]) != self.frame_count or int(parts[3]) != 40 or len(data) != 40:
                raise RuntimeError('audio sequence/length mismatch')
            if self.frame_count >= 1000:
                raise RuntimeError('audio exceeds bounded recording budget')
            self.frames.extend(data)
            self.frame_count += 1
        elif kind == '@E' and len(parts) == 6:
            if self.end.is_set():
                raise RuntimeError('duplicate stream end')
            self.end_info = dict(record_id=record_id, produced=int(parts[2]),
                                 bytes=int(parts[3]), error=int(parts[4]), crc=int(parts[5]))
            self.end.set()
        elif kind == '@F' and len(parts) == 5:
            data = bytes.fromhex(parts[4])
            if not 0 < len(data) <= 80 or len(data) != int(parts[3]):
                raise RuntimeError('file block length mismatch')
            self.files.put_nowait((int(parts[2]), data))
        else:
            raise RuntimeError('unknown data frame')

    def _read(self):
        try:
            while not self.stop.is_set():
                data = self.port.read_until(b'\r\n', size=259)
                if not data:
                    continue
                if len(data) > 258 or not data.endswith(b'\r\n'):
                    raise RuntimeError('incomplete CDC frame')
                self.accept(data[:-2].decode('ascii'))
        except Exception as error:
            if not self.stop.is_set():
                self.error = str(error)

    def request(self, command):
        self.check()
        if not self.replies.empty():
            raise RuntimeError('unexpected queued response')
        data = command.encode('ascii') + b'\r\n'
        if self.port.write(data) != len(data):
            raise RuntimeError('incomplete command write')
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            self.check()
            try:
                return self.replies.get(timeout=0.05)
            except queue.Empty:
                pass
        raise RuntimeError('command timeout; do not retry')

    def verify_stream(self):
        self.check()
        info = self.end_info
        if not self.end.is_set() or not info or info['error'] or not self.frames:
            raise RuntimeError('stream failed or end missing')
        if (info['produced'] != self.frame_count or info['bytes'] != len(self.frames) or
                info['crc'] != zlib.crc32(self.frames)):
            raise RuntimeError('stream incomplete')
        return info

    def close(self):
        self.stop.set()
        if hasattr(self.port, 'cancel_read'):
            self.port.cancel_read()
        self.thread.join(timeout=3)
        if self.thread.is_alive():
            raise RuntimeError('serial reader did not stop')


def run_decoder(arguments, timeout):
    try:
        return subprocess.run(arguments, capture_output=True, timeout=timeout,
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    except subprocess.TimeoutExpired as error:
        raise RuntimeError('音频解码器响应超时') from error


def decoder_path():
    if os.name != 'nt':
        raise RuntimeError('双击播放测试需要 Windows')
    import imageio_ffmpeg
    path = imageio_ffmpeg.get_ffmpeg_exe()
    result = run_decoder([path, '-version'], 10)
    if result.returncode:
        raise RuntimeError('音频解码器不可用')
    return path


def ogg_crc(data):
    crc = 0
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04c11db7 if crc & 0x80000000 else 0)) & 0xffffffff
    return crc


def write_opus(raw, path):
    if not raw or len(raw) % 40:
        raise RuntimeError('invalid mono Opus frame stream')
    packets = [b'OpusHead' + struct.pack('<BBHIhB', 1, 1, 0, 16000, 0, 0),
               b'OpusTags' + struct.pack('<I', 3) + b'RDX' + struct.pack('<I', 0)]
    packets.extend(raw[i:i+40] for i in range(0, len(raw), 40))
    with Path(path).open('xb') as output:
        for index, packet in enumerate(packets):
            flags = 2 if index == 0 else 4 if index == len(packets)-1 else 0
            granule = max(0, index - 1) * 960  # Opus Ogg granules always use 48 kHz.
            page = bytearray(b'OggS' + struct.pack('<BBQIIIB', 0, flags, granule, 1, index, 0, 1))
            page.append(len(packet))
            page.extend(packet)
            struct.pack_into('<I', page, 22, ogg_crc(page))
            output.write(page)


def decode(raw, stem, ffmpeg):
    opus = Path(str(stem) + '.opus')
    wav = Path(str(stem) + '.wav')
    write_opus(raw, opus)
    result = run_decoder([ffmpeg, '-v', 'error', '-xerror', '-nostdin', '-n', '-i', str(opus),
                          '-ac', '1', '-ar', '16000', '-c:a', 'pcm_s16le', str(wav)], 30)
    if result.returncode or not wav.exists():
        raise RuntimeError('Opus 解码失败，保留原始音频供检查')
    with wave.open(str(wav), 'rb') as audio:
        if audio.getnchannels() != 1 or audio.getframerate() != 16000 or not audio.getnframes():
            raise RuntimeError('decoded audio format invalid')
    return wav


def playback(path, ask):
    import winsound
    while True:
        winsound.PlaySound(str(path), winsound.SND_FILENAME | winsound.SND_ASYNC)
        try:
            answer = ask('声音是否清晰正常？[y=通过 / n=失败 / r=重听]：').strip().lower()
        finally:
            winsound.PlaySound(None, 0)
        if answer in ('y', 'n'):
            return answer == 'y'


def wait_recording(link, maximum=15):
    import msvcrt
    started = time.monotonic()
    print('请对着麦克风说话，建议录制 5～10 秒；按回车停止（设备最多录制 15 秒）。', flush=True)
    shown = -1
    while not link.end.is_set():
        link.check()
        elapsed = int(time.monotonic() - started)
        if elapsed != shown:
            print(f'\r已录制 {elapsed:2d} 秒，收到 {link.frame_count} 帧', end='', flush=True)
            shown = elapsed
        if msvcrt.kbhit():
            key = msvcrt.getwch()
            if key == '\r':
                print()
                return True
            if key == '\x03':
                raise KeyboardInterrupt
        if elapsed > maximum + 10:
            raise RuntimeError('device recording limit did not complete')
        time.sleep(0.05)
    print('\n设备已停止录音。')
    return False


def prepare(station, directory):
    try:
        station.ffmpeg = decoder_path()
    except (ImportError, OSError, RuntimeError) as error:
        raise StationError('BLOCKED', '录音播放环境未就绪：' + str(error)) from error
    caps = fields(station.query('GET_AUDIO_CAPS'), ('AUDIO', 'CODEC', 'RATE', 'CHANNELS', 'FRAME', 'MS', 'MAX_MS'))
    if caps != dict(AUDIO='1', CODEC='OPUS', RATE='16000', CHANNELS='1', FRAME='40', MS='20', MAX_MS='15000'):
        raise StationError('BLOCKED', '固件音频能力与脚本不匹配')
    directory.mkdir(parents=True, exist_ok=False)
    station.audio_dir = directory
    station.report['audio'] = dict(directory=str(directory.resolve()), stream={'outcome': 'UNKNOWN'}, download={'outcome': 'BLOCKED'})
    if station.query('AUDIO_OPEN 1') != 'OK AUDIO=1':
        raise StationError('BLOCKED', '设备拒绝音频协议协商')
    station.audio = AudioLink(station.port)
    if station.query(f'SET_TIME {int(time.time())}') != 'OK TIME=1':
        raise StationError('FAIL', '设备 UTC 校时失败')


def run(station):
    link, directory = station.audio, station.audio_dir
    print('\n[5/7] 录音测试', flush=True)
    station.ask('回车开始录音：')
    record_id = station.action('ft_rec_chat', 1, 'REC', '1')
    stopped_id = None
    if wait_recording(link):
        current = station.status()
        if current['ACTIVE'] == 'REC':
            stopped_id = station.action('ft_rec_chat', 0, 'NONE', '1')
    deadline = time.monotonic() + 15
    while not link.end.is_set():
        link.check()
        if time.monotonic() >= deadline:
            raise StationError('UNKNOWN', '未收到录音流结束信息')
        time.sleep(0.05)
    result = fields(station.query('GET_RECORD_RESULT'),
                    ('REC', 'STOP', 'STATE', 'SAVED', 'SN', 'NAME', 'FRAMES', 'ERROR', 'REASON'))
    station.report['audio']['record'] = result
    if (number(result['REC']) != record_id or link.record_id != record_id or
            (stopped_id is not None and number(result['STOP']) != stopped_id)):
        raise StationError('UNKNOWN', '录音动作与结果不匹配')
    raw = bytes(link.frames)
    (directory / 'stream.raw').write_bytes(raw)
    stream = station.report['audio']['stream']
    stream.update(frames=link.frame_count, bytes=len(raw), end=link.end_info,
                  raw=str((directory / 'stream.raw').resolve()))
    stream_passed = False
    try:
        link.verify_stream()
        wav = decode(raw, directory / 'stream', station.ffmpeg)
        stream.update(integrity='PASS', decode='PASS', wav=str(wav.resolve()))
        print('播放电脑实时接收的录音：', flush=True)
        stream_passed = playback(wav, station.ask)
        stream['listening'] = 'PASS' if stream_passed else 'FAIL'
    except RuntimeError as error:
        stream['error'] = str(error)
    stream['outcome'] = 'PASS' if stream_passed else 'FAIL'
    station.note('实时录音流回放', stream['outcome'])
    if result['SAVED'] != '1' or result['STATE'] not in ('DONE', 'FAILED'):
        raise StationError('FAIL', '本次录音未成功保存，不能开始文件下载')
    station.action('ft_dut', 0, 'NONE', '0')
    print('\n[6/7] 设备录音文件下载', flush=True)
    station.report['audio']['download']['outcome'] = 'UNKNOWN'
    opened = station.query(f'FILE_OPEN {record_id}')
    if opened != 'OK STATE=RUNNING':
        station.report['audio']['download'].update(outcome='FAIL', error=opened)
        raise StationError('FAIL', '设备拒绝打开本次录音文件：' + opened)
    deadline = time.monotonic() + 10
    while True:
        meta = fields(station.query('GET_FILE_STATUS'), ('REC', 'STATE', 'SIZE', 'CRC', 'OFFSET'))
        if number(meta['REC']) != record_id:
            raise StationError('UNKNOWN', '下载实例不匹配')
        if meta['STATE'] == 'READY':
            break
        if meta['STATE'] != 'OPENING' or time.monotonic() >= deadline:
            raise StationError('FAIL', '设备文件校验或打开失败')
        time.sleep(0.1)
    size, crc = number(meta['SIZE'], True), number(meta['CRC'])
    if size > 40000 or size % 40 or meta['OFFSET'] != '0':
        raise StationError('UNKNOWN', '设备文件元数据非法')
    download = station.report['audio']['download']
    download.update(meta=meta, sn=result['SN'], name=result['NAME'])
    data = bytearray()
    target = directory / 'download.raw'
    with target.open('xb') as output:
        while len(data) < size:
            if station.query(f'FILE_READ {record_id} {len(data)}') != 'OK STATE=RUNNING':
                raise StationError('FAIL', '设备拒绝读取文件块')
            try:
                offset, block = link.files.get(timeout=3)
            except queue.Empty as error:
                raise StationError('UNKNOWN', '下载数据等待超时') from error
            link.check()
            if offset != len(data) or len(data) + len(block) > size:
                raise StationError('FAIL', '下载偏移或长度错误')
            data.extend(block)
            output.write(block)
            print(f'\r下载 {len(data)}/{size} 字节', end='', flush=True)
    print()
    if station.query(f'FILE_CLOSE {record_id}') != 'OK STATE=RUNNING':
        raise StationError('UNKNOWN', '文件关闭未确认')
    deadline = time.monotonic() + 5
    while True:
        closed = fields(station.query('GET_FILE_STATUS'), ('REC', 'STATE', 'SIZE', 'CRC', 'OFFSET'))
        if closed['REC'] != str(record_id):
            raise StationError('UNKNOWN', '文件关闭实例不匹配')
        if closed['STATE'] == 'CLOSED':
            break
        if time.monotonic() >= deadline:
            raise StationError('UNKNOWN', '文件资源释放超时')
        time.sleep(0.1)
    download.update(bytes=len(data), actual_crc=zlib.crc32(data), raw=str(target.resolve()))
    if len(data) != size or zlib.crc32(data) != crc:
        download['outcome'] = 'FAIL'
        raise StationError('FAIL', '下载文件完整性校验失败')
    download['integrity'] = 'PASS'
    try:
        wav = decode(data, directory / 'download', station.ffmpeg)
    except RuntimeError as error:
        download.update(outcome='FAIL', error=str(error), decode='FAIL')
        raise
    download.update(decode='PASS', wav=str(wav.resolve()))
    print('播放从设备独立下载的录音：', flush=True)
    passed = playback(wav, station.ask)
    download['listening'] = download['outcome'] = 'PASS' if passed else 'FAIL'
    station.note('设备录音文件下载及回放', download['outcome'])
    if not stream_passed or not passed:
        raise StationError('FAIL', '两项音频测试未全部通过，证据已保留')
    print('\n[7/7] 结束测试', flush=True)


# ---------- 交互入口（对应 tests/board/factory_cdc_launcher.py）----------


def launcher(argv=None):
    root = Path(__file__).resolve().parent
    os.chdir(root)
    mode = 'full'
    extra = []
    if argv:
        # Degrades to 'full' (both dependencies) on unparsed input; argparse still validates.
        if '--mode' in argv:
            index = argv.index('--mode')
            mode = argv[index + 1] if index + 1 < len(argv) else mode
    else:
        print('T2620 CDC 产测\n请拨码 ON、插好 USB 数据线，并关闭占用串口的其他工具。\n')
        print('1. 普通测试（基本信息、灯、喇叭、五键、录音及文件下载）\n2. 包装测试（清空存储、恢复参数、解绑、禁用按键进入产测）\n3. CDC 通用文件传输（内存基准 / 真实录音接收端）')
        while True:
            choice = input('选择测试，回车选 1，q 退出：').strip().lower()
            if choice == 'q':
                return 0
            if choice in ('', '1', '2', '3'):
                mode = {'2': 'packaging', '3': 'file-bench'}.get(choice, 'full')
                break
            print('请输入 1、2 或 3。')
        if mode == 'file-bench':
            print('\n选项 1～5 为开发专用，需固件开启 TCFG_T2620_CDC_FILE_BENCH_ENABLE；默认配置请选 6/7。')
            print('\n1. 快速测速：10 MB × 1 次，4 KB 块\n2. 稳定性测速：10 MB × 10 次，4 KB 块\n3. 取消测试：收到约 1 MiB 后请求取消')
            print('4. 接收后校验对照：10 MB × 1 次，4 KB 块\n5. 接收后校验对照：10 MB × 10 次，4 KB 块')
            print('6. 真实录音下载：从设备列表选择，下载 1 次\n7. 真实录音稳定性：从设备列表选择，下载 10 次')
            while True:
                choice = input('选择实验，回车选 1，q 退出：').strip().lower()
                if choice == 'q':
                    return 0
                if choice in ('6', '7'):
                    mode = 'file-download'
                    extra = ['--bench-repeat', '10' if choice == '7' else '1']
                    break
                if choice in ('', '1', '2', '3', '4', '5'):
                    mode = 'file-cancel' if choice == '3' else 'file-bench'
                    extra = ['--bench-repeat', '10' if choice in ('2', '5') else '1',
                             '--bench-receive-mode', 'deferred' if choice in ('4', '5') else 'inline']
                    break
                print('请输入 1 至 7。')
    try:
        import serial.tools.list_ports
        if mode == 'full':
            import imageio_ffmpeg
    except ImportError:
        print('首次运行：正在安装当前模式所需依赖，需要联网……')
        packages = ['pyserial>=3.5,<4']
        if mode == 'full':
            packages.append('imageio-ffmpeg==0.6.0')
        result = subprocess.run([sys.executable, '-m', 'pip', 'install'] + packages)
        if result.returncode:
            print('依赖安装失败，请检查网络和 Python pip 后重试。')
            return 2
        # A fresh interpreter picks up newly installed packages and user site paths.
        # -x skips the batch bootstrap on line 1.
        return subprocess.call([sys.executable, '-X', 'utf8', '-x', os.path.abspath(__file__)]
                               + list(argv or ()))
    if argv:
        return main(argv)

    ports = list(serial.tools.list_ports.comports())
    print('当前串口：')
    for port in ports:
        print(f'  {port.device}  {port.description}')
    if not ports:
        print('  未发现串口，请检查数据线、驱动和 CDC 固件后重新运行。')
        return 2
    print('\n请选择 CDC 端口，不要选择调试日志/JTAG 端口。')
    while True:
        value = input('输入端口号（如 6 或 COM6，直接回车退出）：').strip().upper()
        if not value:
            return 0
        if value.isascii() and value.isdigit():
            value = 'COM' + value
        if re.fullmatch(r'COM[1-9][0-9]*', value):
            break
        print('请输入数字或 COM 加数字。')
    return main(['--port', value, '--mode', mode] + extra)


if __name__ == '__main__':
    try:
        raise SystemExit(launcher(sys.argv[1:]))
    except (KeyboardInterrupt, EOFError):
        print('\n已退出。')
        raise SystemExit(3)
