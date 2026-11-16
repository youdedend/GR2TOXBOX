// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_frame.frag; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIIn {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u, v4 = 0u, v5 = 0u, v6 = 0u, v7 = 0u;
    uint v8 = 0u, v9 = 0u, v10 = 0u, v11 = 0u, v12 = 0u, v13 = 0u, v14 = 0u, v15 = 0u;
    uint v16 = 0u, v17 = 0u;
    uint s0 = 0u, s1 = 0u, s2 = 0u, s3 = 0u, s16 = 0u, s17 = 0u, s18 = 0u, s19 = 0u;
    uint s20 = 0u, s21 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint texture0 = UiWord(draw_block,8);
    uint texture1 = UiWord(draw_block,9);
    uint sampler0 = UiWord(draw_block,10);
    uint sampler1 = UiWord(draw_block,11);
    ArenaRef ps_constants = UiRef(UiWord(draw_block,6),UiWord(draw_block,7));
    s18 = UiWord(ps_constants,4);
    s19 = UiWord(ps_constants,5);
    m0 = s16;
    {
        uint c_out = exec_lo != 0u && (f(s19) == 0.0) ? 1u : 0u;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    {
        uint c_out = exec_lo != 0u && (f(s18) == 0.0) ? 1u : 0u;
        s16 = c_out;
        s17 = 0u;
    }
    {
        uint m_out = ((s16 & 1u) & (vcc_lo & 1u));
        scc = m_out != 0u ? 1u : 0u;
        vcc_lo = m_out;
        vcc_hi = 0u;
    }
    s20 = exec_lo;
    s21 = 0u;
    if (exec_lo != 0u) {
        v2 = u(attr1.x);
        v3 = u(attr1.y);
        v12 = u(attr0.x);
        v11 = u(attr0.y);
        v10 = u(attr0.z);
    }
    if (!((vcc_lo & 1u) == 0u)) {
        if (exec_lo != 0u) {
            {
                float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v2), f(v3)), 0.0);
                v2 = u(t_out.x);
            }
        }
    } else {
        if (exec_lo != 0u) {
            v4 = 0x3f333333u;
            v5 = u(f(v4) * -f(s19) + f(v3));
            v6 = s18;
            v4 = u(f(v4) * -f(s18) + f(v2));
            v6 = u(f(v6) * f(0x3f333333u) + f(v2));
            v7 = v5;
            v8 = v2;
            v9 = u(f(v3) - f(s19));
            {
                float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v4), f(v5)), 0.0);
                v14 = u(t_out.x);
            }
            {
                float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v6), f(v7)), 0.0);
                v15 = u(t_out.x);
            }
            {
                float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v8), f(v9)), 0.0);
                v9 = u(t_out.x);
            }
            {
                float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v2), f(v3)), 0.0);
                v13 = u(t_out.x);
            }
            v7 = u(f(v2) - f(s18));
            v8 = v3;
            v5 = s19;
            v5 = u(f(v5) * f(0x3f333333u) + f(v3));
            {
                float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v7), f(v8)), 0.0);
                v16 = u(t_out.x);
            }
            {
                float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v4), f(v5)), 0.0);
                v17 = u(t_out.x);
            }
            v7 = u(f(s18) + f(v2));
            v4 = u(f(s19) + f(v3));
            v3 = v2;
            {
                float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v7), f(v8)), 0.0);
                v8 = u(t_out.x);
            }
            {
                float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v3), f(v4)), 0.0);
                v3 = u(t_out.x);
            }
            v7 = v5;
            {
                float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v6), f(v7)), 0.0);
                v5 = u(t_out.x);
            }
        }
        s0 = UiWord(ps_constants,0);
        s1 = UiWord(ps_constants,1);
        s2 = UiWord(ps_constants,2);
        s3 = UiWord(ps_constants,3);
        if (exec_lo != 0u) {
            v4 = u(max(f(v14), f(v15)));
            v2 = u(-f(s3) * f(v13));
            v4 = u(max(max(f(v16), f(v4)), f(v9)));
            v4 = u(max(max(f(v17), f(v4)), f(v8)));
            v3 = u(max(max(f(v3), f(v4)), f(v5)));
            v3 = u(min(1.0, f(v3)));
            v7 = u(f(s3) * f(v3) + f(v13));
            v7 = u(f(v3) * f(v2) + f(v7));
        }
        {
            uint c_out = exec_lo != 0u && (0.0 == f(v7)) ? 1u : 0u;
            vcc_lo = c_out;
            vcc_hi = 0u;
        }
        {
            uint m_out = ((vcc_lo & 1u) & (exec_lo & 1u));
            scc = m_out != 0u ? 1u : 0u;
            vcc_lo = m_out;
            vcc_hi = 0u;
        }
        {
            uint m_out = ((s20 & 1u) & (~(vcc_lo & 1u) & 1u));
            scc = m_out != 0u ? 1u : 0u;
            s20 = m_out;
            s21 = 0u;
        }
        if (scc == 0u) {
            exec_lo = 0u;
            if (exec_lo == 0u) discard;
            color0 = float4(unpackHalf2x16(0u), unpackHalf2x16(0u));
            return;
        }
        if (exec_lo != 0u) {
            v5 = u(1.0 / f(v7));
            v4 = u(-f(v13) * f(v5) + 1.0);
            v3 = u(f(s0) * f(v4));
            v5 = u(f(v13) * f(v5));
            v6 = u(f(s1) * f(v4));
            v4 = u(f(s2) * f(v4));
            v12 = u(f(v12) * f(v5) + f(v3));
            v11 = u(f(v11) * f(v5) + f(v6));
            v10 = u(f(v10) * f(v5) + f(v4));
            v2 = u(min(1.0, f(v7)));
        }
    }
    if (exec_lo != 0u) {
        v0 = u(attr0.w);
        v0 = u(f(v0) * f(v2));
        v1 = u(clamp(f(v12) * f(v0), 0.0, 1.0));
        v2 = u(clamp(f(v11) * f(v0), 0.0, 1.0));
        v3 = u(clamp(f(v10) * f(v0), 0.0, 1.0));
        v0 = u(clamp(max(0.0, f(v0)), 0.0, 1.0));
        v1 = packHalf2x16(float2(f(v1), f(v2)));
        v0 = packHalf2x16(float2(f(v3), f(v0)));
    }
    exec_lo = s20;
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(v1), unpackHalf2x16(v0));
    return;
    exec_lo = 0u;
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(0u), unpackHalf2x16(0u));
    return;
}

float4 main(UIIn input):SV_Target0{ui_position=input.position;attr0=input.attr0;attr1=input.attr1;draw_index=input.draw_index;Program();return color0;}
