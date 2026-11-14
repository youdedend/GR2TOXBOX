// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Vertex pulling over the arena: the HLSL counterpart of upstream's
// src/graphics/gr2port/shaders/vertex.glsl.
//
// Upstream pulls vertices by device address, so the vertex shader has no input
// layout:
//
//     Words Element(uint64_t vertices, uint stride, uint element)
//         { return Words(vertices + gl_VertexIndex * stride + (element & 0xff)); }
//
// Here the address is an ArenaRef and the stream base is an ArenaRef too, so the
// arithmetic is identical but stays inside one ByteAddressBuffer.
//
// An element holds a byte offset in its low 8 bits and, above them, an index
// into the game's vertex format table.

#ifndef GR2_COMMON_VERTEX_HLSLI
#define GR2_COMMON_VERTEX_HLSLI

#include "common/arena.hlsli"

// Format indices of the game's vertex format table (unchanged from vertex.glsl).
static const uint FormatRg32Float = 2u;
static const uint FormatRg16Float = 6u;
static const uint FormatRgba16Float = 7u;
static const uint FormatRg16Sint = 12u;
static const uint FormatRgba16Unorm = 10u;
static const uint FormatRgba8Unorm = 16u;

// The address of an element of the vertex being shaded. `vertex_index` replaces
// GLSL's gl_VertexIndex, and `element` carries the byte offset and format.
ArenaRef VertexElement(ArenaRef vertices, uint stride, uint element, uint vertex_index) {
    return ArenaAdd(vertices, vertex_index * stride + (element & 0xFFu));
}

float3 LoadPosition(ArenaRef vertices, uint stride, uint position, uint vertex_index,
                    ArenaTable table) {
    ArenaRef element = VertexElement(vertices, stride, position, vertex_index);
    uint4 words = ArenaLoad4(element, table);
    if ((position >> 8) == FormatRgba16Float) {
        return float3(UnpackHalf2x16(words.x), UnpackHalf2x16(words.y).x);
    }
    return asfloat(float3(words.x, words.y, words.z));
}

// Row r of a bone's 3x4 palette matrix, addressed inside the Skin arena.
//
// This lives here rather than in scene.hlsli, where the GLSL keeps its
// equivalent: vertex.hlsli is included *by* scene.hlsli, so a function defined
// there would be used before it was declared. HLSL, unlike GLSL, requires
// declaration before use and has no prototype syntax for functions -- so the
// only options are a circular include (which the include guards turn into a
// silent no-op) or putting the function in the lower-level header. A bone past
// the palette reads zeros, matching the game's own loads past its records.
float4 BoneRow(ArenaRef palette, uint bones, int bone, uint r, ArenaTable table) {
    if (uint(bone) >= bones) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    return ArenaLoadFloat4(ArenaAdd(palette, (uint(bone) * 3u + r) * 16u), table);
}

// A skinned position: the blend of four bones by their weights, in the order of
// operations of the game's vertex programs. The skin stream holds four RGBA16
// FLOAT weights, then four RGBA16 SINT bone indices.
float3 SkinPosition(float3 p, ArenaRef skin, ArenaRef palette, uint bones, uint vertex_index,
                    ArenaTable table) {
    uint4 words = ArenaLoad4(ArenaAdd(skin, vertex_index * 16u), table);
    float2 w01 = UnpackHalf2x16(words.x);
    float2 w23 = UnpackHalf2x16(words.y);
    // RGBA16 SINT: four signed 16-bit indices, low half first.
    int b0 = asint(words.z << 16) >> 16;
    int b1 = asint(words.z) >> 16;
    int b2 = asint(words.w << 16) >> 16;
    int b3 = asint(words.w) >> 16;

    float3 skinned;
    // The palette is three float4 rows per bone (see BoneRow above).
    [unroll] for (uint r = 0u; r < 3u; ++r) {
        float4 row = BoneRow(palette, bones, b0, r, table) * w01.x +
                     BoneRow(palette, bones, b1, r, table) * w01.y +
                     BoneRow(palette, bones, b2, r, table) * w23.x +
                     BoneRow(palette, bones, b3, r, table) * w23.y;
        skinned[r] = ((row.w + row.x * p.x) + row.y * p.y) + row.z * p.z;
    }
    return skinned;
}

// A skinned direction: as SkinPosition, without the translation. It keeps the
// bones' scale, which the game's vertex programs normalise away after the view
// matrix.
float3 SkinDirection(float3 d, ArenaRef skin, ArenaRef palette, uint bones, uint vertex_index,
                     ArenaTable table) {
    uint4 words = ArenaLoad4(ArenaAdd(skin, vertex_index * 16u), table);
    float2 w01 = UnpackHalf2x16(words.x);
    float2 w23 = UnpackHalf2x16(words.y);
    int b0 = asint(words.z << 16) >> 16;
    int b1 = asint(words.z) >> 16;
    int b2 = asint(words.w << 16) >> 16;
    int b3 = asint(words.w) >> 16;

    float3 turned;
    [unroll] for (uint r = 0u; r < 3u; ++r) {
        float4 row = BoneRow(palette, bones, b0, r, table) * w01.x +
                     BoneRow(palette, bones, b1, r, table) * w01.y +
                     BoneRow(palette, bones, b2, r, table) * w23.x +
                     BoneRow(palette, bones, b3, r, table) * w23.y;
        turned[r] = dot(row.xyz, d);
    }
    return turned;
}

float2 LoadTexcoord(ArenaRef vertices, uint stride, uint element, uint vertex_index,
                    ArenaTable table) {
    const uint format = element >> 8;
    if (format == 0u) {
        return float2(0.0, 0.0);
    }
    ArenaRef ref = VertexElement(vertices, stride, element, vertex_index);
    uint4 words = ArenaLoad4(ref, table);
    if (format == FormatRg16Sint) {
        // The game's vertex shaders scale 16-bit integer texture coordinates by
        // 1/1024.
        const uint word = words.x;
        return float2(float(int(word << 16) >> 16), float(int(word) >> 16)) / 1024.0;
    }
    if (format == FormatRg16Float) {
        return UnpackHalf2x16(words.x);
    }
    return asfloat(float2(words.x, words.y));
}

float3 LoadNormal(ArenaRef vertices, uint stride, uint element, uint vertex_index,
                  ArenaTable table) {
    uint word = ArenaLoad(VertexElement(vertices, stride, element, vertex_index), table);
    return UnpackNormal(word);
}

float4 LoadTangent(ArenaRef vertices, uint stride, uint element, uint vertex_index,
                   ArenaTable table) {
    uint4 words = ArenaLoad4(VertexElement(vertices, stride, element, vertex_index), table);
    return float4(UnpackHalf2x16(words.x), UnpackHalf2x16(words.y));
}

float4 LoadVertexColor(ArenaRef vertices, uint stride, uint element, uint vertex_index,
                       ArenaTable table) {
    uint4 words = ArenaLoad4(VertexElement(vertices, stride, element, vertex_index), table);
    const uint format = element >> 8;
    if (format == FormatRgba8Unorm) {
        return UnpackUnorm4x8(words.x);
    }
    if (format == FormatRgba16Unorm) {
        return float4(UnpackUnorm2x16(words.x), UnpackUnorm2x16(words.y));
    }
    return float4(UnpackHalf2x16(words.x), UnpackHalf2x16(words.y));
}

#endif // GR2_COMMON_VERTEX_HLSLI
