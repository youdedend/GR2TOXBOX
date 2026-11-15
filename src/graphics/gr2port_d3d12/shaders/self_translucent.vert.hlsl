// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// self_translucent.vert -> self_translucent VS_6_6.
//
// Ported from src/graphics/gr2port/shaders/self_translucent.vert: the game's
// SelfTranslucent vertex program, the skin moved along its normal. A draw's
// world_view holds the game's WorldViewProj, and element [0][0] of its
// world_view_projection is the distance to move by.
//
// This shader is a good example of why the projection is converted on the host
// rather than in the shader: it reads the *matrix* itself
// (world_view_projection[0][0]) as a distance, so a shader-side fix-up would
// have to leave that element alone while transforming the rest. The backend
// applies the reverse-Z affine transform once, on upload, and element [0][0]
// (the m00 scale) is untouched by it.

#include "common/scene.hlsli"

cbuffer SceneRoot : register(b1) {
    SceneConstants g_scene;
};

cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

struct VsOutput {
    float4 position : SV_Position;
    nointerpolation uint draw_index : TEXCOORD0;
};

VsOutput main(uint vertex_index : SV_VertexID) {
    VsOutput output;
    const uint draw_index = g_scene.base_instance;
    output.draw_index = draw_index;

    const Draw draw = LoadDrawAt(ArenaAdd(g_scene.draws, draw_index * DRAW_BYTES), g_arena);

    const float3 normal = LoadNormal(draw.vertices, draw.stride, draw.normal, vertex_index, g_arena);
    float3 model = LoadPosition(draw.vertices, draw.stride, draw.position, vertex_index, g_arena) +
                   draw.world_view_projection[0][0] * normal;
    if (draw.bones != 0u) {
        model = SkinPosition(model, draw.skin, draw.palette, draw.bones, vertex_index, g_arena);
    }

    // The GLSL uses world_view here, not world_view_projection: the draw's
    // world_view already carries the game's WorldViewProj for this pass.
    output.position = mul(draw.world_view, float4(model, 1.0));
    return output;
}
