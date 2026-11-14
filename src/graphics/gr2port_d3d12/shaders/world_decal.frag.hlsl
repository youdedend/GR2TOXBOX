// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// world_decal.frag -> world_decal PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/world_decal.frag: the game's decal
// pixel program. The surface under the pixel, if the decal's box holds it, takes
// the decal's colour, normal and material, by the colour's alpha.
//
// The order of operations is load-bearing and preserved: every texture sample
// happens *before* the discard, because the parallax march needs per-pixel
// derivatives, and a derivative taken after a discard is undefined in a quad
// whose other pixels have been discarded.

#include "common/light.hlsli"
#include "common/scene.hlsli"
#include "common/surface.hlsli"
#include "common/texel.hlsli"

// The decal record, repeated here rather than shared with world_decal.vert.hlsl:
// HLSL has no separate interface file for a struct both stages read, and the two
// stages' structs would have to be kept identical by hand either way. If one
// changes, this one changes with it.
#define WORLD_DECAL_BYTES 320u

struct WorldDecal {
    float4x4 world_view_projection;
    float4x4 view_to_decal;
    float4x4 world_view;
    float4 normal_transform;
    float4 color_transform;
    float4 parallax;
    float4 normal_scale;
    float4 color_scale;
    float roughness;
    float metallic;
    float translucency;
    uint color_map;
    uint normal_map;
};

WorldDecal LoadWorldDecalAt(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    WorldDecal decal;
    decal.world_view_projection = ArenaLoadMatrix(ArenaAdd(ref, 0u), table);
    decal.view_to_decal = ArenaLoadMatrix(ArenaAdd(ref, 64u), table);
    decal.world_view = ArenaLoadMatrix(ArenaAdd(ref, 128u), table);
    decal.normal_transform = asfloat(buffer.Load4(ref.offset + 192));
    decal.color_transform = asfloat(buffer.Load4(ref.offset + 208));
    decal.parallax = asfloat(buffer.Load4(ref.offset + 224));
    decal.normal_scale = asfloat(buffer.Load4(ref.offset + 240));
    decal.color_scale = asfloat(buffer.Load4(ref.offset + 256));
    decal.roughness = asfloat(buffer.Load(ref.offset + 272));
    decal.metallic = asfloat(buffer.Load(ref.offset + 276));
    decal.translucency = asfloat(buffer.Load(ref.offset + 280));
    decal.color_map = buffer.Load(ref.offset + 284);
    decal.normal_map = buffer.Load(ref.offset + 288);
    return decal;
}

cbuffer DecalConstants : register(b0) {
    ArenaRef g_decals;
    ArenaRef g_light;
    uint2 g_pad;
};

cbuffer ArenaConstants : register(b1) {
    ArenaTable g_arena;
};

cbuffer DecalTextures : register(b2) {
    // The scene's depth, which the decal reconstructs a view position from.
    uint g_scene_depth;
    uint3 g_scene_pad;
};

struct PsInput {
    float4 position : SV_Position;
    nointerpolation uint decal : TEXCOORD0;
};

struct PsOutput {
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 surface : SV_Target2;
};

PsOutput main(PsInput input) {
    PsOutput output;
    const Light light = LoadLight(g_light, g_arena);
    const WorldDecal decal = LoadWorldDecalAt(ArenaAdd(g_decals, input.decal * WORLD_DECAL_BYTES), g_arena);

    const int2 pixel = int2(input.position.xy);
    const float depth = FetchTexel(g_scene_depth, pixel, 0).r;
    // Reverse depth to the game's OpenGL depth.
    const float distance_to_eye =
        1.0 / (light.inverse_projection.z * (1.0 - 2.0 * depth) + light.inverse_projection.w);
    const float2 uv_on_screen = input.position.xy / float2(FetchTexelSize(g_scene_depth));
    const float2 ndc = float2(2.0 * uv_on_screen.x - 1.0, 1.0 - 2.0 * uv_on_screen.y);
    const float3 position =
        float3(light.inverse_projection.xy * distance_to_eye * ndc, -distance_to_eye);

    const float3 place = mul(decal.view_to_decal, float4(position, 1.0)).xyz;
    const float4 normal_transform = decal.normal_transform;
    const uint normal_map = decal.normal_map;
    float2 normal_uv = Transformed(normal_transform, place.xy);
    const float4 parallax = decal.parallax;
    if (parallax.w > 0.0 && normal_map != 0u) {
        const float3 view = normalize(position);
        // GLSL's mat3(view_to_decal) * view is the upper-left 3x3 in column-major
        // order, which is mul(m3, view) here.
        const float3x3 frame = float3x3(decal.view_to_decal[0].xyz, decal.view_to_decal[1].xyz,
                                        decal.view_to_decal[2].xyz);
        const float3 along = mul(frame, view);
        normal_uv = Parallax(normal_map, normal_uv, parallax.w * along.xy / -along.z,
                             dot(parallax.xyz, view));
    }
    const float2 uv = (normal_uv - normal_transform.xy) / normal_transform.zw;
    const float4 color =
        SampleTex(decal.color_map, Transformed(decal.color_transform, uv)) * decal.color_scale;
    const float3 tangent_normal = normal_map != 0u
                                      ? 2.0 * SampleTex(normal_map, normal_uv).rgb - 1.0
                                      : float3(0.0, 0.0, 1.0);

    // After the samples, whose derivatives need every pixel of their quad. The
    // box holds x from 0 to 1, y from -1 to 0 and z from 0 to 1.
    if (depth <= 0.0 || any(abs(place - float3(0.5, -0.5, 0.5)) > 0.5)) {
        discard;
    }

    const float3x3 decal_frame = float3x3(decal.world_view[0].xyz, decal.world_view[1].xyz,
                                          decal.world_view[2].xyz);
    const float3 n = normalize(-mul(decal_frame, decal.normal_scale.xyz * tangent_normal));
    output.albedo = color;
    output.normal = float4(PackNormal(n), 0.0, color.a);
    output.surface = float4(decal.roughness, decal.metallic, decal.translucency, color.a);
    return output;
}
