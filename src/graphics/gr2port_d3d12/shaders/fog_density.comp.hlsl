// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// fog_density.comp -> fog_density CS_6_6.
//
// Ported from src/graphics/gr2port/shaders/fog_density.comp: the game's FogDist
// (CS 0x1c29f34). Each cell's cloud density from three cumulus layers and the
// stratus and region maps, and their darkness, at a jittered point, blended with
// the last frame's.
//
// The GLSL's `inout` parameters on AddMap become inout in HLSL, which has the
// same meaning here. The volume SRVs are indexed by bindless handle, so the five
// maps are five uints in the pass constants rather than five registers.

#include "common/fog.hlsli"
#include "common/light.hlsli"
#include "common/texel.hlsli"

cbuffer FogDensityConstants : register(b1) {
    uint2 g_fog_words;
    uint2 g_light_words;
    FogGrid g_grid;
    // The five input volumes and the output, all Texture3D bindless handles.
    uint g_last_clouds;
    uint g_stratus;
    uint g_stratus_darkness;
    uint g_region;
    uint g_region_darkness;
    uint g_clouds_out;
    uint2 g_pad;
};
#define g_fog ArenaMake(g_fog_words.x, g_fog_words.y)
#define g_light ArenaMake(g_light_words.x, g_light_words.y)

cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

// The share of the new value each frame, of density and of darkness; the
// density's scale.
static const float DensityBlend = 0.38;
static const float DarknessBlend = 0.18;
static const float DensityScale = 8.0;
static const float Jitter = 0.15;

// Adds a cloud map's density and darkness at a point in the fog's space.
void AddMap(uint map, uint map_darkness, float4 layer, float4 offset, float3 p, FogGrid grid,
            inout float density, inout float darkness) {
    if (layer.z > 0.0) {
        const float3 q = float3(offset.x + layer.w * p.x, offset.y + layer.w * p.z,
                                (p.y - layer.x) * layer.y + 0.5);
        // More than a slice past either end of the maps, both slices the filter
        // reads are the border, which is zero, so the maps are not read.
        const float slice = 1.0 / float(grid.slices);
        float cloud = 0.0;
        float shade = 0.0;
        if (!(q.z < -slice || q.z > 1.0 + slice)) {
            cloud = SampleVolumeLevel(map, q, 0.0).r;
            shade = SampleVolumeLevel(map_darkness, q, 0.0).r;
        }
        density += layer.z * cloud;
        darkness += offset.z * shade;
    }
}

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
    const float j = FogJitter(cell, fog.time, g_grid);
    const float t = 0.5 * (a * a + b * b) + Jitter * j * (b * b - a * a);
    const float3 position = FogPosition(FogNdc(cell, Jitter * j, g_grid), t, fog.projection);
    const float3 p = mul(fog.view_to_fog, float4(position, 1.0)).xyz;

    float density = 0.0;
    [unroll] for (int i = 0; i < 3; ++i) {
        const float4 layer = fog.cumulus[i];
        if (layer.z > 0.0) {
            const float2 offset = fog.cumulus_offsets[i].xy;
            const float2 uv = (p.xz + offset) * layer.w;
            const float u = min(1.0, abs(p.y - layer.x) * layer.y);
            // The game's default texture is white until the cumulus texture
            // loads. At u = 1 the layer's share is zero whatever the texture
            // holds, so it is not read.
            float c = 1.0;
            if (fog.cumulus_texture != 0u && !(u >= 1.0)) {
                c = SampleTex(fog.cumulus_texture, uv).r;
            }
            density += layer.z * c * (1.0 - u * u * u);
        }
    }
    float darkness = 0.0;
    AddMap(g_stratus, g_stratus_darkness, fog.layers[0], fog.layer_offsets[0], p, g_grid, density,
           darkness);
    AddMap(g_region, g_region_darkness, fog.layers[1], fog.layer_offsets[1], p, g_grid, density,
           darkness);

    // The history follows the cell's centre without the jitter.
    const float3 centre =
        FogPosition(FogNdc(cell, 0.0, g_grid), 0.5 * (a * a + b * b), fog.projection);
    const float3 last = FogLastCell(centre, light.view_to_previous, fog.projection);
    const float2 kept = SampleVolumeLevel(g_last_clouds, last, 0.0).rg;

    // The output is RG8_UNORM (layout rg8 in the GLSL), so the store is two
    // channels.
    RWTexture3D<float2> clouds_out = DescriptorRwVolumeF2(g_clouds_out);
    clouds_out[cell] = float2(lerp(kept.r, min(1.0, DensityScale * density), DensityBlend),
                              lerp(kept.g, darkness, DarknessBlend));
}
