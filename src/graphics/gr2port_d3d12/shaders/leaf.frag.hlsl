// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer LeafRoot : register(b1) {
    uint2 scene_leaves_words;
    uint2 shadows_words;
    uint cascade;
    uint base_instance;
};
#define scene_leaves ArenaMake(scene_leaves_words.x,scene_leaves_words.y)
#define shadows ArenaMake(shadows_words.x,shadows_words.y)
struct PixelIO {float4 position:SV_Position;float4 surface:TEXCOORD0;nointerpolation uint leaf:TEXCOORD1;};
struct Targets {float4 albedo:SV_Target0;float2 normal:SV_Target1;float4 surface:SV_Target2;};
static float4 surface,g_albedo,g_surface,pixel_position;static float2 g_normal;
static uint leaf;
float4 Sample(uint h,float2 uv){return Batch2Sample(h,uv);}
bool B3Door(float v){return ScreenDoorShut(v,pixel_position.xy,pixel_position.z);}
// The game's leaf pixel program: the leaf texture over the colour from the tip to the base, cut
// out by its alpha and a screen door, with a normal that faces the eye and leans as the geometry
// program says. Its albedo alpha, which holds the world's hatching, is 1.
void Pixel() {

    const float4 color = Sample(LoadB3Leaf(scene_leaves,leaf,g_arena).texture, surface.zw);
    if (B3Door(LoadB3Leaf(scene_leaves,leaf,g_arena).punch_through) ||
        color.a <= LoadB3Leaf(scene_leaves,leaf,g_arena).alpha_reference) {
        discard;
    }
    const float3 tint =
        lerp(LoadB3Leaf(scene_leaves,leaf,g_arena).tip_color.rgb, LoadB3Leaf(scene_leaves,leaf,g_arena).root_color.rgb, surface.w);
    const float2 lean = surface.xy;
    const float3 n = normalize(float3(lean, sqrt(max(0.0, 1.0 - dot(lean, lean)))));
    g_albedo = float4(color.rgb * tint, 1.0);
    g_normal = PackNormal(n);
    g_surface = float4(LoadB3Leaf(scene_leaves,leaf,g_arena).roughness, LoadB3Leaf(scene_leaves,leaf,g_arena).metallic,
                     LoadB3Leaf(scene_leaves,leaf,g_arena).translucency, 0.0);
}

Targets main(PixelIO input) {pixel_position=input.position;surface=input.surface;leaf=input.leaf;Pixel(); Targets o;o.albedo=g_albedo;o.normal=g_normal;o.surface=g_surface;return o;}
