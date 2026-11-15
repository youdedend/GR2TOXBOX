#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-only regression tests: MAP RVAs, cbuffer ABI, exact detile address code."""
import importlib.util
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class PortTools(unittest.TestCase):
    def test_uwp_dxgi_symbol_allowlist(self):
        with tempfile.TemporaryDirectory(prefix='gr2-import-') as tmp:
            path = Path(tmp) / 'imports.txt'
            for symbol, expected in [('CreateDXGIFactory2', 0), ('CreateDXGIFactory', 1),
                                     ('DXGIGetDebugInterface1', 1), ('', 1)]:
                path.write_text('    dxgi.dll\n' + (f'        12 {symbol}\n' if symbol else ''))
                result = subprocess.run(['python3', str(ROOT / 'tools/ci/check_uwp_imports.py'),
                                         '--exe', 'test.exe', '--imports-file', str(path)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stdout)

    def test_dxc_scalar_splat_regression(self):
        checker = module('hlsl_structure', ROOT / 'tools/shaders/check_hlsl_structure.py')
        with tempfile.TemporaryDirectory(prefix='gr2-hlsl-') as tmp:
            path = Path(tmp) / 'bad.frag.hlsl'
            path.write_text('float4 main() : SV_Target { return float4(0.0); }')
            self.assertTrue(any('scalar-splat' in p for p in checker.check_file(path)))
            path.write_text('float4 main() : SV_Target { return float4(0.0, 0.0, 0.0, 0.0); }')
            self.assertEqual(checker.check_file(path), [])

    def test_map_aslr_and_other_modules(self):
        script = module('symbolicate', ROOT / 'tools/symbolicate_crash.py')
        text = '''Preferred load address is 0000000140000000
 0001:00000000 ?Boot@@YAXXZ 0000000140001000 f boot.obj
 0001:00000100 ?Draw@@YAXXZ 0000000140001100 f draw.obj
'''
        crash = '''FIRST_CHANCE module_base=0x00007ff700000000 rip=0x00007ff700001105 rva=+0x1105
frame[00] rip=0x00007ff700001105 image=0x00007ff700000000 rva=+0x1105
frame[01] rip=0x00007ff800001105 image=0x00007ff800000000 rva=+0x1105
frame[02] rip=0x0000000100000000 JIT/guest-or-leaf
'''
        resolved = list(script.resolve(text, crash))
        self.assertIn('?Draw@@YAXXZ+0x5', resolved[0])
        self.assertIn('?Draw@@YAXXZ+0x5', resolved[1])
        self.assertNotIn('=>', resolved[2])
        self.assertNotIn('=>', resolved[3])
        with self.assertRaises(ValueError): script.load_map('empty')

    def test_detile_shader_address_functions_and_host_guards(self):
        # Compile the actual HLSL address arithmetic as portable C++, not a
        # second implementation of it. Test analytic microtile mappings and
        # macro address bijections, including both P8 configurations and P2.
        shader = (ROOT / 'src/graphics/gr2port_d3d12/shaders/detile.comp.hlsl').read_text()
        code = shader[shader.index('#define BITS_PER_PIXEL'):shader.index('[numthreads')]
        with tempfile.TemporaryDirectory(prefix='gr2-detile-') as tmp:
            cpp = Path(tmp) / 'test.cpp'
            defines = '\n'.join('#define g_' + n + ' p.' + n for n in
                ['pitch','height','depth','texel_count','bytes','micro_mode','thickness','samples',
                 'array_mode','pipe_config','bank_width','bank_height','num_banks','tile_split','macro_aspect','bank_swizzle'])
            cpp.write_text('''#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <set>
#include "graphics/gr2port_d3d12/detile.h"
#include "graphics/gr2port_d3d12/shader_batch_layout.h"
using namespace Gr2Port::D3D12;
using uint = std::uint32_t;
using std::min; using std::max;
uint firstbithigh(uint x) { return 31u - std::countl_zero(x); }
DetileConstants p;
''' + defines + '\n' + code + '''
int main() {
    DetileTextures textures{1,2,0,0};
    for (uint bytes : {1u,2u,4u,8u,12u,16u}) {
        p = {16,16,1,256,bytes,1,1,1,2,0,1,1,2,256,1,0};
        assert(ValidateDetile(p,textures));
        for (uint y=0; y<16; ++y) for (uint x=0; x<16; ++x) {
            uint morton=0;
            for (uint b=0; b<3; ++b) morton |= ((x>>b)&1u)<<(b*2) | ((y>>b)&1u)<<(b*2+1);
            const uint expected = (((y/8)*2+x/8)*64+morton)*bytes;
            assert(SourceOffset(y*16+x)==expected);
        }
    }
    for (uint pipe : {0u,10u,12u}) for (uint banks : {2u,4u,8u,16u})
    for (uint bytes : {1u,2u,4u,8u,16u}) for (uint mode : {0u,1u}) {
        const uint pipes=pipe==0?2:8;
        const uint padding = bytes < 4 ? 4 / bytes : 1;
        p = {8*pipes,8*banks*padding,1,64*pipes*banks*padding,bytes,mode,1,1,4,pipe,1,1,banks,256,1,0};
        assert(ValidateDetile(p,textures));
        std::set<uint> offsets;
        for (uint texel=0; texel<p.texel_count; ++texel) {
            uint offset=SourceOffset(texel);
            assert(offset < p.texel_count*bytes);
            assert(offset%bytes==0);
            assert(offsets.insert(offset).second);
        }
    }
    p.samples=2; assert(!ValidateDetile(p,textures));
    p.samples=1; p.thickness=4; assert(!ValidateDetile(p,textures));
    p.thickness=1; p.array_mode=5; assert(!ValidateDetile(p,textures));
    return 0;
}
''')
            exe = Path(tmp) / 'test'
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Isrc', str(cpp),
                            'src/graphics/gr2port_d3d12/detile_layout.cpp', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
