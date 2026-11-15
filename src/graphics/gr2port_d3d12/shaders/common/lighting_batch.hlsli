// SPDX-License-Identifier: GPL-2.0-or-later
// std430 arena loaders for the next lighting/material batch. Offsets match
// scene.glsl and scene.h, not HLSL cbuffer packing (only roots use cbuffers).
#ifndef GR2_LIGHTING_BATCH_HLSLI
#define GR2_LIGHTING_BATCH_HLSLI
#include "common/scene.hlsli"
#include "common/texel.hlsli"
struct BatchCharacter {
    float4x4 view_inverse;
    float4 split_depths;
    float4 highlight_color;
    float4 diffuse_color;
    float4 ambient_color;
    float4 shadow_color;
    float4 specular_color;
    float4 color_scale;
    float4 gradation_color;
    float4 light_color;
    float4 light_direction;
    float4 camera_light_color;
    float4 target_size_inverse;
    float4 sh_unset[7];
    float2 normal_repeat;
    float2 normal_offset;
    float2 reflection_offset;
    float2 inverse_z;
    float2 punch_through;
    uint point_lights;
    float highlight_border;
    float highlight_smooth;
    float highlight_fresnel;
    float diffuse_border;
    float diffuse_smooth;
    float diffuse_line;
    float shadow_border;
    float shadow_smooth;
    float shadow_fresnel;
    float shadow_gradation;
    float specular_border;
    float specular_smooth;
    float specular_fresnel;
    float rim_smooth;
    float rim_border;
    float mask_density;
    int reflection_mode;
    float reflection_blend;
    float reflection_repeat;
    float reflection_distortion;
    float detail_border;
    float detail_blend;
    float detail_repeat;
    float detail_mask;
    uint alpha_step;
    float alpha_reference;
    float light_intensity_scale;
    float shadow_influence;
    float camera_light_attenuation;
    float camera_light_inner;
    float camera_light_outer;
    float camera_light_directivity;
    float self_translucency;
    float self_translucency_start;
    float self_translucency_depth;
    float self_translucency_offset;
    float self_translucency_power;
    float self_translucency_scale;
    uint self_translucency_mode;
    uint bloom_fresnel_mode;
    float bloom_fresnel_border;
    float bloom_fresnel_smooth;
    float ibl_blend;
    float vertex_shadow;
    float rim_lighting;
    float rim_intensity;
    float padding[3];
    float4 bend_position;
    float bend_range;
    float bend_bias;
    uint normal_map;
    uint reflection;
    uint detail;
    float glow;
};
BatchCharacter LoadBatchCharacter(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    BatchCharacter value;
    value.view_inverse = ArenaLoadMatrix(ArenaAdd(ref, 0u), table);
    value.split_depths = asfloat(buffer.Load4(ref.offset + 64u));
    value.highlight_color = asfloat(buffer.Load4(ref.offset + 80u));
    value.diffuse_color = asfloat(buffer.Load4(ref.offset + 96u));
    value.ambient_color = asfloat(buffer.Load4(ref.offset + 112u));
    value.shadow_color = asfloat(buffer.Load4(ref.offset + 128u));
    value.specular_color = asfloat(buffer.Load4(ref.offset + 144u));
    value.color_scale = asfloat(buffer.Load4(ref.offset + 160u));
    value.gradation_color = asfloat(buffer.Load4(ref.offset + 176u));
    value.light_color = asfloat(buffer.Load4(ref.offset + 192u));
    value.light_direction = asfloat(buffer.Load4(ref.offset + 208u));
    value.camera_light_color = asfloat(buffer.Load4(ref.offset + 224u));
    value.target_size_inverse = asfloat(buffer.Load4(ref.offset + 240u));
    [unroll] for (uint array_index1 = 0u; array_index1 < 7u; ++array_index1) {
        value.sh_unset[array_index1] = asfloat(buffer.Load4(ref.offset + 256u + array_index1 * 16u));
    }
    value.normal_repeat = asfloat(buffer.Load2(ref.offset + 368u));
    value.normal_offset = asfloat(buffer.Load2(ref.offset + 376u));
    value.reflection_offset = asfloat(buffer.Load2(ref.offset + 384u));
    value.inverse_z = asfloat(buffer.Load2(ref.offset + 392u));
    value.punch_through = asfloat(buffer.Load2(ref.offset + 400u));
    value.point_lights = buffer.Load(ref.offset + 408u);
    value.highlight_border = asfloat(buffer.Load(ref.offset + 412u));
    value.highlight_smooth = asfloat(buffer.Load(ref.offset + 416u));
    value.highlight_fresnel = asfloat(buffer.Load(ref.offset + 420u));
    value.diffuse_border = asfloat(buffer.Load(ref.offset + 424u));
    value.diffuse_smooth = asfloat(buffer.Load(ref.offset + 428u));
    value.diffuse_line = asfloat(buffer.Load(ref.offset + 432u));
    value.shadow_border = asfloat(buffer.Load(ref.offset + 436u));
    value.shadow_smooth = asfloat(buffer.Load(ref.offset + 440u));
    value.shadow_fresnel = asfloat(buffer.Load(ref.offset + 444u));
    value.shadow_gradation = asfloat(buffer.Load(ref.offset + 448u));
    value.specular_border = asfloat(buffer.Load(ref.offset + 452u));
    value.specular_smooth = asfloat(buffer.Load(ref.offset + 456u));
    value.specular_fresnel = asfloat(buffer.Load(ref.offset + 460u));
    value.rim_smooth = asfloat(buffer.Load(ref.offset + 464u));
    value.rim_border = asfloat(buffer.Load(ref.offset + 468u));
    value.mask_density = asfloat(buffer.Load(ref.offset + 472u));
    value.reflection_mode = asint(buffer.Load(ref.offset + 476u));
    value.reflection_blend = asfloat(buffer.Load(ref.offset + 480u));
    value.reflection_repeat = asfloat(buffer.Load(ref.offset + 484u));
    value.reflection_distortion = asfloat(buffer.Load(ref.offset + 488u));
    value.detail_border = asfloat(buffer.Load(ref.offset + 492u));
    value.detail_blend = asfloat(buffer.Load(ref.offset + 496u));
    value.detail_repeat = asfloat(buffer.Load(ref.offset + 500u));
    value.detail_mask = asfloat(buffer.Load(ref.offset + 504u));
    value.alpha_step = buffer.Load(ref.offset + 508u);
    value.alpha_reference = asfloat(buffer.Load(ref.offset + 512u));
    value.light_intensity_scale = asfloat(buffer.Load(ref.offset + 516u));
    value.shadow_influence = asfloat(buffer.Load(ref.offset + 520u));
    value.camera_light_attenuation = asfloat(buffer.Load(ref.offset + 524u));
    value.camera_light_inner = asfloat(buffer.Load(ref.offset + 528u));
    value.camera_light_outer = asfloat(buffer.Load(ref.offset + 532u));
    value.camera_light_directivity = asfloat(buffer.Load(ref.offset + 536u));
    value.self_translucency = asfloat(buffer.Load(ref.offset + 540u));
    value.self_translucency_start = asfloat(buffer.Load(ref.offset + 544u));
    value.self_translucency_depth = asfloat(buffer.Load(ref.offset + 548u));
    value.self_translucency_offset = asfloat(buffer.Load(ref.offset + 552u));
    value.self_translucency_power = asfloat(buffer.Load(ref.offset + 556u));
    value.self_translucency_scale = asfloat(buffer.Load(ref.offset + 560u));
    value.self_translucency_mode = buffer.Load(ref.offset + 564u);
    value.bloom_fresnel_mode = buffer.Load(ref.offset + 568u);
    value.bloom_fresnel_border = asfloat(buffer.Load(ref.offset + 572u));
    value.bloom_fresnel_smooth = asfloat(buffer.Load(ref.offset + 576u));
    value.ibl_blend = asfloat(buffer.Load(ref.offset + 580u));
    value.vertex_shadow = asfloat(buffer.Load(ref.offset + 584u));
    value.rim_lighting = asfloat(buffer.Load(ref.offset + 588u));
    value.rim_intensity = asfloat(buffer.Load(ref.offset + 592u));
    value.padding[0] = asfloat(buffer.Load(ref.offset + 596u));
    value.padding[1] = asfloat(buffer.Load(ref.offset + 600u));
    value.padding[2] = asfloat(buffer.Load(ref.offset + 604u));
    value.bend_position = asfloat(buffer.Load4(ref.offset + 608u));
    value.bend_range = asfloat(buffer.Load(ref.offset + 624u));
    value.bend_bias = asfloat(buffer.Load(ref.offset + 628u));
    value.normal_map = buffer.Load(ref.offset + 632u);
    value.reflection = buffer.Load(ref.offset + 636u);
    value.detail = buffer.Load(ref.offset + 640u);
    value.glow = asfloat(buffer.Load(ref.offset + 644u));
    return value;
}
struct BatchGlassMaterial {
    float4x4 view_inverse;
    float4 split_depths;
    float4 light_color;
    float4 light_direction;
    float4 normal_transform;
    float4 reflectance;
    float4 translucency;
    float4 refraction;
    float2 projection;
    float roughness;
    float normal_scale;
};
BatchGlassMaterial LoadBatchGlassMaterial(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    BatchGlassMaterial value;
    value.view_inverse = ArenaLoadMatrix(ArenaAdd(ref, 0u), table);
    value.split_depths = asfloat(buffer.Load4(ref.offset + 64u));
    value.light_color = asfloat(buffer.Load4(ref.offset + 80u));
    value.light_direction = asfloat(buffer.Load4(ref.offset + 96u));
    value.normal_transform = asfloat(buffer.Load4(ref.offset + 112u));
    value.reflectance = asfloat(buffer.Load4(ref.offset + 128u));
    value.translucency = asfloat(buffer.Load4(ref.offset + 144u));
    value.refraction = asfloat(buffer.Load4(ref.offset + 160u));
    value.projection = asfloat(buffer.Load2(ref.offset + 176u));
    value.roughness = asfloat(buffer.Load(ref.offset + 184u));
    value.normal_scale = asfloat(buffer.Load(ref.offset + 188u));
    return value;
}
struct BatchInteriorConstants {
    float4x4 rotation;
    float4 view;
    float4 center;
    float4 reflectance;
    float4 band[2];
    float3 fresnel_floor;
    float roughness;
    float fresnel_bias;
    float fresnel_scale;
    float size_squared[2];
};
BatchInteriorConstants LoadBatchInteriorConstants(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    BatchInteriorConstants value;
    value.rotation = ArenaLoadMatrix(ArenaAdd(ref, 0u), table);
    value.view = asfloat(buffer.Load4(ref.offset + 64u));
    value.center = asfloat(buffer.Load4(ref.offset + 80u));
    value.reflectance = asfloat(buffer.Load4(ref.offset + 96u));
    [unroll] for (uint array_index2 = 0u; array_index2 < 2u; ++array_index2) {
        value.band[array_index2] = asfloat(buffer.Load4(ref.offset + 112u + array_index2 * 16u));
    }
    value.fresnel_floor = asfloat(buffer.Load3(ref.offset + 144u));
    value.roughness = asfloat(buffer.Load(ref.offset + 156u));
    value.fresnel_bias = asfloat(buffer.Load(ref.offset + 160u));
    value.fresnel_scale = asfloat(buffer.Load(ref.offset + 164u));
    value.size_squared[0] = asfloat(buffer.Load(ref.offset + 168u));
    value.size_squared[1] = asfloat(buffer.Load(ref.offset + 172u));
    return value;
}
struct BatchLight {
    float4x4 shadow_projection[3];
    float4x4 inverse_shadow_projection[3];
    float4x4 view_to_sky;
    float4x4 view_to_previous;
    float4 inverse_projection;
    float4 split_depths;
    float4 light_color;
    float4 light_direction;
    float4 occluder_direction;
    float4 gradation_high;
    float4 gradation_middle;
    float4 gradation_low;
    float4 contour_color;
    float2 specular_fade;
    float shadow_fade_start;
    float ibl_intensity;
    float contour_blend;
    float contour_distant_blend;
    float contour_start;
    float contour_range;
    float gradation_start;
    float gradation_range;
    float background_start;
    float background_range;
    float background_intensity;
    float background_lod;
    float hatching_intensity;
    float toning;
    float occluder_intensity;
    float occluder_penumbra;
    float occluder_range;
    uint occluders;
    uint radiance[2];
    uint irradiance[2];
    float ibl_blend;
    float depth_range;
    float occlusion_intensity;
    float occlusion_select;
    float2 grid_scale;
    float grid_start;
    uint point_lights;
    float shadow_influence;
    uint shadow_trace;
    uint light_padding[2];
    float4 occluder_spheres[64];
    float4 sh[7];
};
BatchLight LoadBatchLight(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    BatchLight value;
    value.shadow_projection[0] = ArenaLoadMatrix(ArenaAdd(ref, 0u), table);
    value.shadow_projection[1] = ArenaLoadMatrix(ArenaAdd(ref, 64u), table);
    value.shadow_projection[2] = ArenaLoadMatrix(ArenaAdd(ref, 128u), table);
    value.inverse_shadow_projection[0] = ArenaLoadMatrix(ArenaAdd(ref, 192u), table);
    value.inverse_shadow_projection[1] = ArenaLoadMatrix(ArenaAdd(ref, 256u), table);
    value.inverse_shadow_projection[2] = ArenaLoadMatrix(ArenaAdd(ref, 320u), table);
    value.view_to_sky = ArenaLoadMatrix(ArenaAdd(ref, 384u), table);
    value.view_to_previous = ArenaLoadMatrix(ArenaAdd(ref, 448u), table);
    value.inverse_projection = asfloat(buffer.Load4(ref.offset + 512u));
    value.split_depths = asfloat(buffer.Load4(ref.offset + 528u));
    value.light_color = asfloat(buffer.Load4(ref.offset + 544u));
    value.light_direction = asfloat(buffer.Load4(ref.offset + 560u));
    value.occluder_direction = asfloat(buffer.Load4(ref.offset + 576u));
    value.gradation_high = asfloat(buffer.Load4(ref.offset + 592u));
    value.gradation_middle = asfloat(buffer.Load4(ref.offset + 608u));
    value.gradation_low = asfloat(buffer.Load4(ref.offset + 624u));
    value.contour_color = asfloat(buffer.Load4(ref.offset + 640u));
    value.specular_fade = asfloat(buffer.Load2(ref.offset + 656u));
    value.shadow_fade_start = asfloat(buffer.Load(ref.offset + 664u));
    value.ibl_intensity = asfloat(buffer.Load(ref.offset + 668u));
    value.contour_blend = asfloat(buffer.Load(ref.offset + 672u));
    value.contour_distant_blend = asfloat(buffer.Load(ref.offset + 676u));
    value.contour_start = asfloat(buffer.Load(ref.offset + 680u));
    value.contour_range = asfloat(buffer.Load(ref.offset + 684u));
    value.gradation_start = asfloat(buffer.Load(ref.offset + 688u));
    value.gradation_range = asfloat(buffer.Load(ref.offset + 692u));
    value.background_start = asfloat(buffer.Load(ref.offset + 696u));
    value.background_range = asfloat(buffer.Load(ref.offset + 700u));
    value.background_intensity = asfloat(buffer.Load(ref.offset + 704u));
    value.background_lod = asfloat(buffer.Load(ref.offset + 708u));
    value.hatching_intensity = asfloat(buffer.Load(ref.offset + 712u));
    value.toning = asfloat(buffer.Load(ref.offset + 716u));
    value.occluder_intensity = asfloat(buffer.Load(ref.offset + 720u));
    value.occluder_penumbra = asfloat(buffer.Load(ref.offset + 724u));
    value.occluder_range = asfloat(buffer.Load(ref.offset + 728u));
    value.occluders = buffer.Load(ref.offset + 732u);
    value.radiance[0] = buffer.Load(ref.offset + 736u);
    value.radiance[1] = buffer.Load(ref.offset + 740u);
    value.irradiance[0] = buffer.Load(ref.offset + 744u);
    value.irradiance[1] = buffer.Load(ref.offset + 748u);
    value.ibl_blend = asfloat(buffer.Load(ref.offset + 752u));
    value.depth_range = asfloat(buffer.Load(ref.offset + 756u));
    value.occlusion_intensity = asfloat(buffer.Load(ref.offset + 760u));
    value.occlusion_select = asfloat(buffer.Load(ref.offset + 764u));
    value.grid_scale = asfloat(buffer.Load2(ref.offset + 768u));
    value.grid_start = asfloat(buffer.Load(ref.offset + 776u));
    value.point_lights = buffer.Load(ref.offset + 780u);
    value.shadow_influence = asfloat(buffer.Load(ref.offset + 784u));
    value.shadow_trace = buffer.Load(ref.offset + 788u);
    value.light_padding[0] = buffer.Load(ref.offset + 792u);
    value.light_padding[1] = buffer.Load(ref.offset + 796u);
    [unroll] for (uint array_index3 = 0u; array_index3 < 64u; ++array_index3) {
        value.occluder_spheres[array_index3] = asfloat(buffer.Load4(ref.offset + 800u + array_index3 * 16u));
    }
    [unroll] for (uint array_index4 = 0u; array_index4 < 7u; ++array_index4) {
        value.sh[array_index4] = asfloat(buffer.Load4(ref.offset + 1824u + array_index4 * 16u));
    }
    return value;
}
struct BatchPointLight {
    float3 position;
    float range;
    float3 color;
    float falloff_start;
    float falloff_power;
    float shadow;
    uint padding[2];
};
BatchPointLight LoadBatchPointLight(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    BatchPointLight value;
    value.position = asfloat(buffer.Load3(ref.offset + 0u));
    value.range = asfloat(buffer.Load(ref.offset + 12u));
    value.color = asfloat(buffer.Load3(ref.offset + 16u));
    value.falloff_start = asfloat(buffer.Load(ref.offset + 28u));
    value.falloff_power = asfloat(buffer.Load(ref.offset + 32u));
    value.shadow = asfloat(buffer.Load(ref.offset + 36u));
    value.padding[0] = buffer.Load(ref.offset + 40u);
    value.padding[1] = buffer.Load(ref.offset + 44u);
    return value;
}
struct BatchDecal {
    float4 positions[3];
    float4 targets[3];
    float2 scales[3];
    uint texture;
    uint color_alpha;
    float4 gradation;
};
BatchDecal LoadBatchDecal(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    BatchDecal value;
    [unroll] for (uint array_index5 = 0u; array_index5 < 3u; ++array_index5) {
        value.positions[array_index5] = asfloat(buffer.Load4(ref.offset + 0u + array_index5 * 16u));
    }
    [unroll] for (uint array_index6 = 0u; array_index6 < 3u; ++array_index6) {
        value.targets[array_index6] = asfloat(buffer.Load4(ref.offset + 48u + array_index6 * 16u));
    }
    value.scales[0] = asfloat(buffer.Load2(ref.offset + 96u));
    value.scales[1] = asfloat(buffer.Load2(ref.offset + 104u));
    value.scales[2] = asfloat(buffer.Load2(ref.offset + 112u));
    value.texture = buffer.Load(ref.offset + 120u);
    value.color_alpha = buffer.Load(ref.offset + 124u);
    value.gradation = asfloat(buffer.Load4(ref.offset + 128u));
    return value;
}

