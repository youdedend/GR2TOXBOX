// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include "graphics/gr2port/scene.h"

namespace Gr2Port::D3D12::ShaderBatch {
// HLSL roots use uint2 + ArenaMake, not embedded ArenaRef structs: embedding a
// struct in a cbuffer would align each ref to a fresh 16-byte row.
struct Ref { std::uint32_t arena, offset; };
struct CharacterRoot {
    Ref draws, characters, decals, light;
    float target_inverse[2];
    Ref lights, grid;
    std::uint32_t glow_pass, base_instance;
};
struct GlassRoot { Ref draws, materials, light; std::uint32_t output, base_instance; };
struct DeferredRoot { Ref light, lights, grid, texture_record; };
struct EffectorRoot { Ref jobs, effectors, grid; };
struct AntialiasRoot { std::uint32_t width, height, row_pitch, output_offset; };
struct ShadowRoot { Ref casters, shadows; std::uint32_t cascade; };
// Batch 2 uses the same b0/b1/b2 root signature; all image outputs are
// RGBA32_FLOAT placed footprints in raw scratch UAVs (space108), never new tables.
struct OutputSize { std::uint32_t width, height; };
struct WorldRoot { Ref draws, materials, layers; std::uint32_t base_instance, padding; };
struct CharacterAuxRoot { Ref draws, characters; }; // prefix of CharacterRoot
struct ParticleHalfRoot { std::uint32_t composite; };
struct MotionTileRoot { OutputSize output; Ref constants; };
struct MotionNeighborRoot { OutputSize output; };
struct VelocityRoot { OutputSize output; Ref light; std::uint32_t upscaler_motion; };
struct OcclusionRoot {
    OutputSize output; Ref light;
    std::int32_t frame, scale;
    std::uint32_t occlusion, character_depth;
};
struct OcclusionDepthRoot { OutputSize output; std::int32_t level; };
struct ParticleActiveRoot { Ref owners, emitters, descriptions, steps, listing; std::uint32_t objects; };
struct UiTextRoot { Ref draws; float text_scale[2]; };
struct ScratchFootprint { std::uint32_t row_pitch; std::uint64_t bytes; };
// A zero footprint rejects zero/oversized dispatches before allocating or binding.
constexpr ScratchFootprint Rgba32Scratch(OutputSize size) {
    if (!size.width || !size.height || size.width > 16384 || size.height > 16384) return {};
    const auto pitch = (size.width * 16u + 255u) & ~255u;
    return {pitch, std::uint64_t(pitch) * size.height};
}
static_assert(sizeof(WorldRoot)==32 && offsetof(WorldRoot,base_instance)==24);
static_assert(sizeof(CharacterAuxRoot)==16 && offsetof(CharacterAuxRoot,characters)==8);
static_assert(sizeof(ParticleHalfRoot)==4);
static_assert(sizeof(MotionTileRoot)==16 && offsetof(MotionTileRoot,constants)==8);
static_assert(sizeof(MotionNeighborRoot)==8);
static_assert(sizeof(VelocityRoot)==20 && offsetof(VelocityRoot,upscaler_motion)==16);
static_assert(sizeof(OcclusionRoot)==32 && offsetof(OcclusionRoot,occlusion)==24);
static_assert(sizeof(OcclusionDepthRoot)==12 && offsetof(OcclusionDepthRoot,level)==8);
static_assert(sizeof(ParticleActiveRoot)==44 && offsetof(ParticleActiveRoot,objects)==40);
static_assert(sizeof(UiTextRoot)==16 && offsetof(UiTextRoot,text_scale)==8);
static_assert(sizeof(Scene::LayerMaterial)==352 && offsetof(Scene::LayerMaterial,color_maps)==320);
static_assert(sizeof(Scene::UiDraw)==160 && offsetof(Scene::UiDraw,flags)==152);
static_assert(offsetof(Scene::UiDraw,vs_constants)==8);
struct TextureHandles { std::uint32_t handle[4]; };
static_assert(sizeof(Ref) == 8);
static_assert(sizeof(CharacterRoot) == 64 && offsetof(CharacterRoot, base_instance) == 60);
static_assert(sizeof(GlassRoot) == 32 && offsetof(GlassRoot, output) == 24);
static_assert(sizeof(DeferredRoot) == 32);
static_assert(sizeof(EffectorRoot) == 24 && offsetof(EffectorRoot, grid) == 16);
static_assert(sizeof(AntialiasRoot) == 16);
static_assert(sizeof(ShadowRoot) == 20);
static_assert(sizeof(TextureHandles) == 16);
// Shader arena loaders use the original scene packet's std430 offsets.
static_assert(sizeof(Scene::Character) == 656);
static_assert(offsetof(Scene::Character, normal_map) == 632);
static_assert(sizeof(Scene::GlassMaterial) == 192);
static_assert(sizeof(Scene::Light) == 1936);
static_assert(offsetof(Scene::Light, radiance) == 736);
static_assert(sizeof(Scene::PointLight) == 48);
static_assert(sizeof(Scene::Decal) == 144);
}
