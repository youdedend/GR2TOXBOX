// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// self_translucent.frag -> self_translucent PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/self_translucent.frag: the game's
// SelfTranslucent pixel program, one colour.

#include "common/scene.hlsli"

cbuffer SceneRoot : register(b1) {
    SceneConstants g_scene;
};

cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

struct PsInput {
    float4 position : SV_Position;
    nointerpolation uint draw_index : TEXCOORD0;
};

float4 main(PsInput input) : SV_Target {
    const Draw draw = LoadDrawAt(ArenaAdd(g_scene.draws, input.draw_index * DRAW_BYTES), g_arena);
    return draw.color_scale;
}