float4 BatchCube(uint handle, float3 direction, float lod) {
    return DescriptorCube(handle).SampleLevel(DescriptorSampler(SMP_LINEAR_CLAMP), direction, lod);
}
float4 BatchTexture(uint handle, float2 uv, float lod) {
    return DescriptorTex2D(handle).SampleLevel(DescriptorSampler(SMP_LINEAR_CLAMP), uv, lod);
}
float BatchShadow(uint cascade, uint near_handle, uint far_handle, float3 coord) {
    if (cascade == 2u) return DescriptorDepth(far_handle).SampleCmpLevelZero(DescriptorComparisonSampler(SMP_COMPARISON), coord.xy, coord.z);
    return DescriptorDepthArray(near_handle).SampleCmpLevelZero(DescriptorComparisonSampler(SMP_COMPARISON), float3(coord.xy, float(cascade)), coord.z);
}
float2 BatchSize(uint handle) {
    uint width, height; DescriptorTex2D(handle).GetDimensions(width, height);
    return float2(width, height);
}
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's lit programs share about the point_light lights: the cell of LightCompute's grid a
// pixel reads, and a light's falloff. Needs scene.glsl.

// The first light index of the cell a pixel at uv, at a view depth, falls in: the slices double
// in depth beyond start, the near plane's distance.
uint GridCellFirst(float2 uv, float depth, float start) {
    const float beyond = depth - start;
    const uint slice = beyond > 1.0 ? min(uint(log2(beyond)), 8u - 1u) : 0u;
    const uint2 tile = uint2(float2(60u, 36u) * uv);
    return ((slice * 36u + tile.y) * 60u + tile.x) * 48u;
}

// The share of a light left at a distance: it falls by the light's power from where its falloff
// starts to none at its range.
float PointFade(BatchPointLight point_light, float reach) {
    const float falloff =
        min(max(reach - point_light.falloff_start, 0.0) / (point_light.range - point_light.falloff_start), 1.0);
    return 1.0 - (falloff > 0.0 ? exp2(point_light.falloff_power * log2(falloff)) : 0.0);
}

#endif
