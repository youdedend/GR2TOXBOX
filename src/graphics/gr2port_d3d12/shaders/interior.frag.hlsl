// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from interior.frag; SM6.4, HLSL 2021.
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
    float3 world_position : TEXCOORD0;
    float4 world_normal : TEXCOORD1;
    // Retain character.vert's physical signature/packing for PSO linkage.
    float4 tangent : TEXCOORD2;
    float2 texcoord : TEXCOORD3;
    float2 decal_texcoord : TEXCOORD4;
    float2 model_xy : TEXCOORD5;
    float4 vertex_color : TEXCOORD6;
    nointerpolation uint draw_index : TEXCOORD7;
};
static BatchLight light;
static float3 world_position;
static float4 world_normal;
static uint draw_index;
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// character.vert's outputs. This draw's world_view is its world matrix.

float4 CubeMap(uint handle, float3 direction) {
    if (handle == 0u) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    return BatchCube(handle, direction, 0.0);
}

// The share of a cube map's colour scale at a texel's alpha.
float Band(float alpha, float center) {
    return 1.0 - min(6.0 * abs(alpha - center), 1.0);
}

// The game's G2_CH_Interior pixel program: two cube maps seen from inside spheres around a point_light,
// the first turning with time, under a Fresnel-weighted reflection of the ambient radiance.
float4 main(StageIO input) : SV_Target0 {
    world_position = input.world_position;
    world_normal = input.world_normal;
    draw_index = input.draw_index;
    light = LoadBatchLight(g_light, g_arena);
    const Draw draw = LoadDrawAt(ArenaAdd(g_scene_draws, draw_index * DRAW_BYTES), g_arena);
    float4 color;

    const uint index = draw.material - 1u;
    const ArenaRef ref = ArenaAdd(g_scene_characters, index * 656u);
    const BatchInteriorConstants m = LoadBatchInteriorConstants(ref, g_arena);
    const BatchCharacter material = LoadBatchCharacter(ref, g_arena);

    const float3 ray = normalize(world_position - m.view.xyz);
    const float3 center = m.center.xyz;
    const float3 closest = world_position + ray * dot(center - world_position, ray);
    const float off_axis = dot(center - closest, center - closest);
    const float3 inner0 = normalize(closest + ray * sqrt(m.size_squared[0] + off_axis) - center);
    const float3 inner1 = normalize(closest + ray * sqrt(m.size_squared[1] + off_axis) - center);
    const float4 a = CubeMap(material.normal_map, mul((float3x3)m.rotation, inner0));
    const float4 b = CubeMap(material.reflection, inner1);
    const float3 inner = a.rgb * (1.0 + m.band[0].rgb * Band(a.a, m.band[0].w)) +
                       b.rgb * (1.0 + m.band[1].rgb * Band(b.a, m.band[1].w));

    const float3 n = normalize(world_normal.xyz);
    const float n_dot_v = min(max(-dot(ray, n), 0.0), 1.0);
    const float rough = m.roughness;
    const float gloss = 1.0 - rough;
    const float k =
        1.04 * (min(exp2(-9.28 * n_dot_v), gloss * gloss) * gloss - 0.0275 * rough) + 0.0442;
    const float3 env = m.reflectance.rgb * (1.04 - 0.572 * rough - k) +
                     (0.022 * rough - 0.04 + k) * clamp(50.0 * m.reflectance.g, 0.0, 1.0);
    const float lod = clamp(6.0 + 1.15 * log2(max(rough, 1e-6)), 0.0, 6.5);
    float3 radiance = float3(0.0, 0.0, 0.0);
    if (light.radiance[0] != 0u) {
        radiance =
            BatchCube(light.radiance[0], reflect(ray, n), lod)
                .rgb;
    }
    const float3 fresnel =
        m.fresnel_floor + (1.0 - m.fresnel_floor) * pow(max(1.0 - n_dot_v, 0.0), 5.0);
    const float3 share = m.fresnel_scale * max(fresnel - m.fresnel_bias, float3(0.0, 0.0, 0.0));
    color = float4(inner + (radiance * env - inner) * share, 0.0);
    return color;
}
