// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The fog volume and its cell arithmetic: the HLSL counterpart of upstream's
// scene.glsl `Fog` block and fog.glsl's helpers.
//
// The volumetric fog is a grid of cells over the screen -- 160 columns by 90 rows
// by 128 slices by default, fewer at a lower fog quality -- whose slices grow
// quadratically with depth. Four compute passes fill it (density, shadow,
// darkness, bake) and one pixel pass applies it.
//
// Two structural changes from the GLSL:
//
//   * `Fog` is read from the arena rather than being a buffer_reference, so the
//     std430 offsets are written out and LoadFog reads field by field.
//   * The GLSL's `layout(constant_id)` specialisation constants become a cbuffer
//     the host fills from the fog-quality setting. dxc can do specialisation with
//     -D, but the port ships one DXIL per shader, and the quality setting changes
//     at runtime, so the grid size has to be data.

#ifndef GR2_COMMON_FOG_HLSLI
#define GR2_COMMON_FOG_HLSLI

#include "common/arena.hlsli"

// The grid's dimensions at the default quality. The host overrides them through
// FogGrid; the shaders read the cbuffer, never these, so lowering the quality is
// a matter of filling a different struct.
#define FOG_DEFAULT_COLUMNS 160
#define FOG_DEFAULT_ROWS 90
#define FOG_DEFAULT_SLICES 128

// The grid size, from the host.
struct FogGrid {
    uint columns;
    uint rows;
    uint slices;
    uint pad;
};

// Fog, field for field with scene.glsl, in std430 byte offsets:
//
//    0  float4x4 view_to_fog            64
//   64  float4x4 view_to_world          64
//  128  float4x4 view_to_rotated        64
//  192  float4 projection
//  208  float4 cumulus[3]               48
//  256  float4 cumulus_offsets[3]       48
//  304  float4 layers[2]                32
//  336  float4 layer_offsets[2]         32
//  368  float4 light_color
//  384  float4 light_direction
//  400  float4 noise
//  416  float4 phase_air
//  432  float4 phase_cloud
//  448  float4 cloud_color
//  464  float  depth_scale
//  468  float  darkness_scale
//  472  float  noise_scale
//  476  float  noise_attenuation
//  480  float  shadow_darkness
//  484  float  cloud_shadow
//  488  float  time
//  492  float  shadow_time
//  496  float4 apply_depth
//  512  float4 apply_color
//  528  float4 eye
//  544  float  puddle_height
//  548  float  puddle_density
//  552  float  puddle_range
//  556  uint   cumulus_texture
//  560  float4 bakes[2][2]              64
//  624  uint   bake_cumulus[2]           8
//  632  uint   fog_padding[2]            8
//  640  float4 sun
// = 656 bytes.
#define FOG_BYTES 656u

struct Fog {
    float4x4 view_to_fog;
    float4x4 view_to_world;
    float4x4 view_to_rotated;
    float4 projection;
    float4 cumulus[3];
    float4 cumulus_offsets[3];
    float4 layers[2];
    float4 layer_offsets[2];
    float4 light_color;
    float4 light_direction;
    float4 noise;
    float4 phase_air;
    float4 phase_cloud;
    float4 cloud_color;
    float depth_scale;
    float darkness_scale;
    float noise_scale;
    float noise_attenuation;
    float shadow_darkness;
    float cloud_shadow;
    float time;
    float shadow_time;
    float4 apply_depth;
    float4 apply_color;
    float4 eye;
    float puddle_height;
    float puddle_density;
    float puddle_range;
    uint cumulus_texture;
    float4 bakes[2][2];
    uint bake_cumulus[2];
    uint fog_padding[2];
    float4 sun;
};

