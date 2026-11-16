// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer WorldLinesRoot : register(b1) {
    uint2 light_ref_words;
    uint2 output_size; int frame; uint lines;
};
#define light_ref ArenaMake(light_ref_words.x, light_ref_words.y)
cbuffer WorldLinesTextures:register(b2) {uint scene_depth,surface_normal,history;};
static BatchLight light;
static const float Intensity=1.7,WalkPixels=1.6;
static const float DepthThreshold = 0.065;
static const float MaxFrames = 26.0;
static const float SamplesPerFrame = 4.0;
static const float TwoPi = 6.2831853;

// The surface's facing, from the game's packed normal.
float Facing(float2 packed_normal) {
    return 1.0 - 2.0 * dot(packed_normal, packed_normal);
}

// The game's Contour program: how much a surface's facing differs from four samples along a random
// direction, averaged over frames with the camera's reprojection. A pixel holds its line_value, its
// distance over the depth range and the frames it has averaged. The game's velocity pass does the
// reprojection; surfaces that move on their own count as the camera's motion.
[numthreads(8,8,1)]
void main(uint3 dispatch_id:SV_DispatchThreadID) {
    light=LoadBatchLight(light_ref,g_arena);
    const int2 pixel = int2(dispatch_id.xy);
    const int2 size = Batch2Size(scene_depth, 0);
    if (any((pixel >= size))) {
        return;
    }
    const float depth = Batch2Fetch(scene_depth, pixel, 0).r;
    if (depth <= 0.0) {
        Batch2Store(lines, output_size, pixel, float4(0.0, 0.0, 0.0, 0.0));
        return;
    }
    const float2 inverse_size = 1.0 / float2(size);
    const float2 center = float2(pixel) + 0.5;
    const float2 uv = center * inverse_size;
    const float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    // Reverse depth to the game's OpenGL depth.
    const float distance_to_eye =
        1.0 / (light.inverse_projection.z * (1.0 - 2.0 * depth) + light.inverse_projection.w);
    const float3 position =
        float3(light.inverse_projection.xy * distance_to_eye * ndc, -distance_to_eye);
    const float4 previous = mul(light.view_to_previous, float4(position, 1.0));
    const float2 motion = previous.xy / previous.w - ndc;
    const float2 history_pixel = center + 0.5 * float2(motion.x, -motion.y) * float2(size);
    const float scaled_distance = distance_to_eye / light.depth_range;
    if (any((history_pixel < float2(0.0, 0.0))) || any((history_pixel > float2(size)))) {
        Batch2Store(lines, output_size, pixel, float4(0.0, scaled_distance, 0.0, 0.0));
        return;
    }
    const float3 past = Batch2SampleLevel(history, history_pixel * inverse_size, 0.0).rgb;
    if (abs(1.0 - scaled_distance / max(past.g, 1e-6)) >= DepthThreshold) {
        Batch2Store(lines, output_size, pixel, float4(0.0, scaled_distance, 0.0, 0.0));
        return;
    }
    const float seed = float(int(uint((pixel.x * 3) ^ (frame + pixel.y + pixel.x * pixel.y)) * 3u));
    const float angle = TwoPi * frac(abs(seed / TwoPi));
    const float2 walk = -WalkPixels * float2(sin(angle), cos(angle));
    float around = 0.0;
    for (float t = 0.25; t <= 1.0; t += 0.25) {
        const float2 sample_uv = (center + t * walk) * inverse_size;
        around += Facing(Batch2SampleLevel(surface_normal, sample_uv, 0.0).rg);
    }
    const float facing = Facing(Batch2Fetch(surface_normal, pixel, 0).rg);
    const float line_value = Intensity * (SamplesPerFrame * facing - around);
    const float frames = past.b + SamplesPerFrame;
    Batch2Store(lines, output_size, pixel, float4((past.r * past.b + SamplesPerFrame * line_value) / frames, scaled_distance,
                    min(frames, SamplesPerFrame * MaxFrames), 0.0));
}
