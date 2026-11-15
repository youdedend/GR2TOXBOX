// SPDX-License-Identifier: GPL-2.0-or-later
// Port of antialias_off.comp. Output is a linear_value RGBA8 copy buffer (not an sRGB
// UAV, which would double-encode). Host copies rows to the picture texture.
#include "common/texel.hlsli"
cbuffer AntialiasConstants : register(b1) {
    uint g_width;
    uint g_height;
    uint g_row_pitch;
    uint g_output_offset;
};
cbuffer AntialiasTextures : register(b2) {
    uint g_toned;
    uint g_picture_raw;
    uint g_aa_unused0;
    uint g_aa_unused1;
};
float Encode(float x) {
    const float curve = asfloat(0x3f870a3du) * exp2(asfloat(0x3ed55555u) * log2(max(0.0, x))) + asfloat(0xbd6147aeu);
    return asfloat(0x3b4d2e1cu) > x ? asfloat(0x414eb852u) * x : curve;
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_width || id.y >= g_height) return;
    float4 color = FetchTexel(g_toned, int2(id.xy), 0);
    color = float4(Encode(color.r), Encode(color.g), Encode(color.b), color.a);
    const uint4 bytes = uint4(round(saturate(color) * 255.0));
    DescriptorRawRw(g_picture_raw).Store(g_output_offset + id.y * g_row_pitch + id.x * 4u,
        bytes.x | (bytes.y << 8u) | (bytes.z << 16u) | (bytes.w << 24u));
}
