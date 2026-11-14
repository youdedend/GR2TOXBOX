// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// What the world-surface pixel shaders share: the HLSL counterpart of upstream's
// surface.glsl. Only the parts that need no texture *array* live here; the
// sampling is texel.hlsli's, keyed by descriptor index instead of by a
// nonuniform descriptor array.

#ifndef GR2_COMMON_SURFACE_HLSLI
#define GR2_COMMON_SURFACE_HLSLI

#include "common/texel.hlsli"

// The game's packed normal (surface.glsl PackNormal): the direction of xy, with
// the length sqrt(0.5 - 0.5 z). The G-buffer's normal target is a 16-bit two-
// channel format, so the third component is reconstructed by the lighting pass.
float2 PackNormal(float3 n) {
    const float2 xy = n.xy;
    const float length_xy = sqrt(max(dot(xy, xy), 1e-6));
    return xy / length_xy * sqrt(abs(0.5 - 0.5 * n.z));
}

// A screen door fades a surface in (surface.glsl ScreenDoorShut). The game's
// depth runs the other way from the D3D reverse-Z one, so the callers that port
// a GLSL pass which already flipped depth pass the flipped value in.
//
// `position` is SV_Position.xy (pixel centres, half-integer) and `reverse_z` is
// the reverse-Z depth, 1 at the near plane.
bool ScreenDoorShut(float threshold, float2 position, float reverse_z) {
    const float seed = 256.0 * (0.0771356 * position.x + 0.00888381 * position.y) *
                           (1.0 - reverse_z) +
                       1e-7;
    return threshold < frac(frac(abs(123456792.0 / seed)) * abs(seed));
}

// Parallax occlusion mapping through the normal map's alpha (surface.glsl
// Parallax): 8 to 14 steps along `direction` by how far the surface turns from
// the eye, then the intersection of the last two steps' lines.
//
// GLSL's dFdx/dFdy on the *start* coordinate become ddx/ddy of the same value
// here: the derivative has to be taken in uniform control flow, which is why the
// caller computes `start` before any branching.
float2 Parallax(uint handle, float2 start, float2 direction, float facing) {
    const int steps = int(14.0 - 6.0 * facing);
    const float step_size = 1.0 / float(steps);
    const float2 dx = ddx(start);
    const float2 dy = ddy(start);
    float2 uv = start;
    float level = 1.0;
    float height = 0.0;
    float last_height = 1.0;
    [loop] for (int i = 0; i < 14; ++i) {
        if (i >= steps) {
            break;
        }
        uv -= direction * step_size;
        height = SampleTexGrad(handle, uv, dx, dy).a;
        level -= step_size;
        if (height > level) {
            break;
        }
        last_height = height;
    }
    const float last_level = level + step_size;
    const float last_gap = last_level - last_height;
    const float gap = level - height;
    const float hit =
        last_gap != gap ? (level * last_gap - last_level * gap) / (last_gap - gap) : 0.0;
    return start - direction * (1.0 - hit);
}

#endif // GR2_COMMON_SURFACE_HLSLI
