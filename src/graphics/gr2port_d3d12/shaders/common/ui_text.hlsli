// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch2_scene.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer UiTextRoot : register(b1) {
    uint2 draws_words;
    float2 text_scale;
};
#define draws ArenaMake(draws_words.x, draws_words.y)

struct TextIO {
    float4 position : SV_Position;
    float4 attr0 : TEXCOORD0;
#if TEXT_ATTRIBUTES == 2
    float4 attr1 : TEXCOORD1;
#endif
    nointerpolation uint draw_index : TEXCOORD15;
};
[maxvertexcount(3)]
void main(triangle TextIO input[3], inout TriangleStream<TextIO> stream) {
    ArenaRef draw=ArenaAdd(draws,input[0].draw_index*160u);
    ByteAddressBuffer b=ArenaBuffer(draw,g_arena);
    uint flags=b.Load(draw.offset+152u);
    bool narrow=(flags&1u)!=0u;
    float2 low=float2(1.0e30,1.0e30), high=float2(-1.0e30,-1.0e30);
    for(uint i=0;i<3;++i) {
        float4 p=input[i].position; narrow=narrow&&p.w>0.0;
        low=min(low,p.xy/p.w); high=max(high,p.xy/p.w);
    }
    float2 anchor=0.5*(low+high);
    if(narrow&&(flags&2u)!=0u) {
        uint2 words=b.Load2(draw.offset+8u);
        ArenaRef constants_ref=ArenaMake(words.y,words.x);
        float4 translation=ArenaLoadFloat4(ArenaAdd(constants_ref,48u),g_arena);
        if(abs(translation.w)>1.0e-6) anchor=float2(translation.x,-translation.y)/translation.w;
    }
    for(uint emit_index=0;emit_index<3;++emit_index) {
        TextIO o=input[emit_index];
        if(narrow) o.position.xy=(anchor+(o.position.xy/o.position.w-anchor)*text_scale)*o.position.w;
        stream.Append(o);
    }
    stream.RestartStrip();
}
