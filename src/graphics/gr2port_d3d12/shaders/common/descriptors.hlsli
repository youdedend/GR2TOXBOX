// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The bindless descriptor tables, for Shader Model 6.4.
//
// SM 6.6 lets a shader index a descriptor heap directly:
//
//   Texture2D t = ResourceDescriptorHeap[h];        // 6.6 only
//   SamplerState s = SamplerDescriptorHeap[smp];    // 6.6 only
//
// Xbox in UWP stops at Shader Model 6.4 (SraKmd_arden), and a 6.6 module does
// not load there at all -- so every stage here would have failed to create on
// the console, not merely to bind. The 6.4 equivalent reaches the same heap
// through a descriptor table bound in the root signature, with one typed array
// per resource kind.
//
// Why a table per kind rather than one table of untyped descriptors: a table's
// elements all share the register and space, and their type comes from the
// declaration. One array cannot hold a Texture2D and a TextureCube at the same
// slot, because the shader would have to guess which it was reading. The heap
// keeps one slot per resource as before; the host mirrors each slot into the
// table that matches its kind, so a handle is still a plain u32 index and stays
// what the host writes into a draw record.
//
// NonUniformResourceIndex is still required and still legal at 6.4: the index is
// a per-draw value the compiler cannot prove uniform.
//
// A handle of 0 is the null descriptor slot, as it was for the direct-heap form:
// every table's element 0 is a null view, so a zero handle yields a defined
// black / clamp result rather than garbage.

#ifndef GR2_COMMON_DESCRIPTORS_HLSLI
#define GR2_COMMON_DESCRIPTORS_HLSLI

// Register spaces, one per descriptor kind. Must match
// Gr2Port::D3D12::DescriptorTables in descriptors.cpp, which builds the tables in
// this order and binds them to the same spaces.
#define SPACE_TEX2D_F4 100
#define SPACE_TEX3D_F4 101
#define SPACE_CUBE_F4 102
#define SPACE_TEX2D_F1 103
#define SPACE_RW_TEX3D_F4 104
#define SPACE_RW_TEX3D_F2 105
#define SPACE_RW_TEX3D_F1 106
#define SPACE_RAW 107
#define SPACE_RAW_RW 108
#define SPACE_SAMPLER 109
#define SPACE_SAMPLER_CMP 110

// Every SRV/UAV table spans the whole slot space, because every one of them is
// bound to the same descriptor range -- the heap base -- and a handle is an index
// into it. The shader's declared element count is what bounds an index, so it has
// to be the size of the space, not the number of textures a frame happens to use:
// a table declared at 4096 would silently clamp a handle of 5000 to the black
// null descriptor. The backend allocates from a 65,536-slot heap (BindlessHeap's
// SrvCapacity), so that is the number.
#define GR2_TABLE_SRVS 65536
#define GR2_TABLE_SAMPLERS 256

Texture2D<float4> g_tex2d[GR2_TABLE_SRVS] : register(t0, space100);
Texture3D<float4> g_volume[GR2_TABLE_SRVS] : register(t0, space101);
TextureCube<float4> g_cube[GR2_TABLE_SRVS] : register(t0, space102);
Texture2D<float> g_tex2d_f[GR2_TABLE_SRVS] : register(t0, space103);

RWTexture3D<float4> g_rw3d_f4[GR2_TABLE_SRVS] : register(u0, space104);
RWTexture3D<float2> g_rw3d_f2[GR2_TABLE_SRVS] : register(u0, space105);
RWTexture3D<float> g_rw3d_f1[GR2_TABLE_SRVS] : register(u0, space106);

ByteAddressBuffer g_raw[GR2_TABLE_SRVS] : register(t0, space107);
RWByteAddressBuffer g_raw_rw[GR2_TABLE_SRVS] : register(u0, space108);

SamplerState g_smp[GR2_TABLE_SAMPLERS] : register(s0, space109);
SamplerComparisonState g_smp_cmp[GR2_TABLE_SAMPLERS] : register(s0, space110);

// The accessors. Each replaces one ResourceDescriptorHeap[...] or
// SamplerDescriptorHeap[...] expression, so the call sites in texel.hlsli and
// arena.hlsli do not change shape.

Texture2D<float4> DescriptorTex2D(uint h) {
    return g_tex2d[NonUniformResourceIndex(h)];
}

Texture3D<float4> DescriptorVolume(uint h) {
    return g_volume[NonUniformResourceIndex(h)];
}

TextureCube<float4> DescriptorCube(uint h) {
    return g_cube[NonUniformResourceIndex(h)];
}

Texture2D<float> DescriptorDepth(uint h) {
    return g_tex2d_f[NonUniformResourceIndex(h)];
}

RWTexture3D<float4> DescriptorRwVolumeF4(uint h) {
    return g_rw3d_f4[NonUniformResourceIndex(h)];
}

RWTexture3D<float2> DescriptorRwVolumeF2(uint h) {
    return g_rw3d_f2[NonUniformResourceIndex(h)];
}

RWTexture3D<float> DescriptorRwVolumeF1(uint h) {
    return g_rw3d_f1[NonUniformResourceIndex(h)];
}

ByteAddressBuffer DescriptorRaw(uint h) {
    return g_raw[NonUniformResourceIndex(h)];
}

RWByteAddressBuffer DescriptorRawRw(uint h) {
    return g_raw_rw[NonUniformResourceIndex(h)];
}

// Sampler indices are compile-time constants (SMP_LINEAR_REPEAT and friends), so
// these take a plain index: NonUniformResourceIndex on a literal is noise, and
// dxc warns on it.
SamplerState DescriptorSampler(uint i) {
    return g_smp[i];
}

SamplerComparisonState DescriptorComparisonSampler(uint i) {
    return g_smp_cmp[i];
}

#endif // GR2_COMMON_DESCRIPTORS_HLSLI
