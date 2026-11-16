// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GR2_BATCH3_HLSLI
#define GR2_BATCH3_HLSLI
#include "common/batch2_scene.hlsli"
ArenaRef ArenaLoadRef(ArenaRef r,ArenaTable t) {uint2 w=ArenaLoad2(r,t);return ArenaMake(w.y,w.x);}
uint PackHalf2x16(float2 v) {return f32tof16(v.x)|(f32tof16(v.y)<<16u);}
struct B3Leaf {

    float4x4 world_view_projection;
    float4x4 world;
    float4x4 view_projection;
    float4 tip_color;
    float4 root_color;
    ArenaRef vertices;
    uint stride;
    uint position;
    uint texture;
    float tessellation;
    float isolines;
    float direction;
    float twist;
    float width;
    float gravity;
    float leaf_length;
    float alpha_reference;
    float translucency;
    float roughness;
    float metallic;
    float punch_through;

};
static const uint B3LeafBytes=304u;
B3Leaf LoadB3Leaf(ArenaRef base,uint index,ArenaTable table) {
    ArenaRef ref=ArenaAdd(base,index*B3LeafBytes);
    ByteAddressBuffer buffer=ArenaBuffer(ref,table);
    B3Leaf v;
    v.world_view_projection=ArenaLoadMatrix(ArenaAdd(ref,0u),table);
    v.world=ArenaLoadMatrix(ArenaAdd(ref,64u),table);
    v.view_projection=ArenaLoadMatrix(ArenaAdd(ref,128u),table);
    v.tip_color=asfloat(buffer.Load4(ref.offset+192u));
    v.root_color=asfloat(buffer.Load4(ref.offset+208u));
    v.vertices=ArenaLoadRef(ArenaAdd(ref,224u),table);
    v.stride=buffer.Load(ref.offset+232u);
    v.position=buffer.Load(ref.offset+236u);
    v.texture=buffer.Load(ref.offset+240u);
    v.tessellation=asfloat(buffer.Load(ref.offset+244u));
    v.isolines=asfloat(buffer.Load(ref.offset+248u));
    v.direction=asfloat(buffer.Load(ref.offset+252u));
    v.twist=asfloat(buffer.Load(ref.offset+256u));
    v.width=asfloat(buffer.Load(ref.offset+260u));
    v.gravity=asfloat(buffer.Load(ref.offset+264u));
    v.leaf_length=asfloat(buffer.Load(ref.offset+268u));
    v.alpha_reference=asfloat(buffer.Load(ref.offset+272u));
    v.translucency=asfloat(buffer.Load(ref.offset+276u));
    v.roughness=asfloat(buffer.Load(ref.offset+280u));
    v.metallic=asfloat(buffer.Load(ref.offset+284u));
    v.punch_through=asfloat(buffer.Load(ref.offset+288u));
    return v;
}
struct B3Branch {

    float4x4 world_view_projection;
    float4 eye;
    float4 uv_range;
    float4 tip_color;
    float4 root_color;
    ArenaRef vertices;
    uint stride;
    uint segments;
    uint texture;
    float tip_width;
    float root_width;
    float alpha_reference;
    float translucency;
    float roughness;
    float metallic;
    float punch_through;

};
static const uint B3BranchBytes=176u;
B3Branch LoadB3Branch(ArenaRef base,uint index,ArenaTable table) {
    ArenaRef ref=ArenaAdd(base,index*B3BranchBytes);
    ByteAddressBuffer buffer=ArenaBuffer(ref,table);
    B3Branch v;
    v.world_view_projection=ArenaLoadMatrix(ArenaAdd(ref,0u),table);
    v.eye=asfloat(buffer.Load4(ref.offset+64u));
    v.uv_range=asfloat(buffer.Load4(ref.offset+80u));
    v.tip_color=asfloat(buffer.Load4(ref.offset+96u));
    v.root_color=asfloat(buffer.Load4(ref.offset+112u));
    v.vertices=ArenaLoadRef(ArenaAdd(ref,128u),table);
    v.stride=buffer.Load(ref.offset+136u);
    v.segments=buffer.Load(ref.offset+140u);
    v.texture=buffer.Load(ref.offset+144u);
    v.tip_width=asfloat(buffer.Load(ref.offset+148u));
    v.root_width=asfloat(buffer.Load(ref.offset+152u));
    v.alpha_reference=asfloat(buffer.Load(ref.offset+156u));
    v.translucency=asfloat(buffer.Load(ref.offset+160u));
    v.roughness=asfloat(buffer.Load(ref.offset+164u));
    v.metallic=asfloat(buffer.Load(ref.offset+168u));
    v.punch_through=asfloat(buffer.Load(ref.offset+172u));
    return v;
}
struct B3Grass {

