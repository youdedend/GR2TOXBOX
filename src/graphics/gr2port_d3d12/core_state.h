// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include "graphics/gr2port_d3d12/ui_state.h"
namespace Gr2Port::D3D12::CoreState {
struct Ref {
    u32 arena, offset;
};
constexpr Ref Unpack(u64 a) {
    return {Residency::Arena(a), Residency::Offset(a)};
}
struct WorldRoot {
    Ref draws, materials, layers;
    u32 base, pad = 0;
};
struct CharacterRoot {
    Ref draws, characters, decals, light;
    float inverse[2];
    Ref lights, grid;
    u32 glow, base;
};
struct LambertRoot {
    Ref draws, characters;
    u32 base, pad = 0;
};
struct DeferredRoot {
    Ref light, lights, grid, textures;
};
struct DecalRoot {
    Ref decals, light;
    u32 pad[2]{};
};
struct CompositeRoot {
    Ref grade, meter;
    u32 picture, pad[3]{};
};
struct MeterRoot {
    Ref meter;
    u32 size[2], offset[2]{};
};
struct ExposureRoot {
    Ref grade, meter;
    float share, pad[3]{};
};
struct ToneRoot {
    Ref grade, meter;
};
// Deferred shader order; AO stores occluded fraction, so disabled AO is black.
constexpr std::array<u32, 7> DeferredImages(u32 depth, u32 albedo, u32 normal, u32 material,
                                            u32 black, u32 white) {
    return {depth, albedo, normal, material, black, black, white};
}
struct TextureRoot {
    u32 near_shadow, far_shadow, color, depth;
};
static_assert(sizeof(Ref) == 8 && Unpack(0x0000000400000010ull).arena == 4 &&
              Unpack(0x0000000400000010ull).offset == 16);
static_assert(sizeof(WorldRoot) == 32 && sizeof(CharacterRoot) == 64 &&
              sizeof(CompositeRoot) == 32);
static_assert(sizeof(LambertRoot) == 24 && sizeof(DeferredRoot) == 32 && sizeof(DecalRoot) == 24);
static_assert(sizeof(MeterRoot) == 24 && sizeof(ExposureRoot) == 32 && sizeof(ToneRoot) == 16 &&
              sizeof(TextureRoot) == 16);
static_assert(offsetof(CharacterRoot, base) == 60 && offsetof(WorldRoot, base) == 24);
static_assert(sizeof(Scene::WorldDecal) == 304 &&
              offsetof(Scene::WorldDecal, world_view_projection) == 64);
static_assert(sizeof(Scene::Sky) == 304 && offsetof(Scene::Sky, sun_color) == 192 &&
              offsetof(Scene::Sky, sun_direction) == 208);
static_assert(offsetof(Scene::Sky, cloud_color) == 224 &&
              offsetof(Scene::Sky, cloud_layers) == 240 && offsetof(Scene::Sky, cubes) == 292);
static_assert(sizeof(Scene::Character) == 656 && sizeof(Scene::Draw) == 224);
static_assert(offsetof(Scene::Draw, vertices) == 160 && offsetof(Scene::Draw, skin) == 184 &&
              offsetof(Scene::Draw, palette) == 192);
inline float ReverseZ(float z, float w) {
    return .5f * (w - z);
}
inline bool Header(const Scene::Header& h) {
    u64 total = 0;
    for (auto n : h.draw_commands)
        total += n;
    return h.draws <= Scene::g_room.draws && total <= Scene::g_room.draws &&
           h.materials <= Scene::g_room.materials && h.characters <= Scene::MaxCharacters &&
           h.layer_materials <= Scene::g_room.layer_materials &&
           h.world_decals <= Scene::MaxWorldDecals && h.casters <= Scene::g_room.casters &&
           !(h.cascades & ~((1u << Scene::Cascades) - 1u)) &&
           h.point_lights <= Scene::MaxPointLights && h.skin_jobs <= Scene::MaxSkinJobs &&
           h.soft_body_jobs <= Scene::MaxSoftBodyJobs;
}
struct Limits {
    u64 geometry = 256ull * 1024 * 1024;
    u64 scene = 5ull * 24 * 1024 * 1024;
    u64 deformers = 16ull * 1024 * 1024;
};
inline bool InArena(u64 ref, u32 arena, u64 bytes, u64 capacity) {
    const u64 offset = Residency::Offset(ref);
    return ref && Residency::Arena(ref) == arena && offset <= capacity &&
           bytes <= capacity - offset;
}
inline bool Streams(u64 vertices, u32 stride, u64 skin, u64 palette, u32 bones,
                    Limits limits = {}) {
    return stride &&
           (InArena(vertices, 0, stride, limits.geometry) ||
            InArena(vertices, 2, stride, limits.deformers)) &&
           (!bones || (InArena(skin, 0, 16, limits.geometry) &&
                       InArena(palette, 1, u64(bones) * 48, limits.scene)));
}
inline bool Ready(const Scene::Draw& d, const Scene::Command& cmd, u32 draws, u64 indices,
                  u32 characters, bool forward, Limits limits = {}) {
    return Streams(d.vertices, d.stride, d.skin, d.palette, d.bones, limits) &&
           cmd.instance_count == 1 && cmd.first_instance < draws && cmd.index_count &&
           cmd.first_index <= indices && cmd.index_count <= indices - cmd.first_index &&
           (!forward || (d.material && d.material <= characters));
}
struct ShadowRoot {
    Ref casters, shadows;
    u32 cascade, base;
};
static_assert(sizeof(ShadowRoot) == 24 && offsetof(ShadowRoot, base) == 20);
struct ShadowKey {
    u32 list;
    s32 constant;
    float slope;
    bool operator==(const ShadowKey&) const = default;
};
inline ShadowKey Bias(u32 list, float constant, float slope) {
    return {list,
            std::isfinite(constant) ? s32(std::round(std::clamp(constant, -16384.f, 16384.f))) : 0,
            std::isfinite(slope) ? std::clamp(slope / 16.f, -16.f, 16.f) : 0.f};
}
} // namespace Gr2Port::D3D12::CoreState
