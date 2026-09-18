"""Shared JL C/LLVM execution helpers for host behavioral tests.

Hardware and binary libraries remain mocked by each caller. native=True is
required for harnesses using host libc allocation and pointer-sized structures.
"""
import ctypes
import re
import subprocess
from pathlib import Path

from llvmlite import binding as llvm

ROOT = Path(__file__).resolve().parents[2]


def run_c_checks(path, functions, native=False):
    ir_path = path.with_suffix('.ll')
    target_args = ['-target', llvm.get_default_triple()] if native else ['-target', 'pi32v2', '-mcpu=r3']
    result = subprocess.run(['C:/JL/pi32/bin/clang.exe', *target_args,
                             '-O0', '-S', '-emit-llvm', str(path), '-o', str(ir_path)],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    ir = re.sub(r' "target-(?:cpu|features)"="[^"]*"', '', ir_path.read_text(encoding='utf-8'))
    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()
    target = llvm.Target.from_default_triple().create_target_machine()
    module = llvm.parse_assembly(ir)
    module.triple = llvm.get_default_triple()
    module.data_layout = str(target.target_data)
    module.verify()
    engine = llvm.create_mcjit_compiler(module, target)
    engine.finalize_object()
    for function in functions:
        result = ctypes.CFUNCTYPE(ctypes.c_int)(engine.get_function_address(function))()
        assert result == 0, f'{function} failed at generated C line {result}: {path}'


def function(source, name):
    start = source.index(name)
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'
