// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bloom_extract.frag -> bloom_extract PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/bloom_extract.frag: the game's bloom
// bright pass (PS 0x1bb9644) into a quarter-size image. Takes the brightest of
// four samples one scene pixel away, keeps each channel by its own level and the
// whole by its luma, raises it to a power and scales it by a weight of depth.
// Alpha keeps the depth term, which is the blur's width.

#include "common/grade.hlsli"
#include "common/texel.hlsli"

cbuffer BloomExtractConstants : register(b1) {
    // Read-only per-frame grading data, an arena ref rather than a cbuffer so
    // the std430 layout is preserved exactly (common/grade.hlsli).
    ArenaRef g_grade;
    // One pixel of the scene in texture coordinates.
    float2 g_scene_pixel;
    // The grade block is 240 bytes and its offsets are fixed, so it cannot be a
    // cbuffer member next to loose uniforms; the arena keeps it byte-exact.
    uint2 g_pad;
};

cbuffer BloomExtractTextures : register(b2) {
    uint g_scene;
    uint g_scene_depth;
};

// The arena table: the buffer-table index of each arena, the value the arena
// accessors below pass to DescriptorRaw(). Declared by every stage that reads
// the arena, because a cbuffer must be visible where it is used.
cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

struct PsInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// The share past a soft edge, by the game's smooth curve.
float SoftStep(float start, float end, float value) {
    const float t = saturate((value - start) / (end - start));
    return t * t * (3.0 - 2.0 * t);
}

float4 main(PsInput input) : SV_Target {
    const Grade grade = LoadGrade(g_grade, g_arena);
    const float2 uv = 0.5 * input.uv + 0.5;
    // Reverse depth to the game's depth, 1 far.
    const float depth = pow(abs(1.0 - SampleTex(g_scene_depth, uv).r), grade.bloom_correction.y);
    const float2 pixel = g_scene_pixel;
    const float3 c = max(max(SampleTex(g_scene, uv + pixel).rgb,
                             SampleTex(g_scene, uv + float2(-pixel.x, pixel.y)).rgb),
                         max(SampleTex(g_scene, uv + float2(pixel.x, -pixel.y)).rgb,
                             SampleTex(g_scene, uv - pixel).rgb));
    const float luma = dot(c, float3(0.298912, 0.586611, 0.114478));
    const float gate = SoftStep(grade.bloom_low.w, grade.bloom_high.w, luma);
    const float3 kept =
        c * float3(SoftStep(grade.bloom_low.r, grade.bloom_high.r, c.r),
                   SoftStep(grade.bloom_low.g, grade.bloom_high.g, c.g),
                   SoftStep(grade.bloom_low.b, grade.bloom_high.b, c.b)) *
        gate;
    const float4 depths = grade.bloom_depths;
    const float scale =
        grade.bloom_correction.z * (1.0 - SoftStep(depths.x, depths.y, depth)) +
        grade.bloom_correction.w * SoftStep(depths.z, depths.w, depth);
    // pow(abs(c), vec3(power)) in the GLSL: one exponent for all three channels.
    const float3 powered = pow(abs(kept), grade.bloom_correction.xxx);
    return float4((1.0 + scale) * powered, depth);
}
