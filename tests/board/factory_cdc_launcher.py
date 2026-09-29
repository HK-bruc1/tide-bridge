"""Interactive entry point for the root CDC-test.bat."""
from pathlib import Path
import os
import re
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    os.chdir(root)
    print('T2620 CDC 产测\n请拨码 ON、插好 USB 数据线，并关闭占用串口的其他工具。\n')
    print('1. 普通测试（基本信息、灯、喇叭、五键；暂不含录音）\n2. 包装测试（清空存储、恢复参数、解绑、禁用按键进入产测）')
    while True:
        choice = input('选择测试，回车选 1，q 退出：').strip().lower()
        if choice == 'q':
            return 0
        if choice in ('', '1', '2'):
            mode = 'packaging' if choice == '2' else 'controls'
            break
        print('请输入 1 或 2。')
    try:
        import serial.tools.list_ports
    except ImportError:
        print('首次运行：正在安装串口依赖 pyserial，需要联网……')
        result = subprocess.run([sys.executable, '-m', 'pip', 'install',
                                 '-r', str(root / 'tests/board/requirements.txt')])
        if result.returncode:
            print('依赖安装失败，请检查网络和 Python pip 后重试。')
            return 2
        # A fresh interpreter picks up newly installed packages and user site paths.
        return subprocess.call([sys.executable, '-X', 'utf8', str(Path(__file__).resolve())])

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
    from factory_cdc_flow import main as run_flow
    return run_flow(['--port', value, '--mode', mode])


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (KeyboardInterrupt, EOFError):
        print('\n已退出。')
        raise SystemExit(3)
