// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer BranchRoot : register(b1) {
    uint2 scene_branches_words;
    uint base_instance;
};
#define scene_branches ArenaMake(scene_branches_words.x, scene_branches_words.y)
struct PixelIO {float4 position:SV_Position;float4 surface:TEXCOORD0;float3 color:TEXCOORD1; nointerpolation uint branch:TEXCOORD2;};
struct Targets {float4 albedo:SV_Target0;float2 normal:SV_Target1;float4 surface:SV_Target2;};
static float4 surface,g_albedo,g_surface,pixel_position;static float2 g_normal;
static float3 color; static uint branch;
float4 Sample(uint h,float2 uv){return Batch2Sample(h,uv);}
bool B3Door(float v){return ScreenDoorShut(v,pixel_position.xy,pixel_position.z);}
// The game's branch pixel program without its velocity: the texture over the ribbon's colour, cut
// out by its alpha and a screen door, with a normal that faces the eye and leans across the ribbon.
// Its albedo alpha, which holds the world's hatching, is 1.
void Pixel() {

    const float4 texel = Sample(LoadB3Branch(scene_branches,branch,g_arena).texture, surface.zw);
    if (B3Door(LoadB3Branch(scene_branches,branch,g_arena).punch_through) ||
        texel.a <= LoadB3Branch(scene_branches,branch,g_arena).alpha_reference) {
        discard;
    }
    const float2 lean = surface.xy;
    g_albedo = float4(texel.rgb * color, 1.0);
    g_normal = PackNormal(normalize(float3(lean, sqrt(max(0.0, 1.0 - dot(lean, lean))))));
    g_surface = float4(LoadB3Branch(scene_branches,branch,g_arena).roughness, LoadB3Branch(scene_branches,branch,g_arena).metallic,
                     LoadB3Branch(scene_branches,branch,g_arena).translucency, 0.0);
}

Targets main(PixelIO input) {pixel_position=input.position;surface=input.surface;color=input.color;branch=input.branch;Pixel(); Targets o;o.albedo=g_albedo;o.normal=g_normal;o.surface=g_surface;return o;}
