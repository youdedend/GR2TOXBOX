// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GR2_BATCH2_SCENE_HLSLI
#define GR2_BATCH2_SCENE_HLSLI
#include "common/lighting_batch.hlsli"
#include "common/surface.hlsli"
struct Batch2LayerMaterial {

    float4 color_transform[4];
    float4 normal_transform[4];
    float4 color_scale[4];
    float normal_scale[4];
    float roughness[4];
    float metallic[4];
    float translucency[4];
    float emissive[4];
    float parallax[4];
    uint blend[3];
    float hatching_repeat_u;
    float hatching_repeat_v;
    float punch_through;
    float contour_mask;
    float layer_padding;
    uint color_map[4];
    uint normal_map[4];

};
Batch2LayerMaterial LoadBatch2LayerMaterial(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    Batch2LayerMaterial v;
    v.color_transform[0] = asfloat(buffer.Load4(ref.offset + 0u));
    v.color_transform[1] = asfloat(buffer.Load4(ref.offset + 16u));
    v.color_transform[2] = asfloat(buffer.Load4(ref.offset + 32u));
    v.color_transform[3] = asfloat(buffer.Load4(ref.offset + 48u));
    v.normal_transform[0] = asfloat(buffer.Load4(ref.offset + 64u));
    v.normal_transform[1] = asfloat(buffer.Load4(ref.offset + 80u));
    v.normal_transform[2] = asfloat(buffer.Load4(ref.offset + 96u));
    v.normal_transform[3] = asfloat(buffer.Load4(ref.offset + 112u));
    v.color_scale[0] = asfloat(buffer.Load4(ref.offset + 128u));
    v.color_scale[1] = asfloat(buffer.Load4(ref.offset + 144u));
    v.color_scale[2] = asfloat(buffer.Load4(ref.offset + 160u));
    v.color_scale[3] = asfloat(buffer.Load4(ref.offset + 176u));
    v.normal_scale[0] = asfloat(buffer.Load(ref.offset + 192u));
    v.normal_scale[1] = asfloat(buffer.Load(ref.offset + 196u));
    v.normal_scale[2] = asfloat(buffer.Load(ref.offset + 200u));
    v.normal_scale[3] = asfloat(buffer.Load(ref.offset + 204u));
    v.roughness[0] = asfloat(buffer.Load(ref.offset + 208u));
    v.roughness[1] = asfloat(buffer.Load(ref.offset + 212u));
    v.roughness[2] = asfloat(buffer.Load(ref.offset + 216u));
    v.roughness[3] = asfloat(buffer.Load(ref.offset + 220u));
    v.metallic[0] = asfloat(buffer.Load(ref.offset + 224u));
    v.metallic[1] = asfloat(buffer.Load(ref.offset + 228u));
    v.metallic[2] = asfloat(buffer.Load(ref.offset + 232u));
    v.metallic[3] = asfloat(buffer.Load(ref.offset + 236u));
    v.translucency[0] = asfloat(buffer.Load(ref.offset + 240u));
    v.translucency[1] = asfloat(buffer.Load(ref.offset + 244u));
    v.translucency[2] = asfloat(buffer.Load(ref.offset + 248u));
    v.translucency[3] = asfloat(buffer.Load(ref.offset + 252u));
    v.emissive[0] = asfloat(buffer.Load(ref.offset + 256u));
    v.emissive[1] = asfloat(buffer.Load(ref.offset + 260u));
    v.emissive[2] = asfloat(buffer.Load(ref.offset + 264u));
    v.emissive[3] = asfloat(buffer.Load(ref.offset + 268u));
    v.parallax[0] = asfloat(buffer.Load(ref.offset + 272u));
    v.parallax[1] = asfloat(buffer.Load(ref.offset + 276u));
    v.parallax[2] = asfloat(buffer.Load(ref.offset + 280u));
    v.parallax[3] = asfloat(buffer.Load(ref.offset + 284u));
    v.blend[0] = buffer.Load(ref.offset + 288u);
    v.blend[1] = buffer.Load(ref.offset + 292u);
    v.blend[2] = buffer.Load(ref.offset + 296u);
    v.hatching_repeat_u = asfloat(buffer.Load(ref.offset + 300u));
    v.hatching_repeat_v = asfloat(buffer.Load(ref.offset + 304u));
    v.punch_through = asfloat(buffer.Load(ref.offset + 308u));
    v.contour_mask = asfloat(buffer.Load(ref.offset + 312u));
    v.layer_padding = asfloat(buffer.Load(ref.offset + 316u));
    v.color_map[0] = buffer.Load(ref.offset + 320u);
    v.color_map[1] = buffer.Load(ref.offset + 324u);
    v.color_map[2] = buffer.Load(ref.offset + 328u);
    v.color_map[3] = buffer.Load(ref.offset + 332u);
    v.normal_map[0] = buffer.Load(ref.offset + 336u);
    v.normal_map[1] = buffer.Load(ref.offset + 340u);
    v.normal_map[2] = buffer.Load(ref.offset + 344u);
    v.normal_map[3] = buffer.Load(ref.offset + 348u);
    return v;
}
struct Batch2GemMaterial {

