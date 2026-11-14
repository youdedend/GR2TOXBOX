// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// fog_apply.frag -> fog_apply PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/fog_apply.frag: the game's FogRender
// (CS 0x1c2b88c) as a blend over the scene. Takes the scattered light at the
// pixel's depth, a ground fog below a height, and a fade toward the fog's colour
// as less of the scene shows. The blend adds rgb to the scene times 1 - alpha.
//
// The GLSL uses gl_FragCoord and texelFetch on the depth buffer; both survive,
// SV_Position and Texture2D.Load. The 3D scattered-light volume is sampled with
// an explicit LOD of 0, which D3D calls SampleLevel.

#include "common/fog.hlsli"
#include "common/texel.hlsli"

// cbuffers share one global namespace in HLSL, so two blocks cannot both declare
// a member called g_pad -- dxc reports the second as a redefinition with a
// different type. Each block's padding is named after the block.
cbuffer FogApplyConstants : register(b0) {
    ArenaRef g_fog;
    float3 g_fog_pad;
};

cbuffer ArenaConstants : register(b1) {
    ArenaTable g_arena;
};

cbuffer FogApplyTextures : register(b2) {
    uint g_scene_depth;
    // The scattered light, a Texture3D SRV in the same bindless heap.
    uint g_scattered;
    uint2 g_textures_pad;
};

struct PsInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PsInput input) : SV_Target {
    const Fog fog = LoadFog(g_fog, g_arena);

    const int2 pixel = int2(input.position.xy);
    const float2 size = float2(FetchTexelSize(g_scene_depth));
    // Reverse depth to the game's depth.
    const float z = 1.0 - FetchTexel(g_scene_depth, pixel, 0).r;
    const float distance_to_eye = 1.0 / (fog.apply_depth.x * (2.0 * z - 1.0) + fog.apply_depth.y);
    const float2 uv = (float2(pixel) + 0.5) / size;
    const float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    const float3 position =
        mul(fog.view_to_rotated,
            float4(distance_to_eye * ndc * fog.projection.xy, -distance_to_eye, 1.0))
            .xyz;
    const float low = min(fog.eye.y, position.y);
    const float high = max(fog.eye.y, position.y);
    const float below = saturate((fog.puddle_height - low) / (high - low));
    const float puddle =
        exp(-fog.puddle_density * min(fog.puddle_range, length(fog.eye.xyz - position) * below));
    const float slice = sqrt(saturate((distance_to_eye - fog.apply_depth.z) * fog.apply_depth.w));
    const float4 fogged = SampleVolumeLevel(g_scattered, float3(uv, slice), 0.0);
    const float shown = min(fogged.a, puddle);
    const float fade = fog.apply_color.w * clamp(-log(max(shown, 1e-6)), 0.0, 1.0);
    return float4(lerp(fogged.rgb, fog.apply_color.rgb, fade), 1.0 - (1.0 - fade) * shown);
}
