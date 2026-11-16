// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_stroke.frag; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIIn {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u, v4 = 0u, v5 = 0u, v6 = 0u, v7 = 0u;
    uint v8 = 0u, v9 = 0u, v10 = 0u, v11 = 0u;
    uint s0 = 0u, s4 = 0u, s5 = 0u, s6 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint texture0 = UiWord(draw_block,8);
    uint texture1 = UiWord(draw_block,9);
    uint sampler0 = UiWord(draw_block,10);
    uint sampler1 = UiWord(draw_block,11);
    ArenaRef ps_constants = UiRef(UiWord(draw_block,6),UiWord(draw_block,7));
    if (exec_lo != 0u) {
        v10 = 0x3ad09b89u;
    }
    m0 = s4;
    s4 = UiWord(ps_constants,0);
    s5 = UiWord(ps_constants,1);
    if (exec_lo != 0u) {
        v4 = u(attr1.w);
        v5 = u(attr1.x);
    }
    vcc_lo = 0x40922223u;
    if (exec_lo != 0u) {
        v4 = u(f(s5) * f(v5) + f(v4));
        v8 = 0x3d088889u;
        v3 = u(mad(f(v4), f(v8), f(vcc_lo)));
    }
    vcc_lo = 0x3e5f4679u;
    if (exec_lo != 0u) {
        v6 = u(mad(f(v4), f(v10), f(vcc_lo)));
    }
    vcc_lo = 0x401f6b0eu;
    if (exec_lo != 0u) {
        v6 = u(frac(abs(f(v6))));
        v6 = u(cos(6.283185307 * f(v6)));
        v2 = 0x3e22f983u;
    }
    s6 = 0x3db8b462u;
    if (exec_lo != 0u) {
        v9 = 0x3c94f209u;
        v3 = u(mad(f(v3), f(v2), f(s6)));
    }
    vcc_hi = 0x3d6d0dd5u;
    if (exec_lo != 0u) {
        v3 = u(frac(abs(f(v3))));
        v3 = u(cos(6.283185307 * f(v3)));
        v3 = u(f(v6) + f(v3));
        v6 = u(mad(f(v4), f(v9), f(vcc_lo)));
    }
    vcc_lo = 0x411f7778u;
    if (exec_lo != 0u) {
        v8 = u(mad(f(v4), f(v8), f(vcc_lo)));
    }
    vcc_lo = 0x3ef3a5a9u;
    if (exec_lo != 0u) {
        v8 = u(mad(f(v8), f(v2), f(s6)));
        v10 = u(mad(f(v4), f(v10), f(vcc_lo)));
        v6 = u(mad(f(v6), f(v2), f(vcc_hi)));
        v8 = u(frac(abs(f(v8))));
        v10 = u(frac(abs(f(v10))));
    }
    vcc_lo = 0x40adf6b1u;
    if (exec_lo != 0u) {
        v6 = u(frac(abs(f(v6))));
        v8 = u(cos(6.283185307 * f(v8)));
        v10 = u(cos(6.283185307 * f(v10)));
        v9 = u(mad(f(v4), f(v9), f(vcc_lo)));
        v6 = u(cos(6.283185307 * f(v6)));
        v7 = u(0.5 * f(s4));
        v4 = u(f(v10) + f(v8));
        v2 = u(mad(f(v9), f(v2), f(vcc_hi)));
        v3 = u(f(v3) + f(v6));
        v2 = u(frac(abs(f(v2))));
        v2 = u(cos(6.283185307 * f(v2)));
        v6 = u(f(v4) + f(v2));
        v4 = u(1.0 / f(v7));
        v6 = u(f(v6) * f(v6));
    }
    s0 = UiWord(ps_constants,2);
    if (exec_lo != 0u) {
        v8 = u(f(v6) * f(v7));
        v9 = u(f(v3) * f(v3));
        v3 = u(clamp(f(v5) * f(v4), 0.0, 1.0));
        v2 = u(f(v3) * f(v3));
        v3 = u(f(v3) * f(v2));
    }
    vcc_lo = 0x3ede9bd3u;
    if (exec_lo != 0u) {
        v9 = u(f(v9) * f(v7));
        v2 = u(f(0x40400000u) * f(v2));
        v2 = u(-2.0 * f(v3) + f(v2));
        v3 = u(attr1.z);
        v6 = u(f(v3) - f(v5));
        v3 = u(clamp(f(v6) * f(v4), 0.0, 1.0));
        v10 = u(f(v3) * f(v3));
        v6 = u(0.5 * -f(s4) + f(v6));
        v3 = u(f(v3) * f(v10));
        v10 = u(f(0x40400000u) * f(v10));
        v3 = u(-2.0 * f(v3) + f(v10));
        v10 = 0x3fc00000u;
        v7 = u(f(v10) * f(s4) + -f(v7));
        v11 = u(1.0 / f(v7));
        v10 = u(clamp(f(v6) * f(v11), 0.0, 1.0));
        v7 = u(f(v10) * f(v10));
        v10 = u(f(v10) * f(v7));
        v7 = u(f(0x40400000u) * f(v7));
        v10 = u(-2.0 * f(v10) + f(v7));
        v7 = u(0.5 * -f(s4) + f(v5));
        v11 = u(clamp(f(v7) * f(v11), 0.0, 1.0));
        v5 = u(f(v11) * f(v11));
        v11 = u(f(v11) * f(v5));
        v5 = u(f(0x40400000u) * f(v5));
        v5 = u(-2.0 * f(v11) + f(v5));
        v11 = u(f(v5) * f(v10));
        v8 = u(f(v8) * f(v11));
        v9 = u(f(v9) * f(v11));
        v5 = u(f(v5) * f(v10) + -1.0);
        v10 = s0;
        v5 = u(f(v5) * f(v10) + f(0x40c00000u));
        v10 = u(f(s4) * f(v11));
        v11 = u(f(0x3e4ccccdu) * f(v10));
        v9 = u(f(vcc_lo) * -f(v9) + f(v11));
        v11 = u(f(vcc_lo) * -f(v8) + f(v11));
        v10 = u(attr1.y);
        v8 = u(f(v10) - f(v11));
        v11 = u(0.5 * f(s4) + -f(v11));
        v11 = u(1.0 / f(v11));
        v8 = u(clamp(f(v8) * f(v11), 0.0, 1.0));
        v11 = u(f(s4) - f(v10));
        v10 = u(0.5 * -f(s4) + f(v10));
        v11 = u(f(v11) - f(v9));
        v9 = u(0.5 * f(s4) + -f(v9));
        v9 = u(1.0 / f(v9));
        v10 = u(f(v10) * f(v10));
        v9 = u(clamp(f(v11) * f(v9), 0.0, 1.0));
        v7 = u(f(v7) * f(v7) + f(v10));
        v10 = u(f(v6) * f(v6) + f(v10));
        v6 = u(sqrt(f(v7)));
        v7 = u(sqrt(f(v10)));
        v10 = u(f(v8) * f(v8));
        v7 = u(clamp(f(v4) * f(v7), 0.0, 1.0));
        v6 = u(clamp(f(v6) * f(v4), 0.0, 1.0));
        v4 = u(f(v9) * f(v9));
        v9 = u(f(v9) * f(v4));
        v4 = u(f(0x40400000u) * f(v4));
        v11 = u(f(0x40400000u) * f(v10));
        v4 = u(-2.0 * f(v9) + f(v4));
        v9 = u(f(v8) * f(v10));
        v10 = u(f(v7) * f(v7));
        v8 = u(f(v7) * f(v10));
        v7 = u(-2.0 * f(v9) + f(v11));
        v9 = u(f(v6) * f(v6));
        v11 = u(f(0x40400000u) * f(v10));
        v10 = u(f(v6) * f(v9));
        v11 = u(-2.0 * f(v8) + f(v11));
        v8 = u(f(0x40400000u) * f(v9));
        v8 = u(-2.0 * f(v10) + f(v8));
        v6 = u(1.0 - f(v8));
        v4 = u(f(v4) * f(v7) + -f(v6));
        v6 = u(f(v4) * f(v2) + f(v6));
        v4 = u(1.0 - f(v11));
        v2 = u(f(v6) - f(v4));
        v4 = u(f(v2) * f(v3) + f(v4));
        v2 = u(log2(abs(f(v4))));
        v2 = u(f(v2) * f(v5));
        v2 = u(exp2(f(v2)));
        v3 = u(clamp(2.0 * f(v2) + -1.0, 0.0, 1.0));
        v2 = u(f(v3) * f(v3));
        v3 = u(f(v3) * f(v2));
        v5 = u(f(0x40400000u) * f(v2));
        v4 = u(attr0.w);
        v5 = u(-2.0 * f(v3) + f(v5));
        v3 = u(f(v4) * f(v5));
        v4 = u(attr0.x);
        v2 = u(attr0.y);
        v0 = u(attr0.z);
        v1 = u(clamp(f(v4) * f(v3), 0.0, 1.0));
        v2 = u(clamp(f(v2) * f(v3), 0.0, 1.0));
        v0 = u(clamp(f(v0) * f(v3), 0.0, 1.0));
        v3 = u(clamp(max(0.0, f(v3)), 0.0, 1.0));
        v1 = packHalf2x16(float2(f(v1), f(v2)));
        v0 = packHalf2x16(float2(f(v0), f(v3)));
    }
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(v1), unpackHalf2x16(v0));
    return;
}

float4 main(UIIn input):SV_Target0{ui_position=input.position;attr0=input.attr0;attr1=input.attr1;draw_index=input.draw_index;Program();return color0;}
