// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// shadow_alpha.vert -> shadow_alpha VS_6_6.
//
// Ported from src/graphics/gr2port/shaders/shadow.glsl with ALPHA_TEST: the
// caster pass that also forwards the texture coordinates the pixel stage cuts
// with. See shadow.vert.hlsl for the body; the only difference is the three
// extra interpolants.

#include "common/scene.hlsli"

cbuffer ShadowConstants : register(b0) {
    ArenaRef g_casters;
    Shadows g_shadows;
    uint g_cascade;
};

cbuffer ArenaConstants : register(b1) {
    ArenaTable g_arena;
};

struct VsOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    // Flat: one texture per caster, not per pixel.
    uint texture_index : TEXCOORD1;
    float alpha_reference : TEXCOORD2;
};

VsOutput main(uint instance : SV_InstanceID, uint vertex_index : SV_VertexID) {
    const Caster caster = LoadCasterAt(ArenaAdd(g_casters, instance * CASTER_BYTES), g_arena);

    float3 model = LoadPosition(caster.vertices, caster.stride, caster.position, vertex_index, g_arena);
    if (caster.bones != 0u) {
        model = SkinPosition(model, caster.skin, caster.palette, caster.bones, vertex_index, g_arena);
    }

    const float4 position = float4(model, 1.0);
    const float4 world = mul(caster.world, position);
    float4 clip = mul(LoadShadowMatrix(g_shadows, g_cascade, caster.table), world);
    if (clip.w > 0.5) {
        clip.z = max(-clip.w, clip.z);
    }
    clip.z = 0.5 * (clip.w - clip.z);

    VsOutput output;
    output.position = clip;
    output.texcoord = LoadTexcoord(caster.vertices, caster.stride, caster.texcoord, vertex_index, g_arena);
    output.texture_index = caster.texture;
    output.alpha_reference = caster.alpha_reference;
    return output;
}
