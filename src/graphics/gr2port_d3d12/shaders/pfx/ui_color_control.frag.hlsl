// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_color_control.frag; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIIn {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u, v4 = 0u;
    uint s0 = 0u, s1 = 0u, s2 = 0u, s3 = 0u, s4 = 0u, s5 = 0u, s6 = 0u, s7 = 0u;
    uint s8 = 0u, s9 = 0u, s10 = 0u, s11 = 0u, s16 = 0u, s17 = 0u, s18 = 0u, s19 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint texture0 = UiWord(draw_block,8);
    uint texture1 = UiWord(draw_block,9);
    uint sampler0 = UiWord(draw_block,10);
    uint sampler1 = UiWord(draw_block,11);
    ArenaRef ps_constants = UiRef(UiWord(draw_block,6),UiWord(draw_block,7));
    m0 = s16;
    if (exec_lo != 0u) {
        v2 = u(attr0.x);
        v3 = u(attr0.y);
        {
            float4 t_out = UiSampleLod(texture0,sampler0, float2(f(v2), f(v3)), 0.0);
            v0 = u(t_out.x);
            v1 = u(t_out.y);
            v2 = u(t_out.z);
        }
    }
    vcc_lo = 0x3f0ba2e9u;
    s8 = exec_lo;
    s9 = 0u;
    if (exec_lo != 0u) {
        v0 = u(clamp(max(0.0, f(v0)), 0.0, 1.0));
    }
    {
        uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v0)) ? 1u : 0u;
        vcc_lo = c_out;
        vcc_hi = 0u;
    }
    {
        uint s_in = (vcc_lo & 1u);
        uint s_old = exec_lo;
        exec_lo = (s_in & exec_lo);
        scc = exec_lo != 0u ? 1u : 0u;
        s10 = s_old;
        s11 = 0u;
    }
    if (!(exec_lo == 0u)) {
        vcc_lo = 0x3e8ba2e9u;
        {
            uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v0)) ? 1u : 0u;
            vcc_lo = c_out;
            vcc_hi = 0u;
        }
        {
            uint s_in = (vcc_lo & 1u);
            uint s_old = exec_lo;
            exec_lo = (s_in & exec_lo);
            scc = exec_lo != 0u ? 1u : 0u;
            s16 = s_old;
            s17 = 0u;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3e3a2e8cu;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v0)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            {
                uint s_in = (vcc_lo & 1u);
                uint s_old = exec_lo;
                exec_lo = (s_in & exec_lo);
                scc = exec_lo != 0u ? 1u : 0u;
                s4 = s_old;
                s5 = 0u;
            }
            if (!(exec_lo == 0u)) {
                vcc_lo = 0x3dba2e8cu;
                {
                    uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v0)) ? 1u : 0u;
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
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v3 = 0x41300000u;
                    }
                    s0 = UiWord(ps_constants,0);
                    s1 = UiWord(ps_constants,1);
                    if (exec_lo != 0u) {
                        v4 = s0;
                        v4 = u(f(s1) - f(v4));
                        v0 = u(f(v4) * f(v0));
                        v0 = u(f(v3) * f(v0) + f(s0));
                    }
                }
                {
                    uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                    scc = m_out != 0u ? 1u : 0u;
                    exec_lo = m_out;
                }
                if (!(exec_lo == 0u)) {
                    s0 = UiWord(ps_constants,0);
                    s1 = UiWord(ps_constants,1);
                    s2 = UiWord(ps_constants,2);
                    s3 = UiWord(ps_constants,3);
                    s6 = 0x41300000u;
                    if (exec_lo != 0u) {
                        v3 = s1;
                        v0 = u(f(s6) * f(v0) + -1.0);
                        v3 = u(f(s2) - f(v3));
                        v0 = u(f(v3) * f(v0) + f(s1));
                    }
                }
                exec_lo = vcc_lo;
            }
            {
                uint m_out = ((s4 & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                s0 = UiWord(ps_constants,0);
                s1 = UiWord(ps_constants,1);
                s2 = UiWord(ps_constants,2);
                s3 = UiWord(ps_constants,3);
                vcc_lo = 0x41300000u;
                if (exec_lo != 0u) {
                    v3 = s2;
                    v0 = u(f(vcc_lo) * f(v0) + -2.0);
                    v3 = u(f(s3) - f(v3));
                    v0 = u(f(v3) * f(v0) + f(s2));
                }
            }
            exec_lo = s4;
        }
        {
            uint m_out = ((s16 & 1u) & (~(exec_lo & 1u) & 1u));
            scc = m_out != 0u ? 1u : 0u;
            exec_lo = m_out;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3ee8ba2fu;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v0)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            {
                uint s_in = (vcc_lo & 1u);
                uint s_old = exec_lo;
                exec_lo = (s_in & exec_lo);
                scc = exec_lo != 0u ? 1u : 0u;
                s18 = s_old;
                s19 = 0u;
            }
            if (!(exec_lo == 0u)) {
                vcc_lo = 0x3eba2e8cu;
                {
                    uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v0)) ? 1u : 0u;
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
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v4 = 0x41300000u;
                    }
                    s0 = UiWord(ps_constants,0);
                    s1 = UiWord(ps_constants,1);
                    s2 = UiWord(ps_constants,2);
                    s3 = UiWord(ps_constants,3);
                    s4 = UiWord(ps_constants,4);
                    s5 = UiWord(ps_constants,5);
                    s6 = UiWord(ps_constants,6);
                    s7 = UiWord(ps_constants,7);
                    if (exec_lo != 0u) {
                        v0 = u(f(v4) * f(v0) + f(0xc0400000u));
                        v3 = s3;
                        v3 = u(f(s4) - f(v3));
                        v0 = u(f(v3) * f(v0) + f(s3));
                    }
                }
                {
                    uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                    scc = m_out != 0u ? 1u : 0u;
                    exec_lo = m_out;
                }
                if (!(exec_lo == 0u)) {
                    s0 = UiWord(ps_constants,4);
                    s1 = UiWord(ps_constants,5);
                    s2 = 0x41300000u;
                    if (exec_lo != 0u) {
                        v3 = s0;
                        v0 = u(f(s2) * f(v0) + -4.0);
                        v3 = u(f(s1) - f(v3));
                        v0 = u(f(v3) * f(v0) + f(s0));
                    }
                }
                exec_lo = vcc_lo;
            }
            {
                uint m_out = ((s18 & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x41300000u;
                }
                s0 = UiWord(ps_constants,4);
                s1 = UiWord(ps_constants,5);
                s2 = UiWord(ps_constants,6);
                s3 = UiWord(ps_constants,7);
                if (exec_lo != 0u) {
                    v0 = u(f(v4) * f(v0) + f(0xc0a00000u));
                    v3 = s1;
                    v3 = u(f(s2) - f(v3));
                    v0 = u(f(v3) * f(v0) + f(s1));
                }
            }
        }
        exec_lo = s16;
    }
    {
        uint m_out = ((s10 & 1u) & (~(exec_lo & 1u) & 1u));
        scc = m_out != 0u ? 1u : 0u;
        exec_lo = m_out;
    }
    if (!(exec_lo == 0u)) {
        vcc_lo = 0x3f3a2e8cu;
        {
            uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v0)) ? 1u : 0u;
            vcc_lo = c_out;
            vcc_hi = 0u;
        }
        {
            uint s_in = (vcc_lo & 1u);
            uint s_old = exec_lo;
            exec_lo = (s_in & exec_lo);
            scc = exec_lo != 0u ? 1u : 0u;
            s16 = s_old;
            s17 = 0u;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3f22e8bau;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v0)) ? 1u : 0u;
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
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x41300003u;
                }
                s0 = UiWord(ps_constants,4);
                s1 = UiWord(ps_constants,5);
                s2 = UiWord(ps_constants,6);
                s3 = UiWord(ps_constants,7);
                if (exec_lo != 0u) {
                    v0 = u(f(v4) * f(v0) + f(0xc0c00004u));
                    v3 = s2;
                    v3 = u(f(s3) - f(v3));
                    v0 = u(f(v3) * f(v0) + f(s2));
                }
            }
            {
                uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x412ffffcu;
                }
                s0 = UiWord(ps_constants,4);
                s1 = UiWord(ps_constants,5);
                s2 = UiWord(ps_constants,6);
                s3 = UiWord(ps_constants,7);
                s4 = UiWord(ps_constants,8);
                s5 = UiWord(ps_constants,9);
                s6 = UiWord(ps_constants,10);
                s7 = UiWord(ps_constants,11);
                if (exec_lo != 0u) {
                    v0 = u(f(v4) * f(v0) + f(0xc0dffffbu));
                    v3 = s3;
                    v3 = u(f(s4) - f(v3));
                    v0 = u(f(v3) * f(v0) + f(s3));
                }
            }
            exec_lo = vcc_lo;
        }
        {
            uint m_out = ((s16 & 1u) & (~(exec_lo & 1u) & 1u));
            scc = m_out != 0u ? 1u : 0u;
            exec_lo = m_out;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3f68ba2fu;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v0)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            {
                uint s_in = (vcc_lo & 1u);
                uint s_old = exec_lo;
                exec_lo = (s_in & exec_lo);
                scc = exec_lo != 0u ? 1u : 0u;
                s4 = s_old;
                s5 = 0u;
            }
            if (!(exec_lo == 0u)) {
                vcc_lo = 0x3f51745eu;
                {
                    uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v0)) ? 1u : 0u;
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
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v4 = 0x412ffffcu;
                    }
                    s0 = UiWord(ps_constants,8);
                    s1 = UiWord(ps_constants,9);
                    if (exec_lo != 0u) {
                        v0 = u(f(v4) * f(v0) + f(0xc0fffffbu));
                        v3 = s0;
                        v3 = u(f(s1) - f(v3));
                        v0 = u(f(v3) * f(v0) + f(s0));
                    }
                }
                {
                    uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                    scc = m_out != 0u ? 1u : 0u;
                    exec_lo = m_out;
                }
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v4 = 0x41300003u;
                    }
                    s0 = UiWord(ps_constants,8);
                    s1 = UiWord(ps_constants,9);
                    s2 = UiWord(ps_constants,10);
                    s3 = UiWord(ps_constants,11);
                    if (exec_lo != 0u) {
                        v0 = u(f(v4) * f(v0) + f(0xc1100003u));
                        v3 = s1;
                        v3 = u(f(s2) - f(v3));
                        v0 = u(f(v3) * f(v0) + f(s1));
                    }
                }
                exec_lo = vcc_lo;
            }
            {
                uint m_out = ((s4 & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x41300003u;
                }
                s0 = UiWord(ps_constants,8);
                s1 = UiWord(ps_constants,9);
                s2 = UiWord(ps_constants,10);
                s3 = UiWord(ps_constants,11);
                if (exec_lo != 0u) {
                    v0 = u(f(v4) * f(v0) + f(0xc1200003u));
                    v3 = s2;
                    v3 = u(f(s3) - f(v3));
                    v0 = u(f(v3) * f(v0) + f(s2));
                }
            }
        }
    }
    exec_lo = s10;
    if (exec_lo != 0u) {
        v1 = u(clamp(max(0.0, f(v1)), 0.0, 1.0));
    }
    vcc_lo = 0x3f0ba2e9u;
    {
        uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v1)) ? 1u : 0u;
        vcc_lo = c_out;
        vcc_hi = 0u;
        exec_lo = c_out;
    }
    if (!(exec_lo == 0u)) {
        vcc_lo = 0x3e8ba2e9u;
        {
            uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v1)) ? 1u : 0u;
            vcc_lo = c_out;
            vcc_hi = 0u;
        }
        {
            uint s_in = (vcc_lo & 1u);
            uint s_old = exec_lo;
            exec_lo = (s_in & exec_lo);
            scc = exec_lo != 0u ? 1u : 0u;
            s16 = s_old;
            s17 = 0u;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3e3a2e8cu;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v1)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            {
                uint s_in = (vcc_lo & 1u);
                uint s_old = exec_lo;
                exec_lo = (s_in & exec_lo);
                scc = exec_lo != 0u ? 1u : 0u;
                s4 = s_old;
                s5 = 0u;
            }
            if (!(exec_lo == 0u)) {
                vcc_lo = 0x3dba2e8cu;
                {
                    uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v1)) ? 1u : 0u;
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
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v3 = 0x41300000u;
                    }
                    s0 = UiWord(ps_constants,12);
                    s1 = UiWord(ps_constants,13);
                    if (exec_lo != 0u) {
                        v4 = s0;
                        v4 = u(f(s1) - f(v4));
                        v1 = u(f(v4) * f(v1));
                        v1 = u(f(v3) * f(v1) + f(s0));
                    }
                }
                {
                    uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                    scc = m_out != 0u ? 1u : 0u;
                    exec_lo = m_out;
                }
                if (!(exec_lo == 0u)) {
                    s0 = UiWord(ps_constants,12);
                    s1 = UiWord(ps_constants,13);
                    s2 = UiWord(ps_constants,14);
                    s3 = UiWord(ps_constants,15);
                    s6 = 0x41300000u;
                    if (exec_lo != 0u) {
                        v3 = s1;
                        v1 = u(f(s6) * f(v1) + -1.0);
                        v3 = u(f(s2) - f(v3));
                        v1 = u(f(v3) * f(v1) + f(s1));
                    }
                }
                exec_lo = vcc_lo;
            }
            {
                uint m_out = ((s4 & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                s0 = UiWord(ps_constants,12);
                s1 = UiWord(ps_constants,13);
                s2 = UiWord(ps_constants,14);
                s3 = UiWord(ps_constants,15);
                vcc_lo = 0x41300000u;
                if (exec_lo != 0u) {
                    v3 = s2;
                    v1 = u(f(vcc_lo) * f(v1) + -2.0);
                    v3 = u(f(s3) - f(v3));
                    v1 = u(f(v3) * f(v1) + f(s2));
                }
            }
            exec_lo = s4;
        }
        {
            uint m_out = ((s16 & 1u) & (~(exec_lo & 1u) & 1u));
            scc = m_out != 0u ? 1u : 0u;
            exec_lo = m_out;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3ee8ba2fu;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v1)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            {
                uint s_in = (vcc_lo & 1u);
                uint s_old = exec_lo;
                exec_lo = (s_in & exec_lo);
                scc = exec_lo != 0u ? 1u : 0u;
                s18 = s_old;
                s19 = 0u;
            }
            if (!(exec_lo == 0u)) {
                vcc_lo = 0x3eba2e8cu;
                {
                    uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v1)) ? 1u : 0u;
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
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v4 = 0x41300000u;
                    }
                    s0 = UiWord(ps_constants,12);
                    s1 = UiWord(ps_constants,13);
                    s2 = UiWord(ps_constants,14);
                    s3 = UiWord(ps_constants,15);
                    s4 = UiWord(ps_constants,16);
                    s5 = UiWord(ps_constants,17);
                    s6 = UiWord(ps_constants,18);
                    s7 = UiWord(ps_constants,19);
                    if (exec_lo != 0u) {
                        v1 = u(f(v4) * f(v1) + f(0xc0400000u));
                        v3 = s3;
                        v3 = u(f(s4) - f(v3));
                        v1 = u(f(v3) * f(v1) + f(s3));
                    }
                }
                {
                    uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                    scc = m_out != 0u ? 1u : 0u;
                    exec_lo = m_out;
                }
                if (!(exec_lo == 0u)) {
                    s0 = UiWord(ps_constants,16);
                    s1 = UiWord(ps_constants,17);
                    s2 = 0x41300000u;
                    if (exec_lo != 0u) {
                        v3 = s0;
                        v1 = u(f(s2) * f(v1) + -4.0);
                        v3 = u(f(s1) - f(v3));
                        v1 = u(f(v3) * f(v1) + f(s0));
                    }
                }
                exec_lo = vcc_lo;
            }
            {
                uint m_out = ((s18 & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x41300000u;
                }
                s0 = UiWord(ps_constants,16);
                s1 = UiWord(ps_constants,17);
                s2 = UiWord(ps_constants,18);
                s3 = UiWord(ps_constants,19);
                if (exec_lo != 0u) {
                    v1 = u(f(v4) * f(v1) + f(0xc0a00000u));
                    v3 = s1;
                    v3 = u(f(s2) - f(v3));
                    v1 = u(f(v3) * f(v1) + f(s1));
                }
            }
        }
        exec_lo = s16;
    }
    {
        uint m_out = ((s10 & 1u) & (~(exec_lo & 1u) & 1u));
        scc = m_out != 0u ? 1u : 0u;
        exec_lo = m_out;
    }
    if (!(exec_lo == 0u)) {
        vcc_lo = 0x3f3a2e8cu;
        {
            uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v1)) ? 1u : 0u;
            vcc_lo = c_out;
            vcc_hi = 0u;
        }
        {
            uint s_in = (vcc_lo & 1u);
            uint s_old = exec_lo;
            exec_lo = (s_in & exec_lo);
            scc = exec_lo != 0u ? 1u : 0u;
            s16 = s_old;
            s17 = 0u;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3f22e8bau;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v1)) ? 1u : 0u;
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
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x41300003u;
                }
                s0 = UiWord(ps_constants,16);
                s1 = UiWord(ps_constants,17);
                s2 = UiWord(ps_constants,18);
                s3 = UiWord(ps_constants,19);
                if (exec_lo != 0u) {
                    v1 = u(f(v4) * f(v1) + f(0xc0c00004u));
                    v3 = s2;
                    v3 = u(f(s3) - f(v3));
                    v1 = u(f(v3) * f(v1) + f(s2));
                }
            }
            {
                uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x412ffffcu;
                }
                s0 = UiWord(ps_constants,16);
                s1 = UiWord(ps_constants,17);
                s2 = UiWord(ps_constants,18);
                s3 = UiWord(ps_constants,19);
                s4 = UiWord(ps_constants,20);
                s5 = UiWord(ps_constants,21);
                s6 = UiWord(ps_constants,22);
                s7 = UiWord(ps_constants,23);
                if (exec_lo != 0u) {
                    v1 = u(f(v4) * f(v1) + f(0xc0dffffbu));
                    v3 = s3;
                    v3 = u(f(s4) - f(v3));
                    v1 = u(f(v3) * f(v1) + f(s3));
                }
            }
            exec_lo = vcc_lo;
        }
        {
            uint m_out = ((s16 & 1u) & (~(exec_lo & 1u) & 1u));
            scc = m_out != 0u ? 1u : 0u;
            exec_lo = m_out;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3f68ba2fu;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v1)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            {
                uint s_in = (vcc_lo & 1u);
                uint s_old = exec_lo;
                exec_lo = (s_in & exec_lo);
                scc = exec_lo != 0u ? 1u : 0u;
                s4 = s_old;
                s5 = 0u;
            }
            if (!(exec_lo == 0u)) {
                vcc_lo = 0x3f51745eu;
                {
                    uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v1)) ? 1u : 0u;
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
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v4 = 0x412ffffcu;
                    }
                    s0 = UiWord(ps_constants,20);
                    s1 = UiWord(ps_constants,21);
                    if (exec_lo != 0u) {
                        v1 = u(f(v4) * f(v1) + f(0xc0fffffbu));
                        v3 = s0;
                        v3 = u(f(s1) - f(v3));
                        v1 = u(f(v3) * f(v1) + f(s0));
                    }
                }
                {
                    uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                    scc = m_out != 0u ? 1u : 0u;
                    exec_lo = m_out;
                }
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v4 = 0x41300003u;
                    }
                    s0 = UiWord(ps_constants,20);
                    s1 = UiWord(ps_constants,21);
                    s2 = UiWord(ps_constants,22);
                    s3 = UiWord(ps_constants,23);
                    if (exec_lo != 0u) {
                        v1 = u(f(v4) * f(v1) + f(0xc1100003u));
                        v3 = s1;
                        v3 = u(f(s2) - f(v3));
                        v1 = u(f(v3) * f(v1) + f(s1));
                    }
                }
                exec_lo = vcc_lo;
            }
            {
                uint m_out = ((s4 & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x41300003u;
                }
                s0 = UiWord(ps_constants,20);
                s1 = UiWord(ps_constants,21);
                s2 = UiWord(ps_constants,22);
                s3 = UiWord(ps_constants,23);
                if (exec_lo != 0u) {
                    v1 = u(f(v4) * f(v1) + f(0xc1200003u));
                    v3 = s2;
                    v3 = u(f(s3) - f(v3));
                    v1 = u(f(v3) * f(v1) + f(s2));
                }
            }
        }
    }
    exec_lo = s10;
    if (exec_lo != 0u) {
        v2 = u(clamp(max(0.0, f(v2)), 0.0, 1.0));
    }
    vcc_lo = 0x3f0ba2e9u;
    {
        uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v2)) ? 1u : 0u;
        vcc_lo = c_out;
        vcc_hi = 0u;
        exec_lo = c_out;
    }
    if (!(exec_lo == 0u)) {
        vcc_lo = 0x3e8ba2e9u;
        {
            uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v2)) ? 1u : 0u;
            vcc_lo = c_out;
            vcc_hi = 0u;
        }
        {
            uint s_in = (vcc_lo & 1u);
            uint s_old = exec_lo;
            exec_lo = (s_in & exec_lo);
            scc = exec_lo != 0u ? 1u : 0u;
            s16 = s_old;
            s17 = 0u;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3e3a2e8cu;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v2)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            {
                uint s_in = (vcc_lo & 1u);
                uint s_old = exec_lo;
                exec_lo = (s_in & exec_lo);
                scc = exec_lo != 0u ? 1u : 0u;
                s4 = s_old;
                s5 = 0u;
            }
            if (!(exec_lo == 0u)) {
                vcc_lo = 0x3dba2e8cu;
                {
                    uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v2)) ? 1u : 0u;
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
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v3 = 0x41300000u;
                    }
                    s0 = UiWord(ps_constants,24);
                    s1 = UiWord(ps_constants,25);
                    if (exec_lo != 0u) {
                        v4 = s0;
                        v4 = u(f(s1) - f(v4));
                        v2 = u(f(v4) * f(v2));
                        v2 = u(f(v3) * f(v2) + f(s0));
                    }
                }
                {
                    uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                    scc = m_out != 0u ? 1u : 0u;
                    exec_lo = m_out;
                }
                if (!(exec_lo == 0u)) {
                    s0 = UiWord(ps_constants,24);
                    s1 = UiWord(ps_constants,25);
                    s2 = UiWord(ps_constants,26);
                    s3 = UiWord(ps_constants,27);
                    s6 = 0x41300000u;
                    if (exec_lo != 0u) {
                        v3 = s1;
                        v2 = u(f(s6) * f(v2) + -1.0);
                        v3 = u(f(s2) - f(v3));
                        v2 = u(f(v3) * f(v2) + f(s1));
                    }
                }
                exec_lo = vcc_lo;
            }
            {
                uint m_out = ((s4 & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                s0 = UiWord(ps_constants,24);
                s1 = UiWord(ps_constants,25);
                s2 = UiWord(ps_constants,26);
                s3 = UiWord(ps_constants,27);
                vcc_lo = 0x41300000u;
                if (exec_lo != 0u) {
                    v3 = s2;
                    v2 = u(f(vcc_lo) * f(v2) + -2.0);
                    v3 = u(f(s3) - f(v3));
                    v2 = u(f(v3) * f(v2) + f(s2));
                }
            }
            exec_lo = s4;
        }
        {
            uint m_out = ((s16 & 1u) & (~(exec_lo & 1u) & 1u));
            scc = m_out != 0u ? 1u : 0u;
            exec_lo = m_out;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3ee8ba2fu;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v2)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            {
                uint s_in = (vcc_lo & 1u);
                uint s_old = exec_lo;
                exec_lo = (s_in & exec_lo);
                scc = exec_lo != 0u ? 1u : 0u;
                s18 = s_old;
                s19 = 0u;
            }
            if (!(exec_lo == 0u)) {
                vcc_lo = 0x3eba2e8cu;
                {
                    uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v2)) ? 1u : 0u;
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
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v4 = 0x41300000u;
                    }
                    s0 = UiWord(ps_constants,24);
                    s1 = UiWord(ps_constants,25);
                    s2 = UiWord(ps_constants,26);
                    s3 = UiWord(ps_constants,27);
                    s4 = UiWord(ps_constants,28);
                    s5 = UiWord(ps_constants,29);
                    s6 = UiWord(ps_constants,30);
                    s7 = UiWord(ps_constants,31);
                    if (exec_lo != 0u) {
                        v2 = u(f(v4) * f(v2) + f(0xc0400000u));
                        v3 = s3;
                        v3 = u(f(s4) - f(v3));
                        v2 = u(f(v3) * f(v2) + f(s3));
                    }
                }
                {
                    uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                    scc = m_out != 0u ? 1u : 0u;
                    exec_lo = m_out;
                }
                if (!(exec_lo == 0u)) {
                    s0 = UiWord(ps_constants,28);
                    s1 = UiWord(ps_constants,29);
                    s2 = 0x41300000u;
                    if (exec_lo != 0u) {
                        v3 = s0;
                        v2 = u(f(s2) * f(v2) + -4.0);
                        v3 = u(f(s1) - f(v3));
                        v2 = u(f(v3) * f(v2) + f(s0));
                    }
                }
                exec_lo = vcc_lo;
            }
            {
                uint m_out = ((s18 & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x41300000u;
                }
                s0 = UiWord(ps_constants,28);
                s1 = UiWord(ps_constants,29);
                s2 = UiWord(ps_constants,30);
                s3 = UiWord(ps_constants,31);
                if (exec_lo != 0u) {
                    v2 = u(f(v4) * f(v2) + f(0xc0a00000u));
                    v3 = s1;
                    v3 = u(f(s2) - f(v3));
                    v2 = u(f(v3) * f(v2) + f(s1));
                }
            }
        }
        exec_lo = s16;
    }
    {
        uint m_out = ((s10 & 1u) & (~(exec_lo & 1u) & 1u));
        scc = m_out != 0u ? 1u : 0u;
        exec_lo = m_out;
    }
    if (!(exec_lo == 0u)) {
        vcc_lo = 0x3f3a2e8cu;
        {
            uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v2)) ? 1u : 0u;
            vcc_lo = c_out;
            vcc_hi = 0u;
        }
        {
            uint s_in = (vcc_lo & 1u);
            uint s_old = exec_lo;
            exec_lo = (s_in & exec_lo);
            scc = exec_lo != 0u ? 1u : 0u;
            s16 = s_old;
            s17 = 0u;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3f22e8bau;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v2)) ? 1u : 0u;
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
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x41300003u;
                }
                s0 = UiWord(ps_constants,28);
                s1 = UiWord(ps_constants,29);
                s2 = UiWord(ps_constants,30);
                s3 = UiWord(ps_constants,31);
                if (exec_lo != 0u) {
                    v2 = u(f(v4) * f(v2) + f(0xc0c00004u));
                    v3 = s2;
                    v3 = u(f(s3) - f(v3));
                    v2 = u(f(v3) * f(v2) + f(s2));
                }
            }
            {
                uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x412ffffcu;
                }
                s0 = UiWord(ps_constants,28);
                s1 = UiWord(ps_constants,29);
                s2 = UiWord(ps_constants,30);
                s3 = UiWord(ps_constants,31);
                s4 = UiWord(ps_constants,32);
                s5 = UiWord(ps_constants,33);
                s6 = UiWord(ps_constants,34);
                s7 = UiWord(ps_constants,35);
                if (exec_lo != 0u) {
                    v2 = u(f(v4) * f(v2) + f(0xc0dffffbu));
                    v3 = s3;
                    v3 = u(f(s4) - f(v3));
                    v2 = u(f(v3) * f(v2) + f(s3));
                }
            }
            exec_lo = vcc_lo;
        }
        {
            uint m_out = ((s16 & 1u) & (~(exec_lo & 1u) & 1u));
            scc = m_out != 0u ? 1u : 0u;
            exec_lo = m_out;
        }
        if (!(exec_lo == 0u)) {
            vcc_lo = 0x3f68ba2fu;
            {
                uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v2)) ? 1u : 0u;
                vcc_lo = c_out;
                vcc_hi = 0u;
            }
            {
                uint s_in = (vcc_lo & 1u);
                uint s_old = exec_lo;
                exec_lo = (s_in & exec_lo);
                scc = exec_lo != 0u ? 1u : 0u;
                s4 = s_old;
                s5 = 0u;
            }
            if (!(exec_lo == 0u)) {
                vcc_lo = 0x3f51745eu;
                {
                    uint c_out = exec_lo != 0u && (f(vcc_lo) > f(v2)) ? 1u : 0u;
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
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v4 = 0x412ffffcu;
                    }
                    s0 = UiWord(ps_constants,32);
                    s1 = UiWord(ps_constants,33);
                    if (exec_lo != 0u) {
                        v2 = u(f(v4) * f(v2) + f(0xc0fffffbu));
                        v3 = s0;
                        v3 = u(f(s1) - f(v3));
                        v2 = u(f(v3) * f(v2) + f(s0));
                    }
                }
                {
                    uint m_out = ((vcc_lo & 1u) & (~(exec_lo & 1u) & 1u));
                    scc = m_out != 0u ? 1u : 0u;
                    exec_lo = m_out;
                }
                if (!(exec_lo == 0u)) {
                    if (exec_lo != 0u) {
                        v4 = 0x41300003u;
                    }
                    s0 = UiWord(ps_constants,32);
                    s1 = UiWord(ps_constants,33);
                    s2 = UiWord(ps_constants,34);
                    s3 = UiWord(ps_constants,35);
                    if (exec_lo != 0u) {
                        v2 = u(f(v4) * f(v2) + f(0xc1100003u));
                        v3 = s1;
                        v3 = u(f(s2) - f(v3));
                        v2 = u(f(v3) * f(v2) + f(s1));
                    }
                }
                exec_lo = vcc_lo;
            }
            {
                uint m_out = ((s4 & 1u) & (~(exec_lo & 1u) & 1u));
                scc = m_out != 0u ? 1u : 0u;
                exec_lo = m_out;
            }
            if (!(exec_lo == 0u)) {
                if (exec_lo != 0u) {
                    v4 = 0x41300003u;
                }
                s0 = UiWord(ps_constants,32);
                s1 = UiWord(ps_constants,33);
                s2 = UiWord(ps_constants,34);
                s3 = UiWord(ps_constants,35);
                if (exec_lo != 0u) {
                    v2 = u(f(v4) * f(v2) + f(0xc1200003u));
                    v3 = s2;
                    v3 = u(f(s3) - f(v3));
                    v2 = u(f(v3) * f(v2) + f(s2));
                }
            }
        }
    }
    exec_lo = s10;
    if (exec_lo != 0u) {
        v0 = packHalf2x16(float2(f(v0), f(v1)));
        v1 = packHalf2x16(float2(f(v2), 1.0));
    }
    exec_lo = s8;
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(v0), unpackHalf2x16(v1));
    return;
}

float4 main(UIIn input):SV_Target0{ui_position=input.position;attr0=input.attr0;attr1=input.attr1;draw_index=input.draw_index;Program();return color0;}
