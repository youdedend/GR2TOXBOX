// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Unified Bindless Arena in HLSL: this header is the replacement for
// GLSL's GL_EXT_buffer_reference and for every 64-bit GPU pointer in the
// upstream shaders.
//
// In GLSL an upstream shader says
//
//     layout(buffer_reference) readonly buffer Words { uint words[]; };
//     Words w = Words(vertices + vertex_index * stride);
//     uint v = w.words[0];
//
// where `vertices` is a uint64_t device address stored in a draw record. HLSL
// cannot dereference that address. Here the same access is
//
//     ArenaRef w = ArenaAdd(vertices, VertexIndex * stride);
//     uint v = ArenaLoad(w /* bytes from w */);
//
// where `vertices` is now an `ArenaRef` (arena index + 32-bit byte offset).
// Because the guest's whole GPU memory budget is under 4 GiB (3,200 MiB guest
// memory + 1,200 MiB D3D12 heap), the offset never truncates.
//
// Every accessor in vertex.hlsli, scene.hlsli and skin.hlsli is expressed in
// terms of ArenaLoad*, so the ported shader bodies keep the upstream structure
// and remain reviewable side by side with the GLSL.

#ifndef GR2_COMMON_ARENA_HLSLI
#define GR2_COMMON_ARENA_HLSLI

#include "descriptors.hlsli"

// ---------------------------------------------------------------------------
// Arena kinds. Must match Gr2Port::D3D12::ArenaKind in arena.h.
// ---------------------------------------------------------------------------
#define ARENA_GEOMETRY 0
#define ARENA_SCENE 1
#define ARENA_SKINNING 2
#define ARENA_PARTICLES 3
#define ARENA_SCRATCH 4
#define ARENA_COUNT 5

// A GPU pointer: which arena, and the byte offset inside it.
// Two uints, so it packs into draw records exactly where the 64-bit pointer
// used to be (8 bytes either way).
struct ArenaRef {
    uint arena;
    uint offset;
};

// The arena table lives in the root signature's first 32-bit constants:
// b0 holds the buffer-table index of each arena, so a shader reaches an arena's
// buffer through DescriptorRaw() with no per-draw table binding.
//   b0.x = geometry, b0.y = scene, b0.z = skinning, b0.w = particles,
//   b1.x = scratch.
struct ArenaTable {
    uint geometry;
    uint scene;
    uint skinning;
    uint particles;
    uint scratch;
};

// Resolves a ref to its SRV index in the bindless heap.
uint ArenaIndex(ArenaRef ref, ArenaTable table) {
    // The arena index is a small, uniform value; the branch compiles to a
    // short constant-indexed select on GCN rather than a dependent load.
    [branch] switch (ref.arena) {
    case ARENA_GEOMETRY:
        return table.geometry;
    case ARENA_SCENE:
        return table.scene;
    case ARENA_SKINNING:
        return table.skinning;
    case ARENA_PARTICLES:
        return table.particles;
    case ARENA_SCRATCH:
        return table.scratch;
    default:
        return table.scratch;
    }
}

// A null ref is all zeroes, so a draw with no geometry reads zeroes instead of
// faulting. That mirrors upstream's behaviour of reading a zeroed buffer.
bool ArenaIsNull(ArenaRef ref) {
    return ref.arena == 0 && ref.offset == 0;
}

ArenaRef ArenaMake(uint arena, uint offset) {
    ArenaRef ref;
    ref.arena = arena;
    ref.offset = offset;
    return ref;
}

// Ref + byte offset.
ArenaRef ArenaAdd(ArenaRef ref, uint bytes) {
    ref.offset += bytes;
    return ref;
}

// Ref + element index * element size.
ArenaRef ArenaAt(ArenaRef ref, uint index, uint element_bytes) {
    ref.offset += index * element_bytes;
    return ref;
}

// ---------------------------------------------------------------------------
// Loads
//
// The arena's buffers are reached through the typed ByteAddressBuffer tables in
// common/descriptors.hlsli -- the SM 6.4 form of what SM 6.6 spells as direct
// heap indexing, and the D3D12 equivalent of Vulkan's runtimeDescriptorArray.
// NonUniformResourceIndex is required because the index is a per-draw value the
// compiler cannot prove is uniform.
// ---------------------------------------------------------------------------

