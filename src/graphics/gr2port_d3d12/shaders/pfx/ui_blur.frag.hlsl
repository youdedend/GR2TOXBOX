// SPDX-License-Identifier: GPL-2.0-or-later
// Structure-preserving translation of ui_blur.frag; generated GCN register arithmetic retained.
#include "common/ui_pfx.hlsli"
static uint g_vertex,g_instance,g_primitive,draw_index;
static float4 ui_position,attr0,attr1,color0;
struct UIIn {float4 position:SV_Position;float4 attr0:TEXCOORD0;float4 attr1:TEXCOORD1;nointerpolation uint draw_index:TEXCOORD15;};

void Program() {
    uint v0 = 0u, v1 = 0u, v2 = 0u, v3 = 0u, v4 = 0u, v5 = 0u, v6 = 0u, v7 = 0u;
    uint v8 = 0u, v9 = 0u, v10 = 0u, v11 = 0u, v12 = 0u, v13 = 0u, v14 = 0u, v15 = 0u;
    uint v16 = 0u, v17 = 0u, v18 = 0u, v19 = 0u, v20 = 0u, v21 = 0u, v22 = 0u, v23 = 0u;
    uint v24 = 0u, v25 = 0u, v26 = 0u, v27 = 0u, v28 = 0u, v29 = 0u, v30 = 0u, v31 = 0u;
    uint v32 = 0u, v33 = 0u, v34 = 0u, v35 = 0u, v36 = 0u, v37 = 0u, v38 = 0u, v39 = 0u;
    uint v40 = 0u, v41 = 0u, v42 = 0u, v43 = 0u, v44 = 0u, v45 = 0u, v46 = 0u, v47 = 0u;
    uint v48 = 0u, v49 = 0u, v50 = 0u, v51 = 0u, v52 = 0u, v53 = 0u, v54 = 0u, v55 = 0u;
    uint v56 = 0u, v57 = 0u, v58 = 0u, v59 = 0u, v60 = 0u, v61 = 0u;
    uint s12 = 0u, s13 = 0u, s14 = 0u, s15 = 0u, s16 = 0u, s17 = 0u, s18 = 0u, s19 = 0u;
    uint s20 = 0u, s21 = 0u, s22 = 0u, s23 = 0u, s24 = 0u, s25 = 0u, s26 = 0u, s27 = 0u;
    uint s28 = 0u, s29 = 0u, s30 = 0u, s31 = 0u;
    uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 1u, scc = 0u, m0 = 0u;
    int pc = 0;
    ArenaRef draw_block = ArenaAdd(draws_address,draw_index*160u);
    uint texture0 = UiWord(draw_block,8);
    uint texture1 = UiWord(draw_block,9);
    uint sampler0 = UiWord(draw_block,10);
    uint sampler1 = UiWord(draw_block,11);
    ArenaRef ps_constants = UiRef(UiWord(draw_block,6),UiWord(draw_block,7));
    s28 = UiWord(ps_constants,16);
    s29 = UiWord(ps_constants,17);
    s30 = UiWord(ps_constants,18);
    s31 = UiWord(ps_constants,19);
    m0 = s16;
    s12 = UiWord(ps_constants,0);
    s13 = UiWord(ps_constants,1);
    s14 = UiWord(ps_constants,2);
    s15 = UiWord(ps_constants,3);
    s16 = UiWord(ps_constants,4);
    s17 = UiWord(ps_constants,5);
    s18 = UiWord(ps_constants,6);
    s19 = UiWord(ps_constants,7);
    s20 = UiWord(ps_constants,8);
    s21 = UiWord(ps_constants,9);
    s22 = UiWord(ps_constants,10);
    s23 = UiWord(ps_constants,11);
    s24 = UiWord(ps_constants,12);
    s25 = UiWord(ps_constants,13);
    s26 = UiWord(ps_constants,14);
    s27 = UiWord(ps_constants,15);
    vcc_lo = exec_lo;
    vcc_hi = 0u;
    exec_lo = exec_lo;
    scc = 1u;
    if (exec_lo != 0u) {
        v46 = u(attr0.x);
        v47 = u(attr0.y);
        v53 = s31;
        v52 = s30;
        v20 = u(f(v52) * f(0xc1700000u) + f(v46));
        v21 = u(f(v53) * f(0xc1700000u) + f(v47));
        v0 = u(f(s28) + f(v20));
        v1 = u(f(s29) + f(v21));
        v22 = u(f(v52) * f(0xc1600000u) + f(v46));
        v23 = u(f(v53) * f(0xc1600000u) + f(v47));
        v4 = u(f(v46) - f(s30));
        v5 = u(f(v47) - f(s31));
        v26 = u(f(v52) * f(0xc1500000u) + f(v46));
        v27 = u(f(v53) * f(0xc1500000u) + f(v47));
        v34 = u(-2.0 * f(s30) + f(v46));
        v35 = u(-2.0 * f(s31) + f(v47));
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v0), f(v1)));
            v12 = u(t_out.x);
            v13 = u(t_out.y);
            v14 = u(t_out.z);
            v15 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v46), f(v47)));
            v16 = u(t_out.x);
            v17 = u(t_out.y);
            v18 = u(t_out.z);
            v19 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v4), f(v5)));
            v8 = u(t_out.x);
            v9 = u(t_out.y);
            v10 = u(t_out.z);
            v11 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v34), f(v35)));
            v57 = u(t_out.x);
            v58 = u(t_out.y);
            v59 = u(t_out.z);
            v60 = u(t_out.w);
        }
        v0 = u(f(s28) + f(v22));
        v1 = u(f(s29) + f(v23));
        v30 = u(f(v52) * f(0xc1400000u) + f(v46));
        v31 = u(f(v53) * f(0xc1400000u) + f(v47));
        v36 = u(f(v52) * f(0xc1300000u) + f(v46));
        v37 = u(f(v53) * f(0xc1300000u) + f(v47));
        v32 = u(f(v52) * f(0xc0400000u) + f(v46));
        v33 = u(f(v53) * f(0xc0400000u) + f(v47));
        v38 = u(f(s28) + f(v36));
        v39 = u(f(s29) + f(v37));
        v50 = u(-4.0 * f(s30) + f(v46));
        v51 = u(-4.0 * f(s31) + f(v47));
        v40 = u(f(v52) * f(0xc1200000u) + f(v46));
        v41 = u(f(v53) * f(0xc1200000u) + f(v47));
        v44 = u(f(v52) * f(0xc1100000u) + f(v46));
        v48 = u(f(s28) + f(v40));
        v49 = u(f(s29) + f(v41));
        v28 = u(f(s28) + f(v34));
        v34 = u(f(s28) + f(v30));
        v29 = u(f(s29) + f(v35));
        v35 = u(f(s29) + f(v31));
        v24 = u(f(s28) + f(v4));
        v4 = u(f(s28) + f(v26));
        v25 = u(f(s29) + f(v5));
        v5 = u(f(s29) + f(v27));
        v12 = u(f(v12) + f(v16));
        v16 = u(f(v17) + f(v13));
        v14 = u(f(v18) + f(v14));
        v15 = u(f(v19) + f(v15));
        v45 = u(f(s12) * f(v12));
        v54 = u(f(s12) * f(v14));
        v55 = u(f(s12) * f(v15));
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v0), f(v1)));
            v0 = u(t_out.x);
            v1 = u(t_out.y);
            v2 = u(t_out.z);
            v3 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v4), f(v5)));
            v4 = u(t_out.x);
            v5 = u(t_out.y);
            v6 = u(t_out.z);
            v7 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v32), f(v33)));
            v12 = u(t_out.x);
            v13 = u(t_out.y);
            v14 = u(t_out.z);
            v15 = u(t_out.w);
        }
        v56 = u(f(s12) * f(v16));
        v0 = u(f(v0) + f(v8));
        v1 = u(f(v9) + f(v1));
        v16 = u(f(v58) + f(v5));
        v2 = u(f(v10) + f(v2));
        v3 = u(f(v11) + f(v3));
        v4 = u(f(v4) + f(v57));
        v45 = u(f(s13) * f(v0) + f(v45));
        v6 = u(f(v59) + f(v6));
        v7 = u(f(v60) + f(v7));
        v54 = u(f(s13) * f(v2) + f(v54));
        v55 = u(f(s13) * f(v3) + f(v55));
        v45 = u(f(s14) * f(v4) + f(v45));
        v54 = u(f(s14) * f(v6) + f(v54));
        v55 = u(f(s14) * f(v7) + f(v55));
        v56 = u(f(s13) * f(v1) + f(v56));
        v56 = u(f(s14) * f(v16) + f(v56));
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v34), f(v35)));
            v8 = u(t_out.x);
            v9 = u(t_out.y);
            v10 = u(t_out.z);
            v11 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v38), f(v39)));
            v0 = u(t_out.x);
            v1 = u(t_out.y);
            v2 = u(t_out.z);
            v3 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v50), f(v51)));
            v16 = u(t_out.x);
            v17 = u(t_out.y);
            v18 = u(t_out.z);
            v19 = u(t_out.w);
        }
        v38 = u(f(s28) + f(v50));
        v50 = u(f(s28) + f(v44));
        v39 = u(f(s29) + f(v51));
        v34 = u(f(s28) + f(v32));
        v32 = u(f(v52) * f(0xc0a00000u) + f(v46));
        v42 = u(f(s28) + f(v32));
        v35 = u(f(s29) + f(v33));
        v33 = u(f(v53) * f(0xc0a00000u) + f(v47));
        v43 = u(f(s29) + f(v33));
        v8 = u(f(v8) + f(v12));
        v7 = u(f(v13) + f(v9));
        v4 = u(f(v14) + f(v10));
        v11 = u(f(v15) + f(v11));
        v0 = u(f(v0) + f(v16));
        v16 = u(f(v17) + f(v1));
        v3 = u(f(v19) + f(v3));
        v2 = u(f(v18) + f(v2));
        v58 = u(f(s15) * f(v8) + f(v45));
        v45 = u(f(v53) * f(0xc1100000u) + f(v47));
        v51 = u(f(s29) + f(v45));
        v61 = u(f(s15) * f(v7) + f(v56));
        v60 = u(f(s15) * f(v4) + f(v54));
        v54 = u(f(v52) * f(0xc0c00000u) + f(v46));
        v59 = u(f(s15) * f(v11) + f(v55));
        v55 = u(f(v53) * f(0xc0c00000u) + f(v47));
        v58 = u(f(s16) * f(v0) + f(v58));
        v61 = u(f(s16) * f(v16) + f(v61));
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v48), f(v49)));
            v8 = u(t_out.x);
            v9 = u(t_out.y);
            v10 = u(t_out.z);
            v11 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v32), f(v33)));
            v12 = u(t_out.x);
            v13 = u(t_out.y);
            v14 = u(t_out.z);
            v15 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v50), f(v51)));
            v4 = u(t_out.x);
            v5 = u(t_out.y);
            v6 = u(t_out.z);
            v7 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v54), f(v55)));
            v16 = u(t_out.x);
            v17 = u(t_out.y);
            v18 = u(t_out.z);
            v19 = u(t_out.w);
        }
        v50 = u(f(v52) * f(0xc1000000u) + f(v46));
        v51 = u(f(v53) * f(0xc1000000u) + f(v47));
        v52 = u(f(v52) * f(0xc0e00000u) + f(v46));
        v53 = u(f(v53) * f(0xc0e00000u) + f(v47));
        v56 = u(f(s28) + f(v50));
        v57 = u(f(s29) + f(v51));
        v48 = u(f(s28) + f(v54));
        v49 = u(f(s29) + f(v55));
        v32 = u(f(s28) + f(v46));
        v46 = u(f(s28) + f(v52));
        v33 = u(f(s29) + f(v47));
        v47 = u(f(s29) + f(v53));
        v60 = u(f(s16) * f(v2) + f(v60));
        v59 = u(f(s16) * f(v3) + f(v59));
        v3 = u(f(v13) + f(v9));
        v0 = u(f(v14) + f(v10));
        v8 = u(f(v8) + f(v12));
        v11 = u(f(v15) + f(v11));
        v4 = u(f(v4) + f(v16));
        v16 = u(f(v17) + f(v5));
        v6 = u(f(v18) + f(v6));
        v7 = u(f(v19) + f(v7));
        v58 = u(f(s17) * f(v8) + f(v58));
        v61 = u(f(s17) * f(v3) + f(v61));
        v60 = u(f(s17) * f(v0) + f(v60));
        v59 = u(f(s17) * f(v11) + f(v59));
        v58 = u(f(s18) * f(v4) + f(v58));
        v61 = u(f(s18) * f(v16) + f(v61));
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v56), f(v57)));
            v54 = u(t_out.x);
            v55 = u(t_out.y);
            v56 = u(t_out.z);
            v57 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v52), f(v53)));
            v12 = u(t_out.x);
            v13 = u(t_out.y);
            v14 = u(t_out.z);
            v15 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v46), f(v47)));
            v16 = u(t_out.x);
            v17 = u(t_out.y);
            v18 = u(t_out.z);
            v19 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v50), f(v51)));
            v0 = u(t_out.x);
            v1 = u(t_out.y);
            v2 = u(t_out.z);
            v3 = u(t_out.w);
        }
        v60 = u(f(s18) * f(v6) + f(v60));
        v59 = u(f(s18) * f(v7) + f(v59));
        v8 = u(f(v54) + f(v12));
        v11 = u(f(v15) + f(v57));
        v0 = u(f(v16) + f(v0));
        v16 = u(f(v1) + f(v17));
        v3 = u(f(v3) + f(v19));
        v2 = u(f(v2) + f(v18));
        v7 = u(f(v13) + f(v55));
        v4 = u(f(v14) + f(v56));
        v58 = u(f(s19) * f(v8) + f(v58));
        v61 = u(f(s19) * f(v7) + f(v61));
        v60 = u(f(s19) * f(v4) + f(v60));
        v59 = u(f(s19) * f(v11) + f(v59));
        v58 = u(f(s20) * f(v0) + f(v58));
        v61 = u(f(s20) * f(v16) + f(v61));
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v48), f(v49)));
            v46 = u(t_out.x);
            v47 = u(t_out.y);
            v48 = u(t_out.z);
            v49 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v44), f(v45)));
            v50 = u(t_out.x);
            v51 = u(t_out.y);
            v52 = u(t_out.z);
            v53 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v40), f(v41)));
            v16 = u(t_out.x);
            v17 = u(t_out.y);
            v18 = u(t_out.z);
            v19 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v36), f(v37)));
            v12 = u(t_out.x);
            v13 = u(t_out.y);
            v14 = u(t_out.z);
            v15 = u(t_out.w);
        }
        v60 = u(f(s20) * f(v2) + f(v60));
        v59 = u(f(s20) * f(v3) + f(v59));
        v8 = u(f(v46) + f(v50));
        v11 = u(f(v53) + f(v49));
        v3 = u(f(v51) + f(v47));
        v0 = u(f(v52) + f(v48));
        v58 = u(f(s21) * f(v8) + f(v58));
        v61 = u(f(s21) * f(v3) + f(v61));
        v60 = u(f(s21) * f(v0) + f(v60));
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v42), f(v43)));
            v42 = u(t_out.x);
            v43 = u(t_out.y);
            v44 = u(t_out.z);
            v45 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v34), f(v35)));
            v0 = u(t_out.x);
            v1 = u(t_out.y);
            v2 = u(t_out.z);
            v3 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v22), f(v23)));
            v46 = u(t_out.x);
            v47 = u(t_out.y);
            v48 = u(t_out.z);
            v49 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v32), f(v33)));
            v50 = u(t_out.x);
            v51 = u(t_out.y);
            v52 = u(t_out.z);
            v53 = u(t_out.w);
        }
        v59 = u(f(s21) * f(v11) + f(v59));
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v30), f(v31)));
            v34 = u(t_out.x);
            v35 = u(t_out.y);
            v36 = u(t_out.z);
            v37 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v38), f(v39)));
            v8 = u(t_out.x);
            v9 = u(t_out.y);
            v10 = u(t_out.z);
            v11 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v20), f(v21)));
            v20 = u(t_out.x);
            v21 = u(t_out.y);
            v22 = u(t_out.z);
            v23 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v28), f(v29)));
            v28 = u(t_out.x);
            v29 = u(t_out.y);
            v30 = u(t_out.z);
            v31 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v26), f(v27)));
            v38 = u(t_out.x);
            v39 = u(t_out.y);
            v40 = u(t_out.z);
            v41 = u(t_out.w);
        }
        {
            float4 t_out = UiSample(texture0,sampler0, float2(f(v24), f(v25)));
            v24 = u(t_out.x);
            v25 = u(t_out.y);
            v26 = u(t_out.z);
            v27 = u(t_out.w);
        }
        v4 = u(f(v42) + f(v16));
        v16 = u(f(v17) + f(v43));
        v6 = u(f(v18) + f(v44));
        v7 = u(f(v19) + f(v45));
        v8 = u(f(v8) + f(v12));
        v11 = u(f(v15) + f(v11));
        v58 = u(f(s22) * f(v4) + f(v58));
        v4 = u(f(v14) + f(v10));
        v61 = u(f(s22) * f(v16) + f(v61));
        v60 = u(f(s22) * f(v6) + f(v60));
        v59 = u(f(s22) * f(v7) + f(v59));
        v7 = u(f(v13) + f(v9));
        v0 = u(f(v0) + f(v34));
        v16 = u(f(v35) + f(v1));
        v2 = u(f(v36) + f(v2));
        v3 = u(f(v37) + f(v3));
        v58 = u(f(s23) * f(v8) + f(v58));
        v61 = u(f(s23) * f(v7) + f(v61));
        v60 = u(f(s23) * f(v4) + f(v60));
        v59 = u(f(s23) * f(v11) + f(v59));
        v8 = u(f(v28) + f(v38));
        v11 = u(f(v41) + f(v31));
        v58 = u(f(s24) * f(v0) + f(v58));
        v0 = u(f(v40) + f(v30));
        v61 = u(f(s24) * f(v16) + f(v61));
        v60 = u(f(s24) * f(v2) + f(v60));
        v59 = u(f(s24) * f(v3) + f(v59));
        v3 = u(f(v39) + f(v29));
        v4 = u(f(v24) + f(v46));
        v12 = u(f(v47) + f(v25));
        v6 = u(f(v48) + f(v26));
        v7 = u(f(v49) + f(v27));
        v58 = u(f(s25) * f(v8) + f(v58));
        v61 = u(f(s25) * f(v3) + f(v61));
        v60 = u(f(s25) * f(v0) + f(v60));
        v59 = u(f(s25) * f(v11) + f(v59));
        v0 = u(f(v50) + f(v20));
        v1 = u(f(v21) + f(v51));
        v2 = u(f(v22) + f(v52));
        v3 = u(f(v23) + f(v53));
        v58 = u(f(s26) * f(v4) + f(v58));
        v61 = u(f(s26) * f(v12) + f(v61));
        v60 = u(f(s26) * f(v6) + f(v60));
        v59 = u(f(s26) * f(v7) + f(v59));
        v58 = u(f(s27) * f(v0) + f(v58));
        v61 = u(f(s27) * f(v1) + f(v61));
        v60 = u(f(s27) * f(v2) + f(v60));
        v59 = u(f(s27) * f(v3) + f(v59));
        v0 = packHalf2x16(float2(f(v58), f(v61)));
        v2 = packHalf2x16(float2(f(v60), f(v59)));
    }
    exec_lo = vcc_lo;
    if (exec_lo == 0u) discard;
    color0 = float4(unpackHalf2x16(v0), unpackHalf2x16(v2));
    return;
}

float4 main(UIIn input):SV_Target0{ui_position=input.position;attr0=input.attr0;attr1=input.attr1;draw_index=input.draw_index;Program();return color0;}
