// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// luminance.comp -> luminance CS_6_6.
//
// Ported from src/graphics/gr2port/shaders/luminance.comp: the game's luminance
// program, one sample of each 8x8 tile at an offset that changes each frame. All
// tiles add into one sum, here with an atomic in fixed point.
//
// The GLSL's atomicAdd is on an int inside a push-constant buffer reference;
// here the sum lives in ARENA_SCRATCH, which is the arena the backend creates
// with UAV backing, so InterlockedAdd is legal on it. The GLSL's `int(round(x))`
// is round-to-nearest-even in both languages, so the int(round(...)) below keeps
// the rounding the meter's fixed-point accumulation was tuned against.

#include "common/grade.hlsli"
#include "common/texel.hlsli"

cbuffer LuminanceConstants : register(b0) {
    ArenaRef g_meter;
    uint2 g_size;
    uint2 g_offset;
};

cbuffer ArenaConstants : register(b1) {
    ArenaTable g_arena;
};

cbuffer LuminanceTextures : register(b2) {
    uint g_scene;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint2 tile = id.xy * 8u;
    if (tile.x >= g_size.x || tile.y >= g_size.y) {
        return;
    }
    const int2 pixel = int2(min(tile + g_offset, g_size - 1u));
    const float luminance = dot(FetchTexel(g_scene, pixel, 0).rgb, float3(0.299, 0.587, 0.114));
    const int fixed_point = int(round(log(max(luminance, 1e-6)) * MeterScale));
    // One atomic per 8x8 tile rather than per pixel, which is what upstream's
    // sum does too. The UAV is bound to a local: InterlockedAdd writes through
    // it, and a temporary returned by value is not addressable in HLSL.
    RWByteAddressBuffer meter_buffer = ArenaBufferRw(g_meter, g_arena);
    uint original;
    meter_buffer.InterlockedAdd(METER_LOG_SUM, asuint(fixed_point), original);
}
