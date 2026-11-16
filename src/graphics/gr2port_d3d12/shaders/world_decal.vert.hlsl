// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// world_decal.vert -> world_decal VS_6_6.
//
// Ported from src/graphics/gr2port/shaders/world_decal.vert: the game's decal
// vertex program, one corner of the decal's box per vertex and one decal per
// instance.
//
// gl_InstanceIndex becomes SV_InstanceID and gl_VertexIndex SV_VertexID; the box
// tables are indexed as in GLSL; shared capture retains OpenGL clip matrices.

#include "common/scene.hlsli"

// One decal, field for field with scene.glsl's WorldDecal.
//
// Shared Scene::WorldDecal is 304 bytes; offsets match scene.h exactly.
#define WORLD_DECAL_BYTES 304u

struct WorldDecal {
    float4x4 world_view_projection;
    float4x4 view_to_decal;
    float4x4 world_view;
    float4 normal_transform;
    float4 color_transform;
    float4 parallax;
    float4 normal_scale;
    float4 color_scale;
    float roughness;
    float metallic;
    float translucency;
    uint color_map;
    uint normal_map;
};

WorldDecal LoadWorldDecalAt(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    WorldDecal decal;
    decal.world_view_projection = ArenaLoadMatrix(ArenaAdd(ref, 64u), table);
    decal.view_to_decal = ArenaLoadMatrix(ArenaAdd(ref, 128u), table);
    decal.world_view = ArenaLoadMatrix(ArenaAdd(ref, 0u), table);
    decal.normal_transform = asfloat(buffer.Load4(ref.offset + 240));
    decal.color_transform = asfloat(buffer.Load4(ref.offset + 224));
    decal.parallax = asfloat(buffer.Load4(ref.offset + 256));
    decal.normal_scale = asfloat(buffer.Load4(ref.offset + 208));
    decal.color_scale = asfloat(buffer.Load4(ref.offset + 192));
    decal.roughness = asfloat(buffer.Load(ref.offset + 272));
    decal.metallic = asfloat(buffer.Load(ref.offset + 276));
    decal.translucency = asfloat(buffer.Load(ref.offset + 280));
    decal.color_map = buffer.Load(ref.offset + 284);
    decal.normal_map = buffer.Load(ref.offset + 288);
    return decal;
}

cbuffer DecalConstants : register(b1) {
    // The decal table, an ArenaRef where the GLSL had a buffer_reference.
    uint2 g_decals_words;
    uint3 g_pad;
};
#define g_decals ArenaMake(g_decals_words.x, g_decals_words.y)

cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

struct VsOutput {
    float4 position : SV_Position;
    nointerpolation uint decal : TEXCOORD0;
};

// The game's decal box: x and y from -1 to 1, z from 0 to 1.
static const float3 BoxPoints[8] = {
    float3(-1.0, -1.0, 1.0), float3(1.0, -1.0, 1.0), float3(1.0, -1.0, 0.0),
    float3(-1.0, -1.0, 0.0), float3(-1.0, 1.0, 1.0),  float3(1.0, 1.0, 1.0),
    float3(1.0, 1.0, 0.0),   float3(-1.0, 1.0, 0.0),
};
static const uint BoxCorners[36] = {
    2u, 1u, 0u, 0u, 3u, 2u, 4u, 5u, 6u, 6u, 7u, 4u, 5u, 1u, 6u, 1u, 2u, 6u,
    4u, 7u, 0u, 7u, 3u, 0u, 7u, 6u, 3u, 6u, 2u, 3u, 5u, 4u, 1u, 4u, 0u, 1u,
};

VsOutput main(uint vertex_index : SV_VertexID, uint instance : SV_InstanceID) {
    VsOutput output;
    output.decal = instance;
    const WorldDecal decal = LoadWorldDecalAt(ArenaAdd(g_decals, instance * WORLD_DECAL_BYTES), g_arena);
    output.position =
        mul(decal.world_view_projection, float4(BoxPoints[BoxCorners[vertex_index]], 1.0));
    output.position.z = 0.5 * (output.position.w - output.position.z);
    return output;
}
