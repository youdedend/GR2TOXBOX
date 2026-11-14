// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// fog_bake.comp -> fog_bake CS_6_6.
//
// Ported from src/graphics/gr2port/shaders/fog_bake.comp: the game's cloud map
// bake (CS 0x1c28d50). Keeps noise where it passes a humidity that grows with the
// cumulus texture and toward the map's top and bottom, with a soft edge.
//
// `layer` selects the stratus map (0) or the region map (1), which is what the
// GLSL's `bakes[layer]` indexing expresses.

#include "common/fog.hlsli"
#include "common/texel.hlsli"

cbuffer FogBakeConstants : register(b0) {
    ArenaRef g_fog;
    FogGrid g_grid;
    // 0 for the stratus map, 1 for the region map.
    uint g_layer;
    uint3 g_pad;
};

cbuffer ArenaConstants : register(b1) {
    ArenaTable g_arena;
};

cbuffer FogBakeTextures : register(b2) {
    // The fbm noise volume.
    uint g_fbm;
    // The R8_UNORM output.
    uint g_map;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const Fog fog = LoadFog(g_fog, g_arena);
    const uint layer = min(g_layer, 1u);

    const int3 texel = int3(id);
    const int3 size = int3(g_grid.columns, g_grid.rows, g_grid.slices);
    if (texel.x >= size.x || texel.y >= size.y || texel.z >= size.z) {
        return;
    }
    const float4 humidity = fog.bakes[layer][0];
    const float4 edges = fog.bakes[layer][1];
    const float height = (float(texel.z) + 0.5) / float(size.z);
    const float edge = min(1.0, 2.0 * abs(height - 0.5));
    const float edge_humidity = height > 0.5 ? humidity.x : humidity.y;
    const float2 uv = float2(texel.xy) / float2(size.xy);
    // The GLSL divides by size.x for the noise's third coordinate, which is
    // what makes the noise volume cubic in the grid's own units.
    const float noise = SampleVolumeLevel(g_fbm, float3(uv, float(texel.z) / float(size.x)), 0.0).r;
    // The game's default texture is white until the cumulus texture loads.
    const uint handle = fog.bake_cumulus[layer];
    const float cumulus = handle != 0u ? SampleTex(handle, uv).r : 1.0;
    const float h = humidity.z + humidity.w * cumulus + edge * edge_humidity;
    const float kept = saturate((noise - (1.0 - h)) / edges.x);

    RWTexture3D<float> map = ResourceDescriptorHeap[NonUniformResourceIndex(g_map)];
    map[texel] = pow(kept, edges.y);
}
