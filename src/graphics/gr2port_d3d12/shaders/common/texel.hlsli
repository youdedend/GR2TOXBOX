// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Texture sampling through the bindless heap: the HLSL counterpart of upstream's
// surface.glsl texture set.
//
// GLSL declares `uniform texture2D textures[]` and samples it as
// `texture(sampler2D(textures[nonuniformEXT(h)], texture_sampler), uv)`. D3D12
// splits that in two: the descriptor heap holds the SRVs, and the samplers live
// in a separate shader-visible heap. Both are reached through descriptor tables,
// because the console caps at Shader Model 6.4 -- see common/descriptors.hlsli
// for why each resource kind gets its own table and what a handle means:
//
//   Texture2D<float4> t = DescriptorTex2D(h);
//   SamplerState s = DescriptorSampler(SMP_LINEAR_REPEAT);
//
// Handles are the same 32-bit indices the host writes into draw records, so a
// handle of 0 is the null descriptor slot -- a defined black / clamp result
// rather than garbage, which is what the backend reserves slot 0 for.
//
// Upstream's `texture_sampler` is one sampler shared by every texture; here the
// indices above name the four the backend creates at startup: a null sampler, a
// repeating bilinear one, a clamped one, and a comparison sampler for the shadow
// maps.

#ifndef GR2_COMMON_TEXEL_HLSLI
#define GR2_COMMON_TEXEL_HLSLI

#include "descriptors.hlsli"

// Sampler heap indices. Must match Gr2Port::D3D12::BindlessHeap's startup
// configuration in descriptors.cpp.
#define SMP_NULL 0
#define SMP_LINEAR_REPEAT 1
#define SMP_POINT_CLAMP 2
#define SMP_LINEAR_CLAMP 3
#define SMP_COMPARISON 4

// A sampled colour at uv, through the repeating bilinear sampler that upstream's
// texture_sampler is.
float4 SampleTex(uint handle, float2 uv) {
    Texture2D<float4> texture = DescriptorTex2D(handle);
    SamplerState sampler = DescriptorSampler(SMP_LINEAR_REPEAT);
    return texture.SampleLevel(sampler, uv, 0.0);
}

// The same, with a sampler chosen by the caller (presentation uses clamping).
float4 SampleTexWith(uint handle, uint sampler_index, float2 uv) {
    Texture2D<float4> texture = DescriptorTex2D(handle);
    SamplerState sampler = DescriptorSampler(sampler_index);
    return texture.SampleLevel(sampler, uv, 0.0);
}

// An unfiltered texel at integer coordinates, as GLSL's texelFetch. The load is
// bounds-checked against the texture's own dimensions, which texelFetch requires
// the caller to have done; returning zero past the edge keeps a resolution change
// from reading a neighbouring resource out of the descriptor heap.
float4 FetchTexel(uint handle, int2 pixel, uint mip) {
    Texture2D<float4> texture = DescriptorTex2D(handle);
    uint width;
    uint height;
    // HLSL's Texture2D overload is (mip, out width, out height). The one that
    // takes a mip count as its first argument does not exist, and dxc rejects it
    // -- which is how this was found.
    texture.GetDimensions(width, height);
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= int(width) || pixel.y >= int(height)) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    return texture.Load(int3(pixel, mip));
}

// Sample with an explicit gradient, for the parallax march: GLSL's textureGrad
// with dFdx/dFdy, which keeps the normal map's mip selection equal to the
// surface's instead of to the marched (and therefore aliased) coordinates.
float4 SampleTexGrad(uint handle, float2 uv, float2 dx, float2 dy) {
    Texture2D<float4> texture = DescriptorTex2D(handle);
    SamplerState sampler = DescriptorSampler(SMP_LINEAR_REPEAT);
    return texture.SampleGrad(sampler, uv, dx, dy);
}

// A Texture3D, for the volumetric fog: the same bindless heap and the same
// per-handle indexing, with an explicit mip.
float4 SampleVolumeLevel(uint handle, float3 uv, float level) {
    Texture3D<float4> volume = DescriptorVolume(handle);
    SamplerState sampler = DescriptorSampler(SMP_LINEAR_REPEAT);
    return volume.SampleLevel(sampler, uv, level);
}

// The mip-0 dimensions of a 2D texture, as a uint2. GLSL's textureSize(s, 0),
// which the fog and decal shaders use to convert a pixel coordinate into a uv.
// The top level is the default overload's level, so the mip is not passed.
uint2 FetchTexelSize(uint handle) {
    Texture2D<float4> texture = DescriptorTex2D(handle);
    uint width;
    uint height;
    texture.GetDimensions(width, height);
    return uint2(width, height);
}

// surface.glsl's `Transformed`: a uv transform of scale and offset.
float2 Transformed(float4 transform, float2 uv) {
    return transform.xy + transform.zw * uv;
}

#endif // GR2_COMMON_TEXEL_HLSLI
