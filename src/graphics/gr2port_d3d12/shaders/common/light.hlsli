// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The key light's block: the HLSL counterpart of upstream's scene.glsl `Light`,
// read from the arena like every other per-frame block.
//
// Only the fields the ported shaders use are loaded so far. The block is 1,344
// bytes (the three cascade matrices, the sky transforms, the grading colours, and
// sixty-four occluder spheres); a partial loader keeps this header reviewable,
// and adding a field is a matter of adding a line and its offset.
//
// std430 offsets of the fields loaded here:
//
//     0  float4x4 shadow_projection[3]       192
//   192  float4x4 inverse_shadow_projection[3] 192
//   384  float4x4 view_to_sky                 64
//   448  float4x4 view_to_previous            64
//   512  float4 inverse_projection
//   528  float4 split_depths
//   544  float4 light_color
//   560  float4 light_direction

#ifndef GR2_COMMON_LIGHT_HLSLI
#define GR2_COMMON_LIGHT_HLSLI

#include "common/arena.hlsli"
#include "common/scene.hlsli"

#define LIGHT_SHADOW_PROJECTION 0u
#define LIGHT_INVERSE_SHADOW_PROJECTION 192u
#define LIGHT_VIEW_TO_SKY 384u
#define LIGHT_VIEW_TO_PREVIOUS 448u
#define LIGHT_INVERSE_PROJECTION 512u
#define LIGHT_SPLIT_DEPTHS 528u
#define LIGHT_LIGHT_COLOR 544u
#define LIGHT_LIGHT_DIRECTION 560u

struct Light {
    float4x4 shadow_projection[CASCADES];
    float4x4 inverse_shadow_projection[CASCADES];
    float4x4 view_to_sky;
    float4x4 view_to_previous;
    float4 inverse_projection;
    float4 split_depths;
    float4 light_color;
    float4 light_direction;
};

Light LoadLight(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    Light light;
    [unroll] for (uint i = 0u; i < CASCADES; ++i) {
        light.shadow_projection[i] =
            ArenaLoadMatrix(ArenaAdd(ref, LIGHT_SHADOW_PROJECTION + i * 64u), table);
        light.inverse_shadow_projection[i] =
            ArenaLoadMatrix(ArenaAdd(ref, LIGHT_INVERSE_SHADOW_PROJECTION + i * 64u), table);
    }
    light.view_to_sky = ArenaLoadMatrix(ArenaAdd(ref, LIGHT_VIEW_TO_SKY), table);
    light.view_to_previous = ArenaLoadMatrix(ArenaAdd(ref, LIGHT_VIEW_TO_PREVIOUS), table);
    light.inverse_projection = asfloat(buffer.Load4(ref.offset + LIGHT_INVERSE_PROJECTION));
    light.split_depths = asfloat(buffer.Load4(ref.offset + LIGHT_SPLIT_DEPTHS));
    light.light_color = asfloat(buffer.Load4(ref.offset + LIGHT_LIGHT_COLOR));
    light.light_direction = asfloat(buffer.Load4(ref.offset + LIGHT_LIGHT_DIRECTION));
    return light;
}

#endif // GR2_COMMON_LIGHT_HLSLI
