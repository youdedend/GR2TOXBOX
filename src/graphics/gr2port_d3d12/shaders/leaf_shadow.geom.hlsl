// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer LeafShadowGeometryRoot : register(b1) {
    uint2 scene_leaves_words;

};
#define scene_leaves ArenaMake(scene_leaves_words.x, scene_leaves_words.y)
struct LeafControl {float4 position:SV_Position;float3 control_point:TEXCOORD0;nointerpolation uint leaf_index:TEXCOORD1;};
struct LeafPatch {float tess[2]:SV_TessFactor;uint leaf:TEXCOORD2;};
struct LeafDomain {float4 position:SV_Position;float4 leaf_position:TEXCOORD0;float3 leaf_side:TEXCOORD1;float leaf_along:TEXCOORD2;nointerpolation uint leaf_index:TEXCOORD3;};
struct LeafOutput {float4 position:SV_Position;float2 texcoord:TEXCOORD0;nointerpolation uint texture_index:TEXCOORD1;nointerpolation float alpha_reference:TEXCOORD2;};
[maxvertexcount(4)]
void main(line LeafDomain input[2],inout TriangleStream<LeafOutput> stream) {
    float3 segment=input[1].leaf_position.xyz-input[0].leaf_position.xyz;
    for(uint end_index=0;end_index<2;++end_index) {
        float2 lean=normalize(cross(segment,input[end_index].leaf_side)).xy;
        for(uint side=0;side<2;++side) {
            LeafOutput o;o.position=input[end_index].leaf_position;
            o.position.xy+=(side==0?-1.0:1.0)*input[end_index].leaf_side.xy;
            B3Leaf leaf=LoadB3Leaf(scene_leaves,input[end_index].leaf_index,g_arena);
            o.position.z=0.5*(o.position.z+o.position.w);
            o.texcoord=float2(float(side),input[end_index].leaf_along);
            o.texture_index=leaf.texture;o.alpha_reference=leaf.alpha_reference;
            stream.Append(o);
        }
    }
    stream.RestartStrip();
}
