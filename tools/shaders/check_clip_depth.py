#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Audit every non-pfx VS/DS against its GLSL final-raster stage. No double conversion."""
from pathlib import Path
import re
import sys
ROOT = Path(__file__).resolve().parents[2]
HLSL = ROOT / 'src/graphics/gr2port_d3d12/shaders'
GLSL = ROOT / 'src/graphics/gr2port/shaders'

def code(path):
    text = path.read_text()
    # Follow the two GLSL shadow VS wrappers; the arithmetic is in shadow.glsl.
    if path.suffix == '.vert' and path.stem in ('shadow', 'shadow_alpha'):
        text += (GLSL / 'shadow.glsl').read_text()
    if path.name in ('leaf.geom', 'leaf_shadow.geom'):
        text += (GLSL / 'leaf_quad.glsl').read_text()
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    return re.sub(r'\s+', '', text)

def audit():
    errors = []
    # These stages deliberately export raw intermediate positions. Their GS is
    # the final raster producer and owns the depth conversion, just like GLSL.
    geometry = {'contour.vert.hlsl': ('contour.geom', 'reverse'),
                'leaf.dom.hlsl': ('leaf.geom', 'reverse'),
                'leaf_shadow.dom.hlsl': ('leaf_shadow.geom', 'forward')}
    passthrough = {'leaf.vert.hlsl', 'fullscreen.vert.hlsl', 'motion_blur.vert.hlsl', 'imgui_relay.vert.hlsl'}
    checked = 0
    for path in sorted(HLSL.glob('*.hlsl')):
        if not path.name.endswith(('.vert.hlsl', '.dom.hlsl')):
            continue
        checked += 1
        if path.name == 'imgui_relay.vert.hlsl':
            continue  # Host-only 2D shader: no paired game shader.
        paired = path.name[:-5].replace('.dom', '.tese')
        if path.name in geometry:
            final, kind = geometry[path.name]
            if '.z=0.5*' in code(path):
                errors.append(f'{path.name}: premature depth conversion before geometry')
            source, target = code(GLSL / final), code(HLSL / (final + '.hlsl'))
            if kind == 'reverse' and not ('0.5*(w-z)' in source or '0.5*(clip.w-clip.z)' in source):
                errors.append(f'{final}: GLSL reverse-Z contract changed')
            expected = '0.5*(o.position.w-o.position.z)' if kind == 'reverse' else '0.5*(o.position.z+o.position.w)'
            # Contour uses the local w,z parameters of EmitRibbon.
            if expected not in target and not (final == 'contour.geom' and '0.5*(w-z)' in target):
                errors.append(f'{final}: missing final-stage {kind} conversion')
            continue
        source, target = code(GLSL / paired), code(path)
        if path.name in passthrough:
            continue
        forward = path.stem.startswith('shadow')
        expected = 'clip.z=0.5*(clip.z+clip.w)' if forward else '0.5*(clip.w-clip.z)'
        if forward:
            if expected not in source or expected not in target:
                errors.append(f'{path.name}: shadow forward-Z differs from shadow.glsl')
        else:
            if expected not in source:
                errors.append(f'{paired}: GLSL conversion not recognized')
            actual = '0.5*(output.position.w-output.position.z)' if path.name == 'world_decal.vert.hlsl' else expected
            if target.count(actual) != 1:
                errors.append(f'{path.name}: require exactly one GLSL reverse-Z conversion')
    # Additional shared-capture ABI mismatches found while enabling these stages.
    for stage in ('vert', 'frag'):
        decal = code(HLSL / f'world_decal.{stage}.hlsl')
        for required in ('#defineWORLD_DECAL_BYTES304u',
                         'decal.world_view_projection=ArenaLoadMatrix(ArenaAdd(ref,64u)',
                         'decal.view_to_decal=ArenaLoadMatrix(ArenaAdd(ref,128u)',
                         'decal.world_view=ArenaLoadMatrix(ArenaAdd(ref,0u)'):
            if required not in decal:
                errors.append(f'world_decal.{stage}: shared 304-byte record ABI mismatch')
        if '#defineLAMBERT_CONSTANTS_BYTES656u' not in code(HLSL / f'lambert.{stage}.hlsl'):
            errors.append(f'lambert.{stage}: shared Character table stride mismatch')
    sky = code(HLSL / 'sky.frag.hlsl')
    for field, offset in {'SUN_COLOR':192,'SUN_DIRECTION':208,'CLOUD_COLOR':224,
                          'CLOUD_LAYERS':240,'CLOUD_FADE':272,'SUN_SIZE':280,
                          'SUN_OUTER':284,'BLEND':288,'CUBES':292,'CLOUD':300}.items():
        if f'#defineSKY_{field}{offset}u' not in sky:
            errors.append(f'sky: shared field offset mismatch: {field}')
    draw = code(HLSL / 'common/scene.hlsli')
    for field, offset in {'vertices':160,'skin':184,'palette':192}.items():
        if f'draw.{field}=ArenaMake(buffer.Load(ref.offset+{offset+4}),buffer.Load(ref.offset+{offset}))' not in draw:
            errors.append(f'Draw.{field}: must unpack high arena / low offset')
    return checked, errors

if __name__ == '__main__':
    count, problems = audit()
    for problem in problems:
        print(problem, file=sys.stderr)
    print(f'clip-depth parity: {count} VS/DS audited, final GS conversions included, {len(problems)} errors')
    sys.exit(bool(problems))
