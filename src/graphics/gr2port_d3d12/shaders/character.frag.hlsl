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
static BatchLight light;
static float3 view_position;
static float4 normal;
static float4 tangent;
static float2 texcoord;
static float2 decal_texcoord;
static float2 model_xy;
static float4 vertex_color;
static uint draw_index;
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

static const float2 FirstCascadeTaps[9] = {float2(6.7226e-4, 7.6843e-4), float2(3.3259e-4, 3.0164e-4),
                                         float2(-2.7996e-4, 8.6433e-4), float2(-2.1857e-4, 1.2991e-4),
                                         float2(-6.9220e-4, 4.6003e-4), float2(-1.0455e-3, 3.3551e-6),
                                         float2(-2.2026e-4, -6.5124e-4), float2(2.0574e-4, -2.6155e-4),
                                         float2(7.5692e-4, -8.9779e-5)};
static const float2 SecondCascadeTaps[4] = {float2(1.3e-4, 3.64e-4), float2(3.56e-4, 1.01e-4),
                                          float2(-2.24e-4, 7.96e-5), float2(2.02e-4, -4.23e-4)};

float4 Sample(uint handle, float2 uv) {
    return SampleTex(handle, uv);
}

float CascadeTap(uint cascade, float3 coord, float2 offset) {
    if (cascade == 2u) {
        return BatchShadow(2u, g_shadow, g_last_shadow, float3(coord.xy + offset, coord.z));
    }
    return BatchShadow(cascade, g_shadow, g_last_shadow, float3(coord.xy + offset, coord.z));
}

// How much of the key light reaches the point_light: nine taps in the first cascade, four in the second
// and one in the last.
float KeyShadow() {
    const float depth = -view_position.z;
    const uint cascade = depth < light.split_depths.y ? 0u : depth < light.split_depths.z ? 1u : 2u;
    const float4 clip = mul(light.shadow_projection[cascade], float4(view_position, 1.0));
    const float3 coord = clip.xyz / clip.w;
    if (cascade == 2u) {
        return CascadeTap(cascade, coord, float2(0.0, 0.0));
    }
    float sum = 0.0;
    if (cascade == 0u) {
        for (uint loop_index1 = 0u; loop_index1 < 9u; loop_index1++) {
            sum += CascadeTap(cascade, coord, FirstCascadeTaps[loop_index1]);
        }
        return sum / 9.0;
    }
    for (uint loop_index2 = 0u; loop_index2 < 4u; loop_index2++) {
        sum += CascadeTap(cascade, coord, SecondCascadeTaps[loop_index2]);
    }
    return 0.25 * sum;
}

