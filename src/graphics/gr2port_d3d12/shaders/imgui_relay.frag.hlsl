// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// imgui_relay.frag -> imgui_relay PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/imgui_relay.frag: as the emulator's
// ImGui backend, the vertex colour times the texture, bilinear and repeating.
//
// The GLSL's `layout(offset = 24) uint texture_handle` pushes just the handle,
// because the vertex stage already owns the rest of the push-constant block. Here
// both stages read the same cbuffer, so the fragment stage declares the whole
// struct and uses only the handle.

// common/arena.hlsli for ArenaRef and ArenaTable: texel.hlsli samples through a
// handle and does not declare either, and including it was not enough.
#include "common/arena.hlsli"
#include "common/texel.hlsli"

cbuffer ImGuiConstants : register(b1) {
    ArenaRef g_vertices;
    float2 g_scale;
    float2 g_translate;
    uint g_texture_handle;
    uint2 g_pad;
};

// The arena table: the SRV index of each arena, the value the arena accessors
// going to DescriptorRaw() with. Declared by every stage that reads the
// arena, because a cbuffer has to be visible where it is used.
cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

struct PsInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

float4 main(PsInput input) : SV_Target {
    // The GLSL multiplies by the full RGBA sample, alpha included, because the
    // ImGui pipeline blends with the texture's alpha.
    return input.color * SampleTex(g_texture_handle, input.texcoord);
}
