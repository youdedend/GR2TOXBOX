// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// imgui_relay.vert -> imgui_relay VS_6_6.
//
// Ported from src/graphics/gr2port/shaders/imgui_relay.vert: ImGui's vertices, a
// position, texture coordinates and a colour of four bytes, five words each.
//
// The GLSL's `layout(buffer_reference) readonly buffer Vertices { uint words[]; }`
// becomes a plain bindless ByteAddressBuffer reached through an ArenaRef, the
// same as every other vertex pull (common/arena.hlsli).

#include "common/arena.hlsli"

cbuffer ImGuiConstants : register(b0) {
    ArenaRef g_vertices;
    float2 g_scale;
    float2 g_translate;
    uint g_texture_handle;
    uint2 g_pad;
};

cbuffer ArenaConstants : register(b1) {
    ArenaTable g_arena;
};

struct VsOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

VsOutput main(uint vertex_index : SV_VertexID) {
    VsOutput output;
    const uint base = vertex_index * 5u;
    const uint4 first = ArenaLoad4(ArenaAdd(g_vertices, base * 4u), g_arena);
    const uint color_word = ArenaLoad(ArenaAdd(g_vertices, (base + 4u) * 4u), g_arena);

    const float2 position = asfloat(first.xy);
    output.texcoord = asfloat(first.zw);
    output.color = UnpackUnorm4x8(color_word);
    output.position = float4(position * g_scale + g_translate, 0.0, 1.0);
    return output;
}
