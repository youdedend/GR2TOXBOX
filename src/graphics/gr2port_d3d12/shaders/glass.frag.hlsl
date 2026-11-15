// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from glass.frag; SM6.4, HLSL 2021.
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
static BatchLight light;
static float3 normal;
static float4 tangent;
static float3 view_position;
static float2 texcoord;
static uint draw_index;
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the program writes: the pane over the refracted copy of the scene, as the game draws it;
// or, for the two blends that need no copy, the share of the scene that shows through the pane,
// then the pane's own light.

static const uint RefractedOutput = 0u;
static const uint ThroughOutput = 1u;

static const float Pi = 3.14159265;

// How much of the key light reaches the point_light: one tap of its cascade.
float KeyShadow() {
    const float depth = -view_position.z;
    const uint cascade = depth < light.split_depths.y ? 0u : depth < light.split_depths.z ? 1u : 2u;
    const float4 clip = mul(light.shadow_projection[cascade], float4(view_position, 1.0));
    const float3 coord = clip.xyz / clip.w;
    if (cascade == 2u) {
        return BatchShadow(2u, g_shadow, g_last_shadow, coord);
    }
    return BatchShadow(cascade, g_shadow, g_last_shadow, float3(coord.xy, coord.z));
}

// Where a ray that refracts at the surface lands on the screen.
float2 Refracted(float3 ray, float3 n, float ratio, float2 projection) {
    const float3 bent = refract(ray, n, ratio);
    return float2(0.5, 0.5) + float2(0.5, -0.5) * projection * bent.xy / -bent.z;
}

// The game's G2_BG_Glass pixel program (0x1b8712c): the scene behind the pane, refracted through
// its normal map and tinted, under a Fresnel-weighted reflection of the ambient radiance and the
// key light's highlight.
float4 main(StageIO input) : SV_Target0 {
    normal = input.normal;
    tangent = input.tangent;
    view_position = input.view_position;
    texcoord = input.texcoord;
    draw_index = input.draw_index;
    light = LoadBatchLight(g_light, g_arena);
    const Draw draw = LoadDrawAt(ArenaAdd(g_scene_draws, draw_index * DRAW_BYTES), g_arena);
    float4 color;

    const uint index = draw.material - 1u;
    const BatchGlassMaterial material = LoadBatchGlassMaterial(ArenaAdd(g_scene_materials, index * 192u), g_arena);
    const float3 ray = normalize(view_position);
    const float3 face_normal = normalize(normal);
    const float3 face_tangent = normalize(tangent.xyz);
    const float3 face_bitangent = sign(tangent.w) * cross(face_normal, face_tangent);
    const uint normal_map = draw.texture;
    const float2 normal_uv = (material.normal_transform.xy + material.normal_transform.zw * texcoord);
    const float3 texel = normal_map != 0u ? SampleTex(normal_map, normal_uv).rgb : float3(0.5, 0.5, 1.0);
    const float normal_scale = material.normal_scale;
    const float3 surface_normal = normalize(float3(normal_scale * (2.0 * texel.xy - 1.0), texel.z));
    const float3 n = normalize(surface_normal.x * face_tangent + surface_normal.y * face_bitangent +
                             surface_normal.z * face_normal);
    const float n_dot_v = min(max(-dot(ray, n), 0.0), 1.0);
    const float3 reflectance = material.reflectance.rgb;
    const float3 share = reflectance + (1.0 - reflectance) * pow(max(1.0 - n_dot_v, 0.0), 5.0);
    if (g_glass_output == ThroughOutput) {
        // What the reflection leaves of the scene, tinted.
        color = float4((1.0 - share) * material.translucency.rgb, 1.0);
        return color;
    }

    // In the light pass the target already holds the scene through the pane, so nothing is read.
    float3 behind = float3(0.0, 0.0, 0.0);
    if (g_glass_output == RefractedOutput) {
        const float4 refraction = material.refraction;
        const float2 projection = material.projection;
        if (refraction.w > 0.0) {
            // Each colour bends by its own ratio.
            behind.r = BatchTexture(g_behind_glass, Refracted(ray, n, refraction.r, projection), 0.0).r;
            behind.g = BatchTexture(g_behind_glass, Refracted(ray, n, refraction.g, projection), 0.0).g;
            behind.b = BatchTexture(g_behind_glass, Refracted(ray, n, refraction.b, projection), 0.0).b;
        } else {
            behind = BatchTexture(g_behind_glass, Refracted(ray, n, refraction.r, projection), 0.0).rgb;
        }
    }

    // Key light: GGX and Schlick.
    const float3 travel = material.light_direction.xyz;
    const float3 halfway = normalize(-travel - ray);
    const float n_dot_l = min(max(-dot(travel, n), 0.0), 1.0);
    const float n_dot_h = clamp(dot(n, halfway), 0.0, 1.0);
    const float rough = max(material.roughness, 1e-6);
    const float a2 = rough * rough;
    const float d_term = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    const float ggx = a2 / (Pi * d_term * d_term);
    const float visibility =
        1.0 / ((n_dot_v + sqrt(a2 + (1.0 - a2) * n_dot_v * n_dot_v)) *
               (n_dot_l + sqrt(a2 + (1.0 - a2) * n_dot_l * n_dot_l)));
    const float highlight_fresnel = pow(clamp(1.0 + dot(ray, halfway), 0.0, 1.0), 5.0);
    const float3 highlight = (reflectance + (1.0 - reflectance) * highlight_fresnel) * ggx *
                           visibility * n_dot_l * KeyShadow() *
                           material.light_color.rgb;

    // The ambient radiance along the reflection, in world space.
    const float gloss = 1.0 - rough;
    const float k =
        1.04 * (min(exp2(-9.28 * n_dot_v), gloss * gloss) * gloss - 0.0275 * rough) + 0.0442;
    const float3 env = reflectance * (1.04 - 0.572 * rough - k) +
                     (0.022 * rough - 0.04 + k) * clamp(50.0 * reflectance.g, 0.0, 1.0);
    float3 radiance = float3(0.0, 0.0, 0.0);
    if (light.radiance[0] != 0u) {
        const float3x3 view_to_world = (float3x3)material.view_inverse;
        const float3 reflected =
            reflect(normalize(mul(view_to_world, ray)), normalize(mul(view_to_world, n)));
        const float lod = clamp(6.0 + 1.15 * log2(rough), 0.0, 6.5);
        radiance =
            BatchCube(light.radiance[0], reflected, lod).rgb;
    }

    const float3 seen = material.translucency.rgb * behind;
    color = float4(seen + share * (radiance * env + highlight - seen), 1.0);
    return color;
}
