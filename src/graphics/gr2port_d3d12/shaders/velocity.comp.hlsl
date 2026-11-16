// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch2_scene.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer VelocityRoot : register(b1) {
    uint2 output_size;
    uint2 light_ref_words;
    uint UpscalerMotion;
};
#define light_ref ArenaMake(light_ref_words.x, light_ref_words.y)
cbuffer Batch2Textures : register(b2) { uint scene_depth; uint character_depth; uint velocity; uint upscaler_motion; };
static BatchLight light;
// A pixel at depth that moves with the camera: its place in the last frame's clip space less its
// place in this frame's.
float2 CameraMotion(int2 pixel, int2 size, float depth) {
    const float2 center = (float2(pixel) + 0.5) / float2(size);
    const float2 ndc = float2(2.0 * center.x - 1.0, 1.0 - 2.0 * center.y);
    const float distance_to_eye =
        1.0 / (light.inverse_projection.z * (1.0 - 2.0 * depth) + light.inverse_projection.w);
    const float3 position =
        float3(light.inverse_projection.xy * distance_to_eye * ndc, -distance_to_eye);
    const float4 previous = mul(light.view_to_previous, float4(position, 1.0));
    return previous.xy / previous.w - ndc;
}

// The game's Velocity program, where every surface moves with the camera and a character's pixel
// holds still.
[numthreads(8,8,1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    light=LoadBatchLight(light_ref,g_arena);
    const int2 pixel = int2(dispatch_id.xy);
    const int2 size = Batch2Size(scene_depth, 0);
    if (any((pixel >= size))) {
        return;
    }
    const float depth = Batch2Fetch(scene_depth, pixel, 0).r;
    const float character = Batch2Fetch(character_depth, pixel, 0).r;
    if (character > depth) {
        Batch2Store(velocity, output_size, pixel, float4(0.0, 0.0, 0.0, 0.0));
        if (UpscalerMotion) {
            const float2 motion = CameraMotion(pixel, size, character);
            Batch2Store(upscaler_motion, output_size, pixel, float4(motion, 0.0, 0.0));
        }
        return;
    }
    const float2 motion = CameraMotion(pixel, size, depth);
    Batch2Store(velocity, output_size, pixel, float4(motion, 0.0, 0.0));
    if (UpscalerMotion) {
        Batch2Store(upscaler_motion, output_size, pixel, float4(motion, 0.0, 0.0));
    }
}
