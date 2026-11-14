// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// composite.frag -> composite PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/composite.frag: the game's Composite
// program with the lens and the wobble on. Saturation in YCbCr, each channel's
// Catmull-Rom curve, then the sRGB encoding, as the target is UNORM.
//
// HLSL specifics:
//
//   * GLSL's `mix(a, b, lessThan(x, vec3(...)))` is a per-channel select. HLSL's
//     `lerp` only takes a scalar, and `step`/`select` are the equivalents here:
//     `lerp(a, b, step(edge, x))` selects b where x >= edge, which is the
//     reverse of what the GLSL wants, so the condition is inverted (`1 - step`).
//   * The grade's `knots` is a `float[3][8]` loaded from the arena; indexing it
//     with the `channel` argument is uniform at every call site because the three
//     calls pass constants, so it compiles to three straight-line sequences.

#include "common/grade.hlsli"
#include "common/texel.hlsli"

cbuffer CompositeConstants : register(b0) {
    // Read-only per-frame grading data, an arena ref rather than a cbuffer so
    // the std430 layout is preserved exactly (common/grade.hlsli).
    ArenaRef g_grade;
    ArenaRef g_meter;
    uint g_picture;
    uint3 g_pad;
};

cbuffer ArenaConstants : register(b1) {
    ArenaTable g_arena;
};

struct PsInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// The grade block arrives as a parameter rather than a global: HLSL has no
// file-scope object with a runtime initialiser, so a global loaded once per
// dispatch would need a mutable uniform, and the helpers below are all
// straight-line functions of it.
float2 Distort(float2 texcoord, Grade grade) {
    const float2 center = texcoord - 0.5;
    const float bulge =
        1.0 / (grade.distortion.y * sqrt(1.0 - grade.distortion.z * dot(center, center)));
    const float2 lens = center * (grade.distortion.x * (bulge - 1.0) + 1.0) + 0.5;
    const float phase = grade.fluctuation.x;
    const float2 fine = sin(float2(157.0, 314.0) * lens + phase);
    const float2 coarse = cos(float2(31.4, 62.8) * lens + phase);
    const float2 from_center = 2.0 * lens - 1.0;
    const float edge = 1.0 - dot(from_center, from_center);
    return lens + (0.002 * coarse + 0.001 * fine) * grade.fluctuation.y *
                      (1.0 - edge * edge);
}

// A channel's Catmull-Rom curve through its knots, with the end knots repeated.
float Curve(Grade grade, uint channel, float value) {
    const float scaled = value * 6.0;
    const int segment = min(int(scaled), 5);
    const float x = scaled - float(segment);
    const float p0 = grade.knots[channel][max(segment - 1, 0)];
    const float p1 = grade.knots[channel][segment];
    const float p2 = grade.knots[channel][segment + 1];
    const float p3 = grade.knots[channel][min(segment + 2, 6)];
    const float a = p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
    const float b = 1.5 * (p1 - p2) + 0.5 * (p3 - p0);
    return p1 + x * (0.5 * (p2 - p0) + x * (a + x * b));
}

float3 Composite(Grade grade, float3 c) {
    const float y = dot(c, float3(0.299, 0.587, 0.114));
    const float cb = dot(c, float3(-0.16874, -0.33126, 0.5)) * grade.saturation;
    const float cr = dot(c, float3(0.5, -0.41869, -0.08131)) * grade.saturation;
    const float3 saturated =
        clamp(y + float3(1.402 * cr, -0.34414 * cb - 0.71414 * cr, 1.772 * cb),
              float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
    const float3 curved =
        max(float3(Curve(grade, 0u, saturated.r), Curve(grade, 1u, saturated.g), Curve(grade, 2u, saturated.b)),
            float3(0.0, 0.0, 0.0));
    // The two sRGB segments, selected per channel exactly as the GLSL's
    // mix(..., lessThan(curved, vec3(0.0031308))) selects them.
    const float3 high = 1.055 * pow(curved, float3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4)) - 0.055;
    const float3 low = 12.92 * curved;
    return lerp(high, low, 1.0 - step(float3(0.0031308, 0.0031308, 0.0031308), curved));
}

float4 main(PsInput input) : SV_Target {
    const Grade grade = LoadGrade(g_grade, g_arena);
    float2 texcoord = 0.5 * input.uv + 0.5;
    // The distortion is a lens bulge plus a two-frequency wobble that fades out
    // towards the edges of the frame.
    if (grade.distorted != 0u) {
        texcoord = Distort(texcoord, grade);
    }
    return float4(Composite(grade, SampleTex(g_picture, texcoord).rgb), 1.0);
}