ByteAddressBuffer ArenaBuffer(ArenaRef ref, ArenaTable table) {
    return DescriptorRaw(ArenaIndex(ref, table));
}

RWByteAddressBuffer ArenaBufferRw(ArenaRef ref, ArenaTable table) {
    // Writable arenas reserve an adjacent SRV/UAV descriptor pair on the host.
    return DescriptorRawRw(ArenaIndex(ref, table) + 1u);
}

uint ArenaLoad(ArenaRef ref, ArenaTable table) {
    return ArenaBuffer(ref, table).Load(ref.offset);
}

uint2 ArenaLoad2(ArenaRef ref, ArenaTable table) {
    return ArenaBuffer(ref, table).Load2(ref.offset);
}

uint3 ArenaLoad3(ArenaRef ref, ArenaTable table) {
    return ArenaBuffer(ref, table).Load3(ref.offset);
}

uint4 ArenaLoad4(ArenaRef ref, ArenaTable table) {
    return ArenaBuffer(ref, table).Load4(ref.offset);
}

float ArenaLoadFloat(ArenaRef ref, ArenaTable table) {
    return asfloat(ArenaBuffer(ref, table).Load(ref.offset));
}

float2 ArenaLoadFloat2(ArenaRef ref, ArenaTable table) {
    return asfloat(ArenaBuffer(ref, table).Load2(ref.offset));
}

float3 ArenaLoadFloat3(ArenaRef ref, ArenaTable table) {
    return asfloat(ArenaBuffer(ref, table).Load3(ref.offset));
}

float4 ArenaLoadFloat4(ArenaRef ref, ArenaTable table) {
    return asfloat(ArenaBuffer(ref, table).Load4(ref.offset));
}

// A 4x4 matrix stored column-major in the arena (the layout the game's own
// structs use, and what upstream's `mat4` push constants hold).
float4x4 ArenaLoadMatrix(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    float4 c0 = asfloat(buffer.Load4(ref.offset + 0));
    float4 c1 = asfloat(buffer.Load4(ref.offset + 16));
    float4 c2 = asfloat(buffer.Load4(ref.offset + 32));
    float4 c3 = asfloat(buffer.Load4(ref.offset + 48));
    // GLSL mat4(columns) versus HLSL matrix constructor(rows).
    return transpose(float4x4(c0, c1, c2, c3));
}

// ---------------------------------------------------------------------------
// Stores (only legal against a UAV-backed arena: skinning, particles, scratch)
// ---------------------------------------------------------------------------

void ArenaStore(ArenaRef ref, ArenaTable table, uint value) {
    ArenaBufferRw(ref, table).Store(ref.offset, value);
}

void ArenaStore4(ArenaRef ref, ArenaTable table, uint4 value) {
    ArenaBufferRw(ref, table).Store4(ref.offset, value);
}

// ---------------------------------------------------------------------------
// Half / normal unpacking helpers, matching the GLSL in scene.glsl.
// ---------------------------------------------------------------------------

// unpackHalf2x16: the low half is .x, the high half is .y. f16tof32 takes the
// half in the low 16 bits.
float2 UnpackHalf2x16(uint word) {
    return float2(f16tof32(word & 0xFFFFu), f16tof32(word >> 16));
}

// unpackUnorm4x8: one byte per component, low byte first.
float4 UnpackUnorm4x8(uint word) {
    return float4(float(word & 0xFFu), float((word >> 8) & 0xFFu), float((word >> 16) & 0xFFu),
                  float((word >> 24) & 0xFFu)) /
           255.0;
}

// unpackUnorm2x16: two 16-bit unsigned normalized values.
float2 UnpackUnorm2x16(uint word) {
    return float2(float(word & 0xFFFFu), float(word >> 16)) / 65535.0;
}

// The game's packed normal (scene.glsl UnpackNormal): three unsigned components
// in the exponent and upper mantissa bits of a half float, 11, 11 and 10 bits,
// holding n * 0.5 + 0.5.
float3 UnpackNormal(uint word) {
    float3 halves;
    halves.x = f16tof32((word << 4) & 0x7ff0u);
    halves.y = f16tof32((word >> 7) & 0x7ff0u);
    halves.z = f16tof32((word >> 17) & 0x7fe0u);
    return halves * 2.0 - 1.0;
}

#endif // GR2_COMMON_ARENA_HLSLI
