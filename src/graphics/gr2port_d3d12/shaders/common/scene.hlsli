// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The scene packet in HLSL: the counterpart of upstream's
// src/graphics/gr2port/shaders/scene.glsl.
//
// Layouts are kept byte-for-byte compatible with the host structs in
// graphics/gr2port/scene.h and with the GLSL (std430 / buffer_reference_align),
// so a capture written for the Vulkan backend is equally valid here once the
// 64-bit pointers in it are rewritten as ArenaRef pairs.
//
// The two structural changes from GLSL, both forced by D3D12/HLSL:
//   1. `layout(buffer_reference)` tables become ArenaRefs (see arena.hlsli).
//      This file's Draw/Material/Decal structs keep their fields; only the
//      pointer fields change type (uint64_t -> ArenaRef, 8 bytes either way).
//   2. gl_BaseInstance does not exist in HLSL. The draw index arrives as a
//      root constant (`base_instance`) so SV_InstanceID keeps its D3D meaning.

#ifndef GR2_COMMON_SCENE_HLSLI
#define GR2_COMMON_SCENE_HLSLI

#include "common/arena.hlsli"
#include "common/vertex.hlsli"

// The push-constant block upstream passes as `Draws scene_draws;` and
// `Materials scene_materials;`. Here it is a root-constant view: the draw table
// is an ArenaRef into the Scene arena, and the materials are an ArenaRef into
// the same arena.
//
// Kept in one struct so both are set with a single root-constants write.
struct SceneConstants {
    // The draw table (Draws in scene.glsl).
    ArenaRef draws;
    // The material table (Materials in scene.glsl): Material[count].
    ArenaRef materials;
    // GLSL's gl_BaseInstance. Passed as a root constant on direct draws, and
    // inline in the command signature for ExecuteIndirect multi-draws.
    uint base_instance;
    // Padding keeps the root constant block 16-byte aligned for the vertex
    // stages that read it.
    uint padding0;
    uint padding1;
    uint padding2;
};

// One draw record. Field-for-field with scene.glsl's Draw, with the three
// 64-bit GPU pointers replaced by ArenaRef pairs.
struct Draw {
    float4x4 world_view;
    float4x4 world_view_projection;
    float4 color_scale;
    float4 texcoord_transform;
    ArenaRef vertices; // was uint64_t vertices
    uint stride;
    uint position;
    uint texcoord;
    uint texture;
    ArenaRef skin;    // was uint64_t skin
    ArenaRef palette; // was uint64_t palette
    uint bones;
    uint decal;
    uint normal;
    uint tangent;
    uint vertex_color;
    uint material;
};

// The material the world shaders read for gradation and flags. Field order
// matches scene.glsl's Material; only the declaration style changes.
struct Material {
    float4 color_scale;
    float4 layered_color_scale;
    float4 color_transform;
    float4 layered_color_transform;
    float4 normal_transform;
    float4 layered_normal_transform;
    float4 mask_transform;
    float punch_through;
    float roughness;
    float metallic;
    float translucency;
    float emissive;
    float parallax;
    float layered_roughness;
    float layered_metallic;
    float layered_translucency;
    float layered_emissive;
    float layered_parallax;
    float hatching_repeat;
    float4 gradation_low;
    float4 gradation_high;
    uint normal_map;
    uint layered_color;
    uint layered_normal;
    uint mask;
    uint hatching;
    uint flags;
    uint layer_material;
};

// Material flags used by the world shaders (scene.glsl).
static const uint MaterialGem = 1u << 0;
static const uint MaterialAlpha = 1u << 1;

// sizeof(Draw) in bytes. A macro because HLSL has no sizeof usable in the
// constant expression the arena arithmetic needs. Computed from the field list
// above:
//   2 * 64 (matrices) + 2 * 16 (color_scale, texcoord_transform)
// + 8 (vertices) + 4 * 4 (stride..texture)
// + 8 + 8 (skin, palette) + 6 * 4 (bones..material)
// = 128 + 32 + 8 + 16 + 16 + 24 = 224 bytes, a multiple of 16.
#define DRAW_BYTES 224u

