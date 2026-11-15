// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// exposure.comp -> exposure CS_6_6.
//
// Ported from src/graphics/gr2port/shaders/exposure.comp: the game's adaption
// program, one thread. The adapted brightness moves toward the frame's log
// average, and the exposure aims for the key value within a range of stops around
// the base exposure.
//
// The GLSL's push-constant `Meter` is read *and written* here, so it is an arena
// ref into a UAV-backed arena (ARENA_SCRATCH, see common/grade.hlsli); the
// atomics in luminance.comp.hlsl need the same treatment.

#include "common/grade.hlsli"

cbuffer ExposureConstants : register(b1) {
    // Read-only per-frame grading data, an arena ref rather than a cbuffer so
    // the std430 layout is preserved exactly (common/grade.hlsli).
    uint2 g_grade_words;
    uint2 g_meter_words;
    // 1 / (the number of tiles), so the sum of tile log-luminances becomes a mean.
    float g_tile_share;
    float3 g_pad;
};
#define g_grade ArenaMake(g_grade_words.x, g_grade_words.y)
#define g_meter ArenaMake(g_meter_words.x, g_meter_words.y)

cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const Grade grade = LoadGrade(g_grade, g_arena);
    const int log_sum = MeterLoadLogSum(g_meter, g_arena);
    const float average = exp(float(log_sum) / MeterScale * g_tile_share);
    MeterStoreLogSum(g_meter, g_arena, 0);

    if (grade.adaption_range <= 0.0) {
        // Fully manual exposure: the frame's sum is discarded but the exposure
        // is the launcher's number.
        MeterStoreExposure(g_meter, g_arena, pow(2.0, grade.exposure));
        return;
    }

    const float adapted =
        MeterLoadAdapted(g_meter, g_arena) +
        grade.adaption_speed * (average - MeterLoadAdapted(g_meter, g_arena));
    MeterStoreAdapted(g_meter, g_arena, adapted);

    const float stops = log2(max(grade.key_value / adapted, 1e-6));
    const float range = grade.adaption_range;
    MeterStoreExposure(g_meter, g_arena,
                       pow(2.0, grade.exposure + clamp(stops - grade.exposure, -range, range)));
}
