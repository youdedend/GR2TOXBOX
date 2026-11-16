// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#define DEFORMER_COMPUTE 1
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer SkinRoot : register(b1) {
    uint2 jobs_words;

};
#define jobs ArenaMake(jobs_words.x, jobs_words.y)
// A row of the blended matrix, in the order of operations of the game's program.
float4 BlendRow(ArenaRef rows, uint bones, uint4 b, float4 w, uint r) {
    return ((BoneRow(rows,bones,int(b.x),r,g_arena) * w.x + BoneRow(rows,bones,int(b.y),r,g_arena) * w.y) +
            BoneRow(rows,bones,int(b.z),r,g_arena) * w.z) +
           BoneRow(rows,bones,int(b.w),r,g_arena) * w.w;
}

uint PackSkinNormal(float3 n) {
    const float3 c = max(n * 0.5 + 0.5, float3(0.0, 0.0, 0.0));
    const uint x = PackHalf2x16(float2(c.x, 0.0));
    const uint y = PackHalf2x16(float2(c.y, 0.0));
    const uint z = PackHalf2x16(float2(c.z, 0.0));
    return ((x >> 4u)&0x7ffu) | ((y << 7) & 0x3ff800u) | ((z << 17) & 0xffc00000u);
}

[numthreads(64,1,1)]
void main(uint3 dispatch_id:SV_DispatchThreadID,uint3 group_id:SV_GroupID) {
    const B3SkinJob job=LoadB3SkinJob(jobs,group_id.y,g_arena);
    const uint v = dispatch_id.x;
    if (v >= job.vertices) {
        return;
    }
    const uint base = v * job.stride;
    ArenaRef source = ArenaAdd(job.source, base);
    ArenaRef destination = ArenaAdd(job.destination, base);
    ArenaRef skin = ArenaAdd(job.skin, uint(v) * 16u);
    const float4 w = float4(UnpackHalf2x16(B3Word(skin,0,g_arena)), UnpackHalf2x16(B3Word(skin,1,g_arena)));
    const uint4 b = uint4(B3Word(skin,2,g_arena) & 0xffffu, B3Word(skin,2,g_arena) >> 16, B3Word(skin,3,g_arena) & 0xffffu,
                          B3Word(skin,3,g_arena) >> 16);
    ArenaRef rows = job.palette;
    const float4 r0 = BlendRow(rows, job.bones, b, w, 0u);
    const float4 r1 = BlendRow(rows, job.bones, b, w, 1u);
    const float4 r2 = BlendRow(rows, job.bones, b, w, 2u);
    const float3 p = asfloat(uint3(B3Word(source,0,g_arena), B3Word(source,1,g_arena), B3Word(source,2,g_arena)));
    B3Store(destination,0,asuint(((r0.w + r0.x * p.x) + r0.y * p.y) + r0.z * p.z),g_arena);
    B3Store(destination,1,asuint(((r1.w + r1.x * p.x) + r1.y * p.y) + r1.z * p.z),g_arena);
    B3Store(destination,2,asuint(((r2.w + r2.x * p.x) + r2.y * p.y) + r2.z * p.z),g_arena);
    // Normals and tangents turn by the rows scaled to unit length, translation included, as the
    // game's program does.
    const float3 n0 = r0.xyz * rsqrt(dot(r0, r0));
    const float3 n1 = r1.xyz * rsqrt(dot(r1, r1));
    const float3 n2 = r2.xyz * rsqrt(dot(r2, r2));
    if (job.normal != 0u) {
        const float3 n = UnpackNormal(B3Word(source,job.normal / 4u,g_arena));
        const float3 turned = float3(dot(n0, n), dot(n1, n), dot(n2, n));
        B3Store(destination,job.normal / 4u,PackSkinNormal(turned * rsqrt(dot(turned, turned))),g_arena);
    }
    if (job.tangent != 0u) {
        const uint t01 = B3Word(source,job.tangent / 4u,g_arena);
        const uint t23 = B3Word(source,job.tangent / 4u + 1u,g_arena);
        const float3 t = float3(UnpackHalf2x16(t01), UnpackHalf2x16(t23).x);
        float3 turned = float3(dot(n0, t), dot(n1, t), dot(n2, t));
        turned *= rsqrt(dot(turned, turned));
        B3Store(destination,job.tangent / 4u,PackHalf2x16(turned.xy),g_arena);
        B3Store(destination,job.tangent / 4u + 1u,(PackHalf2x16(float2(turned.z, 0.0)) & 0xffffu) | (t23 & 0xffff0000u),g_arena);
    }
}
