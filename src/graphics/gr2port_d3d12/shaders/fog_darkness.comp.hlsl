// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// fog_darkness.comp -> fog_darkness CS_6_6.
//
// Ported from src/graphics/gr2port/shaders/fog_darkness.comp: the game's cloud
// darkness bake (CS 0x1c2966c), the map summed over 16 texels toward the sun.
//
// The GLSL's imageSize() becomes the grid size the host passes; the format is
// R8_UNORM here as in GLSL (layout r8), so the store is a single channel.

#include "common/fog.hlsli"
#include "common/texel.hlsli"

cbuffer FogDarknessConstants : register(b0) {
    ArenaRef g_fog;
    FogGrid g_grid;
};

cbuffer ArenaConstants : register(b1) {
    ArenaTable g_arena;
};

cbuffer FogDarknessTextures : register(b2) {
    uint g_map;
    // The R8_UNORM output, a UAV in the same heap.
    uint g_darkness;
};

static const int Steps = 16;
static const float StepShare = 0.0625;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const Fog fog = LoadFog(g_fog, g_arena);

    const int3 texel = int3(id);
    const int3 size = int3(g_grid.columns, g_grid.rows, g_grid.slices);
    if (texel.x >= size.x || texel.y >= size.y || texel.z >= size.z) {
        return;
    }
    // The map's y runs along the world's z, its z along the world's y.
    const float3 step_size = -fog.sun.xzy / float3(size);
    const float3 start = (float3(texel) + 0.5) / float3(size);
    float sum = 0.0;
    [loop] for (int k = 0; k < Steps; ++k) {
        sum += SampleVolumeLevel(g_map, start + float(k) * step_size, 0.0).r;
    }

    RWTexture3D<float> darkness = ResourceDescriptorHeap[NonUniformResourceIndex(g_darkness)];
    darkness[texel] = StepShare * sum;
}
