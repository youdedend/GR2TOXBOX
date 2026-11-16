// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer GrassRoot : register(b1) {
    uint2 scene_grass_words;
    uint2 scene_blades_words;

};
#define scene_grass ArenaMake(scene_grass_words.x, scene_grass_words.y)
#define scene_blades ArenaMake(scene_blades_words.x, scene_blades_words.y)
struct Output {float4 position:SV_Position;float4 surface:TEXCOORD0;float3 along:TEXCOORD1;nointerpolation uint instance:TEXCOORD2;};
static float4 position,surface;
static float3 along;static uint instance;
// The game's grass geometry program without its velocity, one corner of a triangle a blade: two at
// the root, as far apart as its fade sets, and the tip. Each end's normal faces the eye.
void VertexPixel(uint vertex_id) {
    const uint index = vertex_id / 3u;
    const uint corner = vertex_id % 3u;
    surface = float4(0.0, 0.0, 0.0, 0.0);
    along = float3(0.0, 0.0, 0.0);
    instance = 0u;
    // The kernel counts past the buffer when it fills.
    if (index >= MaxBlades) {
        position = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }


    const float3 root = LoadB3Blade(scene_blades,index,g_arena).root;
    const float3 tip = LoadB3Blade(scene_blades,index,g_arena).tip;
    instance = LoadB3Blade(scene_blades,index,g_arena).instance;
    const float4x4 world_view_projection = LoadB3Grass(scene_grass,instance,g_arena).draw_world_view_projection;
    const float3x3 turn = (float3x3)world_view_projection;
    const float3 direction = normalize(tip - root);
    const float3 knot = corner == 2u ? tip : root;
    const float3 view = normalize(knot - LoadB3Grass(scene_grass,instance,g_arena).eye.xyz);
    float4 clip = mul(world_view_projection, float4(knot, 1.0));
    float side = 0.0;
    if (corner != 2u) {
        side = corner == 0u ? -1.0 : 1.0;
        const float width =
            0.03 + LoadB3Blade(scene_blades,index,g_arena).fade * (LoadB3Grass(scene_grass,instance,g_arena).width - 0.03);
        clip.xy += side * (mul(turn, (normalize(cross(view, direction)) * width))).xy;
    }
    surface = float4(-(mul(turn, view)).xy, corner == 2u ? 0.5 : float(corner), corner == 2u ? 0.0 : 1.0);
    along = float3((mul(turn, direction)).xy, side);
    // OpenGL depth, -w near to w far, to reverse depth, 1 near to 0 far.
    clip.z = 0.5 * (clip.w - clip.z);
    position = clip;
}

Output main(uint vertex_id:SV_VertexID) {VertexPixel(vertex_id);Output o;o.position=position;o.surface=surface;o.along=along;o.instance=instance;return o;}
