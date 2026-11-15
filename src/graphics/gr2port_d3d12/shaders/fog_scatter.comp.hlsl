// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// fog_scatter.comp -> fog_scatter CS_6_6.
//
// Ported from src/graphics/gr2port/shaders/fog_scatter.comp: the game's fog
// scattering (CS 0x1c2afe8). Along each column, the sun's light scattered by the
// air and the clouds toward the eye, dimmed by the clouds' darkness and the sun's
// shadow, and the share of what lies behind that still shows -- written at the end
// of each slice, so the pixel pass can fetch the slice its depth falls in.
//
// The GLSL's loop bound is the specialisation constant FogSlices, so it was
// unrolled at pipeline creation; here the bound is data, so the loop is marked
// [loop] to stop dxc flattening a 128-iteration body into 128 copies.

#include "common/fog.hlsli"
#include "common/texel.hlsli"

cbuffer FogScatterConstants : register(b1) {
    uint2 g_fog_words;
    FogGrid g_grid;
};
#define g_fog ArenaMake(g_fog_words.x, g_fog_words.y)

cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

cbuffer FogScatterTextures : register(b2) {
    uint g_noise;
    uint g_clouds;
    uint g_shadow;
    uint g_scattered;
};

// The clouds reach CloudReach depth scales from the eye and thin out over the
// last 1 - CloudSustain of that.
static const float CloudSustain = 0.85;
static const float CloudReach = 40.0;
static const float DensityScale = 0.125;

float Phase(float4 terms, float c) {
    return terms.x / pow(abs(terms.y - terms.z * c), 1.5);
}

[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const Fog fog = LoadFog(g_fog, g_arena);

    const int2 column = int2(id.xy);
    if (column.x >= int(g_grid.columns) || column.y >= int(g_grid.rows)) {
        return;
    }
    const float2 ndc = FogNdc(int3(column, 0), 0.0, g_grid);
    const float2 slope = ndc * fog.projection.xy;
    const float3 ray = -fog.projection.w * float3(slope, -1.0);
    const float c = -dot(fog.light_direction.xyz, ray) / length(ray);
    const float3 air = fog.light_color.rgb * Phase(fog.phase_air, c);
    const float3 cloud = fog.cloud_color.rgb * Phase(fog.phase_cloud, c);
    const float inverse_reach = 1.0 / (CloudReach * fog.depth_scale);
    const float inverse_sustain = 1.0 / (1.0 - CloudSustain);
    float optical_depth = 0.0;
    // What shows at a slice is what the slice before it stored as left.
    float shown = 1.0;
    float3 light_in = float3(0.0, 0.0, 0.0);

    RWTexture3D<float4> scattered = DescriptorRwVolumeF4(g_scattered);

    [loop] for (int i = 0; i < int(g_grid.slices); ++i) {
        const float a = float(i) / float(g_grid.slices);
        const float b = float(i + 1) / float(g_grid.slices);
        const float z = 0.5 * fog.projection.z * (a * a + b * b) + fog.projection.w;
        const float3 position = float3(-z * slope, z);
        const float3 world = mul(fog.view_to_world, float4(position, 1.0)).xyz;
        const float n = SampleVolumeLevel(g_noise, (world + fog.noise.xyz) * fog.noise.w, 0.0).r;
        const float amplitude = fog.noise_scale * exp(fog.noise_attenuation * z);
        const float shaped = saturate(amplitude * (2.0 * n - 1.0) + 0.5);
        const float slice = float(g_grid.slices) * (b * b - a * a);
        const float path = fog.depth_scale * slice * shaped * shaped;
        const float fade = saturate((1.0 - length(position) * inverse_reach) * inverse_sustain);
        const float smooth_fade = fade * fade * (3.0 - 2.0 * fade);
        const int3 cell = int3(column, i);
        // texelFetch on a 3D volume. The coordinates are cell centres, so the
        // bilinear sampler returns exactly that texel; the bounds are guaranteed
        // by the column check and the slice loop.
        const float2 cloud_cell = SampleVolumeLevel(g_clouds, VolumeTexelUv(cell, g_grid), 0.0).rg;
        const float extinction = max(fog.light_color.w, cloud_cell.r * smooth_fade * DensityScale);
        const float step_depth = path * extinction;
        const float sun = pow(SampleVolumeLevel(g_shadow, VolumeTexelUv(cell, g_grid), 0.0).r, 4.0);
        const float visible = exp(-fog.darkness_scale * cloud_cell.g);
        const float air_visible = min(visible, 1.0 + fog.shadow_darkness * (sun - 1.0));
        const float cloud_visible = min(visible, 1.0 + fog.cloud_shadow * (sun - 1.0));
        light_in += shown * (air * slice * air_visible + cloud * step_depth * cloud_visible);
        optical_depth += step_depth;
        const float left = exp(-optical_depth);
        scattered[cell] = float4(light_in, left);
        shown = saturate(left);
    }
}
