// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_stroke.vert; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static float4 ring0,ring1;
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIOut {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void EsStore(uint at, uint value) {
    const uint dword = at >> 2;
    if (dword < 4u) {
        ring0[dword] = f(value);
    } else if (dword < 8u) {
        ring1[dword - 4u] = f(value);
    }
}
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
    uint v8 = 0u, v9 = 0u, v10 = 0u, v11 = 0u;
    uint s0 = 0u, s1 = 0u, s2 = 0u, s3 = 0u, s10 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    draw_index = uint(g_instance);
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint vertex = UiVertex(draw_block);
    ArenaRef vs_constants = UiRef(UiWord(draw_block,2),UiWord(draw_block,3));
    v0 = vertex;
    uint4 input0 = UiAttribute(draw_block, 0u, vertex);
    v4 = input0.x;
    v5 = input0.y;
    v6 = input0.z;
    uint4 input2 = UiAttribute(draw_block, 2u, vertex);
    v8 = input2.x;
    v9 = input2.y;
    v10 = input2.z;
    v11 = input2.w;
    s0 = UiWord(vs_constants,0);
    s1 = UiWord(vs_constants,1);
    s2 = UiWord(vs_constants,2);
    s3 = UiWord(vs_constants,3);
    if (exec_lo != 0u) {
        v1 = u(f(s0) * f(v8));
        v2 = u(f(s1) * f(v9));
        v3 = u(f(s2) * f(v10));
        v7 = u(f(s3) * f(v11));
        {
            uint r_at = s10;
            EsStore(r_at + 0u, v4);
        }
        {
            uint r_at = s10 + 4u;
            EsStore(r_at + 0u, v5);
        }
        {
            uint r_at = s10 + 8u;
            EsStore(r_at + 0u, v6);
        }
        {
            uint r_at = s10 + 16u;
            EsStore(r_at + 0u, v1);
        }
        {
            uint r_at = s10 + 20u;
            EsStore(r_at + 0u, v2);
        }
        {
            uint r_at = s10 + 24u;
            EsStore(r_at + 0u, v3);
        }
        {
            uint r_at = s10 + 28u;
            EsStore(r_at + 0u, v7);
        }
    }
    return;
}

UIOut main(uint vertex:SV_VertexID,uint instance:SV_InstanceID){g_vertex=vertex;g_instance=base_draw+instance;Program();UIOut o;o.position=ui_position;o.attr0=ring0;o.attr1=ring1;o.draw_index=draw_index;return o;}
