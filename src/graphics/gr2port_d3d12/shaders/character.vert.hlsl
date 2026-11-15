// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/lighting_batch.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer CharacterRoot : register(b1) {
    uint2 g_scene_draws_words;
    uint2 g_scene_characters_words;
    uint2 g_scene_decals_words;
    uint2 g_light_words;
    float2 g_target_inverse;
    uint2 g_lights_words;
    uint2 g_grid_words;
    uint g_glow_pass;
    uint g_base_instance;
};
#define g_scene_draws ArenaMake(g_scene_draws_words.x, g_scene_draws_words.y)
#define g_scene_characters ArenaMake(g_scene_characters_words.x, g_scene_characters_words.y)
#define g_scene_decals ArenaMake(g_scene_decals_words.x, g_scene_decals_words.y)
#define g_light ArenaMake(g_light_words.x, g_light_words.y)
#define g_lights ArenaMake(g_lights_words.x, g_lights_words.y)
#define g_grid ArenaMake(g_grid_words.x, g_grid_words.y)
cbuffer CharacterTextures : register(b2) {
    uint g_shadow; uint g_last_shadow;
    uint self_translucent_color; uint self_translucent_depth;
};
struct StageIO {
    float4 position : SV_Position;
    float3 view_position : TEXCOORD0;
    float4 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 texcoord : TEXCOORD3;
    float2 decal_texcoord : TEXCOORD4;
    float2 model_xy : TEXCOORD5;
    float4 vertex_color : TEXCOORD6;
    nointerpolation uint draw_index : TEXCOORD7;
};
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The normal, and in w the rim's share of the bend.
// The tangent, and in w the sign of the bitangent.

// Where a model position falls on a decal, and in z its squared distance from the decal's position.
// The decal faces its target direction with its right side horizontal.
float3 OnDecal(float3 model, float3 position, float3 target, float2 scale) {
    const float3 direction = normalize(target);
    const float3 right = normalize(float3(direction.z, 0.0, -direction.x));
    const float3 up = normalize(cross(direction, right));
    const float3 offset = model - position;
    return float3(scale.x * dot(offset, right) + 0.5, 0.5 - scale.y * dot(offset, up),
                dot(offset, offset));
}

// The nearest decal's coordinates, from the unskinned position.
float2 DecalTexcoord(float3 model, uint index) {
    const BatchDecal decal = LoadBatchDecal(ArenaAdd(g_scene_decals, index * 144u), g_arena);
    float3 nearest = OnDecal(model, decal.positions[0].xyz,
                           decal.targets[0].xyz,
                           decal.scales[0]);
    for (uint loop_index1 = 1u; loop_index1 < 3u; loop_index1++) {
        const float3 other = OnDecal(model, decal.positions[loop_index1].xyz,
                                   decal.targets[loop_index1].xyz,
                                   decal.scales[loop_index1]);
        if (nearest.z >= other.z) {
            nearest = other;
        }
    }
    return nearest.xy;
}

// The game's G2_CH_Main vertex programs: the view-space frame, with the normal bent toward a point_light
// past the material's distance from it, the decal's coordinates and the model's height for the
// reflection map.
StageIO main(uint vertex_index : SV_VertexID, uint instance : SV_InstanceID) {
    StageIO output;
    const uint draw_index = g_base_instance + instance;
    const Draw draw = LoadDrawAt(ArenaAdd(g_scene_draws, draw_index * DRAW_BYTES), g_arena);
    const BatchCharacter material = LoadBatchCharacter(ArenaAdd(g_scene_characters, (draw.material - 1u) * 656u), g_arena);

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

    const float4 transform = draw.texcoord_transform;
    output.texcoord = transform.xy + transform.zw * LoadTexcoord(vertices, stride, draw.texcoord, vertex_index, g_arena);
    const uint decal_index = draw.decal;
    output.decal_texcoord = decal_index != 0u ? DecalTexcoord(unskinned, decal_index - 1u) : float2(0.0);
    output.model_xy = unskinned.xy;

    const uint normal_element = draw.normal;
    float3 n = (normal_element >> 8) != 0u ? LoadNormal(vertices, stride, normal_element, vertex_index, g_arena)
                                         : float3(0.0, 0.0, 1.0);
    n = bones != 0u ? SkinDirection(n, skin, palette, bones, vertex_index, g_arena) : n;
    n = mul((float3x3)world_view, n);
    float rim = 1.0;
    const uint index = draw.material - 1u;
    const float bias = material.bend_bias;
    if (bias > 0.0) {
        const float3 offset = output.view_position - material.bend_position.xyz;
        const float offset_length = length(offset);
        const float range = material.bend_range;
        const float past = max(min(range * offset_length, 1.0) - bias, 0.0);
        const float share = past / (1.0 - bias);
        rim = min(share + 0.3, 1.0);
        n = lerp(offset / offset_length, n, share);
    }
    output.normal = float4(normalize(n), rim);

    const uint tangent_element = draw.tangent;
    output.tangent = float4(1.0, 0.0, 0.0, 1.0);
    if ((tangent_element >> 8) != 0u) {
        const float4 t = LoadTangent(vertices, stride, tangent_element, vertex_index, g_arena);
        const float3 turned = bones != 0u ? SkinDirection(t.xyz, skin, palette, bones, vertex_index, g_arena) : t.xyz;
        output.tangent = float4(normalize(mul((float3x3)world_view, turned)), -t.w);
    }
    const uint color_element = draw.vertex_color;
    output.vertex_color = (color_element >> 8) != 0u ? LoadVertexColor(vertices, stride, color_element, vertex_index, g_arena)
                                              : float4(1.0);
    return output;
}
