// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/lighting_batch.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer DeferredRoot : register(b1) {
    uint2 g_light_words; uint2 g_lights_words; uint2 g_grid_words; uint2 g_texture_record_words;
};
#define g_light ArenaMake(g_light_words.x, g_light_words.y)
#define g_lights ArenaMake(g_lights_words.x, g_lights_words.y)
#define g_grid ArenaMake(g_grid_words.x, g_grid_words.y)
#define g_texture_record ArenaMake(g_texture_record_words.x, g_texture_record_words.y)
cbuffer DeferredTextures : register(b2) {
    uint g_shadow; uint g_last_shadow; uint g_shadow_depth; uint g_last_shadow_depth;
};
#define g_scene_depth ArenaLoad(ArenaAdd(g_texture_record, 0u), g_arena)
#define g_surface_albedo ArenaLoad(ArenaAdd(g_texture_record, 4u), g_arena)
#define g_surface_normal ArenaLoad(ArenaAdd(g_texture_record, 8u), g_arena)
#define g_surface_material ArenaLoad(ArenaAdd(g_texture_record, 12u), g_arena)
#define g_world_lines ArenaLoad(ArenaAdd(g_texture_record, 16u), g_arena)
#define g_occlusion ArenaLoad(ArenaAdd(g_texture_record, 20u), g_arena)
#define g_traced_shadow ArenaLoad(ArenaAdd(g_texture_record, 24u), g_arena)
static BatchLight light;
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

static const float Pi = 3.14159265;
static const float3 LumaWeights = float3(0.299, 0.587, 0.114);
static const float ShadowFadeRange = 0.1;
// The distance the depth under a surface is read behind it, in the first cascade and the others.
static const float ThicknessOffsets[2] = {0.012, 0.08};
static const float TranslucencyRange = 1.2;
static const float AmbientTranslucency = 2.0;
// The ambient occlusion fades out from this distance, over the inverse of the range.
static const float OcclusionStart = 100.0;
static const float OcclusionRange = 1.0 / 60.0;

float3 ShadowCoord(uint cascade, float3 position) {
    const float4 coord = mul(light.shadow_projection[cascade], float4(position, 1.0));
    return coord.xyz / coord.w;
}

float ShadowTap(uint cascade, float3 coord, float2 offset) {
    if (cascade == 2u) {
        return BatchShadow(2u, g_shadow, g_last_shadow, float3(coord.xy + offset, coord.z));
    }
    return BatchShadow(cascade, g_shadow, g_last_shadow, float3(coord.xy + offset, coord.z));
}

// How much of the key light reaches a point_light: four taps in the first two cascades, one in the last.
float KeyShadow(uint cascade, float3 position) {
    const float3 coord = ShadowCoord(cascade, position);
    if (cascade == 2u) {
        return ShadowTap(cascade, coord, float2(0.0, 0.0));
    }
    return 0.25 * (ShadowTap(cascade, coord, float2(1.3e-4, 3.64e-4)) +
                   ShadowTap(cascade, coord, float2(3.56e-4, 1.01e-4)) +
                   ShadowTap(cascade, coord, float2(-2.24e-4, 7.96e-5)) +
                   ShadowTap(cascade, coord, float2(2.02e-4, -4.23e-4)));
}

// The distance the key light travels through the surface to reach a point_light.
float Thickness(uint cascade, float3 position, float3 normal) {
    const float offset = ThicknessOffsets[min(cascade, 1u)];
    const float3 coord = ShadowCoord(cascade, position - offset * normal);
    const float depth = cascade == 2u
                            ? DescriptorDepth(g_last_shadow_depth).SampleLevel(DescriptorSampler(SMP_LINEAR_CLAMP), coord.xy, 0.0).xxxx.r
                            : DescriptorDepthArray(g_shadow_depth).SampleLevel(DescriptorSampler(SMP_LINEAR_CLAMP), float3(coord.xy, float(cascade)), 0.0).xxxx.r;
    const float4 caster =
        mul(light.inverse_shadow_projection[cascade], float4(coord.xy, depth, 1.0));
    return length(caster.xyz / caster.w - position);
}

