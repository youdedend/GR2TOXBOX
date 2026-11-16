// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// lambert.vert -> lambert VS_6_6.
//
// Ported from src/graphics/gr2port/shaders/lambert.vert: the game's opaque/Lambert
// vertex program, which is the main surface path. It writes the model's clip-space
// position, its three shadow-cascade coordinates and the model-space normal the
// pixel stage lights with.
//
// gl_InstanceIndex becomes SV_InstanceID and gl_BaseInstance becomes the
// `base_instance` root constant (scene.hlsli explains why HLSL has no equivalent).
// Shared capture supplies raw OpenGL clip matrices; apply the GLSL Z conversion.

#include "common/scene.hlsli"

// The per-character constants the pixel stage reads, at
// Characters + (material - 1) * CharacterBytes in scene.glsl's terms.
//
//   0   float4 light_direction    16
//  16   float4 color_scale        16
//  32   float4x4 shadow_projection[3]   192
// = 224 bytes, a multiple of the 16-byte alignment the GLSL declares.
//
// The struct and its loader are repeated in lambert.frag.hlsl, as they are in
// world_decal's pair: HLSL has no shared interface file for one record read by two
// stages, and a header for a single struct would cost more indirection than the
// duplication does. If one changes, both change.
#define LAMBERT_CONSTANTS_BYTES 656u

struct LambertConstants {
    float4 light_direction;
    float4 color_scale;
    float4x4 shadow_projection[3];
};

LambertConstants LoadLambertConstantsAt(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    LambertConstants constants;
    constants.light_direction = asfloat(buffer.Load4(ref.offset + 0));
    constants.color_scale = asfloat(buffer.Load4(ref.offset + 16));
    [unroll] for (uint cascade = 0u; cascade < 3u; ++cascade) {
        constants.shadow_projection[cascade] =
            ArenaLoadMatrix(ArenaAdd(ref, 32u + cascade * 64u), table);
    }
    return constants;
}

// b0 is the arena table and b1 the scene constants, which is the root signature's
// layout (pipeline.h): the arena table is shared by every shader and never moves,
// so it takes the lowest slot.
cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

cbuffer LambertSceneRoot : register(b1) {
    // The draw table, then the character table (scene.glsl's `Draws scene_draws;
    // Characters scene_characters;`), each an ArenaRef where the GLSL had a
    // buffer_reference. g_base_instance is gl_BaseInstance.
    uint2 g_scene_draws_words;
    uint2 g_scene_characters_words;
    uint g_base_instance;
    // One pad word: the block is two ArenaRefs and this uint, which is exactly
    // RootConstants::DrawBytes (24 bytes, 6 words) in pipeline.h. The root
    // signature binds that many 32-bit values at b1, so a longer block here would
    // bind fewer than it declares and read whatever followed.
    uint g_pad;
};
#define g_scene_draws ArenaMake(g_scene_draws_words.x, g_scene_draws_words.y)
#define g_scene_characters ArenaMake(g_scene_characters_words.x, g_scene_characters_words.y)

struct VsOutput {
    float4 position : SV_Position;
    // The cascades' coordinates before the divide, as the GLSL writes them.
    float4 shadow_clip0 : TEXCOORD0;
    float4 shadow_clip1 : TEXCOORD1;
    float4 shadow_clip2 : TEXCOORD2;
    float3 model_normal : TEXCOORD3;
    float2 texcoord : TEXCOORD4;
    // Flat: the pixel stage indexes the draw table with it, so it has to be the
    // provoking vertex's value for the whole primitive rather than interpolated.
    nointerpolation uint draw_index : TEXCOORD5;
};

VsOutput main(uint vertex_index : SV_VertexID, uint instance : SV_InstanceID) {
    VsOutput output;

    // gl_BaseInstance, not SV_InstanceID: an instanced draw's instance index is
    // relative to the base the game passes. Both are equal on a direct draw.
    const uint draw_index = g_base_instance + instance;
    output.draw_index = draw_index;

    const Draw draw = LoadDrawAt(ArenaAdd(g_scene_draws, draw_index * DRAW_BYTES), g_arena);

    const float3 unskinned = LoadPosition(draw.vertices, draw.stride, draw.position,
                                          vertex_index, g_arena);
    const float4 model =
        float4(draw.bones != 0u
                   ? SkinPosition(unskinned, draw.skin, draw.palette, draw.bones, vertex_index,
                                  g_arena)
                   : unskinned,
               1.0);
    float4 clip = mul(draw.world_view_projection, model);
    clip.z = 0.5 * (clip.w - clip.z);
    output.position = clip;

    // The character constants for this draw's material. `material` is 1-based and
    // 0 means none, which underflows to a slot before the table; the GLSL has the
    // same expression, so it is kept rather than "fixed" -- the capture never
    // writes a draw with no material into this pass.
    const uint index = draw.material - 1u;
    const LambertConstants constants = LoadLambertConstantsAt(
        ArenaAdd(g_scene_characters, index * LAMBERT_CONSTANTS_BYTES), g_arena);
    output.shadow_clip0 = mul(constants.shadow_projection[0], model);
    output.shadow_clip1 = mul(constants.shadow_projection[1], model);
    output.shadow_clip2 = mul(constants.shadow_projection[2], model);

    // The normal element's format index is above the byte offset; 0 means the
    // shape has no normals and the GLSL falls back to +Z.
    const float3 normal = (draw.normal >> 8) != 0u
                              ? LoadNormal(draw.vertices, draw.stride, draw.normal, vertex_index,
                                           g_arena)
                              : float3(0.0, 0.0, 1.0);
    output.model_normal = draw.bones != 0u
                              ? SkinDirection(normal, draw.skin, draw.palette, draw.bones,
                                              vertex_index, g_arena)
                              : normal;
    output.texcoord = LoadTexcoord(draw.vertices, draw.stride, draw.texcoord, vertex_index,
                                   g_arena);
    return output;
}
