// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_threshold.frag; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIIn {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u, v4 = 0u, v5 = 0u, v6 = 0u, v7 = 0u;
    uint s0 = 0u, s1 = 0u, s12 = 0u, s13 = 0u, s14 = 0u, s15 = 0u, s16 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint texture0 = UiWord(draw_block,8);
    uint texture1 = UiWord(draw_block,9);
    uint sampler0 = UiWord(draw_block,10);
    uint sampler1 = UiWord(draw_block,11);
    ArenaRef ps_constants = UiRef(UiWord(draw_block,6),UiWord(draw_block,7));
    m0 = s16;
    s12 = UiWord(ps_constants,0);
    s13 = UiWord(ps_constants,1);
    vcc_lo = 0x3dea7371u;
    s14 = exec_lo;
    s15 = 0u;
    exec_lo = exec_lo;
    scc = 1u;
    if (exec_lo != 0u) {
        v2 = u(attr0.x);
        v3 = u(attr0.y);
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v2), f(v3)));
            v0 = u(t_out.x);
            v1 = u(t_out.y);
            v2 = u(t_out.z);
        }
        v3 = 0x0u;
        v7 = 0x0u;
        v4 = 0x0u;
        v5 = 0x0u;
        v6 = u(f(0x3e990afeu) * f(v0));
        v6 = u(f(v1) * f(0x3f162c23u) + f(v6));
        v6 = u(clamp(f(vcc_lo) * f(v2) + f(v6), 0.0, 1.0));
    }
    {
        uint c_out = exec_lo != 0u && (f(s13) >= f(v6)) ? 1u : 0u;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    {
        uint c_out = exec_lo != 0u && (f(s12) < f(v6)) ? 1u : 0u;
        s0 = c_out;
        s1 = 0u;
    }
    {
        uint m_out = ((s0 & 1u) & (vcc_lo & 1u));
        scc = m_out != 0u ? 1u : 0u;
        vcc_lo = m_out;
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
    if (!(exec_lo == 0u)) {
        if (exec_lo != 0u) {
            v3 = v0;
            v0 = s12;
            v0 = u(f(s13) - f(v0));
            v0 = u(1.0 / f(v0));
            v5 = u(f(v0) * f(v6));
            v7 = v1;
            v4 = v2;
            v5 = u(-f(s12) * f(v0) + f(v5));
        }
    }
    exec_lo = vcc_lo;
    if (exec_lo != 0u) {
        v0 = packHalf2x16(float2(f(v3), f(v7)));
        v1 = packHalf2x16(float2(f(v4), f(v5)));
    }
    exec_lo = s14;
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(v0), unpackHalf2x16(v1));
    return;
}

float4 main(UIIn input):SV_Target0{ui_position=input.position;attr0=input.attr0;attr1=input.attr1;draw_index=input.draw_index;Program();return color0;}