    float4x4 world;
    float4x4 world_inverse;
    float4x4 world_view;
    float4x4 world_view_projection;
    float4 extra_force;
    ArenaRef tufts;
    uint tuft_count;
    float clip_min;
    float clip_max;
    float coherency;
    float range_min;
    float range_max;
    float height_offset;
    float offset_curve;
    float normal_lerp;
    float normal_limit;
    float cluster_scale;
    float phase;
    uint grass_padding[2];
    float4x4 draw_world_view_projection;
    float4 eye;
    float4 tip_color;
    float4 root_color;
    float width;
    float alpha_reference;
    float translucency;
    float roughness;
    float metallic;
    float punch_through;
    float normal_scale;
    uint color_map;
    uint normal_map;

};
static const uint B3GrassBytes=496u;
B3Grass LoadB3Grass(ArenaRef base,uint index,ArenaTable table) {
    ArenaRef ref=ArenaAdd(base,index*B3GrassBytes);
    ByteAddressBuffer buffer=ArenaBuffer(ref,table);
    B3Grass v;
    v.world=ArenaLoadMatrix(ArenaAdd(ref,0u),table);
    v.world_inverse=ArenaLoadMatrix(ArenaAdd(ref,64u),table);
    v.world_view=ArenaLoadMatrix(ArenaAdd(ref,128u),table);
    v.world_view_projection=ArenaLoadMatrix(ArenaAdd(ref,192u),table);
    v.extra_force=asfloat(buffer.Load4(ref.offset+256u));
    v.tufts=ArenaLoadRef(ArenaAdd(ref,272u),table);
    v.tuft_count=buffer.Load(ref.offset+280u);
    v.clip_min=asfloat(buffer.Load(ref.offset+284u));
    v.clip_max=asfloat(buffer.Load(ref.offset+288u));
    v.coherency=asfloat(buffer.Load(ref.offset+292u));
    v.range_min=asfloat(buffer.Load(ref.offset+296u));
    v.range_max=asfloat(buffer.Load(ref.offset+300u));
    v.height_offset=asfloat(buffer.Load(ref.offset+304u));
    v.offset_curve=asfloat(buffer.Load(ref.offset+308u));
    v.normal_lerp=asfloat(buffer.Load(ref.offset+312u));
    v.normal_limit=asfloat(buffer.Load(ref.offset+316u));
    v.cluster_scale=asfloat(buffer.Load(ref.offset+320u));
    v.phase=asfloat(buffer.Load(ref.offset+324u));
    v.grass_padding[0]=buffer.Load(ref.offset+328u);
    v.grass_padding[1]=buffer.Load(ref.offset+332u);
    v.draw_world_view_projection=ArenaLoadMatrix(ArenaAdd(ref,336u),table);
    v.eye=asfloat(buffer.Load4(ref.offset+400u));
    v.tip_color=asfloat(buffer.Load4(ref.offset+416u));
    v.root_color=asfloat(buffer.Load4(ref.offset+432u));
    v.width=asfloat(buffer.Load(ref.offset+448u));
    v.alpha_reference=asfloat(buffer.Load(ref.offset+452u));
    v.translucency=asfloat(buffer.Load(ref.offset+456u));
    v.roughness=asfloat(buffer.Load(ref.offset+460u));
    v.metallic=asfloat(buffer.Load(ref.offset+464u));
    v.punch_through=asfloat(buffer.Load(ref.offset+468u));
    v.normal_scale=asfloat(buffer.Load(ref.offset+472u));
    v.color_map=buffer.Load(ref.offset+476u);
    v.normal_map=buffer.Load(ref.offset+480u);
    return v;
}
struct B3Blade {

