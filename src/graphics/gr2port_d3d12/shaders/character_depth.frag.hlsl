// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch2_scene.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer CharacterAuxRoot : register(b1) {
    uint2 scene_draws_words;
    uint2 scene_characters_words;
};
#define scene_draws ArenaMake(scene_draws_words.x, scene_draws_words.y)
#define scene_characters ArenaMake(scene_characters_words.x, scene_characters_words.y)
struct CharacterAuxIO { float4 position : SV_Position; float2 texcoord : TEXCOORD3; nointerpolation uint draw_index : TEXCOORD7; };
static float2 texcoord;
static uint draw_index;
static float4 pixel_position;
float4 Sample(uint h, float2 uv) { return Batch2Sample(h, uv); }
Draw AuxDraw() { return LoadDrawAt(ArenaAdd(scene_draws, draw_index * DRAW_BYTES), g_arena); }
BatchCharacter AuxCharacter(uint index) { return LoadBatchCharacter(ArenaAdd(scene_characters, index*656u), g_arena); }
// A character's depth for the ambient occlusion: the pixels G2_CH_Main keeps, past its alpha step
// and its screen door, as character.frag decides them.
void CharacterDepth() {
    
    const uint index = AuxDraw().material - 1u;
    const float2 uv = texcoord;
    if (AuxCharacter(index).alpha_step != 0u) {
        const float mask = Sample(AuxDraw().texture, uv).a;
        const float2 height_uv =
            AuxCharacter(index).normal_offset + AuxCharacter(index).normal_repeat * uv;
        const float height = pow(abs(Sample(AuxCharacter(index).normal_map, height_uv).a), 2.2);
        if (AuxCharacter(index).alpha_reference >= height * mask) {
            discard;
        }
    }
    const float2 punch = AuxCharacter(index).punch_through;
    const float seed =
        256.0 * punch.x * (0.0771356 * pixel_position.x + 0.00888381 * pixel_position.y) + 1e-7;
    if (punch.y < frac(frac(abs(123456792.0 / seed)) * abs(seed))) {
        discard;
    }
}

void main(CharacterAuxIO input) { texcoord=input.texcoord; draw_index=input.draw_index; pixel_position=input.position; CharacterDepth(); }