// Loads a Draw out of the arena. Written field by field rather than as one
// struct load, because ByteAddressBuffer only loads scalars and vectors.
Draw LoadDrawAt(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    Draw draw;
    draw.world_view = ArenaLoadMatrix(ref, table);
    draw.world_view_projection = ArenaLoadMatrix(ArenaAdd(ref, 64), table);
    draw.color_scale = asfloat(buffer.Load4(ref.offset + 128));
    draw.texcoord_transform = asfloat(buffer.Load4(ref.offset + 144));
    draw.vertices = ArenaMake(ARENA_GEOMETRY, buffer.Load(ref.offset + 160));
    draw.stride = buffer.Load(ref.offset + 168);
    draw.position = buffer.Load(ref.offset + 172);
    draw.texcoord = buffer.Load(ref.offset + 176);
    draw.texture = buffer.Load(ref.offset + 180);
    draw.skin = ArenaMake(ARENA_SKINNING, buffer.Load(ref.offset + 184));
    draw.palette = ArenaMake(ARENA_SKINNING, buffer.Load(ref.offset + 192));
    draw.bones = buffer.Load(ref.offset + 200);
    draw.decal = buffer.Load(ref.offset + 204);
    draw.normal = buffer.Load(ref.offset + 208);
    draw.tangent = buffer.Load(ref.offset + 212);
    draw.vertex_color = buffer.Load(ref.offset + 216);
    draw.material = buffer.Load(ref.offset + 220);
    return draw;
}

// The draw this shader instance is shading. `base_instance` is the D3D
// replacement for gl_BaseInstance.
Draw LoadDraw(SceneConstants constants, uint base_instance, ArenaTable table) {
    return LoadDrawAt(ArenaAdd(constants.draws, base_instance * DRAW_BYTES), table);
}

// Row r of a bone, over the arena. A bone past the palette reads zeros, as the
// game's buffer loads past its records do.
float4 BoneRow(ArenaRef palette, uint bones, int bone, uint r, ArenaTable table) {
    if (uint(bone) >= bones) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    return ArenaLoadFloat4(ArenaAdd(palette, (uint(bone) * 3u + r) * 16u), table);
}

// sizeof(Material): 112 bytes of transform rows + 48 of scalars
// (punch_through..hatching_repeat) + 32 of gradation + 28 of texture indices
// = 220, rounded up to the 16-byte buffer_reference_align the GLSL declares.
#define MATERIAL_BYTES 224u

// Loads a Material from the material table.
Material LoadMaterial(SceneConstants constants, uint index, ArenaTable table) {
    ArenaRef ref = ArenaAdd(constants.materials, index * MATERIAL_BYTES);
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    Material material;
    material.color_scale = asfloat(buffer.Load4(ref.offset + 0));
    material.layered_color_scale = asfloat(buffer.Load4(ref.offset + 16));
    material.color_transform = asfloat(buffer.Load4(ref.offset + 32));
    material.layered_color_transform = asfloat(buffer.Load4(ref.offset + 48));
    material.normal_transform = asfloat(buffer.Load4(ref.offset + 64));
    material.layered_normal_transform = asfloat(buffer.Load4(ref.offset + 80));
    material.mask_transform = asfloat(buffer.Load4(ref.offset + 96));
    material.punch_through = asfloat(buffer.Load(ref.offset + 112));
    material.roughness = asfloat(buffer.Load(ref.offset + 116));
    material.metallic = asfloat(buffer.Load(ref.offset + 120));
    material.translucency = asfloat(buffer.Load(ref.offset + 124));
    material.emissive = asfloat(buffer.Load(ref.offset + 128));
    material.parallax = asfloat(buffer.Load(ref.offset + 132));
    material.layered_roughness = asfloat(buffer.Load(ref.offset + 136));
    material.layered_metallic = asfloat(buffer.Load(ref.offset + 140));
    material.layered_translucency = asfloat(buffer.Load(ref.offset + 144));
    material.layered_emissive = asfloat(buffer.Load(ref.offset + 148));
    material.layered_parallax = asfloat(buffer.Load(ref.offset + 152));
    material.hatching_repeat = asfloat(buffer.Load(ref.offset + 156));
    material.gradation_low = asfloat(buffer.Load4(ref.offset + 160));
    material.gradation_high = asfloat(buffer.Load4(ref.offset + 176));
    material.normal_map = buffer.Load(ref.offset + 192);
    material.layered_color = buffer.Load(ref.offset + 196);
    material.layered_normal = buffer.Load(ref.offset + 200);
    material.mask = buffer.Load(ref.offset + 204);
    material.hatching = buffer.Load(ref.offset + 208);
    material.flags = buffer.Load(ref.offset + 212);
    material.layer_material = buffer.Load(ref.offset + 216);
    return material;
}

// A decal in the deferred decal pass (scene.glsl's Decal).
struct Decal {
    float4 positions[3];
    float4 targets[3];
    float2 scales[3];
    uint texture;
    uint color_alpha;
    float4 gradation;
};

#endif // GR2_COMMON_SCENE_HLSLI
