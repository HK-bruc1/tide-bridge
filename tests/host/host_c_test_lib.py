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
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stderr)
    ir = re.sub(r' "target-(?:cpu|features)"="[^"]*"', '', ir_path.read_text(encoding='utf-8'))
    # JL clang uses untyped ABI attributes; newer LLVM requires a pointee type.
    ir = re.sub(r'(%[\w.]+)\* (noalias )?(sret|byval)(?!\()',
                lambda m: f'{m[1]}* {m[2] or ""}{m[3]}({m[1]})', ir)
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
        if result:
            # Temporary harness directories disappear on failure; retain the C input.
            failure = ROOT / 'cache/host-test-failures' / path.parent.name / path.name
            failure.parent.mkdir(parents=True, exist_ok=True)
            failure.write_bytes(path.read_bytes())
            raise AssertionError(f'{function} failed at generated C line {result}: {failure}')


def function(source, name):
    # Mask comments/literals without changing offsets, and skip declarations.
    masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: re.sub(r'[^\n]', ' ', m[0]), source, flags=re.S)
    signature = re.escape(name if '(' in name else name + '(')
    if re.fullmatch(r'\w+\(?', name):
        signature = r'(?:\w+[ \t*]+)+' + signature
    match = re.search(r'^[ \t]*' + signature + r'[^;{}]*\{', masked, re.M)
    if not match:
        raise ValueError(f'C function definition not found: {name}')
    start, brace = match.start(), match.end() - 1
    depth = 1
    end = brace + 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return source[start:end] + '\n'
