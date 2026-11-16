// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_transform.vert; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIOut {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

uint UiVertex(ArenaRef draw) {
    ArenaRef indices = UiRef(UiWord(draw,0),UiWord(draw,1));
    if (ArenaIsNull(indices)) {
        return uint(g_vertex);
    }
    ArenaRef data = indices;
    const uint at = UiWord(draw,37) + uint(g_vertex);
    if (UiWord(draw,36) == 32u) {
        return UiWord(data,at);
    }
    return UiWord(data,at >> 1) >> ((at & 1u) * 16u) & 0xffffu;
}
uint UiSelect(uint select, uint4 raw, uint one) {
    switch (select) {
    case 1u:
        return one;
    case 4u:
        return raw.x;
    case 5u:
        return raw.y;
    case 6u:
        return raw.z;
    case 7u:
        return raw.w;
    default:
        return 0u;
    }
}
uint4 UiAttribute(ArenaRef draw, uint slot, uint vertex) {
    const uint at = 12u + slot * 4u;
    ArenaRef base = UiRef(UiWord(draw,at),UiWord(draw,at+1u));
    const uint format = UiWord(draw,at + 3u);
    const uint data_format = format >> 15 & 15u;
    const uint number_format = format >> 12 & 7u;
    ArenaRef data = ArenaAdd(base,vertex*UiWord(draw,at+2u));
    const uint one = number_format == 7u || number_format == 0u ? 0x3f800000u : 1u;
    uint4 raw = uint4(0u, 0u, 0u, one);
    if (data_format == 14u) {
        raw = uint4(UiWord(data,0), UiWord(data,1), UiWord(data,2), UiWord(data,3));
    } else if (data_format == 13u) {
        raw.xyz = uint3(UiWord(data,0), UiWord(data,1), UiWord(data,2));
    } else if (data_format == 11u) {
        raw.xy = uint2(UiWord(data,0), UiWord(data,1));
    } else if (data_format == 4u) {
        raw.x = UiWord(data,0);
    } else if (data_format == 10u) {
        const uint4 bytes = uint4(UiWord(data,0),UiWord(data,0),UiWord(data,0),UiWord(data,0)) >> uint4(0u, 8u, 16u, 24u) & 255u;
        raw = number_format == 0u ? asuint(float4(bytes) / 255.0) : bytes;
    } else if (data_format == 12u) {
        const uint4 shorts = uint4(UiWord(data,0) & 0xffffu, UiWord(data,0) >> 16,
                                   UiWord(data,1) & 0xffffu, UiWord(data,1) >> 16);
        raw = number_format == 0u ? asuint(float4(shorts) / 65535.0) : shorts;
    }
    const uint4 selects = uint4(format,format,format,format) >> uint4(0u, 3u, 6u, 9u) & 7u;
    return uint4(UiSelect(selects.x, raw, one), UiSelect(selects.y, raw, one),
                 UiSelect(selects.z, raw, one), UiSelect(selects.w, raw, one));
}
void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u, v4 = 0u, v5 = 0u, v6 = 0u, v7 = 0u;
    uint v8 = 0u, v9 = 0u, v10 = 0u, v12 = 0u, v13 = 0u, v14 = 0u, v15 = 0u;
    uint s0 = 0u, s1 = 0u, s2 = 0u, s3 = 0u, s4 = 0u, s5 = 0u, s6 = 0u, s7 = 0u;
    uint s8 = 0u, s9 = 0u, s10 = 0u, s11 = 0u, s12 = 0u, s13 = 0u, s14 = 0u, s15 = 0u;
    uint s16 = 0u, s17 = 0u, s18 = 0u, s19 = 0u, s20 = 0u, s21 = 0u, s22 = 0u, s23 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    draw_index = g_instance;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint vertex = UiVertex(draw_block);
    ArenaRef vs_constants = UiRef(UiWord(draw_block,2),UiWord(draw_block,3));
    ArenaRef vs_palette = UiRef(UiWord(draw_block,4),UiWord(draw_block,5));
    ui_position = float4(0.0,0.0,0.0,0.0);
    v0 = vertex;
    uint4 input0 = UiAttribute(draw_block, 0u, vertex);
    v4 = input0.x;
    v5 = input0.y;
    v6 = input0.z;
    uint4 input1 = UiAttribute(draw_block, 1u, vertex);
    v8 = input1.x;
    v9 = input1.y;
    uint4 input2 = UiAttribute(draw_block, 2u, vertex);
    v12 = input2.x;
    v13 = input2.y;
    v14 = input2.z;
    v15 = input2.w;
    if (exec_lo != 0u) {
        v1 = u(-0.5 + f(v8));
    }
    s8 = UiWord(vs_constants,0);
    s9 = UiWord(vs_constants,1);
    s10 = UiWord(vs_constants,2);
    s11 = UiWord(vs_constants,3);
    s12 = UiWord(vs_constants,4);
    s13 = UiWord(vs_constants,5);
    s14 = UiWord(vs_constants,6);
    s15 = UiWord(vs_constants,7);
    s16 = UiWord(vs_constants,8);
    s17 = UiWord(vs_constants,9);
    s18 = UiWord(vs_constants,10);
    s19 = UiWord(vs_constants,11);
    s20 = UiWord(vs_constants,12);
    s21 = UiWord(vs_constants,13);
    s22 = UiWord(vs_constants,14);
    s23 = UiWord(vs_constants,15);
    s0 = UiWord(vs_constants,16);
    s1 = UiWord(vs_constants,17);
    s2 = UiWord(vs_constants,18);
    s3 = UiWord(vs_constants,19);
    s4 = UiWord(vs_constants,20);
    s5 = UiWord(vs_constants,21);
    s6 = UiWord(vs_constants,22);
    s7 = UiWord(vs_constants,23);
    if (exec_lo != 0u) {
        v0 = u(-0.5 + f(v9));
        v3 = s23;
        v7 = s20;
        v8 = s21;
        v3 = u(f(s19) * f(v6) + f(v3));
        v2 = u(f(s15) * f(v5) + f(v3));
        v3 = u(f(s16) * f(v6) + f(v7));
        v8 = u(f(s17) * f(v6) + f(v8));
        v2 = u(f(s11) * f(v4) + f(v2));
        v3 = u(f(s12) * f(v5) + f(v3));
        v8 = u(f(s13) * f(v5) + f(v8));
        v5 = u(0.5 * f(v2));
        v3 = u(f(s8) * f(v4) + f(v3));
        v8 = u(f(s9) * f(v4) + f(v8));
    }
    ui_position = float4(f(v3), f(v8), 0.5 * (f(v2) - f(v5)), f(v2));
    if (exec_lo != 0u) {
        v4 = s6;
        v7 = u(f(s4) * f(v1) + f(v4));
        v4 = s7;
        v1 = u(-f(s5) * f(v1) + f(v4));
        v9 = u(f(s1) * f(v13));
        v4 = u(f(s2) * f(v14));
        v10 = u(f(s3) * f(v15));
        v6 = 0x0u;
        v7 = u(f(s5) * f(v0) + f(v7));
        v1 = u(f(s4) * f(v0) + f(v1));
        v0 = u(f(s0) * f(v12));
    }
    attr1 = float4(f(v7), f(v1), f(v6), f(v6));
    attr0 = float4(f(v0), f(v9), f(v4), f(v10));
    return;
}

UIOut main(uint vertex:SV_VertexID,uint instance:SV_InstanceID){g_vertex=vertex;g_instance=base_draw+instance;Program();UIOut o;o.position=UiClip(ui_position);o.attr0=attr0;o.attr1=attr1;o.draw_index=draw_index;return o;}
