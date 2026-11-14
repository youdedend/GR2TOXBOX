// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// shadow_alpha.frag -> shadow_alpha PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/shadow_alpha.frag: texels with less
// alpha than the caster's reference let the light through.

#include "common/texel.hlsli"

struct PsInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    // Flat, so this is the provoking vertex's value for the whole primitive.
    nointerpolation uint texture_index : TEXCOORD1;
    nointerpolation float alpha_reference : TEXCOORD2;
};

void main(PsInput input) {
    const float alpha = SampleTex(input.texture_index, input.texcoord).a;
    if (alpha < input.alpha_reference) {
        discard;
    }
}
