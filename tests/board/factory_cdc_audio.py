"""CDC AUDIO=1: one continuous reader, two independently verified audio paths."""
import os
from pathlib import Path
import queue
import re
import struct
import subprocess
import threading
import time
import wave
import zlib


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
    from factory_cdc_flow import StationError, fields
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
    from factory_cdc_flow import StationError, fields, number
    link, directory = station.audio, station.audio_dir
    print('\n[5/7] 录音测试', flush=True)
    station.ask('回车开始录音：')
    record_id = station.action('ft_rec', 1, 'REC', '1')
    stopped_id = None
    if wait_recording(link):
        current = station.status()
        if current['ACTIVE'] == 'REC':
            stopped_id = station.action('ft_rec', 0, 'NONE', '1')
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
