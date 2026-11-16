// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GR2_UI_PFX_HLSLI
#define GR2_UI_PFX_HLSLI
#include "common/arena.hlsli"
#include "common/ui_raster.hlsli"
cbuffer ArenaConstants:register(b0) {ArenaTable g_arena;};
cbuffer UiProgramRoot:register(b1) {uint2 draws_words;float2 text_scale;uint base_draw;};
#define draws_address ArenaMake(draws_words.x,draws_words.y)
// Scene packets use arithmetic-friendly addresses: offset low, arena high.
ArenaRef UiRef(uint lo,uint hi){return ArenaMake(hi,lo);}
uint UiWord(ArenaRef ref,uint at){return ArenaLoad(ArenaAdd(ref,at*4u),g_arena);}
float f(uint x){return asfloat(x);} uint u(float x){return asuint(x);}
float mul_legacy(float a,float b){return a==0.0||b==0.0?0.0:a*b;}
float max_legacy(float a,float b){return a>=b?a:b;}
float min_legacy(float a,float b){return a<b?a:b;}
float med3(float a,float b,float c){return max(min(a,b),min(max(a,b),c));}
uint cvt_i32(float x){return isnan(x)?0u:uint(int(clamp(x,-2147483648.0,2147483520.0)));}
uint cvt_u32(float x){return isnan(x)?0u:uint(clamp(x,0.0,4294967040.0));}
uint abs_diff(uint a,uint b){return a>b?a-b:b-a;} int sext24(uint a){return int(a<<8)>>8;}
uint bfe_u(uint a,uint o,uint w){w&=31u;return w==0?0u:(a>>(o&31u))&((1u<<w)-1u);}
uint bfe_i(uint a,uint o,uint w){w&=31u;return w==0?0u:uint(int(a<<(32u-(o&31u)-w))>>(32u-w));}
uint s_bfe_u(uint a,uint b){return bfe_u(a,b&31u,(b>>16)&127u);}
uint s_bfe_i(uint a,uint b){return bfe_i(a,b&31u,(b>>16)&127u);}
uint mul_hi_u(uint a,uint b){return uint((uint64_t(a)*uint64_t(b))>>32u);}
uint mul_hi_i(uint a,uint b){return uint((int64_t(int(a))*int64_t(int(b)))>>32u);}
uint packHalf2x16(float2 v){return f32tof16(v.x)|(f32tof16(v.y)<<16);}
float2 unpackHalf2x16(uint x){return float2(f16tof32(x&65535u),f16tof32(x>>16));}
float4 UiSample(uint t,uint s,float2 uv){Texture2D<float4> image=DescriptorTex2D(t);SamplerState sampler=g_smp[NonUniformResourceIndex(16u+s)];return image.Sample(sampler,uv);}
float4 UiSampleLod(uint t,uint s,float2 uv,float lod){Texture2D<float4> image=DescriptorTex2D(t);SamplerState sampler=g_smp[NonUniformResourceIndex(16u+s)];return image.SampleLevel(sampler,uv,lod);}
#endif
