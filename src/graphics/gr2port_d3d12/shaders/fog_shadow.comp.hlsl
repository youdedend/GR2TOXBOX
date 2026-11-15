// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// fog_shadow.comp -> fog_shadow CS_6_6.
//
// Ported from src/graphics/gr2port/shaders/fog_shadow.comp: the game's fog shadow
// (CS 0x1c2caac). Whether the sun reaches a random point of each cell, by one
// sample of its cascade, blended with the last frame's.
//
// The two shadow lookups are comparison samples (a Texture2DArrayShadow and a
// Texture2DShadow). In HLSL those are `Texture2DArray<float>` / `Texture2D<float>`
// read through SampleCmpLevelZero with an explicit comparison sampler, which is
// what GLSL's sampler2DArrayShadow does implicitly against the shadow comparison
// sampler. The GLSL passes an explicit zero gradient to force the top mip; the
// LevelZero intrinsics are that, said directly.

#include "common/fog.hlsli"
#include "common/light.hlsli"
#include "common/texel.hlsli"

cbuffer FogShadowConstants : register(b1) {
    uint2 g_fog_words;
    uint2 g_light_words;
    FogGrid g_grid;
};
#define g_fog ArenaMake(g_fog_words.x, g_fog_words.y)
#define g_light ArenaMake(g_light_words.x, g_light_words.y)

cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

cbuffer FogShadowTextures : register(b2) {
    // The last frame's shadow volume.
    uint g_last_shadow;
    // The R8_UNORM output.
    uint g_shadow_out;
    // The current cascades (Texture2DArray depth) and the last cascade
    // (Texture2D depth).
    uint g_shadow_map;
    uint g_last_shadow_map;
};

static const float Jitter = 0.18;
static const float Blend = 0.14;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const Fog fog = LoadFog(g_fog, g_arena);
    const Light light = LoadLight(g_light, g_arena);

    const int3 cell = int3(id);
    if (cell.x >= int(g_grid.columns) || cell.y >= int(g_grid.rows)) {
        return;
    }
    const float a = float(cell.z) / float(g_grid.slices);
    const float b = float(cell.z + 1) / float(g_grid.slices);
    const float j = FogJitter(cell, fog.shadow_time, g_grid);
    const float3 position =
        FogPosition(FogNdc(cell, Jitter * j, g_grid), a * a + j * (b * b - a * a), fog.projection);

    const uint cascade = -position.z < light.split_depths.y   ? 0u
                         : -position.z < light.split_depths.z ? 1u
                                                              : 2u;
    const float4 clip = mul(light.shadow_projection[cascade], float4(position, 1.0));
    const float3 coord = clip.xyz / clip.w;

    // One comparison sample at the top level, as the game's.
    float lit;
    if (cascade == 2u) {
        Texture2D<float> map = DescriptorDepth(g_last_shadow_map);
        SamplerComparisonState sampler = DescriptorComparisonSampler(SMP_COMPARISON);
        lit = map.SampleCmpLevelZero(sampler, coord.xy, coord.z);
    } else {
        Texture2DArray<float> map = DescriptorDepthArray(g_shadow_map);
        SamplerComparisonState sampler = DescriptorComparisonSampler(SMP_COMPARISON);
        lit = map.SampleCmpLevelZero(sampler, float3(coord.xy, float(cascade)), coord.z);
    }

    // The history follows the slice's near edge at the cell's corner.
    const float3 corner = FogPosition(FogNdc(cell, 0.0, g_grid), a * a, fog.projection);
    const float3 last = FogLastCell(corner, light.view_to_previous, fog.projection);
    const float previous = SampleVolumeLevel(g_last_shadow, last, 0.0).r;

    RWTexture3D<float> shadow_out = DescriptorRwVolumeF1(g_shadow_out);
    shadow_out[cell] = lerp(previous, lit, Blend);
}
