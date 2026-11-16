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
// Keep character.vert's full physical signature, including unused attributes.
struct CharacterAuxIO {
    float4 position : SV_Position;
    float3 view_position : TEXCOORD0;
    float4 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 texcoord : TEXCOORD3;
    float2 decal_texcoord : TEXCOORD4;
    float2 model_xy : TEXCOORD5;
    float4 vertex_color : TEXCOORD6;
    nointerpolation uint draw_index : TEXCOORD7;
};
static float2 texcoord;
static uint draw_index;
static float4 pixel_position;
float4 Sample(uint h, float2 uv) { return Batch2Sample(h, uv); }
Draw AuxDraw() { return LoadDrawAt(ArenaAdd(scene_draws, draw_index * DRAW_BYTES), g_arena); }
BatchCharacter AuxCharacter(uint index) { return LoadBatchCharacter(ArenaAdd(scene_characters, index*656u), g_arena); }
struct VisionConstants { float4 movie_size, post_texcoord; float punch_through, glow; };
static float4 color;
// A texel of the screen at uv, as a point sampler reads it.
float ScreenTexel(uint handle, float2 uv) {
    const int2 size = Batch2Size(handle, 0);
    const int2 texel = clamp(int2(floor(uv * float2(size))), int2(0, 0), size - 1);
    return Batch2Fetch(handle, texel, 0).r;
}

// A movie frame in the game's surface: luma in the top two thirds, then rows of chroma pairs, U
// in the even column and V in the odd one, in video range, raised to the power 2.2.
float3 MovieColor(uint handle, float2 uv, float4 movie_size) {
    const float column = floor(movie_size.y * uv.x);
    const bool even = fmod(column, 2.0) == 0.0;
    const float u_x = even ? uv.x : uv.x - movie_size.x;
    const float v_x = even ? uv.x + movie_size.x : uv.x;
    const float chroma_y = 1.0 - uv.y / 3.0;
    const float u = ScreenTexel(handle, float2(u_x, chroma_y));
    const float v = ScreenTexel(handle, float2(v_x, chroma_y));
    const float y = 1.1644 * ScreenTexel(handle, float2(uv.x, 2.0 / 3.0 * (1.0 - uv.y))) - 0.0729;
    const float3 rgb = float3(y + 1.596 * v - 0.798, y - 0.3917 * u + 0.1958 - 0.8129 * v + 0.4065,
                          y + 2.0172 * u - 1.0086);
    return exp2(2.2 * log2(abs(rgb)));
}

// The game's G2_BG_Vision pixel program: the screen's movie, or its texture when it has none,
// under the damage map by its alpha, times the scrolling overlay, plus an offset; the glow's draw
// scales it by the blend constant the game's blend applies.
void VisionPixel() {
    const uint index = AuxDraw().material - 1u;
    ArenaRef material_ref = ArenaAdd(scene_characters, index * 656u);
    VisionConstants m; m.movie_size=ArenaLoadFloat4(material_ref,g_arena); m.post_texcoord=ArenaLoadFloat4(ArenaAdd(material_ref,16u),g_arena);
    m.punch_through=ArenaLoadFloat(ArenaAdd(material_ref,32u),g_arena); m.glow=ArenaLoadFloat(ArenaAdd(material_ref,36u),g_arena);
    if (ScreenDoorShut(m.punch_through, pixel_position.xy, pixel_position.z)) {
        discard;
    }
    // A screen whose texture is not ready yet, as a new movie's first frame, shows black.
    const uint screen = AuxDraw().texture;
    const float3 picture = screen == 0u              ? float3(0.0, 0.0, 0.0)
                         : m.movie_size.y > 1.0 ? MovieColor(screen, texcoord, m.movie_size)
                                                : Sample(screen, texcoord).rgb;
    const uint damage_map = AuxCharacter(index).normal_map;
    const float4 damage = damage_map != 0u ? Sample(damage_map, texcoord) : float4(0.0, 0.0, 0.0, 0.0);
    const float3 overlay =
        Sample(AuxCharacter(index).reflection,
               m.post_texcoord.xy + m.post_texcoord.zw * texcoord)
            .rgb;
    const float3 screen_color = lerp(picture, damage.rgb, damage.a);
    color = float4(m.glow * clamp(overlay * screen_color + m.movie_size.z, 0.0, 1.0), 1.0);
}

float4 main(CharacterAuxIO input) : SV_Target0 { texcoord=input.texcoord; draw_index=input.draw_index; pixel_position=input.position; VisionPixel(); return color; }
