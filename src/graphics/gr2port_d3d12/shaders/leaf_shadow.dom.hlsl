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
struct LeafControl {float4 position:SV_Position;float3 control_point:TEXCOORD0;nointerpolation uint leaf_index:TEXCOORD1;};
struct LeafPatch {float tess[2]:SV_TessFactor;uint leaf:TEXCOORD2;};
struct LeafDomain {float4 position:SV_Position;float4 leaf_position:TEXCOORD0;float3 leaf_side:TEXCOORD1;float leaf_along:TEXCOORD2;nointerpolation uint leaf_index:TEXCOORD3;};
// The game's cosine takes revolutions: the fraction of the angle over 2 pi.
float3 Cosine(float3 angle) {
    return cos(6.2831855 * frac(abs(angle * 0.159154937)));
}

// The knot u along the leaf at t, in world space, and the offset of the leaf's edge there, in
// model space. The leaf leaves the curve's knot at the refraction of the curve's direction
// through a direction across it, which a direction of its own sets; its width turns along it by
// the twist, and gravity bends its heading down in world space.
void GrowLeaf(uint i,float2 tess_coord,float3 p0,float3 p1,float3 p2, out float4 position, out float3 offset) {
    const float t = tess_coord.y;
    const float u = tess_coord.x;
    
    const float3 a = p0 + t * (p1 - p0);
    const float3 b = p1 + t * (p2 - p1);
    const float3 tangent = b - a;
    const float3 base = a + t * tangent;
    const float3 branch = normalize(tangent);
    const float3 random = normalize(Cosine(p0 * (100.0 * t + 100.0)));
    const float3 across = cross(random, branch);
    const float eta = LoadB3Leaf(scene_leaves,i,g_arena).direction;
    const float slant = dot(across, branch);
    const float k = eta * eta - eta * eta * slant * slant;
    const float3 refracted =
        (k < 1.0 ? 1.0 : 0.0) * (eta * branch + (-eta * slant - sqrt(abs(1.0 - k))) * across);
    const float3 direction = normalize(refracted);
    const float3 width = random + LoadB3Leaf(scene_leaves,i,g_arena).twist * u * (across - random);
    offset = LoadB3Leaf(scene_leaves,i,g_arena).width * cross(cross(width, branch), direction);
    const float4x4 world = LoadB3Leaf(scene_leaves,i,g_arena).world;
    const float4 heading =
        mul(world, float4(direction, 0.0)) - float4(0.0, LoadB3Leaf(scene_leaves,i,g_arena).gravity * u, 0.0, 0.0);
    position = mul(world, float4(base, 1.0)) + LoadB3Leaf(scene_leaves,i,g_arena).leaf_length * u * normalize(heading);
}

[domain("isoline")]
LeafDomain main(LeafPatch patch,float2 uv:SV_DomainLocation,const OutputPatch<LeafControl,3> points) {
    B3Leaf leaf=LoadB3Leaf(scene_leaves,patch.leaf,g_arena);
    float4 position;float3 offset;
    GrowLeaf(patch.leaf,uv,points[0].control_point,points[1].control_point,points[2].control_point,position,offset);
    LeafDomain o;
    float4x4 projection=ArenaLoadMatrix(ArenaAdd(shadows,64u+cascade*3u*64u),g_arena);
    o.leaf_position=mul(projection,position);o.leaf_side=mul(projection,mul(leaf.world,float4(offset,0.0))).xyz;
    o.position=o.leaf_position;o.leaf_along=1.0-uv.x;o.leaf_index=patch.leaf;return o;
}
