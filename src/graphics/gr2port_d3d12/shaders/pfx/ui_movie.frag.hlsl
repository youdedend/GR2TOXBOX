// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_movie.frag; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIIn {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u, v4 = 0u, v5 = 0u, v6 = 0u, v7 = 0u;
    uint s12 = 0u, s13 = 0u, s14 = 0u, s15 = 0u, s16 = 0u, s17 = 0u, s18 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint texture0 = UiWord(draw_block,8);
    uint texture1 = UiWord(draw_block,9);
    uint sampler0 = UiWord(draw_block,10);
    uint sampler1 = UiWord(draw_block,11);
    ArenaRef ps_constants = UiRef(UiWord(draw_block,6),UiWord(draw_block,7));
    m0 = s16;
    s14 = UiWord(ps_constants,0);
    s15 = 0x3e4890d6u;
    s16 = 0xbf811687u;
    s17 = 0x3ed01a37u;
    s18 = 0xbf4c432du;
    s12 = exec_lo;
    s13 = 0u;
    exec_lo = exec_lo;
    scc = 1u;
    if (exec_lo != 0u) {
        v2 = u(attr0.x);
        v3 = u(f(s14) * f(v2));
        v3 = u(floor(f(v3)));
        v4 = u(0.5 * f(v3));
        v4 = u((frac(abs(f(v4)))) * 2.0);
    }
    {
        uint c_out = exec_lo != 0u && (0.0 > f(v3)) ? 1u : 0u;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    if (exec_lo != 0u) {
        v5 = (vcc_lo & 1u) != 0u ? (v4 ^ 0x80000000u) : v4;
        v4 = u(1.0 / f(s14));
    }
    {
        uint c_out = exec_lo != 0u && (0.0 == f(v5)) ? 1u : 0u;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    if (exec_lo != 0u) {
        v0 = u(f(v2) - f(v4));
        v3 = u(attr0.y);
        v1 = 0x3eaaaaaau;
        v0 = (vcc_lo & 1u) != 0u ? v2 : v0;
        v1 = u(f(v1) * f(v3) + f(0x3f2aaaaau));
        v3 = u(f(0x3f2aaaaau) * f(v3));
        v5 = u(f(v2) + f(v4));
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v0), f(v1)));
            v0 = u(t_out.x);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v2), f(v3)));
            v4 = u(t_out.x);
        }
        v2 = (vcc_lo & 1u) != 0u ? v5 : v2;
        v3 = v1;
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v2), f(v3)));
            v1 = u(t_out.x);
        }
        v3 = 0x3f9507c8u;
        v7 = 0xbec890d6u;
        v6 = 0x40011687u;
        v5 = 0xbf501a37u;
        v2 = 0x3fcc432du;
        v7 = u(mad(f(v0), f(v7), f(s15)));
        v3 = u(f(v3) * f(v4) + f(0xbd9507c8u));
        v4 = u(f(v3) + f(v7));
        v5 = u(mad(f(v1), f(v5), f(s17)));
        v1 = u(mad(f(v1), f(v2), f(s18)));
        v0 = u(mad(f(v0), f(v6), f(s16)));
        v2 = u(f(v4) + f(v5));
        v1 = u(f(v3) + f(v1));
        v0 = u(f(v3) + f(v0));
        v1 = packHalf2x16(float2(f(v1), f(v2)));
        v0 = packHalf2x16(float2(f(v0), 1.0));
    }
    exec_lo = s12;
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(v1), unpackHalf2x16(v0));
    return;
}

float4 main(UIIn input):SV_Target0{ui_position=input.position;attr0=input.attr0;attr1=input.attr1;draw_index=input.draw_index;Program();return color0;}
