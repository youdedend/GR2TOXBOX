// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// ui_present.frag -> ui_present PS_6_6.
//
// The frame's back buffer over the window, through the clamping bilinear
// sampler. Ported from src/graphics/gr2port/shaders/ui_present.frag.
//
// The GLSL picks samplers[1] out of a descriptor array; here sampler index 3 of
// the bindless sampler heap is the clamping bilinear one (SMP_LINEAR_CLAMP), and
// the picture handle arrives as a root constant.

#include "common/texel.hlsli"

cbuffer PresentConstants : register(b0) {
    uint g_picture;
};

struct PsInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PsInput input) : SV_Target {
    // The GLSL takes `position` in (-1, 1); fullscreen.vert.hlsl forwards the
    // same value, so the 0.5 * position + 0.5 is the same arithmetic.
    const float2 texcoord = 0.5 * input.uv + 0.5;
    const float3 picture = SampleTexWith(g_picture, SMP_LINEAR_CLAMP, texcoord).rgb;
    return float4(picture, 1.0);
}
