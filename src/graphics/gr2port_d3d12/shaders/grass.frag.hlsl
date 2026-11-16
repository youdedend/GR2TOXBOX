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
struct PixelIO {float4 position:SV_Position;float4 surface:TEXCOORD0;float3 along:TEXCOORD1; nointerpolation uint instance:TEXCOORD2;};
struct Targets {float4 albedo:SV_Target0;float2 normal:SV_Target1;float4 surface:SV_Target2;};
static float4 surface,g_albedo,g_surface,pixel_position;static float2 g_normal;
static float3 along; static uint instance;
float4 Sample(uint h,float2 uv){return Batch2Sample(h,uv);}
bool B3Door(float v){return ScreenDoorShut(v,pixel_position.xy,pixel_position.z);}
// The game's grass pixel program without its velocity: the texture over the colour from the tip to
// the root, cut out by its alpha and a screen door, with a normal map in the blade's frame. Its
// albedo alpha, which holds the world's hatching, is 1.
void Pixel() {

    const float4 color = Sample(LoadB3Grass(scene_grass,instance,g_arena).color_map, surface.zw);
    if (B3Door(LoadB3Grass(scene_grass,instance,g_arena).punch_through) ||
        color.a <= LoadB3Grass(scene_grass,instance,g_arena).alpha_reference) {
        discard;
    }
    const float3 b = normalize(float3(along.xy, sqrt(max(0.0, 1.0 - dot(along.xy, along.xy)))));
    const float3 facing =
        normalize(float3(surface.xy, sqrt(max(0.0, 1.0 - dot(surface.xy, surface.xy)))));
    const float3 t = along.z * cross(facing, b);
    float3 tangent_normal = float3(0.0, 0.0, 1.0);
    const uint normal_map = LoadB3Grass(scene_grass,instance,g_arena).normal_map;
    if (normal_map != 0u) {
        const float3 texel = 2.0 * Sample(normal_map, surface.zw).rgb - 1.0;
        tangent_normal =
            normalize(float3(LoadB3Grass(scene_grass,instance,g_arena).normal_scale * texel.xy, texel.z));
    }
    const float3 n =
        normalize(b * tangent_normal.x + t * tangent_normal.y + facing * tangent_normal.z);
    g_albedo = float4(color.rgb * lerp(LoadB3Grass(scene_grass,instance,g_arena).tip_color.rgb,
                                    LoadB3Grass(scene_grass,instance,g_arena).root_color.rgb, surface.w),
                    1.0);
    g_normal = PackNormal(n);
    g_surface = float4(LoadB3Grass(scene_grass,instance,g_arena).roughness,
                     LoadB3Grass(scene_grass,instance,g_arena).metallic,
                     LoadB3Grass(scene_grass,instance,g_arena).translucency, 0.0);
}

Targets main(PixelIO input) {pixel_position=input.position;surface=input.surface;along=input.along;instance=input.instance;Pixel(); Targets o;o.albedo=g_albedo;o.normal=g_normal;o.surface=g_surface;return o;}
