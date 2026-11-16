// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer BranchSimulationRoot : register(b1) {
    uint2 scene_jobs_words;
    uint2 scene_effectors_words;
    uint2 scene_grid_words;
    float steady_phase,shaken_phase,start_phase,shake_frames;
};
#define scene_jobs ArenaMake(scene_jobs_words.x, scene_jobs_words.y)
#define scene_effectors ArenaMake(scene_effectors_words.x, scene_effectors_words.y)
#define scene_grid ArenaMake(scene_grid_words.x, scene_grid_words.y)
static const uint PointFloats = 4u;
static const uint PatchPoints = 3u;
static const uint StateFloats = 3u;
// The sphere mask's bit of the spheres that shake branches without stopping.
static const uint MaskSteady = 8u;
static const float QuietSway = 0.03;
static const float ShakenSway = 0.1;

float3 Point(ArenaRef words, uint knot) {
    const uint first = knot * PointFloats;
    return asfloat(
        uint3(B3Word(words,first,g_arena), B3Word(words,first + 1u,g_arena), B3Word(words,first + 2u,g_arena)));
}

// Whether a segment from a to b passes through a sphere around a knot.
bool Touches(float3 knot, float3 a, float3 b, float radius_squared) {
    const float3 span = b - a;
    const float span_squared = dot(span, span);
    const float3 to_point = knot - a;
    if (dot(to_point, to_point) <= radius_squared) {
        return true;
    }
    if (span_squared < 1e-4) {
        return false;
    }
    const float3 from_point = b - knot;
    if (dot(from_point, from_point) <= radius_squared) {
        return true;
    }
    const float t = dot(span, to_point) / span_squared;
    if (t < 0.0 || t > 1.0) {
        return false;
    }
    const float3 gap = span * t - to_point;
    return dot(gap, gap) <= radius_squared;
}

// 2 when a steady sphere of the list touches the segment, else 1 when another does, else 0. A
// sphere also touches where its motion would carry it in five frames.
uint Hit(uint list, float3 a, float3 b) {


    const uint spheres = B3Count(scene_effectors,g_arena);
    uint hit = 0u;
    for (uint entry = 0u; entry < spheres; entry++) {
        const uint sphere_index = B3Word(scene_grid,list+entry,g_arena);
        if (sphere_index == 0xffffffffu) {
            break;
        }
        const uint4 sphere = B3Sphere(scene_effectors,2u * sphere_index,g_arena);
        const uint4 motion = B3Sphere(scene_effectors,2u * sphere_index + 1u,g_arena);
        const bool steady = (motion.w & MaskSteady) != 0u;
        if (!steady && hit != 0u) {
            continue;
        }
        const float3 center = asfloat(sphere.xyz);
        const float3 ahead = center + 5.0 * (center - asfloat(motion.xyz));
        const float radius = UnpackHalf2x16(sphere.w).x;
        const float radius_squared = radius * radius;
        if (Touches(center, a, b, radius_squared) || Touches(ahead, a, b, radius_squared)) {
            if (steady) {
                return 2u;
            }
            hit = 1u;
        }
    }
    return hit;
}

[numthreads(64,1,1)]
void main(uint3 dispatch_id:SV_DispatchThreadID,uint3 group_id:SV_GroupID) {

    const uint index = group_id.y;
    const uint segment = dispatch_id.x;
    if (segment >= LoadB3BranchJob(scene_jobs,index,g_arena).segments) {
        return;
    }
    ArenaRef source = ArenaAdd(LoadB3BranchJob(scene_jobs,index,g_arena).source, uint(segment) * PatchPoints * PointFloats * 4u);
    const float3 base = Point(source, 0u);
    const float3 middle = Point(source, 1u);
    const float3 end = Point(source, 2u);
    const float4x4 world = LoadB3BranchJob(scene_jobs,index,g_arena).world;
    const float3 world_base = (mul(world, float4(base, 1.0))).xyz;
    const float4 world_end = mul(world, float4(end, 1.0));
    const float3 span = world_end.xyz - world_base;
    const float span_squared = dot(span, span);
    if (span_squared == 0.0) {
        return;
    }
    const uint hit = Hit(GridList(mul(LoadB3BranchJob(scene_jobs,index,g_arena).world_view_projection, float4(end, 1.0)),
                                  LoadB3BranchJob(scene_jobs,index,g_arena).cluster_scale),
                         world_base, world_end.xyz);

    ArenaRef state = LoadB3BranchJob(scene_jobs,index,g_arena).state;
    const uint first_state = segment * StateFloats;
    float timer = B3Float(state,first_state,g_arena);
    float phase = B3Float(state,first_state + 1u,g_arena);
    float sway = QuietSway;
    if (hit == 2u) {
        phase += steady_phase;
    } else if (timer >= 1.0) {
        timer -= 1.0;
        phase += shaken_phase;
        sway = ShakenSway;
    } else if (timer != 0.0) {
        phase = 0.0;
        timer = hit == 0u ? 0.0 : timer;
    } else if (hit == 1u) {
        timer = shake_frames;
        phase += start_phase;
        sway = ShakenSway;
    }
    B3Store(state,first_state,asuint(timer),g_arena);
    B3Store(state,first_state + 1u,asuint(phase),g_arena);

    const float half_angle =
        0.5 * sway * (1.0 + cos(base.x + phase + LoadB3BranchJob(scene_jobs,index,g_arena).cycle + base.z));
    const float3 along = span * rsqrt(span_squared);
    const float3 turned = Turned(along, Turn(float3(0.0, along.z, -along.y), half_angle));
    const float3 new_end = (mul(LoadB3BranchJob(scene_jobs,index,g_arena).world_inverse, float4(world_base + sqrt(span_squared) * turned, world_end.w)))
                             .xyz;
    const float3 new_middle = middle + 0.5 * (new_end - end);
    ArenaRef points = ArenaAdd(LoadB3BranchJob(scene_jobs,index,g_arena).destination, uint(segment) * PatchPoints * PointFloats * 4u);
    for (uint axis = 0u; axis < 3u; axis++) {
        B3Store(points,PointFloats + axis,asuint(new_middle[axis]),g_arena);
        B3Store(points,2u * PointFloats + axis,asuint(new_end[axis]),g_arena);
    }
}
