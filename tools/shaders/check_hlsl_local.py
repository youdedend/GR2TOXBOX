#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fast native Slang front-end check; DXC remains the HLSL 2021/DXIL authority.

Install: python3 -m venv /tmp/gr2-slang-venv &&
         /tmp/gr2-slang-venv/bin/pip install slangpy
SLANG_LIBRARY may name another libslang-compiler shared library. No GPU needed.
--dxc /path/to/dxc instead compiles all stages, with -HV 2021 -WX, to SM6.4.
Slang's no-codegen mode checks types, not DXIL legality or exact DXC semantics.
"""
import argparse
import ctypes as c
from concurrent.futures import ThreadPoolExecutor
import glob
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2] / 'src/graphics/gr2port_d3d12/shaders'
STAGES = {'vert': (1, 'vs'), 'hull': (2, 'hs'), 'dom': (3, 'ds'),
          'geom': (4, 'gs'), 'frag': (5, 'ps'), 'comp': (6, 'cs')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dxc')
    parser.add_argument('--require', action='store_true')
    parser.add_argument('--serial', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('files', nargs='*', type=Path)
    args = parser.parse_args()
    files = args.files or sorted(ROOT.rglob('*.hlsl'))
    start = time.monotonic()
    failed = 0
    if not args.dxc:
        candidates = ([os.environ['SLANG_LIBRARY']] if os.environ.get('SLANG_LIBRARY') else
                      glob.glob('/tmp/gr2-slang-venv/lib/python*/site-packages/slangpy/libslang-compiler.so*'))
        if not candidates:
            print('SKIP native Slang typecheck: /tmp/gr2-slang-venv absent (see --help)')
            return int(args.require)
        if len(files) > 8 and not args.serial:
            # Isolated native sessions/processes, not concurrent calls into one
            # potentially non-thread-safe compiler session. Bounded at 2 workers.
            def worker(chunk):
                return subprocess.run([sys.executable, __file__, '--serial', '--require',
                                       *map(str, chunk)], capture_output=True, text=True)
            with ThreadPoolExecutor(max_workers=2) as pool:
                results = list(pool.map(worker, [files[i::2] for i in range(2)]))
            for result in results:
                if result.returncode:
                    print(result.stdout + result.stderr)
            failed = sum(result.returncode != 0 for result in results)
            print(f'Slang SM6.4 front-end typecheck: {len(files)} stages, '
                  f'{"FAILED" if failed else "passed"} in {time.monotonic()-start:.2f}s (2 isolated workers)')
            return int(failed != 0)
        lib = c.CDLL(candidates[0])
        def api(name, result, *params):
            fn = getattr(lib, name)
            fn.restype, fn.argtypes = result, list(params)
            return fn
        ptr, i, text = c.c_void_p, c.c_int, c.c_char_p
        session = api('spCreateSession', ptr, text)(None)
        create = api('spCreateCompileRequest', ptr, ptr)
        destroy = api('spDestroyCompileRequest', None, ptr)
        flags = api('spSetCompileFlags', None, ptr, c.c_uint)
        target = api('spSetCodeGenTarget', None, ptr, i)
        profile = api('spFindProfile', c.c_uint, ptr, text)
        set_profile = api('spSetTargetProfile', None, ptr, i, c.c_uint)
        search = api('spAddSearchPath', None, ptr, text)
        unit = api('spAddTranslationUnit', i, ptr, i, text)
        source = api('spAddTranslationUnitSourceFile', None, ptr, i, text)
        entry = api('spAddEntryPoint', i, ptr, i, text, i)
        compile_ = api('spCompile', i, ptr)
        diagnostics = api('spGetDiagnosticOutput', text, ptr)
        request = create(session)
        try:
            flags(request, 1 << 4)  # SLANG_COMPILE_FLAG_NO_CODEGEN
            target(request, 5)  # HLSL target: never needs DXIL or a GPU
            set_profile(request, 0, profile(session, b'sm_6_4'))
            search(request, str(ROOT).encode())
            # Separate HLSL translation units share the compiler's builtins and
            # request setup, but never each other's main/cbuffer declarations.
            for index, path in enumerate(files):
                stage, _ = STAGES[path.name.split('.')[-2]]
                tu = unit(request, 2, f'stage_{index}'.encode())
                source(request, tu, str(path.resolve()).encode())
                entry(request, tu, b'main', stage)
            failed = int(compile_(request) < 0)
            if failed:
                print((diagnostics(request) or b'').decode(errors='replace'))
        finally:
            destroy(request)
            api('spDestroySession', None, ptr)(session)
        print(f'Slang SM6.4 front-end typecheck: {len(files)} stages, '
              f'{"FAILED" if failed else "passed"} in {time.monotonic()-start:.2f}s')
        return failed
    for path in files:
        _, prefix = STAGES[path.name.split('.')[-2]]
        result = subprocess.run([args.dxc, '-T', prefix + '_6_4', '-E', 'main',
                                 '-HV', '2021', '-WX', '-I', str(ROOT),
                                 '-Fo', os.devnull, str(path)], capture_output=True, text=True)
        if result.returncode:
            print(f'FAIL {path}\n{result.stdout}{result.stderr}')
            failed += 1
    print(f'DXC HLSL 2021 SM6.4 compile: {len(files)-failed}/{len(files)} passed '
          f'in {time.monotonic()-start:.2f}s')
    return int(failed != 0)


if __name__ == '__main__':
    sys.exit(main())