// The share of light that passes through a thickness: a sum of six Gaussians.
float Transmittance(float s) {
    const float s2 = -s * s;
    return 0.233 * exp(s2 / 0.0064) + 0.1 * exp(s2 / 0.0484) + 0.118 * exp(s2 / 0.187) +
           0.113 * exp(s2 / 0.567) + 0.358 * exp(s2 / 1.99) + 0.078 * exp(s2 / 7.41);
}

float3 Irradiance(float3 direction) {
    if (light.irradiance[0] == 0u) {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 first =
        BatchCube(light.irradiance[0], direction, 0.0).rgb;
    return light.irradiance[1] == 0u
               ? first
               : lerp(first,
                     BatchCube(light.irradiance[1], direction, 0.0)
                         .rgb,
                     light.ibl_blend);
}

float3 Radiance(float3 direction, float lod) {
    if (light.radiance[0] == 0u) {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 first =
        BatchCube(light.radiance[0], direction, lod).rgb;
    return light.radiance[1] == 0u
               ? first
               : lerp(first,
                     BatchCube(light.radiance[1], direction, lod)
                         .rgb,
                     light.ibl_blend);
}

// What the occluders leave of the ambient light at a position.
float OccluderShade(float3 position, float strength) {
    float shade = 1.0;
    for (uint loop_index1 = 0u; loop_index1 < light.occluders; loop_index1++) {
        const float4 sphere = light.occluder_spheres[loop_index1];
        const float3 offset = position - sphere.xyz;
        const float along = dot(light.occluder_direction.xyz, offset);
        const float outside = dot(offset, offset) - sphere.w * sphere.w;
        const float clear = along > 0.0
                                ? (outside >= -0.0001 ? 1.0 : 0.0)
                                : clamp(light.occluder_penumbra / along * (along * along - outside),
                                        0.0, 1.0);
        const float blocked = max(0.8 - clear, 0.0) *
                              (1.0 - min(light.occluder_range * abs(along), 1.0));
        shade *= 1.0 - blocked * strength;
    }
    return shade;
}

// Bands above 1 / toning: each whole step keeps only the top of its range.
float3 Tone(float3 c) {
    const float3 t = c * light.toning;
    const float3 f = frac(t);
    const float3 f2 = f * f;
    return lerp(t, t - f + f2 * f2, step(float3(1.0, 1.0, 1.0), t)) / light.toning;
}

// The game's LightPass, IBL and Resolve programs in one pass.
float4 main(float4 position_in : SV_Position) : SV_Target0 {
    light = LoadBatchLight(g_light, g_arena);
    float4 color;

    const int2 pixel = int2(position_in.xy);
    const float depth = FetchTexel(g_scene_depth, pixel, 0).r;
    if (depth <= 0.0) {
        discard;
    }
    const float2 uv = position_in.xy / float2(BatchSize(g_scene_depth));
    // Reverse depth to the game's OpenGL depth.
    const float distance_to_eye =
        1.0 / (light.inverse_projection.z * (1.0 - 2.0 * depth) + light.inverse_projection.w);
    const float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    const float3 position =
        float3(light.inverse_projection.xy * distance_to_eye * ndc, -distance_to_eye);
    const float4 albedo_hatching = FetchTexel(g_surface_albedo, pixel, 0);
    const float3 albedo = albedo_hatching.rgb;
    const float2 packed_normal = FetchTexel(g_surface_normal, pixel, 0).rg;
    const float4 surface = FetchTexel(g_surface_material, pixel, 0);
    const float translucency = surface.b;
    // What the ambient occlusion leaves of the key light, where thin surfaces lose less, and of
    // the ambient light. At the scene's size its sampler is nearest, at half size linear_value.
    const float occluded =
        BatchTexture(g_occlusion, uv, 0.0).r *
        (1.0 - clamp((distance_to_eye - OcclusionStart) * OcclusionRange, 0.0, 1.0));
    const float key_open = 1.0 - min(occluded * max(1.0 - 4.0 * translucency, 0.0), 1.0);
    const float ambient_open = 1.0 - min(occluded, 1.0);

    const float packed_squared = dot(packed_normal, packed_normal);
    const float3 normal =
        normalize(float3(2.0 * packed_normal * sqrt(max(1.0 - packed_squared, 0.0)),
                       1.0 - 2.0 * packed_squared));

    // Distant surfaces lose their highlights.
    const float highlight_fade =
        smoothstep(light.specular_fade.x, light.specular_fade.y, distance_to_eye);
    const float roughness = max(lerp(surface.r, 0.7, highlight_fade), 1e-6);
    const float metallic = lerp(surface.g, 0.3, highlight_fade);
    const float3 reflectance = albedo * metallic;
    const float3 diffuse = albedo - reflectance;

    const uint cascade = distance_to_eye < light.split_depths.y   ? 0u
                         : distance_to_eye < light.split_depths.z ? 1u
                                                                  : 2u;
    const float shadow_fade =
        1.0 - clamp((distance_to_eye - light.shadow_fade_start) * ShadowFadeRange, 0.0, 1.0);

    // Key light: GGX and Schlick for the highlight, Lambert for the rest.
    const float3 to_point = normalize(position);
    const float3 travel = light.light_direction.xyz;
    const float light_on_normal = dot(travel, normal);
    const float n_dot_l = min(max(-light_on_normal, 0.0), 1.0);
    // The shadow maps are read only where their result shows: every use of it is multiplied by
    // n_dot_l, and past the fade it is 1 whatever the maps hold.
    const float visible = n_dot_l > 0.0 && shadow_fade > 0.0
                              ? 1.0 - shadow_fade * (1.0 - KeyShadow(cascade, position))
                              : 1.0;
    const float3 halfway = normalize(-to_point - travel);
    const float n_dot_h = clamp(dot(normal, halfway), 0.0, 1.0);
    const float n_dot_v = min(max(-dot(to_point, normal), 0.0), 1.0);
    const float fresnel = pow(clamp(1.0 + dot(to_point, halfway), 0.0, 1.0), 5.0);
    const float a2 = roughness * roughness;
    const float d_term = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    const float ggx = a2 / (Pi * d_term * d_term);
    const float visibility =
        1.0 / ((n_dot_v + sqrt(a2 + (1.0 - a2) * n_dot_v * n_dot_v)) *
               (n_dot_l + sqrt(a2 + (1.0 - a2) * n_dot_l * n_dot_l)));
    const float3 highlight =
        key_open * (reflectance + (1.0 - reflectance) * fresnel) * ggx * visibility;
    float3 lit = light.light_color.rgb * visible * key_open * n_dot_l * (highlight + diffuse / Pi);

    // Light through thin surfaces, from behind them.
    if (translucency > 0.0) {
        const float s = TranslucencyRange * Thickness(cascade, position, normal) *
                        (1.0 - translucency);
        lit += light.light_color.rgb * diffuse * translucency * Transmittance(s) *
               max(0.3 + light_on_normal, 0.0);
    }

    // Point lights from the pixel's cell of the grid: the key light's terms toward each light,
    // faded over the end of its range, less where the key light is shadowed or faces away or
    // ShadowTrace shadows the pixel, and through thin surfaces toward the eye.
    if (light.point_lights > 0u) {
        const uint first =
            GridCellFirst(uv, distance_to_eye,
                          1.0 / (light.inverse_projection.w - light.inverse_projection.z));
        const float key_reach = 1.0 + light.shadow_influence * (n_dot_l * visible - 1.0);
        const float view_term = n_dot_v + sqrt(a2 + (1.0 - a2) * n_dot_v * n_dot_v);
        // ShadowTrace's screen-space shadow, which each light takes by its share.
        // A cell without lights reads nothing of it.
        const float traced = light.shadow_trace != 0u && ArenaLoad(ArenaAdd(g_grid, (first) * 4u), g_arena) != ~0u
                                 ? FetchTexel(g_traced_shadow, int2(position_in.xy), 0).x
                                 : 0.0;
        for (uint loop_index2 = 0u; loop_index2 < light.point_lights; loop_index2++) {
            const uint index = ArenaLoad(ArenaAdd(g_grid, (first + loop_index2) * 4u), g_arena);
            if (index == ~0u) {
                break;
            }
            const BatchPointLight point_light = LoadBatchPointLight(ArenaAdd(g_lights, index * 48u), g_arena);
            const float3 offset = point_light.position - position;
            const float reach = length(offset);
            if (reach > point_light.range) {
                continue;
            }
            const float3 toward = offset / reach;
            const float3 middle = normalize(toward - to_point);
            const float n_dot_toward = clamp(dot(normal, toward), 0.0, 1.0);
            const float n_dot_middle = clamp(dot(normal, middle), 0.0, 1.0);
            const float edge = pow(clamp(1.0 + dot(to_point, middle), 0.0, 1.0), 5.0);
            const float spread = n_dot_middle * n_dot_middle * (a2 - 1.0) + 1.0;
            const float point_ggx = a2 / (Pi * spread * spread);
            const float point_visibility =
                1.0 / (view_term *
                       (n_dot_toward + sqrt(a2 + (1.0 - a2) * n_dot_toward * n_dot_toward)));
            const float3 point_highlight = key_open * (reflectance + (1.0 - reflectance) * edge) *
                                         point_ggx * point_visibility;
            const float behind = max(dot(toward, to_point), 0.0);
            const float fade = key_reach * PointFade(point_light, reach) * (1.0 - point_light.shadow * traced);
            lit += point_light.color * fade *
                   (key_open * n_dot_toward * (point_highlight + diffuse / Pi) +
                    translucency * behind * behind * diffuse);
        }
    }
    lit = max(lit, float3(0.0, 0.0, 0.0)) + 6.0 * surface.a * albedo;

    // Ambient light from the sky's cube maps.
    const float3 sky_normal = normalize(mul((float3x3)light.view_to_sky, normal));
    float3 ambient = ambient_open * diffuse * Irradiance(sky_normal);
    if (translucency > 0.0) {
        ambient += AmbientTranslucency * translucency * diffuse * Irradiance(-sky_normal);
    }
    const float3 sky_ray = normalize(mul((float3x3)light.view_to_sky, to_point));
    const float n_dot_v_sky = min(max(-dot(sky_ray, sky_normal), 0.0), 1.0);
    const float rough = max(roughness, 0.001);
    const float lod = clamp(6.0 + 1.15 * log2(rough), 0.0, 6.5);
    const float inverse_rough = 1.0 - rough;
    const float a004 =
        min(inverse_rough * inverse_rough, pow(2.0, -9.28 * n_dot_v_sky)) * inverse_rough -
        0.0275 * rough + 0.0425;
    const float scale = 1.04 - 0.572 * rough - 1.04 * a004;
    const float bias = (0.022 * rough - 0.04 + 1.04 * a004) *
                       clamp(50.0 * reflectance.g, 0.0, 1.0);
    ambient += ambient_open * ambient_open * (reflectance * scale + bias) *
               Radiance(reflect(sky_ray, sky_normal), lod);

    // Comic shading: the occluders' shade, bands, then the fades to the gradation and background.
    ambient *= light.ibl_intensity;
    const float strength = light.occluder_intensity * max(1.0 - dot(lit, LumaWeights), 0.0);
    ambient *= OccluderShade(position, strength);
    float3 shaded = Tone(max(lit + ambient, float3(0.0, 0.0, 0.0)));
    const float gradation_amount =
        clamp((distance_to_eye - light.gradation_start) * light.gradation_range, 0.0, 1.0);
    const float3 gradation = lerp(lerp(light.gradation_middle.rgb, light.gradation_low.rgb,
                                   max(-sky_ray.y, 0.0)),
                               light.gradation_high.rgb, max(sky_ray.y, 0.0));
    shaded = lerp(shaded, gradation, gradation_amount);
    const float background_amount =
        clamp((distance_to_eye - light.background_start) * light.background_range, 0.0, 1.0) *
        light.background_intensity;
    if (background_amount > 0.0 && light.radiance[0] != 0u) {
        const float3 background = BatchCube(light.radiance[0], sky_ray, light.background_lod)
                                    .rgb;
        shaded = lerp(shaded, background, background_amount);
    }
    // The lines and the hatching in occluded places darken or lighten by the contour colour, with
    // distance by another share.
    const float line_share = lerp(
        light.contour_blend, light.contour_distant_blend,
        clamp((distance_to_eye - light.contour_start) * light.contour_range, 0.0, 1.0));
    const float key_occluded = 1.0 - key_open;
    const float hatching =
        light.hatching_intensity * key_occluded * key_occluded * (1.0 - albedo_hatching.a);
    const float line_value =
        clamp(max(FetchTexel(g_world_lines, pixel, 0).r, hatching), 0.0, 1.0) * line_share;
    color = float4(shaded * (1.0 + (2.0 * light.contour_color.rgb - 1.0) * line_value), 1.0);
    return color;
}
