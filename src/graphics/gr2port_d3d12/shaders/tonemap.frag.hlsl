// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// tonemap.frag -> tonemap PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/tonemap.frag. The game's Tonemap
// program: a filmic curve scaled so a white of 11.2 maps to 1, then the bloom.
// Alpha holds the luma the antialias program reads.
//
// The GLSL's two push-constant buffer references (Grade, Meter) become a cbuffer
// and an arena ref respectively -- Grade is read-only per-frame data, Meter is
// state the compute passes write (common/grade.hlsli).

#include "common/grade.hlsli"
#include "common/texel.hlsli"

cbuffer TonemapConstants : register(b0) {
    // Read-only per-frame grading data, an arena ref rather than a cbuffer so
    // the std430 layout is preserved exactly (common/grade.hlsli).
    ArenaRef g_grade;
    ArenaRef g_meter;
};

cbuffer ArenaConstants : register(b1) {
    ArenaTable g_arena;
};

// The scene and bloom targets. Two separate SRVs in the GLSL's set 0; here two
// root constants holding bindless indices, since the backend never binds a
// per-draw descriptor table.
cbuffer TonemapTextures : register(b2) {
    uint g_scene;
    uint g_bloom;
};

struct PsInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PsInput input) : SV_Target {
    const Grade grade = LoadGrade(g_grade, g_arena);
    const float2 texcoord = 0.5 * input.uv + 0.5;
    const float meter_exposure = MeterLoadExposure(g_meter, g_arena);
    const float3 x = max(SampleTex(g_scene, texcoord).rgb, float3(0.0, 0.0, 0.0)) * meter_exposure;
    const float3 curve = ((0.22 * x + 0.03) * x + 0.002) / ((0.22 * x + 0.3) * x + 0.06);
    float3 toned = 2.3060 * curve - 0.07687;
    if (grade.bloom_strength.w != 0.0) {
        toned += grade.bloom_strength.rgb * SampleTex(g_bloom, texcoord).rgb;
    }
    return float4(toned, dot(toned, float3(0.299, 0.587, 0.114)));
}
