"""CDC terminal station: foundation checks and gated, interactive control tests.

Exit codes: 0 scoped checks passed, 1 failed, 2 blocked, 3 result unknown.
Packaging explicitly runs the shared finalpack operation; no automatic retries.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import sys
import time

from factory_cdc_protocol import request, validate_info


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
        reply = fields(self.query(f'DUT {name} {value}'), ('ID', 'STATE'))
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
            from factory_cdc_audio import run
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


def main(argv=None):
    parser = argparse.ArgumentParser(description='T2620 CDC 终端测试（不等同整机产测验收）')
    parser.add_argument('--list-ports', action='store_true', help='列举串口，不打开设备')
    parser.add_argument('--port', help='例如 COM6')
    parser.add_argument('--mode', choices=('foundation', 'controls', 'packaging', 'full'), default='full',
                        help='foundation=基础；controls=普通测试；packaging=包装测试；full=普通测试含录音及文件下载')
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
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        print('请先运行: python -m pip install -r tests/board/requirements.txt', file=sys.stderr)
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
                    from factory_cdc_audio import prepare
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


if __name__ == '__main__':
    raise SystemExit(main())
