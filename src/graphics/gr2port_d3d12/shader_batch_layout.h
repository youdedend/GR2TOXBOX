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
