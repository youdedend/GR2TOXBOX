// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer GrassSimulationRoot : register(b1) {
    uint2 scene_grass_words;
    uint2 scene_blades_words;
    uint2 scene_effectors_words;
    uint2 scene_grid_words;
    uint2 scene_count_words;

};
#define scene_grass ArenaMake(scene_grass_words.x, scene_grass_words.y)
#define scene_blades ArenaMake(scene_blades_words.x, scene_blades_words.y)
#define scene_effectors ArenaMake(scene_effectors_words.x, scene_effectors_words.y)
#define scene_grid ArenaMake(scene_grid_words.x, scene_grid_words.y)
#define scene_count ArenaMake(scene_count_words.x, scene_count_words.y)
static const uint TuftWords = 16u;

float3 Point(ArenaRef words, uint first) {
    return asfloat(
        uint3(B3Word(words,first,g_arena), B3Word(words,first + 1u,g_arena), B3Word(words,first + 2u,g_arena)));
}

float Acos(float x) {
    return acos(clamp(x, -1.0, 1.0));
}

// Outside the game's clip box, or as far from the eye as blades go.
bool Culled(float3 p, float4x4 world_view_projection, float4x4 world_view, float clip_max) {
    const float4 clip = mul(world_view_projection, float4(p, 1.0));
    return any((abs(clip.xyz) > float3(clip.w,clip.w,clip.w))) ||
           length((mul(world_view, float4(p, 1.0))).xyz) >= clip_max;
}

// A near blade's tip in model space: the blade leans with the wind in world space, then away from
// the first effector sphere of its grid cell that holds the tip.
float3 BentTip(uint index, float3 root, float3 tip) {



    const float4x4 world = LoadB3Grass(scene_grass,index,g_arena).world;
    const float3 world_root = (mul(world, float4(root, 1.0))).xyz;
    const float4 world_tip = mul(world, float4(tip, 1.0));
    const float blade_length = length(world_tip.xyz - world_root);
    float3 along = (world_tip.xyz - world_root) / blade_length;
    const float lean =
        0.035 + 0.035 * cos(LoadB3Grass(scene_grass,index,g_arena).phase + root.x + root.z);
    along = Turned(along, Turn(cross(along, LoadB3Grass(scene_grass,index,g_arena).extra_force.xyz), lean));
    float3 bent = world_root + blade_length * along;

    const uint list = GridList(mul(LoadB3Grass(scene_grass,index,g_arena).world_view_projection, float4(tip, 1.0)),
                               LoadB3Grass(scene_grass,index,g_arena).cluster_scale);
    const uint spheres = B3Count(scene_effectors,g_arena);
    for (uint entry = 0u; entry < spheres; entry++) {
        const uint sphere_index = B3Word(scene_grid,list+entry,g_arena);
        if (sphere_index == 0xffffffffu) {
            break;
        }
        const uint4 sphere = B3Sphere(scene_effectors,2u * sphere_index,g_arena);
        const float3 center = asfloat(sphere.xyz);
        const float radius = UnpackHalf2x16(sphere.w).x;
        const float3 away = bent - center;
        const float gap = length(away);
        if (gap < radius) {
            const float3 shell =
                gap < 1e-6 ? center - float3(0.0, radius, 0.0) : center + away * radius / gap;
            const float3 to_shell = normalize(shell - world_root);
            along = Turned(along, Turn(cross(along, to_shell),
                                       min(0.3, 0.5 * Acos(dot(along, to_shell)))));
            bent = world_root + blade_length * along;
            break;
        }
    }
    return (mul(LoadB3Grass(scene_grass,index,g_arena).world_inverse, float4(bent, world_tip.w))).xyz;
}

