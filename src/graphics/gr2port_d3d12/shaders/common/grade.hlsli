// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The grading and exposure constants: the HLSL counterpart of upstream's `Grade`
// and `Meter` buffer references in scene.glsl.
//
// Why these are arena refs and not cbuffers. Both would fit in a root-constant or
// cbuffer block, and doing that was the first attempt -- but a cbuffer array
// element is padded to a 16-byte register: `float knots[3][8]` would occupy 384
// bytes in a cbuffer where std430 (and the host struct the Vulkan backend shares)
// gives it 96. Every field after it would then be read from the wrong offset, and
// the failure mode is a black screen on a console with no diagnostic anywhere.
// Going through the arena keeps the layouts identical by construction, at the
// cost of explicit byte offsets, which are listed below and checked against
// upstream's struct.
//
// Grade is per-frame read-only data; Meter is written across frames -- the
// luminance compute shader accumulates into `log_sum`, and the exposure shader
// updates `exposure` and `adapted` -- so Meter additionally needs the UAV-backed
// ARENA_SCRATCH.

#ifndef GR2_COMMON_GRADE_HLSLI
#define GR2_COMMON_GRADE_HLSLI

#include "common/arena.hlsli"

// Grade, field for field with scene.glsl's Grade, in std430 byte offsets:
//
//    0  float  exposure
//    4  float  adaption_range
//    8  float  adaption_speed
//   12  float  key_value
//   16  float  knots[3][8]        96 bytes (three rows of eight)
//  112  float4 distortion
//  128  float2 fluctuation
//  136  float  saturation
//  140  uint   distorted
//  144  float4 bloom_low
//  160  float4 bloom_high
//  176  float4 bloom_correction
//  192  float4 bloom_depths
//  208  float4 bloom_strength
//  224  float2 bloom_width
//  232  float  bloom_feedback
//  236  float  bloom_padding
// = 240 bytes, a multiple of the 16-byte buffer_reference_align.
#define GRADE_BYTES 240u

struct Grade {
    float exposure;
    float adaption_range;
    float adaption_speed;
    float key_value;
    // A row-major 3x8 table of tone-curve knots.
    float knots[3][8];
    float4 distortion;
    float2 fluctuation;
    float saturation;
    uint distorted;
    float4 bloom_low;
    float4 bloom_high;
    float4 bloom_correction;
    float4 bloom_depths;
    float4 bloom_strength;
    float2 bloom_width;
    float bloom_feedback;
    float bloom_padding;
};

// Loads the block field by field. ByteAddressBuffer only loads scalars and
// vectors, and a struct load would reintroduce the packing question this whole
// header exists to avoid.
Grade LoadGrade(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    Grade grade;
    grade.exposure = asfloat(buffer.Load(ref.offset + 0));
    grade.adaption_range = asfloat(buffer.Load(ref.offset + 4));
    grade.adaption_speed = asfloat(buffer.Load(ref.offset + 8));
    grade.key_value = asfloat(buffer.Load(ref.offset + 12));
    [unroll] for (uint row = 0u; row < 3u; ++row) {
        [unroll] for (uint column = 0u; column < 8u; ++column) {
            grade.knots[row][column] =
                asfloat(buffer.Load(ref.offset + 16 + (row * 8u + column) * 4u));
        }
    }
    grade.distortion = asfloat(buffer.Load4(ref.offset + 112));
    grade.fluctuation = asfloat(buffer.Load2(ref.offset + 128));
    grade.saturation = asfloat(buffer.Load(ref.offset + 136));
    grade.distorted = buffer.Load(ref.offset + 140);
    grade.bloom_low = asfloat(buffer.Load4(ref.offset + 144));
    grade.bloom_high = asfloat(buffer.Load4(ref.offset + 160));
    grade.bloom_correction = asfloat(buffer.Load4(ref.offset + 176));
    grade.bloom_depths = asfloat(buffer.Load4(ref.offset + 192));
    grade.bloom_strength = asfloat(buffer.Load4(ref.offset + 208));
    grade.bloom_width = asfloat(buffer.Load2(ref.offset + 224));
    grade.bloom_feedback = asfloat(buffer.Load(ref.offset + 232));
    grade.bloom_padding = asfloat(buffer.Load(ref.offset + 236));
    return grade;
}

// Meter, field for field with scene.glsl's Meter (16 bytes, 12 used):
//
//   0  int   log_sum   -- the frame's sum of log luminance, in units of 1/MeterScale
//   4  float exposure  -- the exposure the picture uses
//   8  float adapted   -- the brightness the meter has adapted to
#define METER_BYTES 16u
#define METER_LOG_SUM 0u
#define METER_EXPOSURE 4u
#define METER_ADAPTED 8u

// 1 / MeterScale from scene.glsl: the fixed-point scale of the log-luminance sum.
static const float MeterScale = 256.0;

float MeterLoadExposure(ArenaRef meter, ArenaTable table) {
    return asfloat(ArenaLoad(ArenaAdd(meter, METER_EXPOSURE), table));
}

void MeterStoreExposure(ArenaRef meter, ArenaTable table, float exposure) {
    ArenaStore(ArenaAdd(meter, METER_EXPOSURE), table, asuint(exposure));
}

float MeterLoadAdapted(ArenaRef meter, ArenaTable table) {
    return asfloat(ArenaLoad(ArenaAdd(meter, METER_ADAPTED), table));
}

void MeterStoreAdapted(ArenaRef meter, ArenaTable table, float adapted) {
    ArenaStore(ArenaAdd(meter, METER_ADAPTED), table, asuint(adapted));
}

int MeterLoadLogSum(ArenaRef meter, ArenaTable table) {
    return asint(ArenaLoad(ArenaAdd(meter, METER_LOG_SUM), table));
}

void MeterStoreLogSum(ArenaRef meter, ArenaTable table, int log_sum) {
    ArenaStore(ArenaAdd(meter, METER_LOG_SUM), table, asuint(log_sum));
}

#endif // GR2_COMMON_GRADE_HLSLI