    float3 root;
    float fade;
    float3 tip;
    uint instance;

};
static const uint B3BladeBytes=32u;
B3Blade LoadB3Blade(ArenaRef base,uint index,ArenaTable table) {
    ArenaRef ref=ArenaAdd(base,index*B3BladeBytes);
    ByteAddressBuffer buffer=ArenaBuffer(ref,table);
    B3Blade v;
    v.root=asfloat(buffer.Load3(ref.offset+0u));
    v.fade=asfloat(buffer.Load(ref.offset+12u));
    v.tip=asfloat(buffer.Load3(ref.offset+16u));
    v.instance=buffer.Load(ref.offset+28u);
    return v;
}
struct B3SkinJob {

    ArenaRef source;
    ArenaRef destination;
    ArenaRef skin;
    ArenaRef palette;
    uint bones;
    uint vertices;
    uint stride;
    uint normal;
    uint tangent;
    uint padding[3];

};
static const uint B3SkinJobBytes=64u;
B3SkinJob LoadB3SkinJob(ArenaRef base,uint index,ArenaTable table) {
    ArenaRef ref=ArenaAdd(base,index*B3SkinJobBytes);
    ByteAddressBuffer buffer=ArenaBuffer(ref,table);
    B3SkinJob v;
    v.source=ArenaLoadRef(ArenaAdd(ref,0u),table);
    v.destination=ArenaLoadRef(ArenaAdd(ref,8u),table);
    v.skin=ArenaLoadRef(ArenaAdd(ref,16u),table);
    v.palette=ArenaLoadRef(ArenaAdd(ref,24u),table);
    v.bones=buffer.Load(ref.offset+32u);
    v.vertices=buffer.Load(ref.offset+36u);
    v.stride=buffer.Load(ref.offset+40u);
    v.normal=buffer.Load(ref.offset+44u);
    v.tangent=buffer.Load(ref.offset+48u);
    v.padding[0]=buffer.Load(ref.offset+52u);
    v.padding[1]=buffer.Load(ref.offset+56u);
    v.padding[2]=buffer.Load(ref.offset+60u);
    return v;
}
struct B3BranchJob {

    float4x4 world;
    float4x4 world_inverse;
    float4x4 world_view_projection;
    ArenaRef source;
    ArenaRef destination;
    ArenaRef state;
    uint segments;
    float cycle;
    float cluster_scale;

};
static const uint B3BranchJobBytes=240u;
B3BranchJob LoadB3BranchJob(ArenaRef base,uint index,ArenaTable table) {
    ArenaRef ref=ArenaAdd(base,index*B3BranchJobBytes);
    ByteAddressBuffer buffer=ArenaBuffer(ref,table);
    B3BranchJob v;
    v.world=ArenaLoadMatrix(ArenaAdd(ref,0u),table);
    v.world_inverse=ArenaLoadMatrix(ArenaAdd(ref,64u),table);
    v.world_view_projection=ArenaLoadMatrix(ArenaAdd(ref,128u),table);
    v.source=ArenaLoadRef(ArenaAdd(ref,192u),table);
    v.destination=ArenaLoadRef(ArenaAdd(ref,200u),table);
    v.state=ArenaLoadRef(ArenaAdd(ref,208u),table);
    v.segments=buffer.Load(ref.offset+216u);
    v.cycle=asfloat(buffer.Load(ref.offset+220u));
    v.cluster_scale=asfloat(buffer.Load(ref.offset+224u));
    return v;
}
struct B3SoftBodyJob {