[numthreads(64,1,1)]
void main(uint3 dispatch_id:SV_DispatchThreadID,uint3 group_id:SV_GroupID) {
    const uint index = group_id.y;
    const uint tuft = dispatch_id.x;
    if (tuft >= LoadB3Grass(scene_grass,index,g_arena).tuft_count) {
        return;
    }
    ArenaRef words = ArenaAdd(LoadB3Grass(scene_grass,index,g_arena).tufts, uint(tuft) * TuftWords * 4u);
    const float3 root = Point(words, 0u);
    const float3 tip = Point(words, 8u);
    const float4x4 world_view_projection = LoadB3Grass(scene_grass,index,g_arena).world_view_projection;
    const float4x4 world_view = LoadB3Grass(scene_grass,index,g_arena).world_view;
    const float clip_max = LoadB3Grass(scene_grass,index,g_arena).clip_max;
    if (Culled(root, world_view_projection, world_view, clip_max) &&
        Culled(tip, world_view_projection, world_view, clip_max)) {
        return;
    }
    const float seed = asfloat(B3Word(words,3u,g_arena));
    const float blade_count = asfloat(B3Word(words,7u,g_arena));
    const uint count = uint(floor(blade_count));
    if (count == 0u) {
        return;
    }
    uint first; ArenaBufferRw(scene_count,g_arena).InterlockedAdd(scene_count.offset,3u*count,first);first/=3u;
    if (first >= MaxBlades) {
        return;
    }
    const uint written = min(count, MaxBlades - first);

    const float3 span = tip - root;
    const float inverse_length = rsqrt(dot(span, span));
    const float3 up = span * inverse_length;
    const float4 to_tuft = Turn(float3(up.z, 0.0, -up.x), 0.5 * Acos(up.y));
    const float fade =
        clamp((clip_max - length((mul(world_view, float4(root, 1.0))).xyz)) /
                  (clip_max - LoadB3Grass(scene_grass,index,g_arena).clip_min),
              0.0, 1.0);
    const float seed_floor = floor(seed);
    const float coherency = LoadB3Grass(scene_grass,index,g_arena).coherency;
    const float range_min = LoadB3Grass(scene_grass,index,g_arena).range_min;
    const float range_max = LoadB3Grass(scene_grass,index,g_arena).range_max;
    const float curve = LoadB3Grass(scene_grass,index,g_arena).offset_curve / range_max;
    const float height_offset = LoadB3Grass(scene_grass,index,g_arena).height_offset;
    const float normal_lerp = LoadB3Grass(scene_grass,index,g_arena).normal_lerp;
    const float normal_limit = LoadB3Grass(scene_grass,index,g_arena).normal_limit;
    for (uint blade = 0u; blade < written; blade++) {
        const float position =
            count == 1u ? blade_count : blade_count * float(blade) / float(count - 1u);
        const float r = frac(0.001 * (seed * seed_floor * position + seed));
        const float phi = seed_floor * sin(r * position + r);
        const float radius = range_min + r * (range_max - range_min);
        const float3 offset = Turned(float3(cos(phi) * radius, -(curve * radius + height_offset),
                                        sin(phi) * radius),
                                   to_tuft);
        float3 direction = span;
        if (coherency > 0.0) {
            const float spin_sin = sin(phi * phi);
            const float spin_cos = cos(phi * phi);
            const float3 axis = float3(spin_sin * up.y, spin_cos * up.z - spin_sin * up.x,
                                   -spin_cos * up.y);
            direction = Turned(span, Turn(axis, 0.5 * coherency));
        }
        const float3 leaning = direction * inverse_length;
        const float tilt = Acos(leaning.y);
        direction = Turned(direction, Turn(float3(leaning.z, 0.0, -leaning.x),
                                           0.5 * (min(normal_limit, normal_lerp * tilt) - tilt)));
        const float3 blade_root = root + offset;
        float3 blade_tip = blade_root + direction;
        if (fade > 0.9) {
            blade_tip = BentTip(index, blade_root, blade_tip);
        }
        ArenaRef out_ref=ArenaAdd(scene_blades,(first+blade)*32u);
        ArenaBufferRw(out_ref,g_arena).Store4(out_ref.offset,asuint(float4(blade_root,fade)));
        ArenaBufferRw(out_ref,g_arena).Store4(out_ref.offset+16u,uint4(asuint(blade_tip),index));
    }
}
