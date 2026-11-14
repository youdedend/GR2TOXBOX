// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Texture sampling through the bindless heap: the HLSL counterpart of upstream's
// surface.glsl texture set.
//
// GLSL declares `uniform texture2D textures[]` and samples it as
// `texture(sampler2D(textures[nonuniformEXT(h)], texture_sampler), uv)`. D3D12
// splits that in two: the descriptor heap holds the SRVs, and the samplers live
// in a separate shader-visible heap. SM 6.6's direct heap indexing reaches both:
//
//   Texture2D t = ResourceDescriptorHeap[NonUniformResourceIndex(h)];
//   SamplerState s = SamplerDescriptorHeap[smp];
//
// Handles are the same 32-bit indices the host writes into draw records, so a
// handle of 0 is the null descriptor slot -- a defined black / clamp result
// rather than garbage, which is what the backend reserves slot 0 for.
//
// Upstream's `texture_sampler` is one sampler shared by every texture; here it is
// index 1 of the sampler heap (index 0 is the null sampler, indices 2-3 are the
// point and shadow-comparison samplers the backend creates at startup).

#ifndef GR2_COMMON_TEXEL_HLSLI
#define GR2_COMMON_TEXEL_HLSLI

// Sampler heap indices. Must match Gr2Port::D3D12::BindlessHeap's startup
// configuration in descriptors.cpp.
#define SMP_NULL 0
#define SMP_LINEAR_REPEAT 1
#define SMP_POINT_CLAMP 2
#define SMP_LINEAR_CLAMP 3

// A sampled colour at uv, through the repeating bilinear sampler that upstream's
// texture_sampler is.
float4 SampleTex(uint handle, float2 uv) {
    Texture2D texture = ResourceDescriptorHeap[NonUniformResourceIndex(handle)];
    SamplerState sampler = SamplerDescriptorHeap[SMP_LINEAR_REPEAT];
    return texture.SampleLevel(sampler, uv, 0.0);
}

// The same, with a sampler chosen by the caller (presentation uses clamping).
float4 SampleTexWith(uint handle, uint sampler_index, float2 uv) {
    Texture2D texture = ResourceDescriptorHeap[NonUniformResourceIndex(handle)];
    SamplerState sampler = SamplerDescriptorHeap[sampler_index];
    return texture.SampleLevel(sampler, uv, 0.0);
}

// An unfiltered texel at integer coordinates, as GLSL's texelFetch. The load is
// bounds-checked against the texture's own dimensions, which texelFetch requires
// the caller to have done; returning zero past the edge keeps a resolution change
// from reading a neighbouring resource out of the descriptor heap.
float4 FetchTexel(uint handle, int2 pixel, uint mip) {
    Texture2D texture = ResourceDescriptorHeap[NonUniformResourceIndex(handle)];
    uint width;
    uint height;
    texture.GetDimensions(mip, width, height);
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= int(width) || pixel.y >= int(height)) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    return texture.Load(int3(pixel, mip));
}

// Sample with an explicit gradient, for the parallax march: GLSL's textureGrad
// with dFdx/dFdy, which keeps the normal map's mip selection equal to the
// surface's instead of to the marched (and therefore aliased) coordinates.
float4 SampleTexGrad(uint handle, float2 uv, float2 dx, float2 dy) {
    Texture2D texture = ResourceDescriptorHeap[NonUniformResourceIndex(handle)];
    SamplerState sampler = SamplerDescriptorHeap[SMP_LINEAR_REPEAT];
    return texture.SampleGrad(sampler, uv, dx, dy);
}

// surface.glsl's `Transformed`: a uv transform of scale and offset.
float2 Transformed(float4 transform, float2 uv) {
    return transform.xy + transform.zw * uv;
}

#endif // GR2_COMMON_TEXEL_HLSLI
