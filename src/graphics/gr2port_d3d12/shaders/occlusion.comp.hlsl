// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch2_scene.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer OcclusionRoot : register(b1) {
    uint2 output_size;
    uint2 light_ref_words;
    int frame; int Scale;
    uint occlusion; uint character_depth;
};
#define light_ref ArenaMake(light_ref_words.x, light_ref_words.y)
cbuffer Batch2Textures : register(b2) { uint scene_depth; uint depth_levels; uint surface_normal; uint history; };
static BatchLight light;
// The game's SSAO constants: the sample radius and its square, the bias and softening of a
// sample's angle, the weight of the sum, and the history's depth test and length.
static const float Radius = 1.4;
static const float RadiusSquared = 1.96;
static const float Bias = 0.05;
static const float Softening = 0.01;
static const float SumWeight = 1.25;
static const float DepthThreshold = 0.065;
static const float MaxFrames = 26.0;
static const float SamplesPerFrame = 4.0;
// The disk's size in pixels at distance 1 per unit of radius at the game's own 1080 lines. At
// other heights the disk keeps its share of the scene's height.
static const float DiskPixels = 500.0;
static const float GameLines = 1080.0;
static const float TurnsPerRadian = 0.15915494;
static const float TwoPi = 6.2831853;
// The pyramid holds the levels from 1 to this one.
static const int TopLevel = 5;
static const float Shares[4] = {0.125, 0.375, 0.625, 0.875};
static const float Turns[4] = {0.8745564, 2.6236691, 4.3727822, 6.1218953};

float DistanceOf(float depth) {
    return 1.0 / (light.inverse_projection.z * (1.0 - 2.0 * depth) + light.inverse_projection.w);
}

// Whether the world shows at a pixel: neither the sky nor a character in front of it.
bool WorldAt(int2 pixel, float depth) {
    return depth > 0.0 && Batch2Fetch(character_depth, pixel, 0).r <= depth;
}

// The history's coordinate of a place in scene pixels. At half size a texel's value belongs to its
// first pixel's centre, a quarter texel before the texel's own centre.
float2 HistoryCoord(float2 place, float2 inverse_size) {
    if (Scale == 1) {
        return place * inverse_size;
    }
    return (place + 0.5 * float(Scale - 1)) / (float(Scale) * float2(Batch2Size(history, 0)));
}

// The game's SSAO (CS 0x1c31e4c above occlusion_select 0.9, else 0x1c32a8c): four samples on a
// disk that turns per pixel and frame, read from depth levels that grow with the disk, averaged
// over frames with the camera's reprojection. A texel holds its pixel's occlusion, distance over
// the depth range and the samples averaged. The samples see the characters' depth too.
[numthreads(8,8,1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    light=LoadBatchLight(light_ref,g_arena);
    const int2 texel = int2(dispatch_id.xy);
    const int2 pixel = texel * Scale;
    const int2 size = Batch2Size(scene_depth, 0);
    if (any((pixel >= size))) {
        return;
    }
    const float depth = Batch2Fetch(scene_depth, pixel, 0).r;
    if (!WorldAt(pixel, depth)) {
        Batch2Store(occlusion, output_size, texel, float4(0.0, 1.0, 0.0, 0.0));
        return;
    }
    const float2 inverse_size = 1.0 / float2(size);
    const float2 center = float2(pixel) + 0.5;
    const float2 ndc =
        float2(2.0 * center.x * inverse_size.x - 1.0, 1.0 - 2.0 * center.y * inverse_size.y);
    const float distance_to_eye = DistanceOf(depth);
    const float3 position =
        float3(light.inverse_projection.xy * distance_to_eye * ndc, -distance_to_eye);
    const float2 packed_normal = Batch2Fetch(surface_normal, pixel, 0).rg;
    const float packed_squared = dot(packed_normal, packed_normal);
    const float3 normal = float3(2.0 * packed_normal * sqrt(max(1.0 - packed_squared, 0.0)),
                             1.0 - 2.0 * packed_squared);

    const bool world_samples = light.occlusion_select > 0.9;
    const float seed = float(uint((pixel.x * 3) ^ (frame + pixel.y + pixel.x * pixel.y)) * 3u);
    const float disk = DiskPixels * (float(size.y) / GameLines) * Radius / distance_to_eye;
    float sum = 0.0;
    for (int i = 0; i < 4; i++) {
        const float turn = TwoPi * frac(mad(seed, TurnsPerRadian, Turns[i]));
        const float reach = Shares[i] * disk;
        const int2 tap = pixel + int2(float2(sin(turn), cos(turn)) * reach);
        if (any((tap < int2(0, 0))) || any((tap >= size))) {
            continue;
        }
        const int level = clamp(int(floor(log2(reach))) - 3, 0, TopLevel);
        // The tap's own depths are read only where the world test or level 0 uses them.
        float world_depth = 0.0;
        float character = 0.0;
        if (world_samples || level == 0) {
            world_depth = Batch2Fetch(scene_depth, tap, 0).r;
            if (level == 0 || world_depth > 0.0) {
                character = Batch2Fetch(character_depth, tap, 0).r;
            }
            if (world_samples && !(world_depth > 0.0 && character <= world_depth)) {
                continue;
            }
        }
        const int2 texel = tap >> level;
        float tap_depth;
        if (level == 0) {
            tap_depth = max(world_depth, character);
        } else if (all((texel < Batch2Size(depth_levels, level - 1)))) {
            tap_depth = Batch2Fetch(depth_levels, texel, level - 1).r;
        } else {
            continue;
        }
        const float tap_distance = DistanceOf(tap_depth);
        const float2 tap_ndc = float2(2.0 * float(tap.x) * inverse_size.x - 1.0,
                                  1.0 - 2.0 * float(tap.y) * inverse_size.y);
        const float3 v = float3(light.inverse_projection.xy * tap_distance * tap_ndc - position.xy,
                            distance_to_eye - tap_distance);
        const float squared = dot(v, v);
        const float falloff = max(RadiusSquared - squared, 0.0);
        sum += falloff * falloff * falloff *
               max((dot(v, normal) - Bias) / (squared + Softening), 0.0);
    }
    const float occluded = 1.0 - max(1.0 - SumWeight * light.occlusion_intensity * sum, 0.0);

    const float4 previous = mul(light.view_to_previous, float4(position, 1.0));
    const float2 motion = previous.xy / previous.w - ndc;
    const float2 history_pixel = center + 0.5 * float2(motion.x, -motion.y) * float2(size);
    const float scaled_distance = distance_to_eye / light.depth_range;
    const float4 restart = float4(occluded, scaled_distance, SamplesPerFrame, 0.0);
    if (any((history_pixel < float2(0.0, 0.0))) || any((history_pixel > float2(size)))) {
        Batch2Store(occlusion, output_size, texel, restart);
        return;
    }
    const float3 past = Batch2SampleLevel(history, HistoryCoord(history_pixel, inverse_size), 0.0).rgb;
    if (abs(1.0 - scaled_distance / max(past.g, 1e-6)) >= DepthThreshold) {
        Batch2Store(occlusion, output_size, texel, restart);
        return;
    }
    const float samples = past.b + SamplesPerFrame;
    Batch2Store(occlusion, output_size, texel, float4(clamp((past.b * past.r + SamplesPerFrame * occluded) / samples, 0.0, 1.0),
                    scaled_distance, min(samples, SamplesPerFrame * MaxFrames), 0.0));
}
