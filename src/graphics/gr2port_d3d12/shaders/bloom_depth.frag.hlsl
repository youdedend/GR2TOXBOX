// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// bloom_depth.frag -> bloom_depth PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/bloom_depth.frag: the game's bloom
// pass 0 (PS 0x1bb8334), the scene's depth at the bloom's size, which only the
// glow draws test against -- 1e-4 farther, so that a glow passes on its own
// surface.
//
// The D3D12 pipeline needs a colour target even for a depth-only pass, so this
// writes nothing to the bound target and sets the depth through SV_Depth. That is
// the same shape as the GLSL, which only assigns gl_FragDepth.
//
// `SV_Depth` requires the pixel shader to declare a float output or to write
// SV_Depth as an output *parameter*, which is what the out parameter below does;
// the colour target is declared but never written.

#include "common/texel.hlsli"

cbuffer BloomDepthTextures : register(b0) {
    uint g_scene_depth;
};

struct PsInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

void main(PsInput input, out float depth : SV_Depth) {
    const float2 texcoord = 0.5 * input.uv + 0.5;
    // max(x - 1e-4, 0): the D3D reverse-Z depth, so "farther" is smaller. This
    // is the same expression as the GLSL's, whose depth already runs the other
    // way from the game's.
    depth = max(SampleTex(g_scene_depth, texcoord).r - 1e-4, 0.0);
    // The GLSL has no colour output. Some drivers require a value; returning
    // nothing is legal only when there is no render target, and the backend binds
    // one for the pipeline's sake.
}
