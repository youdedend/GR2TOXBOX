// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from glass.vert; SM6.4, HLSL 2021.
#include "common/lighting_batch.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer GlassRoot : register(b1) {
    uint2 g_scene_draws_words; uint2 g_scene_materials_words; uint2 g_light_words;
    uint g_glass_output; uint g_base_instance;
};
#define g_scene_draws ArenaMake(g_scene_draws_words.x, g_scene_draws_words.y)
#define g_scene_materials ArenaMake(g_scene_materials_words.x, g_scene_materials_words.y)
#define g_light ArenaMake(g_light_words.x, g_light_words.y)
cbuffer GlassTextures : register(b2) {
    uint g_shadow; uint g_last_shadow; uint g_behind_glass; uint g_glass_unused;
};
struct StageIO {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float4 tangent : TEXCOORD1;
    float3 view_position : TEXCOORD2;
    float2 texcoord : TEXCOORD3;
    nointerpolation uint draw_index : TEXCOORD4;
};
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The output.tangent, and in w the sign of the bitangent.

// The game's G2_BG_Glass vertex programs (0x1b14820, skinned 0x1b14ebc): the view-space frame and
// position. The pixel shader finds the shadow coordinates from the position.
StageIO main(uint vertex_index : SV_VertexID, uint instance : SV_InstanceID) {
    StageIO output;
    const uint draw_index = g_base_instance + instance;
    const Draw draw = LoadDrawAt(ArenaAdd(g_scene_draws, draw_index * DRAW_BYTES), g_arena);

    output.draw_index = draw_index;
    const ArenaRef vertices = draw.vertices;
    const uint stride = draw.stride;
    const uint bones = draw.bones;
    const ArenaRef skin = draw.skin;
    const ArenaRef palette = draw.palette;
    const float3 unskinned = LoadPosition(vertices, stride, draw.position, vertex_index, g_arena);
    const float3 model = bones != 0u ? SkinPosition(unskinned, skin, palette, bones, vertex_index, g_arena) : unskinned;
    const float4x4 world_view = draw.world_view;
    output.view_position = (mul(world_view, float4(model, 1.0))).xyz;
    float4 clip = mul(draw.world_view_projection, float4(model, 1.0));
    // OpenGL depth, -w near to w far, to reverse depth, 1 near to 0 far.
    // Depth conversion is already part of the uploaded D3D projection.
    output.position = clip;
    output.texcoord = LoadTexcoord(vertices, stride, draw.texcoord, vertex_index, g_arena);
    const float3 n = LoadNormal(vertices, stride, draw.normal, vertex_index, g_arena);
    const float3 turned_normal = bones != 0u ? SkinDirection(n, skin, palette, bones, vertex_index, g_arena) : n;
    output.normal = normalize(mul((float3x3)world_view, turned_normal));
    const float4 t = LoadTangent(vertices, stride, draw.tangent, vertex_index, g_arena);
    const float3 turned_tangent = bones != 0u ? SkinDirection(t.xyz, skin, palette, bones, vertex_index, g_arena) : t.xyz;
    output.tangent = float4(normalize(mul((float3x3)world_view, turned_tangent)), t.w);
    return output;
}