// Reads one Fog block. Skips nothing that any shader uses: a pass that wants only
// the projection still pays for the loads, but a ByteAddressBuffer load from a
// resident block is a cache hit by the time the second pass reads it, and one
// loader means one place for the offsets to be wrong.
Fog LoadFog(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    Fog fog;
    fog.view_to_fog = ArenaLoadMatrix(ArenaAdd(ref, 0u), table);
    fog.view_to_world = ArenaLoadMatrix(ArenaAdd(ref, 64u), table);
    fog.view_to_rotated = ArenaLoadMatrix(ArenaAdd(ref, 128u), table);
    fog.projection = asfloat(buffer.Load4(ref.offset + 192));
    [unroll] for (uint i = 0u; i < 3u; ++i) {
        fog.cumulus[i] = asfloat(buffer.Load4(ref.offset + 208 + i * 16u));
        fog.cumulus_offsets[i] = asfloat(buffer.Load4(ref.offset + 256 + i * 16u));
    }
    [unroll] for (uint i = 0u; i < 2u; ++i) {
        fog.layers[i] = asfloat(buffer.Load4(ref.offset + 304 + i * 16u));
        fog.layer_offsets[i] = asfloat(buffer.Load4(ref.offset + 336 + i * 16u));
    }
    fog.light_color = asfloat(buffer.Load4(ref.offset + 368));
    fog.light_direction = asfloat(buffer.Load4(ref.offset + 384));
    fog.noise = asfloat(buffer.Load4(ref.offset + 400));
    fog.phase_air = asfloat(buffer.Load4(ref.offset + 416));
    fog.phase_cloud = asfloat(buffer.Load4(ref.offset + 432));
    fog.cloud_color = asfloat(buffer.Load4(ref.offset + 448));
    fog.depth_scale = asfloat(buffer.Load(ref.offset + 464));
    fog.darkness_scale = asfloat(buffer.Load(ref.offset + 468));
    fog.noise_scale = asfloat(buffer.Load(ref.offset + 472));
    fog.noise_attenuation = asfloat(buffer.Load(ref.offset + 476));
    fog.shadow_darkness = asfloat(buffer.Load(ref.offset + 480));
    fog.cloud_shadow = asfloat(buffer.Load(ref.offset + 484));
    fog.time = asfloat(buffer.Load(ref.offset + 488));
    fog.shadow_time = asfloat(buffer.Load(ref.offset + 492));
    fog.apply_depth = asfloat(buffer.Load4(ref.offset + 496));
    fog.apply_color = asfloat(buffer.Load4(ref.offset + 512));
    fog.eye = asfloat(buffer.Load4(ref.offset + 528));
    fog.puddle_height = asfloat(buffer.Load(ref.offset + 544));
    fog.puddle_density = asfloat(buffer.Load(ref.offset + 548));
    fog.puddle_range = asfloat(buffer.Load(ref.offset + 552));
    fog.cumulus_texture = buffer.Load(ref.offset + 556);
    [unroll] for (uint layer = 0u; layer < 2u; ++layer) {
        [unroll] for (uint row = 0u; row < 2u; ++row) {
            fog.bakes[layer][row] =
                asfloat(buffer.Load4(ref.offset + 560 + (layer * 2u + row) * 16u));
        }
        fog.bake_cumulus[layer] = buffer.Load(ref.offset + 624 + layer * 4u);
        fog.fog_padding[layer] = buffer.Load(ref.offset + 632 + layer * 4u);
    }
    fog.sun = asfloat(buffer.Load4(ref.offset + 640));
    return fog;
}

// The game's jitter of a cell from 0 to 1, by its place and a clock
// (fog.glsl FogJitter). The magic constants are the game's own hashing
// coefficients, kept as bit patterns so the float values survive exactly.
float FogJitter(int3 cell, float clock, FogGrid grid) {
    const float share = float(cell.z) / float(grid.slices);
    const float seed = 256.0 * (clock * asfloat(0x41ce725au) +
                                asfloat(0x3e1419b6u) * (float(cell.x) * share) +
                                asfloat(0x3cf2965du) * (float(cell.y) * share)) +
                       asfloat(0x33d6bf95u);
    return frac(frac(abs(asfloat(0x4ceb79a3u) / seed)) * abs(seed));
}

// A cell's corner in normalized device coordinates, moved by a jitter in pixels
// of the grid (fog.glsl FogNdc). D3D's NDC has y up like Vulkan's here, because
// the port's projection is already flipped, so the expression matches.
float2 FogNdc(int3 cell, float shift, FogGrid grid) {
    return float2((2.0 * float(cell.x) + shift) / float(grid.columns) - 1.0,
                  1.0 - (2.0 * float(cell.y) - shift) / float(grid.rows));
}

// The view position at ndc and depth share t; projection is
// (1 / P00, 1 / P11, near - far, -near) (fog.glsl FogPosition).
float3 FogPosition(float2 ndc, float t, float4 projection) {
    const float z = projection.z * t + projection.w;
    return float3(-z * ndc.x * projection.x, -z * ndc.y * projection.y, z);
}

// The uv that lands exactly on a cell's texel centre. The fog's compute passes
// address cells with texelFetch in the GLSL; HLSL's Texture3D.Load takes integer
// coordinates, which VolumeTexelUv avoids having to re-derive at each call site.
float3 VolumeTexelUv(int3 cell, FogGrid grid) {
    return (float3(cell) + 0.5) / float3(grid.columns, grid.rows, grid.slices);
}

// Where a view position was among the last frame's cells, in texture coordinates
// (fog.glsl FogLastCell).
float3 FogLastCell(float3 position, float4x4 view_to_previous, float4 projection) {
    const float4 clip = mul(view_to_previous, float4(position, 1.0));
    const float2 ndc = clip.xy / clip.w;
    const float t = (-clip.w - projection.w) / projection.z;
    return float3(clamp(0.5 * ndc.x + 0.5, 0.0, 1.0), clamp(0.5 - 0.5 * ndc.y, 0.0, 1.0),
                  sqrt(clamp(t, 0.0, 1.0)));
}

#endif // GR2_COMMON_FOG_HLSLI
