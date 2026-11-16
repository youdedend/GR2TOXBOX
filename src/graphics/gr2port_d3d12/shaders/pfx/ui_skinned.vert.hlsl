// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_skinned.vert; generated GCN register arithmetic retained.
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
    uint v8 = 0u, v9 = 0u, v10 = 0u, v11 = 0u, v12 = 0u, v13 = 0u, v14 = 0u, v15 = 0u;
    uint v16 = 0u, v17 = 0u, v18 = 0u, v19 = 0u, v20 = 0u, v21 = 0u, v22 = 0u, v23 = 0u;
    uint v24 = 0u, v25 = 0u, v26 = 0u, v27 = 0u, v28 = 0u, v29 = 0u, v30 = 0u, v31 = 0u;
    uint v32 = 0u, v33 = 0u, v34 = 0u, v35 = 0u, v36 = 0u, v37 = 0u, v38 = 0u, v39 = 0u;
    uint v40 = 0u, v41 = 0u, v42 = 0u, v43 = 0u, v44 = 0u, v45 = 0u, v46 = 0u, v47 = 0u;
    uint v48 = 0u, v49 = 0u, v50 = 0u, v51 = 0u, v52 = 0u, v53 = 0u, v54 = 0u, v55 = 0u;
    uint v56 = 0u, v57 = 0u, v58 = 0u, v59 = 0u, v60 = 0u, v61 = 0u, v62 = 0u, v63 = 0u;
    uint v64 = 0u, v65 = 0u, v66 = 0u, v67 = 0u, v68 = 0u, v69 = 0u, v70 = 0u, v71 = 0u;
    uint v72 = 0u, v73 = 0u, v74 = 0u, v75 = 0u, v76 = 0u, v77 = 0u, v78 = 0u, v79 = 0u;
    uint v80 = 0u;
    uint s0 = 0u, s1 = 0u, s2 = 0u, s3 = 0u, s4 = 0u, s5 = 0u, s6 = 0u, s7 = 0u;
    uint s24 = 0u, s25 = 0u, s26 = 0u, s27 = 0u, s28 = 0u, s29 = 0u, s30 = 0u, s31 = 0u;
    uint s32 = 0u, s33 = 0u, s34 = 0u, s35 = 0u, s36 = 0u, s37 = 0u, s38 = 0u, s39 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    draw_index = uint(base_draw);
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
    uint4 input3 = UiAttribute(draw_block, 3u, vertex);
    v16 = input3.x;
    v17 = input3.y;
    v18 = input3.z;
    v19 = input3.w;
    uint4 input4 = UiAttribute(draw_block, 4u, vertex);
    v20 = input4.x;
    v21 = input4.y;
    v22 = input4.z;
    v23 = input4.w;
    if (exec_lo != 0u) {
        v1 = v16 << (0x6u & 31u);
        v3 = v17 << (0x6u & 31u);
        v0 = uint(int(v1) >> (0x4u & 31u));
        v16 = uint(int(v3) >> (0x4u & 31u));
        v39 = 0xfu & v3;
        v3 = v18 << (0x6u & 31u);
    }
    {
        uint c_out = exec_lo != 0u && (0x3u > 0xffffffffu - v0) ? 1u : 0u;
        if (exec_lo != 0u) v10 = 0x3u + v0;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    {
        uint c_out = exec_lo != 0u && (0x3u > 0xffffffffu - v16) ? 1u : 0u;
        if (exec_lo != 0u) v17 = 0x3u + v16;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    if (exec_lo != 0u) {
        v2 = 0xfu & v1;
    }
    {
        uint c_out = exec_lo != 0u && (0x2u > 0xffffffffu - v0) ? 1u : 0u;
        if (exec_lo != 0u) v1 = 0x2u + v0;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    if (exec_lo != 0u) {
        v18 = v39;
        v37 = 0xfu & v3;
        v11 = v2;
        v38 = abs_diff(0x2u, 0x0u) + v16;
        {
            uint b_at = (v1 * 16u + v2) >> 2;
            v72 = UiWord(vs_palette,b_at + 0u);
            v73 = UiWord(vs_palette,b_at + 1u);
            v74 = UiWord(vs_palette,b_at + 2u);
            v75 = UiWord(vs_palette,b_at + 3u);
        }
        {
            uint b_at = (v10 * 16u + v11) >> 2;
            v76 = UiWord(vs_palette,b_at + 0u);
            v77 = UiWord(vs_palette,b_at + 1u);
            v78 = UiWord(vs_palette,b_at + 2u);
            v79 = UiWord(vs_palette,b_at + 3u);
        }
        {
            uint b_at = (v38 * 16u + v39) >> 2;
            v56 = UiWord(vs_palette,b_at + 0u);
            v57 = UiWord(vs_palette,b_at + 1u);
            v58 = UiWord(vs_palette,b_at + 2u);
            v59 = UiWord(vs_palette,b_at + 3u);
        }
        {
            uint b_at = (v17 * 16u + v18) >> 2;
            v64 = UiWord(vs_palette,b_at + 0u);
            v65 = UiWord(vs_palette,b_at + 1u);
            v66 = UiWord(vs_palette,b_at + 2u);
            v67 = UiWord(vs_palette,b_at + 3u);
        }
        v10 = uint(int(v3) >> (0x4u & 31u));
    }
    {
        uint c_out = exec_lo != 0u && (0x1u > 0xffffffffu - v0) ? 1u : 0u;
        if (exec_lo != 0u) v17 = 0x1u + v0;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    if (exec_lo != 0u) {
        v1 = v19 << (0x6u & 31u);
        v18 = v2;
        v40 = uint(int(v1) >> (0x4u & 31u));
        v42 = 0xfu & v1;
        v36 = abs_diff(0x2u, 0x0u) + v10;
        v1 = v2;
        {
            uint b_at = (v17 * 16u + v18) >> 2;
            v68 = UiWord(vs_palette,b_at + 0u);
            v69 = UiWord(vs_palette,b_at + 1u);
            v70 = UiWord(vs_palette,b_at + 2u);
            v71 = UiWord(vs_palette,b_at + 3u);
        }
        {
            uint b_at = (v36 * 16u + v37) >> 2;
            v24 = UiWord(vs_palette,b_at + 0u);
            v25 = UiWord(vs_palette,b_at + 1u);
            v26 = UiWord(vs_palette,b_at + 2u);
            v27 = UiWord(vs_palette,b_at + 3u);
        }
        {
            uint b_at = (v0 * 16u + v1) >> 2;
            v60 = UiWord(vs_palette,b_at + 0u);
            v61 = UiWord(vs_palette,b_at + 1u);
            v62 = UiWord(vs_palette,b_at + 2u);
            v63 = UiWord(vs_palette,b_at + 3u);
        }
        v17 = abs_diff(0x3u, 0x0u) + v10;
        v18 = v37;
        v41 = abs_diff(0x2u, 0x0u) + v40;
        {
            uint b_at = (v17 * 16u + v18) >> 2;
            v44 = UiWord(vs_palette,b_at + 0u);
            v45 = UiWord(vs_palette,b_at + 1u);
            v46 = UiWord(vs_palette,b_at + 2u);
            v47 = UiWord(vs_palette,b_at + 3u);
        }
        {
            uint b_at = (v41 * 16u + v42) >> 2;
            v0 = UiWord(vs_palette,b_at + 0u);
            v1 = UiWord(vs_palette,b_at + 1u);
            v2 = UiWord(vs_palette,b_at + 2u);
            v3 = UiWord(vs_palette,b_at + 3u);
        }
        v17 = abs_diff(0x1u, 0x0u) + v16;
        v18 = v39;
        {
            uint b_at = (v17 * 16u + v18) >> 2;
            v48 = UiWord(vs_palette,b_at + 0u);
            v49 = UiWord(vs_palette,b_at + 1u);
            v50 = UiWord(vs_palette,b_at + 2u);
            v51 = UiWord(vs_palette,b_at + 3u);
        }
        v17 = abs_diff(0x3u, 0x0u) + v40;
        v18 = v42;
        {
            uint b_at = (v17 * 16u + v18) >> 2;
            v28 = UiWord(vs_palette,b_at + 0u);
            v29 = UiWord(vs_palette,b_at + 1u);
            v30 = UiWord(vs_palette,b_at + 2u);
            v31 = UiWord(vs_palette,b_at + 3u);
        }
        v17 = abs_diff(0x1u, 0x0u) + v10;
        v18 = v37;
        {
            uint b_at = (v17 * 16u + v18) >> 2;
            v32 = UiWord(vs_palette,b_at + 0u);
            v33 = UiWord(vs_palette,b_at + 1u);
            v34 = UiWord(vs_palette,b_at + 2u);
            v35 = UiWord(vs_palette,b_at + 3u);
        }
        v17 = v39;
        v11 = v37;
        {
            uint b_at = (v16 * 16u + v17) >> 2;
            v52 = UiWord(vs_palette,b_at + 0u);
            v53 = UiWord(vs_palette,b_at + 1u);
            v54 = UiWord(vs_palette,b_at + 2u);
            v55 = UiWord(vs_palette,b_at + 3u);
        }
        {
            uint b_at = (v10 * 16u + v11) >> 2;
            v36 = UiWord(vs_palette,b_at + 0u);
            v37 = UiWord(vs_palette,b_at + 1u);
            v38 = UiWord(vs_palette,b_at + 2u);
            v39 = UiWord(vs_palette,b_at + 3u);
        }
        v16 = abs_diff(0x1u, 0x0u) + v40;
        v17 = v42;
        v41 = v42;
        {
            uint b_at = (v16 * 16u + v17) >> 2;
            v16 = UiWord(vs_palette,b_at + 0u);
            v17 = UiWord(vs_palette,b_at + 1u);
            v18 = UiWord(vs_palette,b_at + 2u);
            v19 = UiWord(vs_palette,b_at + 3u);
        }
        {
            uint b_at = (v40 * 16u + v41) >> 2;
            v40 = UiWord(vs_palette,b_at + 0u);
            v41 = UiWord(vs_palette,b_at + 1u);
            v42 = UiWord(vs_palette,b_at + 2u);
            v43 = UiWord(vs_palette,b_at + 3u);
        }
    }
    s24 = UiWord(vs_constants,0);
    s25 = UiWord(vs_constants,1);
    s26 = UiWord(vs_constants,2);
    s27 = UiWord(vs_constants,3);
    s28 = UiWord(vs_constants,4);
    s29 = UiWord(vs_constants,5);
    s30 = UiWord(vs_constants,6);
    s31 = UiWord(vs_constants,7);
    s32 = UiWord(vs_constants,8);
    s33 = UiWord(vs_constants,9);
    s34 = UiWord(vs_constants,10);
    s35 = UiWord(vs_constants,11);
    s36 = UiWord(vs_constants,12);
    s37 = UiWord(vs_constants,13);
    s38 = UiWord(vs_constants,14);
    s39 = UiWord(vs_constants,15);
    s0 = UiWord(vs_constants,16);
    s1 = UiWord(vs_constants,17);
    s2 = UiWord(vs_constants,18);
    s3 = UiWord(vs_constants,19);
    s4 = UiWord(vs_constants,20);
    s5 = UiWord(vs_constants,21);
    s6 = UiWord(vs_constants,22);
    s7 = UiWord(vs_constants,23);
    if (exec_lo != 0u) {
        v8 = u(-0.5 + f(v8));
        v9 = u(-0.5 + f(v9));
        v7 = 0x0u;
        v11 = s6;
        v10 = s7;
        v11 = u(f(s4) * f(v8) + f(v11));
        v10 = u(-f(s5) * f(v8) + f(v10));
        v8 = u(f(s5) * f(v9) + f(v11));
        v9 = u(f(s4) * f(v9) + f(v10));
        v10 = u(f(s0) * f(v12));
        v11 = u(f(s1) * f(v13));
        v12 = u(f(s2) * f(v14));
        v13 = u(f(s3) * f(v15));
        v75 = u(f(v20) * f(v75));
        v79 = u(f(v20) * f(v79));
        v14 = u(f(v20) * f(v74));
        v74 = u(f(v20) * f(v78));
        v79 = u(f(v21) * f(v67) + f(v79));
        v75 = u(f(v21) * f(v59) + f(v75));
        v74 = u(f(v21) * f(v66) + f(v74));
        v14 = u(f(v21) * f(v58) + f(v14));
        v58 = u(f(v20) * f(v73));
        v80 = u(f(v20) * f(v63));
        v66 = u(f(v20) * f(v70));
        v73 = u(f(v20) * f(v77));
        v75 = u(f(v22) * f(v27) + f(v75));
        v79 = u(f(v22) * f(v47) + f(v79));
        v47 = u(f(v20) * f(v62));
        v57 = u(f(v57) * f(v21) + f(v58));
        v27 = u(f(v20) * f(v69));
        v15 = u(f(v72) * f(v20));
        v74 = u(f(v22) * f(v46) + f(v74));
        v66 = u(f(v21) * f(v50) + f(v66));
        v14 = u(f(v22) * f(v26) + f(v14));
        v26 = u(f(v76) * f(v20));
        v79 = u(f(v23) * f(v31) + f(v79));
        v73 = u(f(v21) * f(v65) + f(v73));
        v65 = u(f(v20) * f(v71));
        v65 = u(f(v21) * f(v51) + f(v65));
        v75 = u(f(v23) * f(v3) + f(v75));
        v65 = u(f(v22) * f(v35) + f(v65));
        v79 = u(f(v75) * f(v6) + f(v79));
        v14 = u(f(v23) * f(v2) + f(v14));
        v65 = u(f(v23) * f(v19) + f(v65));
        v19 = u(f(v20) * f(v61));
        v74 = u(f(v23) * f(v30) + f(v74));
        v30 = u(f(v64) * f(v21) + f(v26));
        v26 = u(f(v68) * f(v20));
        v66 = u(f(v22) * f(v34) + f(v66));
        v47 = u(f(v21) * f(v54) + f(v47));
        v57 = u(f(v22) * f(v25) + f(v57));
        v25 = u(f(v55) * f(v21) + f(v80));
        v25 = u(f(v22) * f(v39) + f(v25));
        v73 = u(f(v22) * f(v45) + f(v73));
        v27 = u(f(v21) * f(v49) + f(v27));
        v15 = u(f(v21) * f(v56) + f(v15));
        v39 = u(f(v43) * f(v23) + f(v25));
        v3 = u(f(v53) * f(v21) + f(v19));
        v79 = u(f(v65) * f(v5) + f(v79));
        v74 = u(f(v14) * f(v6) + f(v74));
        v66 = u(f(v23) * f(v18) + f(v66));
        v18 = u(f(v60) * f(v20));
        v47 = u(f(v22) * f(v38) + f(v47));
        v57 = u(f(v23) * f(v1) + f(v57));
        v1 = u(f(v33) * f(v22) + f(v27));
        v73 = u(f(v23) * f(v29) + f(v73));
        v15 = u(f(v22) * f(v24) + f(v15));
        v30 = u(f(v22) * f(v44) + f(v30));
        v26 = u(f(v21) * f(v48) + f(v26));
        v79 = u(f(v39) * f(v4) + f(v79));
        v74 = u(f(v66) * f(v5) + f(v74));
        v47 = u(f(v23) * f(v42) + f(v47));
        v73 = u(f(v57) * f(v6) + f(v73));
        v1 = u(f(v23) * f(v17) + f(v1));
        v3 = u(f(v22) * f(v37) + f(v3));
        v15 = u(f(v23) * f(v0) + f(v15));
        v30 = u(f(v23) * f(v28) + f(v30));
        v26 = u(f(v22) * f(v32) + f(v26));
        v18 = u(f(v21) * f(v52) + f(v18));
        v0 = u(f(s39) * f(v79));
        v74 = u(f(v47) * f(v4) + f(v74));
        v73 = u(f(v1) * f(v5) + f(v73));
        v3 = u(f(v23) * f(v41) + f(v3));
        v30 = u(f(v15) * f(v6) + f(v30));
        v26 = u(f(v23) * f(v16) + f(v26));
        v18 = u(f(v22) * f(v36) + f(v18));
        v1 = u(f(s36) * f(v79));
        v2 = u(f(s37) * f(v79));
        v0 = u(f(s35) * f(v74) + f(v0));
        v73 = u(f(v3) * f(v4) + f(v73));
        v30 = u(f(v26) * f(v5) + f(v30));
        v18 = u(f(v23) * f(v40) + f(v18));
        v0 = u(f(s31) * f(v73) + f(v0));
        v30 = u(f(v18) * f(v4) + f(v30));
        v1 = u(f(s32) * f(v74) + f(v1));
        v2 = u(f(s33) * f(v74) + f(v2));
        v4 = u(f(s29) * f(v73) + f(v2));
        v0 = u(f(s27) * f(v30) + f(v0));
        v1 = u(f(s28) * f(v73) + f(v1));
        v2 = u(0.5 * f(v0));
        v3 = u(f(s24) * f(v30) + f(v1));
        v4 = u(f(s25) * f(v30) + f(v4));
    }
    ui_position = float4(f(v3), f(v4), 0.5 * (f(v0) - f(v2)), f(v0));
    attr1 = float4(f(v8), f(v9), f(v7), f(v7));
    attr0 = float4(f(v10), f(v11), f(v12), f(v13));
    return;
}

UIOut main(uint vertex:SV_VertexID,uint instance:SV_InstanceID){g_vertex=vertex;g_instance=base_draw+instance;Program();UIOut o;o.position=ui_position;o.position.y=-o.position.y;o.attr0=attr0;o.attr1=attr1;o.draw_index=draw_index;return o;}
