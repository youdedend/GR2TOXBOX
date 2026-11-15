#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile real UWP display code and extracted DXGI call site, without the SDK.

Deliberately narrow: this proves the method/argument contracts, not SDK coverage
of the entire device.cpp translation unit. Mutation tests keep both regressions
observable. Actual Windows builds and binary import checks remain mandatory.
"""
import os
import glob
import sys
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
compiler = os.environ.get('UWP_CXX') or shutil.which('clang++')
start = time.monotonic()
flags = ['-std=c++20', '-fsyntax-only', '-DGR2_XBOX_UWP=1',
         '-DWINAPI_FAMILY=WINAPI_FAMILY_APP', '-Itools/ci/uwp_stubs', '-Isrc']
# Sandboxes without clang++ may use PyPI's native libclang front end, installed
# outside the snapshot. CI invokes the ordinary clang++ driver with the same
# sources/defines. Never disguise g++ as clang++.
if not compiler:
    packages = glob.glob('/tmp/gr2-clang-venv/lib/python*/site-packages')
    if packages:
        sys.path.insert(0, packages[0])
        from clang.cindex import Index, Diagnostic
        index = Index.create()
        gcc_include = subprocess.check_output(['g++', '-print-file-name=include'], text=True).strip()
        version = subprocess.check_output(['g++', '-dumpversion'], text=True).strip().split('.')[0]
        native_flags = flags + ['-isystem', '/usr/include/c++/' + version,
            '-isystem', '/usr/include/x86_64-linux-gnu/c++/' + version,
            '-isystem', '/usr/include/x86_64-linux-gnu', '-isystem', gcc_include]
        def check(files, quiet=False):
            errors = []
            for file in files:
                unit = index.parse(file, args=native_flags)
                errors.extend(str(d) for d in unit.diagnostics if d.severity >= Diagnostic.Error)
            if errors and not quiet: print('\n'.join(errors))
            return not errors
        compiler_label = 'native libclang (PyPI)'
    else:
        compiler = shutil.which('g++')
if compiler:
    def check(files, quiet=False):
        return subprocess.run([compiler, *flags, *files], capture_output=quiet).returncode == 0
    compiler_label = Path(compiler).name
app = Path('src/graphics/gr2port_d3d12/uwp/uwp_app.cpp')
device = Path('src/graphics/gr2port_d3d12/device.cpp').read_text()
a = device.index('DXGI_MATRIX_3X2_F matrix{};')
b = device.index('\n            if (FAILED(t_hr))', a)
fragment = device[a:b]
wrapper = '''#include <windows.ui.core.h>
#include "common/logging/log.h"
#include "uwp/blackbox_log.h"
namespace Blackbox = Gr2Port::Blackbox;
const char* HresultToString(HRESULT);
constexpr unsigned width = 1920, height = 1080;
#include "graphics/gr2port_d3d12/uwp/display_mode.h"
void Check(IDXGISwapChain2* swapchain2, Gr2Port::Uwp::Display::Plan plan) {
'''
with tempfile.TemporaryDirectory(prefix='gr2-uwp-') as tmp:
    unit = Path(tmp) / 'matrix.cpp'
    unit.write_text(wrapper + fragment + '\n}')
    if not check(['src/core/uwp_guest_arena.cpp', str(app), str(unit)]):
        raise SystemExit('FAIL UWP type contracts')
    # Negative fixtures compile the same real source with only the broken API
    # expression restored. A weak stub accepting either spelling fails this test.
    for name, text in [('bounds', app.read_text().replace('core_window->get_Bounds(&bounds)',
                                                        'core_window->Bounds')),
                       ('matrix', wrapper + fragment.replace('SetMatrixTransform(&matrix)',
                                                             'SetMatrixTransform(matrix)') + '\n}')]:
        unit.write_text(text)
        if check([str(unit)], quiet=True):
            raise SystemExit(f'FAIL: {name} negative fixture was accepted')
print(f'UWP ABI/DXGI type contracts + negative fixtures passed ({compiler_label}, {time.monotonic()-start:.2f}s)')