    ArenaRef skinned;
    ArenaRef particles;
    ArenaRef constraints;
    ArenaRef map;
    ArenaRef state;
    ArenaRef destination;
    uint reset;
    uint padding[3];
    uint4 constants[15];
    uint4 write[5];

};
static const uint B3SoftBodyJobBytes=384u;
B3SoftBodyJob LoadB3SoftBodyJob(ArenaRef base,uint index,ArenaTable table) {
    ArenaRef ref=ArenaAdd(base,index*B3SoftBodyJobBytes);
    ByteAddressBuffer buffer=ArenaBuffer(ref,table);
    B3SoftBodyJob v;
    v.skinned=ArenaLoadRef(ArenaAdd(ref,0u),table);
    v.particles=ArenaLoadRef(ArenaAdd(ref,8u),table);
    v.constraints=ArenaLoadRef(ArenaAdd(ref,16u),table);
    v.map=ArenaLoadRef(ArenaAdd(ref,24u),table);
    v.state=ArenaLoadRef(ArenaAdd(ref,32u),table);
    v.destination=ArenaLoadRef(ArenaAdd(ref,40u),table);
    v.reset=buffer.Load(ref.offset+48u);
    v.padding[0]=buffer.Load(ref.offset+52u);
    v.padding[1]=buffer.Load(ref.offset+56u);
    v.padding[2]=buffer.Load(ref.offset+60u);
    v.constants[0]=buffer.Load4(ref.offset+64u);
    v.constants[1]=buffer.Load4(ref.offset+80u);
    v.constants[2]=buffer.Load4(ref.offset+96u);
    v.constants[3]=buffer.Load4(ref.offset+112u);
    v.constants[4]=buffer.Load4(ref.offset+128u);
    v.constants[5]=buffer.Load4(ref.offset+144u);
    v.constants[6]=buffer.Load4(ref.offset+160u);
    v.constants[7]=buffer.Load4(ref.offset+176u);
    v.constants[8]=buffer.Load4(ref.offset+192u);
    v.constants[9]=buffer.Load4(ref.offset+208u);
    v.constants[10]=buffer.Load4(ref.offset+224u);
    v.constants[11]=buffer.Load4(ref.offset+240u);
    v.constants[12]=buffer.Load4(ref.offset+256u);
    v.constants[13]=buffer.Load4(ref.offset+272u);
    v.constants[14]=buffer.Load4(ref.offset+288u);
    v.write[0]=buffer.Load4(ref.offset+304u);
    v.write[1]=buffer.Load4(ref.offset+320u);
    v.write[2]=buffer.Load4(ref.offset+336u);
    v.write[3]=buffer.Load4(ref.offset+352u);
    v.write[4]=buffer.Load4(ref.offset+368u);
    return v;
}
uint B3Word(ArenaRef r,uint i,ArenaTable table) {
#ifdef DEFORMER_COMPUTE
    // Copies/state and the effector grid are in UAV state during simulation.
    if (r.arena == ARENA_SKINNING || r.arena == ARENA_SCRATCH)
        return ArenaBufferRw(r,table).Load(r.offset+i*4u);
#endif
    return ArenaLoad(ArenaAdd(r,i*4u),table);
}
float B3Float(ArenaRef r,uint i,ArenaTable table) { return asfloat(B3Word(r,i,table)); }
void B3Store(ArenaRef r,uint i,uint v,ArenaTable table) { ArenaStore(ArenaAdd(r,i*4u),table,v); }
float4 B3State(ArenaRef r,uint i,ArenaTable table) {return asfloat(ArenaBufferRw(r,table).Load4(r.offset+32u+i*16u));}
void B3SetState(ArenaRef r,uint i,float4 v,ArenaTable table) {ArenaBufferRw(r,table).Store4(r.offset+32u+i*16u,asuint(v));}
static const uint GridCells=360u, GridEntries=128u, MaxBlades=131072u;
uint GridList(float4 clip,float cluster_scale) {
    uint3 cell=min(uint3(6.0*clamp(0.5*clip.x/clip.w+0.5,0.0,1.0),6.0*clamp(-0.5*clip.y/clip.w+0.5,0.0,1.0),10.0*clamp(clip.z*cluster_scale,0.0,1.0)),uint3(5u,5u,9u));
    return ((cell.z*6u+cell.y)*6u+cell.x)*GridEntries;
}
uint B3Count(ArenaRef r,ArenaTable t) {return ArenaLoad(ArenaAdd(r,76u),t);}
uint4 B3Sphere(ArenaRef r,uint i,ArenaTable t) {return ArenaLoad4(ArenaAdd(r,96u+i*16u),t);}
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The turns of the game's vegetation kernels, as unit quaternions.

// The turn by twice half_angle about an axis of any length; no turn about no axis.
float4 Turn(float3 axis, float half_angle) {
    const float length_squared = dot(axis, axis);
    return length_squared > 0.0
               ? float4(axis * rsqrt(length_squared) * sin(half_angle), cos(half_angle))
               : float4(0.0, 0.0, 0.0, 1.0);
}

float3 Turned(float3 v, float4 turn) {
    return v + 2.0 * cross(turn.xyz, cross(turn.xyz, v) + turn.w * v);
}

#endif
