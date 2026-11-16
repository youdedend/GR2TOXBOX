// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer SoftBodyRoot : register(b1) {
    uint2 jobs_words;
    uint2 effectors_words;
    uint2 grid_words;

};
#define jobs ArenaMake(jobs_words.x, jobs_words.y)
#define effectors ArenaMake(effectors_words.x, effectors_words.y)
#define grid ArenaMake(grid_words.x, grid_words.y)
uint Word(B3SoftBodyJob job,uint i) {return job.constants[i/4u][i%4u];}
float Constant(B3SoftBodyJob job,uint i) {return asfloat(Word(job,i));}
float3 ConstantVector(B3SoftBodyJob job,uint i) {return asfloat(job.constants[i/4u].xyz);}
float4x4 ConstantMatrix(B3SoftBodyJob job,uint i) {return transpose(float4x4(asfloat(job.constants[i/4u]),asfloat(job.constants[i/4u+1u]),asfloat(job.constants[i/4u+2u]),asfloat(job.constants[i/4u+3u])));}
[numthreads(64,1,1)]
void main(uint3 dispatch_id:SV_DispatchThreadID,uint3 group_id:SV_GroupID) {
    B3SoftBodyJob job=LoadB3SoftBodyJob(jobs,group_id.y,g_arena);
    ArenaRef state=job.state;
    const bool game_reset = job.reset != 0u;
    const uint count = Word(job, game_reset ? 0x12u : 0x2eu);
    const uint stride = Word(job, game_reset ? 0x11u : 0x2du);
    const uint i = dispatch_id.x;
    if (i >= count) {
        return;
    }
    // Particle record: anchor vertex and first constraint, then constraint count, anchor weight
    // and wind weight.
    ArenaRef particles = job.particles;
    const uint w0 = B3Word(particles,2u * i,g_arena);
    const uint w1 = B3Word(particles,2u * i + 1u,g_arena);
    ArenaRef skinned = ArenaAdd(job.skinned, uint(w0 & 0xffffu) * stride);
    const float3 anchor =
        asfloat(uint3(B3Word(skinned,0,g_arena), B3Word(skinned,1,g_arena), B3Word(skinned,2,g_arena)));
    const float4x4 world = ConstantMatrix(job, 0u);
    if (game_reset || ArenaBufferRw(state,g_arena).Load(state.offset) == 0u) {
        const float3 x = (mul(world, float4(anchor, 1.0))).xyz;
        B3SetState(state,2u * i,float4(x,B3State(state,2u * i,g_arena).w),g_arena);
        B3SetState(state,2u * i + 1u,float4(x,B3State(state,2u * i + 1u,g_arena).w),g_arena);
        return;
    }
    const float4 p = B3State(state,2u * i,g_arena);
    const float previous_w = B3State(state,2u * i + 1u,g_arena).w;
    float3 n = mul((float3x3)world, UnpackNormal(B3Word(skinned,3,g_arena)));
    n *= rsqrt(dot(n, n));
    const float a = Constant(job, 0x24u) * (dot(p.xyz, p.xyz) + Constant(job, 0x25u));
    const float3 wind = ConstantVector(job, 0x20u) + Constant(job, 0x26u) * (sin(a) + cos(2.0 * a));
    float3 force = Constant(job, 0x37u) * n - ConstantVector(job, 0x28u) +
                 wind * (float(w1 >> 24) * (1.0 / 255.0));

    // The particle's cell of the effector grid. Entries past the grid read 0, as the game's
    // buffer loads past their records do.
    const float4 q = mul(ConstantMatrix(job, 0x10u), float4(p.xyz, 1.0));
    const float r = 1.0 / q.w;
    const uint cx = uint(clamp(0.5 * (q.x * r) + 0.5, 0.0, 1.0) * 6.0);
    const uint cy = uint(clamp(-0.5 * (q.y * r) + 0.5, 0.0, 1.0) * 6.0);
    const uint cz = uint(clamp(q.z, 0.0, 1.0) * 10.0);
    const uint cell = (cz * 6u + cy) * 6u + cx;
    
    
    const uint mask = Word(job, 0x30u) & 0xffffu;
    const uint spheres = Word(job, 0x31u);
    for (uint s = 0u; s < spheres; s++) {
        const uint index = cell * GridEntries + s;
        const uint entry = index < GridCells * GridEntries ? B3Word(grid,index,g_arena) : 0u;
        if (entry >= B3Count(effectors,g_arena)) {
            break;
        }
        const uint4 sphere = B3Sphere(effectors,2u * entry,g_arena);
        const uint flags = B3Sphere(effectors,2u * entry + 1u,g_arena).w;
        if ((flags & mask) == 0u) {
            continue;
        }
        const float3 d = p.xyz - asfloat(sphere.xyz);
        const float l2 = max(1e-6, dot(d, d));
        const float2 radii = UnpackHalf2x16(sphere.w);
        const float t = (1.0 - min(1.0, max(0.0, sqrt(l2) - radii.y) / (radii.x - radii.y))) *
                        UnpackHalf2x16(flags).y;
        force += rsqrt(l2) * (d * t);
    }

    const float3 previous = B3State(state,2u * i + 1u,g_arena).xyz;
    const float step = Constant(job, 0x3bu);
    float3 x = Constant(job, 0x34u) * (p.xyz - previous) + p.xyz + (step * step) * force;

    // Distance constraints: u32s of neighbour particle and half float rest length.
    ArenaRef constraints = ArenaAdd(job.constraints, uint(w0 >> 16) * 4u);
    const uint links = w1 & 0xffffu;
    const uint iterations = Word(job, 0x2fu);
    const float rest_scale = Constant(job, 0x38u);
    const float stiffness = Constant(job, 0x35u);
    for (uint k = 0u; k < iterations; k++) {
        float3 sum = float3(0.0, 0.0, 0.0);
        for (uint j = 0u; j < links; j++) {
            const uint link = B3Word(constraints,j,g_arena);
            const float3 d = B3State(state,2u * (link & 0xffffu),g_arena).xyz - x;
            const float l2 = max(1e-6, dot(d, d));
            sum += 0.5 *
                   (d * ((sqrt(l2) - rest_scale * UnpackHalf2x16(link).y) * rsqrt(l2)));
        }
        x += stiffness * sum;
        B3SetState(state,2u * i,float4(x, p.w),g_arena);
        B3SetState(state,2u * i + 1u,float4(p.xyz, previous_w),g_arena);
    }
    B3SetState(state,2u * i + 1u,float4(p.xyz, previous_w),g_arena);

    // Hold: within a distance of the anchor, pulled toward it by the anchor weight and by depth
    // past the grid's fade start, and out of the anchor's surface along its normal.
    const float scale = Constant(job, 0x33u);
    const float fade = clamp(1.0 - (scale * q.z - Constant(job, 0x32u) * scale), 0.0, 1.0);
    const float anchor_weight = float((w1 >> 16) & 0xffu);
    const float held = 1.0 - (fade - (anchor_weight * fade) * (1.0 / 255.0));
    const float pin = Constant(job, 0x36u);
    const float f = max(0.0, held - pin) * (1.0 / (1.0 - pin));
    const float3 target = (mul(world, float4(anchor, 1.0))).xyz;
    const float3 offset = x - target;
    const float l2 = max(1e-6, dot(offset, offset));
    x = offset * rsqrt(l2) * min(Constant(job, 0x39u), sqrt(l2)) + target;
    x += (target - x) * f;
    x += (Constant(job, 0x3au) * n) * max(0.0, dot(n, target - x));
    B3SetState(state,2u * i,float4(x, p.w),g_arena);
}
