// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// shadow.vert -> shadow VS_6_6.
//
// Ported from src/graphics/gr2port/shaders/shadow.glsl without ALPHA_TEST: the
// depth-only caster pass. A caster in one cascade, as the game's vertex program
// draws it: the skinned position, the world matrix, then the cascade's
// view-projection, with depth clamped to the near plane (pancaking).
//
// Two mechanical changes:
//
//   * `Casters`, `Shadows` and the cascade index are push constants in the GLSL.
//     Here `Casters` is an ArenaRef and the rest is a cbuffer (b0).
//   * The depth conversion differs from the GLSL's. Upstream writes
//     `clip.z = 0.5 * (clip.z + clip.w)`, the GL to Vulkan [0, w] conversion.
//     Shadows keep the GLSL's forward-Z conversion and comparison convention.
//     Only the main scene uses reverse Z. Cascade execution is a 5B module.

#include "common/scene.hlsli"

cbuffer ShadowConstants : register(b1) {
    uint2 g_casters_words;
    uint2 g_shadows_words;
    uint g_cascade;
};
#define g_casters ArenaMake(g_casters_words.x, g_casters_words.y)
#define g_shadows ArenaMake(g_shadows_words.x, g_shadows_words.y)

cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

float4 main(uint instance : SV_InstanceID, uint vertex_index : SV_VertexID) : SV_Position {
    // gl_BaseInstance: one caster per instance.
    const Caster caster = LoadCasterAt(ArenaAdd(g_casters, instance * CASTER_BYTES), g_arena);

    float3 model = LoadPosition(caster.vertices, caster.stride, caster.position, vertex_index, g_arena);
    if (caster.bones != 0u) {
        model = SkinPosition(model, caster.skin, caster.palette, caster.bones, vertex_index, g_arena);
    }

    const float4 position = float4(model, 1.0);
    const float4 world = mul(caster.world, position);
    float4 clip = mul(ArenaLoadMatrix(ArenaAdd(g_shadows, ShadowMatrixOffset(g_cascade, caster.table)), g_arena), world);

    // Pancaking: a caster behind the cascade's near plane is clamped onto it
    // rather than clipped away, so its shadow still starts at the plane.
    if (clip.w > 0.5) {
        clip.z = max(-clip.w, clip.z);
    }
    // Shadow maps use forward depth, exactly as shadow.glsl (world uses reverse Z).
    clip.z = 0.5 * (clip.z + clip.w);
    return clip;
}