// The share past a soft edge, by the game's smooth curve.
float SoftStep(float start, float end, float value) {
    const float t = clamp((value - start) / (end - start), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The ambient light on a world-space normal: spherical harmonics in the game's seven constants.
float3 AmbientOn(float3 n) {
    const float4 linear_value = float4(n, 1.0);
    const float4 quadratic = float4(n.x * n.y, n.y * n.z, n.z * n.z, n.x * n.z);
    return float3(dot(light.sh[0], linear_value), dot(light.sh[1], linear_value), dot(light.sh[2], linear_value)) +
           float3(dot(light.sh[3], quadratic), dot(light.sh[4], quadratic),
                dot(light.sh[5], quadratic)) +
           light.sh[6].rgb * (n.x * n.x - n.y * n.y);
}

// The game's G2_CH_Main pixel programs, in the order they run: bands of the toon light, ambient
// light, point_light lights, camera light, reflection, detail, highlight, rim, self-translucency.
float4 main(StageIO input) : SV_Target0 {
    view_position = input.view_position;
    normal = input.normal;
    tangent = input.tangent;
    texcoord = input.texcoord;
    decal_texcoord = input.decal_texcoord;
    model_xy = input.model_xy;
    vertex_color = input.vertex_color;
    draw_index = input.draw_index;
    light = LoadBatchLight(g_light, g_arena);
    const Draw draw = LoadDrawAt(ArenaAdd(g_scene_draws, draw_index * DRAW_BYTES), g_arena);
    const BatchCharacter material = LoadBatchCharacter(ArenaAdd(g_scene_characters, (draw.material - 1u) * 656u), g_arena);
    float4 color;

    const uint index = draw.material - 1u;
    const uint color_handle = draw.texture;
    const uint normal_handle = material.normal_map;
    const float2 uv = texcoord;
    const float4 color_map = Sample(color_handle, uv);
    float mask = color_map.a;
    if (material.alpha_step != 0u) {
        const float2 height_uv =
            material.normal_offset + material.normal_repeat * uv;
        const float height = pow(abs(Sample(normal_handle, height_uv).a), 2.2);
        if (material.alpha_reference >= height * mask) {
            discard;
        }
        mask = 1.0;
    }
    // A screen door fades instances in, each with its own pattern.
    const float2 punch = material.punch_through;
    const float seed =
        256.0 * punch.x * (0.0771356 * input.position.x + 0.00888381 * input.position.y) + 1e-7;
    if (punch.y < frac(frac(abs(123456792.0 / seed)) * abs(seed))) {
        discard;
    }

    const float3 detail =
        Sample(material.detail, material.detail_repeat * uv).rgb;
    const float detail_value = (detail.r + detail.g + detail.b) / 3.0;
    const float detail_mask =
        max(1.0 - material.detail_mask * detail_value, 0.0);
    const float shadow = KeyShadow();

    // The normal map's strength follows the detail map.
    const float strength =
        material.detail_border * (100.0 * detail_value - 50.0) + 1.0;
    // A material without a normal map holds a constant texture.
    float4 normal_texel = float4(128.0 / 255.0, 128.0 / 255.0, 1.0, 1.0);
    if (normal_handle != 0u) {
        normal_texel = Sample(normal_handle, uv);
    }
    const float roughness_map = normal_texel.a * vertex_color.x;
    const float3 surface_normal = normalize(
        float3(strength * (2.0 * normal_texel.rg - 1.0), normal_texel.b));
    const float3 t = normalize(tangent.xyz);
    const float3 base_normal = normalize(normal.xyz);
    const float3 b = cross(base_normal, t) * sign(tangent.w);
    const float3 n = normalize(t * surface_normal.x + b * surface_normal.y +
                             base_normal * surface_normal.z);

    // Bands of the toon light, over half Lambert.
    const float3 travel = material.light_direction.xyz;
    const float half_lambert = 0.5 - 0.5 * dot(travel, n);
    const float facing = max(n.z, 0.0);
    const float facing8 = pow(facing, 8.0);
    const float highlight_center = 1.0 + material.highlight_fresnel * facing8 -
                                   (1.0 - material.highlight_border) * roughness_map;
    const float highlight_soft =
        max(material.highlight_smooth * roughness_map, 1e-4);
    const float highlight = SoftStep(highlight_center - highlight_soft,
                                     highlight_center + highlight_soft, half_lambert);
    const float diffuse =
        SoftStep(material.diffuse_border - material.diffuse_smooth,
                 material.diffuse_border + material.diffuse_smooth,
                 half_lambert);
    const float darkened = material.mask_density * (1.0 - shadow);
    const float3 ambient_color = material.ambient_color.rgb;
    const float3 tone = lerp(lerp(material.diffuse_color.rgb, ambient_color, darkened),
                          lerp(material.highlight_color.rgb, ambient_color, darkened),
                          highlight);
    const float3 light_color = material.light_color.rgb;
    const float light_scale = material.light_color.a;
    const float3 lit_tone = light_color * tone;
    const float3 line_value = lerp(0.5 * (ambient_color + lit_tone), ambient_color * (0.89 - 0.39 * shadow),
                          material.diffuse_line);
    const float3 toon = lerp(lerp(lit_tone, line_value, min(2.0 - 2.0 * diffuse, 1.0)), ambient_color,
                          2.0 * max(0.5 - diffuse, 0.0));

    // The surface colour, drawn to the gradation where the vertex colour's z is low, with its
    // decal. A material without a decal file holds a white constant texture.
    const float4 gradation = material.gradation_color;
    float3 albedo = lerp(color_map.rgb * material.color_scale.rgb, gradation.rgb,
                      gradation.a * (1.0 - vertex_color.z));
    float4 decal = float4(1.0, 1.0, 1.0, 1.0);
    const uint decal_index = draw.decal;
    if (decal_index != 0u) {
        const uint handle = LoadBatchDecal(ArenaAdd(g_scene_decals, (decal_index - 1u) * 144u), g_arena).texture;
        decal = BatchTexture(handle, decal_texcoord, 0.0);
    }
    albedo *= lerp(float3(1.0, 1.0, 1.0), lerp(decal.rgb, gradation.rgb, gradation.a), decal.a * mask);

    // In shadow, the shadow colour carries a share of the surface's colour.
    const float3 shadow_color = material.shadow_color.rgb;
    const float3 in_shadow =
        shadow_color * (1.0 + material.shadow_gradation * (albedo - 1.0));
    const float lit_share = SoftStep(
        clamp(material.shadow_border - material.shadow_smooth, 0.0,
              1.0),
        material.shadow_border + material.shadow_smooth,
        half_lambert + material.shadow_fresnel * n.z * n.z);
    float3 result = light_scale * lerp(in_shadow, albedo * toon, lit_share);
    result += material.ibl_blend * albedo *
              AmbientOn(normalize(mul((float3x3)material.view_inverse, n)));

    // Point lights from the pixel's cell of the grid: each wraps round the surface as the camera's
    // light does and fades over the end of its range, less where the key light is shadowed or
    // faces away.
    if (light.point_lights > 0u) {
        const uint first =
            GridCellFirst(input.position.xy * abs(g_target_inverse), -view_position.z,
                          1.0 / (light.inverse_projection.w - light.inverse_projection.z));
        const float key_lit = max(-dot(travel, n), 0.0) * shadow;
        const float key_reach = 1.0 + material.shadow_influence * (key_lit - 1.0);
        const float scale = material.light_intensity_scale * key_reach;
        for (uint loop_index3 = 0u; loop_index3 < light.point_lights; loop_index3++) {
            const uint light_index = ArenaLoad(ArenaAdd(g_grid, (first + loop_index3) * 4u), g_arena);
            if (light_index == ~0u) {
                break;
            }
            const BatchPointLight point_light = LoadBatchPointLight(ArenaAdd(g_lights, light_index * 48u), g_arena);
            const float3 offset = point_light.position - view_position;
            const float reach = length(offset);
            const float wrap =
                clamp(3.3333333 * (dot(offset, n) / reach + 1.0) - 2.3333333, 0.0, 1.0);
            result += scale * wrap * wrap * (3.0 - 2.0 * wrap) * PointFade(point_light, reach) *
                      point_light.color * albedo;
        }
    }

    // The camera's light.
    const float distance_to_eye = length(view_position);
    const float3 to_eye = -view_position / distance_to_eye;
    const float camera_wrap = clamp(3.3333333 * (dot(n, to_eye) + 1.0) - 2.3333333, 0.0, 1.0);
    const float camera_reach =
        min(max(distance_to_eye - material.camera_light_inner, 0.0) /
                (material.camera_light_outer -
                 material.camera_light_inner),
            1.0);
    const float camera_share =
        camera_wrap * camera_wrap * (3.0 - 2.0 * camera_wrap) *
        (1.0 - pow(abs(camera_reach), material.camera_light_attenuation));
    const float camera_facing =
        material.camera_light_directivity * (max(dot(travel, to_eye), 0.0) - 1.0) +
        1.0;
    result +=
        camera_facing * camera_share * material.camera_light_color.rgb * albedo;
    result = max(result, float3(0.0, 0.0, 0.0));

    // The reflection map, on the model or on the screen.
    const float2 distortion = material.reflection_distortion * n.xy;
    const int reflection_mode = material.reflection_mode;
    const float repeat = material.reflection_repeat;
    const float2 reflection_uv =
        reflection_mode == 0
            ? material.reflection_offset + repeat * (model_xy + distortion)
            : repeat * (distortion + input.position.xy * g_target_inverse);
    // A material without a reflection map holds a black constant texture.
    const uint reflection_map = material.reflection;
    const float3 reflection =
        reflection_map != 0u ? Sample(reflection_map, reflection_uv).rgb : float3(0.0, 0.0, 0.0);
    const float reflection_share =
        light_scale * material.reflection_blend * mask * detail_mask;
    if (reflection_mode == 1) {
        result += reflection_share * (2.0 * reflection * result - result);
    } else {
        result += reflection_share * reflection;
    }

    // The detail map overlays the colour, less so in the light.
    result += material.detail_blend * vertex_color.w * (1.0 - 0.5 * diffuse) *
              (2.0 * detail * result - result);

    // The highlight and the rim.
    const float half_z = normalize(n - travel).z;
    const float highlight_value =
        pow(max(half_z, 0.0), 40.0) -
        2.0 * material.specular_fresnel * pow(facing8, 4.0);
    const float specular = SoftStep(
        material.specular_border - material.specular_smooth,
        material.specular_border + material.specular_smooth,
        highlight_value);
    const float3 specular_color = material.specular_color.rgb * diffuse;
    result += min(result, specular_color) * diffuse * specular * detail_mask * vertex_color.x;
    const float rim_light =
        min(material.rim_lighting * max(travel.z, 0.0), 1.0) * normal.w;
    const float rim =
        SoftStep(material.rim_border - material.rim_smooth,
                 material.rim_border + material.rim_smooth,
                 rim_light * max(1.0 - n.z, 0.0));
    result += (1.8 * result + 0.2) * light_color * material.rim_intensity * rim;
    if (draw.vertex_color != 0u) {
        result = lerp(shadow_color * material.vertex_shadow, result, vertex_color.y);
    }

    // The colour or alpha of a glow. The alpha is the colour map's, which picks the bloom blur's
    // kernel in a glow draw.
    const uint bloom_mode = material.bloom_fresnel_mode;
    float glow_alpha = mask;
    if (bloom_mode != 0u) {
        const float glow = SoftStep(
            material.bloom_fresnel_border -
                material.bloom_fresnel_smooth,
            material.bloom_fresnel_border +
                material.bloom_fresnel_smooth,
            1.0 - n.z * n.z);
        if (bloom_mode == 1u) {
            result *= glow;
        } else {
            glow_alpha *= glow;
        }
    }

    // What SelfTranslucent drew shows through the skin, by how much skin lies over it.
    const float translucency = material.self_translucency;
    if (translucency > 0.0) {
        // Its images are the scene's size, and a glow draw's target is smaller.
        const float2 size = float2(BatchSize(self_translucent_color));
        const int2 pixel = int2(input.position.xy * abs(g_target_inverse) * size);
        const float3 inside =
            material.self_translucency_scale *
            FetchTexel(self_translucent_color, pixel, 0).rgb;
        // Its reverse depth as the game's depth, then as a distance.
        const float game_depth =
            1.0 - FetchTexel(self_translucent_depth, pixel, 0).r;
        const float2 inverse_z = material.inverse_z;
        const float inside_distance = 1.0 / (inverse_z.y + inverse_z.x * (2.0 * game_depth - 1.0));
        const float start = smoothstep(
            0.0, 1.0, inside_distance / material.self_translucency_start);
        const float skin = smoothstep(0.0, 1.0,
                                      (inside_distance + view_position.z) / start /
                                          material.self_translucency_depth);
        const float share =
            translucency * pow(max(n.z, 0.0), material.self_translucency_power) *
            clamp(1.0 + material.self_translucency_offset - skin, 0.0, 1.0);
        result = material.self_translucency_mode != 0u ? result + share * inside
                                                                      : lerp(result, inside, share);
    }
    color = float4(result, 0.0);
    // A glow draw takes the game's blend constant.
    if (g_glow_pass != 0u) {
        color = float4(result * material.glow, glow_alpha);
    }
    return color;
}
