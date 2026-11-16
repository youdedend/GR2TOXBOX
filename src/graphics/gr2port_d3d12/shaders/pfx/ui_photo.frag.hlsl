// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_photo.frag; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIIn {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u, v4 = 0u, v5 = 0u, v6 = 0u, v7 = 0u;
    uint v8 = 0u, v9 = 0u, v10 = 0u, v11 = 0u, v12 = 0u, v13 = 0u, v14 = 0u, v15 = 0u;
    uint v16 = 0u, v17 = 0u, v18 = 0u, v19 = 0u;
    uint s12 = 0u, s13 = 0u, s14 = 0u, s15 = 0u, s17 = 0u, s18 = 0u, s20 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint texture0 = UiWord(draw_block,8);
    uint texture1 = UiWord(draw_block,9);
    uint sampler0 = UiWord(draw_block,10);
    uint sampler1 = UiWord(draw_block,11);
    ArenaRef ps_constants = UiRef(UiWord(draw_block,6),UiWord(draw_block,7));
    m0 = s14;
    s14 = exec_lo;
    s15 = 0u;
    exec_lo = exec_lo;
    scc = 1u;
    if (exec_lo != 0u) {
        v4 = u(attr0.z);
        v2 = u(2.0 * f(v4) + -1.0);
        v5 = u(attr0.w);
        v2 = u(f(v2) * f(v2));
        v3 = u(-2.0 * f(v5) + 1.0);
        v2 = u(f(v3) * f(v3) + f(v2));
        v2 = u(sqrt(f(v2)));
        v2 = u(f(0x3f3504f3u) * f(v2));
        v2 = u(min(1.0, f(v2)));
        v2 = u(f(0x437f0000u) * f(v2));
        v2 = cvt_i32(f(v2));
    }
    {
        uint c_out = exec_lo != 0u && (0x100u > 0xffffffffu - v2) ? 1u : 0u;
        if (exec_lo != 0u) v2 = 0x100u + v2;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    if (exec_lo != 0u) {
        {
            uint b_at = (v2 * 16u) >> 2;
            v8 = UiWord(ps_constants,b_at + 0u);
            v9 = UiWord(ps_constants,b_at + 1u);
            v10 = UiWord(ps_constants,b_at + 2u);
        }
        v13 = u(attr0.x);
        v14 = u(attr0.y);
        v15 = 0x0u;
        v11 = 0x0u;
        v12 = 0x0u;
        v16 = 0x0u;
        v17 = 0x0u;
    }
    s20 = 0xfffffff0u;
    bool again_1 = false;
    do {
        again_1 = false;
        if (exec_lo != 0u) {
            v0 = u(float(int(s20)));
            v0 = u(f(v0) * f(v10));
            v6 = u(f(v0) * f(0x3a800000u) + f(v13));
        }
        s17 = s20 * s20;
        s18 = 0xfffffff0u;
        bool again_2 = false;
        do {
            again_2 = false;
            if (exec_lo != 0u) {
                v0 = u(float(int(s18)));
                v0 = u(f(v0) * f(v10));
                v7 = u(f(v0) * f(0x3a800000u) + f(v14));
                {
                    float4 t_out = UiSample(texture0,sampler0, float2(f(v6), f(v7)));
                    v0 = u(t_out.x);
                    v1 = u(t_out.y);
                    v2 = u(t_out.z);
                    v3 = u(t_out.w);
                }
            }
            vcc_lo = s18 * s18;
            {
                uint r_out = s17 + vcc_lo;
                scc = ((int(s17) ^ int(r_out)) & (int(vcc_lo) ^ int(r_out))) < 0 ? 1u : 0u;
                vcc_lo = r_out;
            }
            {
                uint c_out = exec_lo != 0u && (vcc_lo > 0x0u) ? 1u : 0u;
                if (exec_lo != 0u) v7 = 0x0u - vcc_lo;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            if (exec_lo != 0u) {
                v7 = u(float(int(v7)));
                v7 = u(f(0x3bb8aa3bu) * f(v7));
                v7 = u(exp2(f(v7)));
                v17 = u(f(v7) + f(v17));
            }
            {
                uint r_out = s18 + 0x1u;
                scc = ((int(s18) ^ int(r_out)) & (int(0x1u) ^ int(r_out))) < 0 ? 1u : 0u;
                s18 = r_out;
            }
            scc = int(s18) <= int(0x10u) ? 1u : 0u;
            if (exec_lo != 0u) {
                v16 = u(f(v7) * f(v0) + f(v16));
                v12 = u(f(v1) * f(v7) + f(v12));
                v11 = u(f(v2) * f(v7) + f(v11));
                v15 = u(f(v3) * f(v7) + f(v15));
            }
            if (scc != 0u) { pc = 2; again_2 = true; }
        } while (again_2);
        {
            uint r_out = s20 + 0x1u;
            scc = ((int(s20) ^ int(r_out)) & (int(0x1u) ^ int(r_out))) < 0 ? 1u : 0u;
            s20 = r_out;
        }
        scc = int(s20) <= int(0x10u) ? 1u : 0u;
        if (scc != 0u) { pc = 1; again_1 = true; }
    } while (again_1);
    if (exec_lo != 0u) {
        v6 = u(1.0 / f(v17));
        v0 = u(clamp(f(v6) * f(v16), 0.0, 1.0));
        v0 = u(f(0x437f0000u) * f(v0));
        v2 = u(clamp(f(v6) * f(v12), 0.0, 1.0));
        v1 = cvt_i32(f(v0));
        v2 = u(f(0x437f0000u) * f(v2));
        v0 = u(clamp(f(v6) * f(v11), 0.0, 1.0));
        v0 = u(f(0x437f0000u) * f(v0));
        v0 = cvt_i32(f(v0));
        {
            uint b_at = (v1 * 16u) >> 2;
            v7 = UiWord(ps_constants,b_at + 0u);
        }
        {
            uint b_at = (v0 * 16u) >> 2;
            v10 = UiWord(ps_constants,b_at + 0u);
            v11 = UiWord(ps_constants,b_at + 1u);
            v12 = UiWord(ps_constants,b_at + 2u);
        }
        v1 = cvt_i32(f(v2));
        {
            uint b_at = (v1 * 16u) >> 2;
            v13 = UiWord(ps_constants,b_at + 0u);
            v14 = UiWord(ps_constants,b_at + 1u);
        }
        v16 = u(f(0x41b92023u) * f(v4));
        v17 = 0x43800000u;
        v18 = u(2.0 * f(v9));
        {
            float4 t_out = UiSample(texture1,sampler1, float2(f(v4), f(v5)));
            v0 = u(t_out.x);
            v1 = u(t_out.y);
            v2 = u(t_out.z);
            v3 = u(t_out.w);
        }
        v4 = u(f(v5) * f(0x402a91b9u) + f(v16));
        v4 = u(f(v17) * f(v4) + f(0x33d6bf95u));
        v5 = u(1.0 / f(v4));
        v5 = u(f(0x4ceb79a3u) * f(v5));
        v5 = u(frac(abs(f(v5))));
        v4 = u(f(v5) * abs(f(v4)));
        v4 = u(frac(f(v4)));
    }
    s12 = 0x3eb0331eu;
    if (exec_lo != 0u) {
        v9 = u(-1.0 + f(v4));
    }
    s13 = 0x3f36d1e1u;
    if (exec_lo != 0u) {
        v19 = u(clamp(f(v6) * f(v15), 0.0, 1.0));
        v5 = u(f(0x3e991687u) * f(v7));
        v4 = u(f(0xbe2cca2eu) * f(v7));
        v5 = u(f(v14) * f(0x3f1645a2u) + f(v5));
        v13 = u(0.5 * f(v7));
        v15 = u(f(v14) * f(0xbea99ae9u) + f(v4));
        v4 = u(f(v12) * f(0x3de978d5u) + f(v5));
        v4 = u(f(0x437f0000u) * f(v4));
        v4 = cvt_i32(f(v4));
        {
            uint b_at = (v4 * 16u) >> 2;
            v4 = UiWord(ps_constants,b_at + 0u);
            v5 = UiWord(ps_constants,b_at + 1u);
            v6 = UiWord(ps_constants,b_at + 2u);
            v7 = UiWord(ps_constants,b_at + 3u);
        }
        v10 = u(f(v14) * f(0xbed65e89u) + f(v13));
        v15 = u(0.5 * f(v12) + f(v15));
        v10 = u(f(v12) * f(0xbda685dbu) + f(v10));
        v14 = u(f(v15) * f(v18));
        v13 = u(f(v10) * f(v18));
        v11 = u(f(v3) - f(v19));
        v11 = u(f(v11) * f(v3) + f(v19));
        v8 = u(f(v8) * f(v7));
        v4 = u(f(0x437f0000u) * f(v8));
        v4 = cvt_i32(f(v4));
    }
    {
        uint c_out = exec_lo != 0u && (0x100u > 0xffffffffu - v4) ? 1u : 0u;
        if (exec_lo != 0u) v4 = 0x100u + v4;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    if (exec_lo != 0u) {
        {
            uint b_at = (v4 * 16u) >> 2;
            v4 = UiWord(ps_constants,b_at + 0u);
            v5 = UiWord(ps_constants,b_at + 1u);
            v6 = UiWord(ps_constants,b_at + 2u);
            v7 = UiWord(ps_constants,b_at + 3u);
        }
        v10 = u(f(s12) * -f(v14) + f(v8));
        v12 = u(f(v13) * f(0x3fb374bcu) + f(v8));
        v8 = u(f(v14) * f(0x3fe2d0e5u) + f(v8));
        v10 = u(f(s13) * -f(v13) + f(v10));
        v7 = u(f(v7) * f(v9) + 1.0);
        v4 = u(f(v12) * f(v7));
        v5 = u(f(v10) * f(v7));
        v6 = u(f(v8) * f(v7));
        v0 = u(f(v7) * -f(v12) + f(v0));
        v1 = u(f(v7) * -f(v10) + f(v1));
        v2 = u(f(v7) * -f(v8) + f(v2));
        v4 = u(f(v3) * f(v0) + f(v4));
        v5 = u(f(v3) * f(v1) + f(v5));
        v6 = u(f(v3) * f(v2) + f(v6));
        v0 = packHalf2x16(float2(f(v4), f(v5)));
        v1 = packHalf2x16(float2(f(v6), f(v11)));
    }
    exec_lo = s14;
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(v0), unpackHalf2x16(v1));
    return;
}

float4 main(UIIn input):SV_Target0{ui_position=input.position;attr0=input.attr0;attr1=input.attr1;draw_index=input.draw_index;Program();return color0;}
