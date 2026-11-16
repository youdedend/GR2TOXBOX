// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "graphics/gr2port/scene.h"
#include "graphics/gr2port/scene_ring.h"
#include "graphics/gr2port_d3d12/residency_state.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <d3d12.h>
#include <vector>

namespace Gr2Port::D3D12::UiState {
struct UiRoot {
    u32 draws_arena, draws_offset;
    float text_scale[2];
    u32 base_draw;
};
static_assert(sizeof(UiRoot) == 20 && offsetof(UiRoot, base_draw) == 16);
constexpr UiRoot Root(u64 draws, u32 base, float x = 1, float y = 1) {
    return {Residency::Arena(draws), Residency::Offset(draws), {x, y}, base};
}
static_assert(Root(0x0000000100001234ull, 17).draws_arena == 1);
static_assert(Root(0x0000000100001234ull, 17).draws_offset == 0x1234);
static_assert(sizeof(Scene::UiDraw) == 160 && offsetof(Scene::UiDraw, flags) == 152);
inline constexpr u32 VertexPrograms = static_cast<u32>(Scene::UiVertexProgram::Count);
inline constexpr u32 PixelPrograms = static_cast<u32>(Scene::UiPixelProgram::Count);
inline constexpr std::array<u32, VertexPrograms> Outputs{3, 1, 3, 3, 3, 3, 3, 3, 3};
inline constexpr std::array<u32, PixelPrograms> Inputs{1, 2, 1, 2, 1, 3, 3, 3, 2, 2, 2, 2, 1, 3, 3};
inline constexpr u32 StrokeVertex = static_cast<u32>(Scene::UiVertexProgram::Stroke);
inline constexpr u32 StrokePixel = static_cast<u32>(Scene::UiPixelProgram::Stroke);
constexpr bool Pairs(u32 v, u32 p) {
    return v < VertexPrograms && p < PixelPrograms && (Inputs[p] & ~Outputs[v]) == 0 &&
           ((v == StrokeVertex) == (p == StrokePixel));
}
enum class Geometry { None, Stroke, Text, TextFlat };
constexpr Geometry SelectGeometry(u32 v, bool narrow) {
    return v == StrokeVertex                ? Geometry::Stroke
           : !narrow || v >= VertexPrograms ? Geometry::None
           : Outputs[v] == 1                ? Geometry::TextFlat
                                            : Geometry::Text;
}
inline std::array<float, 2> TextScale(u32 sw, u32 sh, u32 tw, u32 th, bool narrow) {
    if (!narrow || !sw || !sh || !tw || !th)
        return {1, 1};
    const float aspect = float(sw) / sh, shape = float(tw) / th;
    if (std::abs(shape - aspect) > 0.01f)
        return {1, 1};
    return {std::min(1.f, (16.f / 9.f) / aspect), std::min(1.f, aspect / (16.f / 9.f))};
}
inline D3D12_SAMPLER_DESC Sampler(u32 bits) {
    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter =
        (bits & 2) ? D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW =
        (bits & 1) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxAnisotropy = 1;
    sampler.MaxLOD = FLT_MAX;
    return sampler;
}
D3D12_BLEND Factor(u32 value, bool alpha = false);
D3D12_BLEND_OP Operation(u32 value);
D3D12_RENDER_TARGET_BLEND_DESC Blend(u32 control);
constexpr u64 Key(u32 v, u32 p, u32 blend, bool narrow, bool strip) {
    return u64(blend) | (u64(v) << 32) | (u64(p) << 40) | (u64(narrow) << 48) | (u64(strip) << 49);
}
struct Rect {
    u32 x = 0, y = 0, width = 0, height = 0;
};
inline Rect Fit(u32 sw, u32 sh, u32 dw, u32 dh) {
    if (!sw || !sh || !dw || !dh)
        return {};
    const float scale = std::min(float(dw) / sw, float(dh) / sh);
    const auto w = std::clamp(u32(std::lround(scale * sw)), 1u, dw);
    const auto h = std::clamp(u32(std::lround(scale * sh)), 1u, dh);
    return {(dw - w) / 2, (dh - h) / 2, w, h};
}
struct Viewport {
    float x, y, width, height, y_sign;
};
inline bool View(const float (&v)[4], Viewport& out) {
    for (float value : v)
        if (!std::isfinite(value))
            return false;
    if (v[2] <= 0 || v[3] == 0 || v[2] > 32767 || std::abs(v[3]) > 32767 ||
        std::abs(v[0]) > 32767 || std::abs(v[1]) > 32767)
        return false;
    out = {v[0], std::min(v[1], v[1] + v[3]), v[2], std::abs(v[3]), v[3] < 0 ? 1.f : -1.f};
    return out.y >= -32768 && out.y + out.height <= 32767 && out.x + out.width <= 32767;
}
constexpr bool Range(u64 offset, u64 count, u64 stride, u64 bytes) {
    return offset <= bytes && stride && count <= (bytes - offset) / stride;
}
inline bool CommandRange(const Scene::UiRun& run, const Scene::Header& header, u64 bytes) {
    return run.first <= header.ui.draws && run.count <= header.ui.draws - run.first &&
           Range(Scene::UiCommandsOffset + u64(run.first) * sizeof(Scene::VertexCommand), run.count,
                 sizeof(Scene::VertexCommand), bytes);
}
inline bool Command(const Scene::VertexCommand& cmd, u32 draws) {
    return cmd.vertex_count && cmd.instance_count && cmd.first_instance < draws &&
           cmd.instance_count <= draws - cmd.first_instance &&
           cmd.first_vertex <= ~0u - (cmd.vertex_count - 1);
}
inline bool Header(const Scene::Header& h, u64 bytes) {
    return bytes >= sizeof(Scene::Header) && h.ui.targets <= Scene::MaxUiTargets &&
           h.ui.runs <= Scene::MaxUiRuns && h.ui.draws <= Scene::MaxUiDraws &&
           h.ui.dynamic <= Scene::MaxUiDynamicTextures &&
           Range(Scene::UiTargetsOffset, h.ui.targets, sizeof(Scene::UiTarget), bytes) &&
           Range(Scene::UiRunsOffset, h.ui.runs, sizeof(Scene::UiRun), bytes) &&
           Range(Scene::UiDrawsOffset, h.ui.draws, sizeof(Scene::UiDraw), bytes) &&
           Range(Scene::UiCommandsOffset, h.ui.draws, sizeof(Scene::VertexCommand), bytes) &&
           Range(Scene::UiDynamicOffset, h.ui.dynamic, sizeof(u32), bytes);
}
// The production presenter and host tests use exactly the same retirement queue.
class Slots {
    struct Held {
        u32 slot;
        u64 fence;
    };
    std::vector<Held> held_;

  public:
    bool Hold(u32 slot, u64 fence) {
        if (slot >= SceneRing::Slots || !fence || held_.size() >= SceneRing::Slots)
            return false;
        for (const auto& h : held_)
            if (h.slot == slot)
                return false;
        held_.push_back({slot, fence});
        return true;
    }
    template <class F> void Collect(u64 completed, F&& release) {
        std::erase_if(held_, [&](const Held& h) {
            if (h.fence > completed)
                return false;
            release(h.slot);
            return true;
        });
    }
    template <class F> void DrainAfterIdle(F&& release) {
        for (const auto& h : held_)
            release(h.slot);
        held_.clear();
    }
    size_t Size() const { return held_.size(); }
};
} // namespace Gr2Port::D3D12::UiState