    float4 color_scale;
    float4 color;
    float2 projection;
    float2 offset;
    float2 scale;
    float normal_scale;
    float roughness;
    float metallic;
    float translucency;
    float emissive;
    float refraction;
    float punch_through;
    float constants_padding[59];
    uint color_map;
    uint color_maps_padding[3];
    uint normal_map;
    uint normal_maps_padding[3];

};
Batch2GemMaterial LoadBatch2GemMaterial(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    Batch2GemMaterial v;
    v.color_scale = asfloat(buffer.Load4(ref.offset + 0u));
    v.color = asfloat(buffer.Load4(ref.offset + 16u));
    v.projection = asfloat(buffer.Load2(ref.offset + 32u));
    v.offset = asfloat(buffer.Load2(ref.offset + 40u));
    v.scale = asfloat(buffer.Load2(ref.offset + 48u));
    v.normal_scale = asfloat(buffer.Load(ref.offset + 56u));
    v.roughness = asfloat(buffer.Load(ref.offset + 60u));
    v.metallic = asfloat(buffer.Load(ref.offset + 64u));
    v.translucency = asfloat(buffer.Load(ref.offset + 68u));
    v.emissive = asfloat(buffer.Load(ref.offset + 72u));
    v.refraction = asfloat(buffer.Load(ref.offset + 76u));
    v.punch_through = asfloat(buffer.Load(ref.offset + 80u));
    v.constants_padding[0] = asfloat(buffer.Load(ref.offset + 84u));
    v.constants_padding[1] = asfloat(buffer.Load(ref.offset + 88u));
    v.constants_padding[2] = asfloat(buffer.Load(ref.offset + 92u));
    v.constants_padding[3] = asfloat(buffer.Load(ref.offset + 96u));
    v.constants_padding[4] = asfloat(buffer.Load(ref.offset + 100u));
    v.constants_padding[5] = asfloat(buffer.Load(ref.offset + 104u));
    v.constants_padding[6] = asfloat(buffer.Load(ref.offset + 108u));
    v.constants_padding[7] = asfloat(buffer.Load(ref.offset + 112u));
    v.constants_padding[8] = asfloat(buffer.Load(ref.offset + 116u));
    v.constants_padding[9] = asfloat(buffer.Load(ref.offset + 120u));
    v.constants_padding[10] = asfloat(buffer.Load(ref.offset + 124u));
    v.constants_padding[11] = asfloat(buffer.Load(ref.offset + 128u));
    v.constants_padding[12] = asfloat(buffer.Load(ref.offset + 132u));
    v.constants_padding[13] = asfloat(buffer.Load(ref.offset + 136u));
    v.constants_padding[14] = asfloat(buffer.Load(ref.offset + 140u));
    v.constants_padding[15] = asfloat(buffer.Load(ref.offset + 144u));
    v.constants_padding[16] = asfloat(buffer.Load(ref.offset + 148u));
    v.constants_padding[17] = asfloat(buffer.Load(ref.offset + 152u));
    v.constants_padding[18] = asfloat(buffer.Load(ref.offset + 156u));
    v.constants_padding[19] = asfloat(buffer.Load(ref.offset + 160u));
    v.constants_padding[20] = asfloat(buffer.Load(ref.offset + 164u));
    v.constants_padding[21] = asfloat(buffer.Load(ref.offset + 168u));
    v.constants_padding[22] = asfloat(buffer.Load(ref.offset + 172u));
    v.constants_padding[23] = asfloat(buffer.Load(ref.offset + 176u));
    v.constants_padding[24] = asfloat(buffer.Load(ref.offset + 180u));
    v.constants_padding[25] = asfloat(buffer.Load(ref.offset + 184u));
    v.constants_padding[26] = asfloat(buffer.Load(ref.offset + 188u));
    v.constants_padding[27] = asfloat(buffer.Load(ref.offset + 192u));
    v.constants_padding[28] = asfloat(buffer.Load(ref.offset + 196u));
    v.constants_padding[29] = asfloat(buffer.Load(ref.offset + 200u));
    v.constants_padding[30] = asfloat(buffer.Load(ref.offset + 204u));
    v.constants_padding[31] = asfloat(buffer.Load(ref.offset + 208u));
    v.constants_padding[32] = asfloat(buffer.Load(ref.offset + 212u));
    v.constants_padding[33] = asfloat(buffer.Load(ref.offset + 216u));
    v.constants_padding[34] = asfloat(buffer.Load(ref.offset + 220u));
    v.constants_padding[35] = asfloat(buffer.Load(ref.offset + 224u));
    v.constants_padding[36] = asfloat(buffer.Load(ref.offset + 228u));
    v.constants_padding[37] = asfloat(buffer.Load(ref.offset + 232u));
    v.constants_padding[38] = asfloat(buffer.Load(ref.offset + 236u));
    v.constants_padding[39] = asfloat(buffer.Load(ref.offset + 240u));
    v.constants_padding[40] = asfloat(buffer.Load(ref.offset + 244u));
    v.constants_padding[41] = asfloat(buffer.Load(ref.offset + 248u));
    v.constants_padding[42] = asfloat(buffer.Load(ref.offset + 252u));
    v.constants_padding[43] = asfloat(buffer.Load(ref.offset + 256u));
    v.constants_padding[44] = asfloat(buffer.Load(ref.offset + 260u));
    v.constants_padding[45] = asfloat(buffer.Load(ref.offset + 264u));
    v.constants_padding[46] = asfloat(buffer.Load(ref.offset + 268u));
    v.constants_padding[47] = asfloat(buffer.Load(ref.offset + 272u));
    v.constants_padding[48] = asfloat(buffer.Load(ref.offset + 276u));
    v.constants_padding[49] = asfloat(buffer.Load(ref.offset + 280u));
    v.constants_padding[50] = asfloat(buffer.Load(ref.offset + 284u));
    v.constants_padding[51] = asfloat(buffer.Load(ref.offset + 288u));
    v.constants_padding[52] = asfloat(buffer.Load(ref.offset + 292u));
    v.constants_padding[53] = asfloat(buffer.Load(ref.offset + 296u));
    v.constants_padding[54] = asfloat(buffer.Load(ref.offset + 300u));
    v.constants_padding[55] = asfloat(buffer.Load(ref.offset + 304u));
    v.constants_padding[56] = asfloat(buffer.Load(ref.offset + 308u));
    v.constants_padding[57] = asfloat(buffer.Load(ref.offset + 312u));
    v.constants_padding[58] = asfloat(buffer.Load(ref.offset + 316u));
    v.color_map = buffer.Load(ref.offset + 320u);
    v.color_maps_padding[0] = buffer.Load(ref.offset + 324u);
    v.color_maps_padding[1] = buffer.Load(ref.offset + 328u);
    v.color_maps_padding[2] = buffer.Load(ref.offset + 332u);
    v.normal_map = buffer.Load(ref.offset + 336u);
    v.normal_maps_padding[0] = buffer.Load(ref.offset + 340u);
    v.normal_maps_padding[1] = buffer.Load(ref.offset + 344u);
    v.normal_maps_padding[2] = buffer.Load(ref.offset + 348u);
    return v;
}
int2 Batch2Size(uint handle, int mip) {
    uint w, h, levels;
    DescriptorTex2D(handle).GetDimensions(uint(mip), w, h, levels);
    return int2(w, h);
}
// Linear float4 scratch output in the existing space108 raw UAV table.
// Host allocates alignedRowPitch*height bytes and copies/converts at the consuming pass.
void Batch2Store(uint handle, uint2 size, int2 p, float4 v) {
    if(any(p< int2(0,0)) || any(uint2(p)>=size)) return;
    uint row_pitch = (size.x * 16u + 255u) & ~255u;
    DescriptorRawRw(handle).Store4(uint(p.y)*row_pitch+uint(p.x)*16u,asuint(v));
}
// Raw outputs may be rebound as SRVs after a UAV barrier, without a texture copy.
float4 Batch2FetchRaw(uint handle, uint2 size, int2 p) {
    p=clamp(p,int2(0,0),int2(size)-int2(1,1));
    uint row_pitch=(size.x*16u+255u)&~255u;
    return asfloat(DescriptorRaw(handle).Load4(uint(p.y)*row_pitch+uint(p.x)*16u));
}
float4 Batch2Fetch(uint handle, int2 p, int mip) {
    return DescriptorTex2D(handle).Load(int3(p, mip));
}
float4 Batch2SampleLevel(uint handle, float2 uv, float lod) {
    return DescriptorTex2D(handle).SampleLevel(DescriptorSampler(SMP_LINEAR_CLAMP), uv, lod);
}
float4 Batch2Sample(uint handle, float2 uv) {
    return DescriptorTex2D(handle).Sample(DescriptorSampler(SMP_LINEAR_REPEAT), uv);
}
#endif
