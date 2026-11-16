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
static const float SamplesPerFrame = 4.0;
static const float TwoPi = 6.2831853;

// The surface's facing, from the game's packed normal.
float Facing(float2 packed_normal) {
    return 1.0 - 2.0 * dot(packed_normal, packed_normal);
}

// Spatial contour estimate. The Xbox pass intentionally omits the upstream temporal reprojection
// until a velocity/history resource is available; it still evaluates the four-sample normal-facing
// contrast used by the game's contour program.
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
    Batch2Store(lines, output_size, pixel, float4(line_value, 0.0, 0.0, 0.0));
}
