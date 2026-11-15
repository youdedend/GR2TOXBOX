// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bloom_blur.frag -> bloom_blur PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/bloom_blur.frag: the game's bloom blur
// (PS 0x1bb84d0, and 0x1bb8d3c with the last frame). 32 taps along one axis,
// half a texel off centre, with a width picked by the depth term in alpha.
//
// HLSL specifics in this file:
//
//   * `RowWeights` cannot take `out float weights[16]` and fill it with a
//     variable-trip loop, because the compiler unrolls the array indexing. The
//     array is filled with [unroll] over a constant bound instead, which is what
//     the GLSL's constant `Weights` bound already implied.
//   * `isnan`/`isinf` are HLSL intrinsics with the same meaning.
//   * The GLSL's `texture(source, uv)` and the taps below all use the same
//     sampler; the backend's repeating bilinear sampler matches it.

#include "common/grade.hlsli"
#include "common/texel.hlsli"

cbuffer BloomBlurConstants : register(b1) {
    // Read-only per-frame grading data, an arena ref rather than a cbuffer so
    // the std430 layout is preserved exactly (common/grade.hlsli).
    uint2 g_grade_words;
    // One texel of the source along the blur's direction.
    float2 g_step_size;
    // 1 when the last frame's bloom blends in.
    uint g_feedback;
    float g_pad;
};
#define g_grade ArenaMake(g_grade_words.x, g_grade_words.y)

cbuffer BloomBlurTextures : register(b2) {
    uint g_source;
    uint g_last_bloom;
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

// The game's weight table: 16 rows of 16 Gaussian weights, row j of variance
// 1.875 * (j + 1), each row scaled so that its 32 taps sum to 1.
#define WEIGHTS 16u
static const float VariancePerRow = 1.875;
// The last frame's bloom fades by this much each frame.
static const float FeedbackFade = 1.0 / 255.0;

// Fills `weights` for one row. `weights` is passed through a static array rather
// than by reference because HLSL has no reference semantics across a call for an
// array of this kind: the GLSL's out-parameter idiom does not exist.
struct RowTable {
    float weights[16];
};

RowTable RowWeights(uint row) {
    const float variance = VariancePerRow * float(row + 1u);
    RowTable table;
    float sum = 0.0;
    [unroll] for (uint i = 0u; i < WEIGHTS; ++i) {
        table.weights[i] = exp(-float(i * i) / (2.0 * variance));
        sum += 2.0 * table.weights[i];
    }
    [unroll] for (uint second_index = 0u; second_index < WEIGHTS; ++second_index) {
        table.weights[second_index] /= sum;
    }
    return table;
}

float4 main(PsInput input) : SV_Target {
    const Grade grade = LoadGrade(g_grade, g_arena);
    const float2 uv = 0.5 * input.uv + 0.5;
    const float4 center = SampleTex(g_source, uv);
    const float t = saturate((center.a - grade.bloom_width.x) /
                             (grade.bloom_width.y - grade.bloom_width.x));
    // The table's row as the game's bilinear sample picks it, between texel
    // centres.
    const float row = clamp(t * t * (3.0 - 2.0 * t) * float(WEIGHTS) - 0.5, 0.0, 15.0);
    const uint row0 = uint(row);
    const RowTable weights0 = RowWeights(row0);
    const RowTable weights1 = RowWeights(min(row0 + 1u, WEIGHTS - 1u));
    const float between = row - float(row0);

    float3 sum = float3(0.0, 0.0, 0.0);
    [unroll] for (uint i = 0u; i < WEIGHTS; ++i) {
        const float3 pair =
            (i == 0u ? center.rgb : SampleTex(g_source, uv - float(i) * g_step_size).rgb) +
            SampleTex(g_source, uv + float(float(i + 1u)) * g_step_size).rgb;
        sum += lerp(weights0.weights[i], weights1.weights[i], between) * pair;
    }

    if (g_feedback != 0u) {
        const float keep = grade.bloom_feedback;
        sum = (1.0 - keep) * sum + keep * SampleTex(g_last_bloom, uv).rgb - FeedbackFade;
    }

    // A degenerate weight table can produce a NaN; the game zeroes it rather
    // than letting it spread through the bloom chain.
    [unroll] for (int c = 0; c < 3; ++c) {
        if (isnan(sum[c]) || isinf(sum[c])) {
            sum[c] = 0.0;
        }
    }
    return float4(sum, center.a);
}
