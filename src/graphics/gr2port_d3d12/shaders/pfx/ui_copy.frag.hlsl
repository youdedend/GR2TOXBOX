// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_copy.frag; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIIn {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u;
    uint s12 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint texture0 = UiWord(draw_block,8);
    uint texture1 = UiWord(draw_block,9);
    uint sampler0 = UiWord(draw_block,10);
    uint sampler1 = UiWord(draw_block,11);
    ArenaRef ps_constants = UiRef(UiWord(draw_block,6),UiWord(draw_block,7));
    m0 = s12;
    vcc_lo = exec_lo;
    vcc_hi = 0u;
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
            v3 = u(t_out.w);
        }
        v1 = u(clamp(max(0.0, f(v1)), 0.0, 1.0));
        v0 = u(clamp(max(0.0, f(v0)), 0.0, 1.0));
        v3 = u(clamp(max(0.0, f(v3)), 0.0, 1.0));
        v2 = u(clamp(max(0.0, f(v2)), 0.0, 1.0));
        v0 = packHalf2x16(float2(f(v0), f(v1)));
        v1 = packHalf2x16(float2(f(v2), f(v3)));
    }
    exec_lo = vcc_lo;
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(v0), unpackHalf2x16(v1));
    return;
}

float4 main(UIIn input):SV_Target0{ui_position=input.position;attr0=input.attr0;attr1=input.attr1;draw_index=input.draw_index;Program();return color0;}
