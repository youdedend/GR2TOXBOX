// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_gamma.frag; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIIn {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u, v4 = 0u, v5 = 0u;
    uint s0 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint texture0 = UiWord(draw_block,8);
    uint texture1 = UiWord(draw_block,9);
    uint sampler0 = UiWord(draw_block,10);
    uint sampler1 = UiWord(draw_block,11);
    ArenaRef ps_constants = UiRef(UiWord(draw_block,6),UiWord(draw_block,7));
    m0 = s0;
    if (exec_lo != 0u) {
        v2 = u(attr0.w);
        v3 = u(log2(abs(f(v2))));
        v5 = u(1.0 - f(v2));
        v4 = u(f(0x400ccccdu) * f(v3));
        v2 = u(exp2(f(v4)));
        v4 = u(log2(abs(f(v5))));
        v4 = u(f(0x400ccccdu) * f(v4));
        v4 = u(exp2(f(v4)));
        v4 = u(clamp(1.0 - f(v4), 0.0, 1.0));
        v3 = u(attr0.z);
        v3 = u(clamp(f(v2) * f(v3), 0.0, 1.0));
        v3 = packHalf2x16(float2(f(v3), f(v4)));
        v4 = u(attr0.x);
        v0 = u(attr0.y);
        v1 = u(clamp(f(v2) * f(v4), 0.0, 1.0));
        v0 = u(clamp(f(v2) * f(v0), 0.0, 1.0));
        v0 = packHalf2x16(float2(f(v1), f(v0)));
    }
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(v0), unpackHalf2x16(v3));
    return;
}

float4 main(UIIn input):SV_Target0{ui_position=input.position;attr0=input.attr0;attr1=input.attr1;draw_index=input.draw_index;Program();return color0;}
