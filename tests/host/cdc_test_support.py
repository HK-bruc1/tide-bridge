"""Standard-library helpers for tests of the standalone CDC tool."""
from pathlib import Path
import types

ROOT = Path(__file__).resolve().parents[2]


def load_cdc_tool(name='cdc_tool'):
    # The first line is the Windows batch bootstrap, not Python.
    path = ROOT / 'CDC-test.bat'
    lines = path.read_text(encoding='utf-8').splitlines(keepends=True)
    module = types.ModuleType(name)
    module.__file__ = str(path)
    exec(compile(''.join(lines[1:]), str(path), 'exec'), module.__dict__)
    return module
