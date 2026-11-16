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
LeafControl main(uint id:SV_VertexID) {
    B3Leaf leaf=LoadB3Leaf(scene_leaves,base_instance,g_arena);
    LeafControl o;o.control_point=LoadPosition(leaf.vertices,leaf.stride,leaf.position,id,g_arena);
    o.leaf_index=base_instance;o.position=float4(o.control_point,1.0);return o;
}
