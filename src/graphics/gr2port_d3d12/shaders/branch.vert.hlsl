// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer BranchRoot : register(b1) {
    uint2 scene_branches_words;
    uint base_instance;
};
#define scene_branches ArenaMake(scene_branches_words.x, scene_branches_words.y)
struct Output {float4 position:SV_Position;float4 surface:TEXCOORD0;float3 color:TEXCOORD1;nointerpolation uint branch:TEXCOORD2;};
static float4 position,surface;
static float3 color;static uint branch;
// The two triangles of a quad between two points of a curve, each corner an end and a side.
static const uint QuadCorners[6] = {0u,1u,2u,2u,1u,3u};

float3 Point(ArenaRef vertices, uint knot, uint stride) {
    ArenaRef words = ArenaAdd(vertices, uint(knot) * stride);
    return asfloat(uint3(B3Word(words,0,g_arena), B3Word(words,1,g_arena), B3Word(words,2,g_arena)));
}

// The game's branch domain and geometry programs without their velocity: a ribbon along each
// quadratic Bezier curve of three vertices, of the instance's segments, facing the eye. A vertex is
// a quad's corner, of a segment, of a curve.
void VertexPixel(uint vertex_id) {

    branch = base_instance;
    const uint corner = QuadCorners[vertex_id % 6u];
    const uint quad = vertex_id / 6u;
    const uint segments = LoadB3Branch(scene_branches,branch,g_arena).segments;
    const uint first_point = quad / segments * 3u;
    const uint side_index = corner & 1u;
    const float u = float(quad % segments + (corner >> 1u)) / float(segments);
    const ArenaRef vertices = LoadB3Branch(scene_branches,branch,g_arena).vertices;
    const uint stride = LoadB3Branch(scene_branches,branch,g_arena).stride;
    const float3 p0 = Point(vertices, first_point, stride);
    const float3 p1 = Point(vertices, first_point + 1u, stride);
    const float3 p2 = Point(vertices, first_point + 2u, stride);
    const float3 a = lerp(p0, p1, u);
    const float3 b = lerp(p1, p2, u);
    const float3 knot = lerp(a, b, u);
    const float3 view = normalize(knot - LoadB3Branch(scene_branches,branch,g_arena).eye.xyz);
    const float width = lerp(LoadB3Branch(scene_branches,branch,g_arena).root_width,
                            LoadB3Branch(scene_branches,branch,g_arena).tip_width, u);
    const float3 across = normalize(cross(view, normalize(b - a))) * width;
    const float4x4 world_view_projection = LoadB3Branch(scene_branches,branch,g_arena).world_view_projection;
    const float2 clip_across = (mul((float3x3)world_view_projection, across)).xy;
    const float side = side_index == 0u ? -1.0 : 1.0;
    float4 clip = mul(world_view_projection, float4(knot, 1.0));
    clip.xy += side * clip_across;
    const float4 uv_range = LoadB3Branch(scene_branches,branch,g_arena).uv_range;
    surface = float4(side * normalize(clip_across), uv_range.x + float(side_index) * uv_range.z,
                   uv_range.y + uv_range.w * (1.0 - u));
    color = lerp(LoadB3Branch(scene_branches,branch,g_arena).root_color.rgb, LoadB3Branch(scene_branches,branch,g_arena).tip_color.rgb,
                u);
    // OpenGL depth, -w near to w far, to reverse depth, 1 near to 0 far.
    clip.z = 0.5 * (clip.w - clip.z);
    position = clip;
}

Output main(uint vertex_id:SV_VertexID) {VertexPixel(vertex_id);Output o;o.position=position;o.surface=surface;o.color=color;o.branch=branch;return o;}
