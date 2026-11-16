// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_masked.frag; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIIn {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u, v4 = 0u, v5 = 0u, v6 = 0u, v7 = 0u;
    uint v8 = 0u, v9 = 0u, v10 = 0u;
    uint s0 = 0u, s1 = 0u, s12 = 0u, s13 = 0u, s14 = 0u, s15 = 0u, s16 = 0u, s17 = 0u;
    uint s18 = 0u, s19 = 0u, s32 = 0u, s33 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint texture0 = UiWord(draw_block,8);
    uint texture1 = UiWord(draw_block,9);
    uint sampler0 = UiWord(draw_block,10);
    uint sampler1 = UiWord(draw_block,11);
    ArenaRef ps_constants = UiRef(UiWord(draw_block,6),UiWord(draw_block,7));
    m0 = s14;
    s32 = exec_lo;
    s33 = 0u;
    exec_lo = exec_lo;
    scc = 1u;
    if (exec_lo != 0u) {
        v7 = bfe_u(v2, 0x8u, 0x1u);
        v4 = u(attr1.z);
        v2 = (0x1u & v7) | (~0x1u & v4);
        v3 = u(attr1.w);
        {
            float4 t_out = UiSample(texture1,sampler1, float2(f(v2), f(v3)));
            v4 = u(t_out.x);
        }
    }
    s16 = UiWord(ps_constants,4);
    s17 = UiWord(ps_constants,5);
    s18 = UiWord(ps_constants,6);
    s19 = UiWord(ps_constants,7);
    {
        uint c_out = exec_lo != 0u && (f(s18) != 0.0) ? 1u : 0u;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    if (exec_lo != 0u) {
        v2 = u(attr1.x);
        v3 = u(attr1.y);
    }
    if (!((vcc_lo & 1u) == 0u)) {
        if (exec_lo != 0u) {
            v6 = s19;
            v6 = u(clamp(f(s18) * f(v4) + f(v6), 0.0, 1.0));
        }
        {
            uint c_out = exec_lo != 0u && (0.0 == f(v6)) ? 1u : 0u;
            vcc_lo = c_out;
            vcc_hi = 0u;
        }
        {
            uint s_in = (vcc_lo & 1u);
            uint s_old = exec_lo;
            exec_lo = (s_in & exec_lo);
            scc = exec_lo != 0u ? 1u : 0u;
            vcc_lo = s_old;
            vcc_hi = 0u;
        }
        {
            uint m_out = ((s32 & 1u) & (~(exec_lo & 1u) & 1u));
            scc = m_out != 0u ? 1u : 0u;
            s32 = m_out;
            s33 = 0u;
        }
        if (scc == 0u) {
            exec_lo = 0u;
            if (exec_lo == 0u) discard;
            color0 = float4(unpackHalf2x16(0u), unpackHalf2x16(0u));
            return;
        }
        exec_lo = vcc_lo;
    } else {
        {
            uint c_out = exec_lo != 0u && (f(s19) > f(v4)) ? 1u : 0u;
            vcc_lo = c_out;
            vcc_hi = 0u;
        }
        if (exec_lo != 0u) {
            v6 = 0x3f800000u;
        }
        {
            uint s_in = (vcc_lo & 1u);
            uint s_old = exec_lo;
            exec_lo = (s_in & exec_lo);
            scc = exec_lo != 0u ? 1u : 0u;
            vcc_lo = s_old;
            vcc_hi = 0u;
        }
        {
            uint m_out = ((s32 & 1u) & (~(exec_lo & 1u) & 1u));
            scc = m_out != 0u ? 1u : 0u;
            s32 = m_out;
            s33 = 0u;
        }
        if (scc == 0u) {
            exec_lo = 0u;
            if (exec_lo == 0u) discard;
            color0 = float4(unpackHalf2x16(0u), unpackHalf2x16(0u));
            return;
        }
        exec_lo = vcc_lo;
    }
    s12 = UiWord(ps_constants,0);
    s13 = UiWord(ps_constants,1);
    s14 = UiWord(ps_constants,2);
    s15 = UiWord(ps_constants,3);
    {
        uint c_out = exec_lo != 0u && (f(s12) < 0.0) ? 1u : 0u;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    if (exec_lo != 0u) {
        v8 = u(attr0.z);
        v9 = u(attr0.y);
        v10 = u(attr0.x);
    }
    if (!((vcc_lo & 1u) == 0u)) {
        {
            uint c_out = exec_lo != 0u && (f(s13) != 1.0) ? 1u : 0u;
            vcc_lo = c_out;
            vcc_hi = 0u;
        }
        if (!((vcc_lo & 1u) == 0u)) {
            if (exec_lo != 0u) {
                v2 = (0x1u & v7) | (~0x1u & v2);
                {
                    float4 t_out = UiSample(texture0,sampler0, float2(f(v2), f(v3)));
                    v0 = u(t_out.x);
                    v1 = u(t_out.y);
                    v2 = u(t_out.z);
                }
                v0 = u(f(v0) * f(v10));
                v1 = u(f(v9) * f(v1));
                v2 = u(f(v8) * f(v2));
            }
        } else {
            if (exec_lo != 0u) {
                v0 = u(f(s14) * f(v2));
                v0 = u(floor(f(v0)));
                v1 = u(0.5 * f(v0));
                v1 = u((frac(abs(f(v1)))) * 2.0);
            }
            {
                uint c_out = exec_lo != 0u && (0.0 > f(v0)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            if (exec_lo != 0u) {
                v1 = (vcc_lo & 1u) != 0u ? (v1 ^ 0x80000000u) : v1;
                v0 = u(1.0 / f(s14));
            }
            {
                uint c_out = exec_lo != 0u && (0.0 == f(v1)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            if (exec_lo != 0u) {
                v1 = u(f(v2) - f(v0));
                v1 = (vcc_lo & 1u) != 0u ? v2 : v1;
                v8 = u(f(v2) + f(v0));
                v4 = 0x3eaaaaaau;
                v0 = (0x1u & v7) | (~0x1u & v1);
                v1 = u(f(v4) * f(v3) + f(0x3f2aaaaau));
                v4 = (0x1u & v7) | (~0x1u & v2);
                v5 = u(f(0x3f2aaaaau) * f(v3));
                v2 = (vcc_lo & 1u) != 0u ? v8 : v2;
                {
                    float4 t_out = UiSample(texture0,sampler0, float2(f(v0), f(v1)));
                    v0 = u(t_out.x);
                }
                {
                    float4 t_out = UiSample(texture0,sampler0, float2(f(v4), f(v5)));
                    v4 = u(t_out.x);
                }
                v2 = (0x1u & v7) | (~0x1u & v2);
                v3 = v1;
                {
                    float4 t_out = UiSample(texture0,sampler0, float2(f(v2), f(v3)));
                    v1 = u(t_out.x);
                }
                v3 = 0x3f9507c8u;
                v8 = 0xbec890d6u;
            }
            vcc_hi = 0x3e4890d6u;
            if (exec_lo != 0u) {
                v5 = 0xbf501a37u;
            }
            s0 = 0x3ed01a37u;
            if (exec_lo != 0u) {
                v2 = 0x3fcc432du;
            }
            s1 = 0xbf4c432du;
            if (exec_lo != 0u) {
                v7 = 0x40011687u;
            }
            vcc_lo = 0xbf811687u;
            if (exec_lo != 0u) {
                v8 = u(mad(f(v0), f(v8), f(vcc_hi)));
                v3 = u(f(v3) * f(v4) + f(0xbd9507c8u));
                v4 = u(f(v3) + f(v8));
                v2 = u(mad(f(v1), f(v2), f(s1)));
                v7 = u(mad(f(v0), f(v7), f(vcc_lo)));
                v0 = u(mad(f(v1), f(v5), f(s0)));
                v1 = u(f(v4) + f(v0));
                v0 = u(f(v3) + f(v2));
                v2 = u(f(v3) + f(v7));
            }
        }
        if (exec_lo != 0u) {
            v3 = (s12 ^ 0x80000000u);
        }
    } else {
        if (exec_lo != 0u) {
            v4 = (0x1u & v7) | (~0x1u & v2);
            v5 = v3;
            {
                float4 t_out = UiSample(texture0,sampler0, float2(f(v4), f(v5)));
                v2 = u(t_out.x);
                v3 = u(t_out.y);
                v4 = u(t_out.z);
                v5 = u(t_out.w);
            }
            v0 = u(attr0.w);
            v7 = u(f(s12) * f(v0));
            v0 = u(f(v2) * f(v10));
            v1 = u(f(v9) * f(v3));
            v2 = u(f(v8) * f(v4));
            v3 = u(f(v7) * f(v5));
        }
    }
    if (exec_lo != 0u) {
        v3 = u(f(v6) * f(v3));
        v0 = u(clamp(f(v0) * f(v3), 0.0, 1.0));
        v1 = u(clamp(f(v1) * f(v3), 0.0, 1.0));
        v2 = u(clamp(f(v2) * f(v3), 0.0, 1.0));
        v3 = u(clamp(max(0.0, f(v3)), 0.0, 1.0));
        v0 = packHalf2x16(float2(f(v0), f(v1)));
        v1 = packHalf2x16(float2(f(v2), f(v3)));
    }
    exec_lo = s32;
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(v0), unpackHalf2x16(v1));
    return;
    exec_lo = 0u;
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(0u), unpackHalf2x16(0u));
    return;
}

float4 main(UIIn input):SV_Target0{ui_position=input.position;attr0=input.attr0;attr1=input.attr1;draw_index=input.draw_index;Program();return color0;}
