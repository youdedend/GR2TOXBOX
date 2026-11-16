// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
struct LeafControl {float4 position:SV_Position;float3 control_point:TEXCOORD0;nointerpolation uint leaf_index:TEXCOORD1;};
struct LeafPatch {float tess[2]:SV_TessFactor;uint leaf:TEXCOORD2;};
struct LeafDomain {float4 position:SV_Position;float4 leaf_position:TEXCOORD0;float3 leaf_side:TEXCOORD1;float leaf_along:TEXCOORD2;nointerpolation uint leaf_index:TEXCOORD3;};
struct LeafOutput {float4 position:SV_Position;float4 surface:TEXCOORD0;nointerpolation uint leaf:TEXCOORD1;};
[maxvertexcount(4)]
void main(line LeafDomain input[2],inout TriangleStream<LeafOutput> stream) {
    float3 segment=input[1].leaf_position.xyz-input[0].leaf_position.xyz;
    for(uint end_index=0;end_index<2;++end_index) {
        float2 lean=normalize(cross(segment,input[end_index].leaf_side)).xy;
        for(uint side=0;side<2;++side) {
            LeafOutput o;o.position=input[end_index].leaf_position;
            o.position.xy+=(side==0?-1.0:1.0)*input[end_index].leaf_side.xy;
            o.position.z=0.5*(o.position.w-o.position.z);
            o.surface=float4(lean,float(side),input[end_index].leaf_along);o.leaf=input[end_index].leaf_index;
            stream.Append(o);
        }
    }
    stream.RestartStrip();
}
