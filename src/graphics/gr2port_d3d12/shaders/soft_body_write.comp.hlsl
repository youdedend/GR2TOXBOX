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
    const uint v = dispatch_id.x;
    if (v == 0u) {
        ArenaBufferRw(state,g_arena).Store(state.offset,1u);
    }
    // Write constants: the matrix, then vertex count and stride.
    if (v >= job.write[4].x) {
        return;
    }
    const uint pair = B3Word(job.map,v>>1u,g_arena);
    const uint particle = (pair >> ((v & 1u) * 16u)) & 0xffffu;
    const float4x4 m = transpose(float4x4(asfloat(job.write[0]), asfloat(job.write[1]),asfloat(job.write[2]),asfloat(job.write[3])));
    const float3 x = (mul(m, float4(B3State(state,2u * particle,g_arena).xyz, 1.0))).xyz;
    ArenaRef destination = ArenaAdd(job.destination, uint(v) * job.write[4].y);
    B3Store(destination,0,asuint(x.x),g_arena);
    B3Store(destination,1,asuint(x.y),g_arena);
    B3Store(destination,2,asuint(x.z),g_arena);
}
