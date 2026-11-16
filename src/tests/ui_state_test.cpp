// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics/gr2port_d3d12/ui_state.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace Gr2Port;
using namespace Gr2Port::D3D12;
int main() {
    using namespace UiState;
    for (u32 bits = 0; bits < 4; ++bits) {
        const auto sampler = Sampler(bits);
        assert(sampler.AddressU ==
               ((bits & 1) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP));
        assert(sampler.AddressU == sampler.AddressV && sampler.AddressV == sampler.AddressW);
        assert(sampler.Filter == ((bits & 2) ? D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR
                                             : D3D12_FILTER_MIN_MAG_MIP_LINEAR));
        assert(sampler.MaxLOD == FLT_MAX && sampler.MinLOD == 0);
    }
    assert(Sampler(1).Filter == D3D12_FILTER_MIN_MAG_MIP_LINEAR &&
           Sampler(1).AddressU == D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    const auto root = Root(Residency::Pack(3, 0x1234), 29, .75f, 1.f);
    assert(root.draws_arena == 3 && root.draws_offset == 0x1234 && root.base_draw == 29 &&
           root.text_scale[0] == .75f);
    assert(offsetof(UiRoot, draws_arena) == 0 && offsetof(UiRoot, draws_offset) == 4 &&
           sizeof(root) == 20);
    const auto blend = Blend((1u << 30) | (1u << 29) | 4u | (5u << 8) | (1u << 5) | (2u << 16) |
                             (9u << 24) | (4u << 21));
    assert(blend.BlendEnable && blend.SrcBlend == D3D12_BLEND_SRC_ALPHA &&
           blend.DestBlend == D3D12_BLEND_INV_SRC_ALPHA);
    assert(blend.BlendOp == D3D12_BLEND_OP_SUBTRACT &&
           blend.SrcBlendAlpha == D3D12_BLEND_SRC_ALPHA);
    assert(blend.DestBlendAlpha == D3D12_BLEND_INV_DEST_ALPHA &&
           blend.BlendOpAlpha == D3D12_BLEND_OP_REV_SUBTRACT);
    assert(!Blend(0).BlendEnable && Blend(0).RenderTargetWriteMask == 15);
    for (u32 n : {13, 19})
        assert(Factor(n) == D3D12_BLEND_BLEND_FACTOR);
    for (u32 n : {14, 20})
        assert(Factor(n, true) == D3D12_BLEND_INV_BLEND_FACTOR);
    assert(Factor(10, true) == D3D12_BLEND_ONE && Factor(31) == D3D12_BLEND_ZERO);
    assert(Operation(2) == D3D12_BLEND_OP_MIN && Operation(3) == D3D12_BLEND_OP_MAX);
    assert(Blend((1u << 30) | 2u | (8u << 8)).SrcBlendAlpha == D3D12_BLEND_SRC_ALPHA);
    constexpr std::array<u32, 15> expected{1, 2, 1, 2, 1, 3, 3, 3, 2, 2, 2, 2, 1, 3, 3};
    for (u32 v = 0; v < 9; ++v)
        for (u32 p = 0; p < 15; ++p) {
            bool pair = (v == 8) == (p == 14);
            if (v == 1 && (expected[p] & 2))
                pair = false;
            assert(Pairs(v, p) == pair);
        }
    assert(!Pairs(9, 0) && !Pairs(0, 15));
    assert(SelectGeometry(8, false) == Geometry::Stroke &&
           SelectGeometry(8, true) == Geometry::Stroke);
    assert(SelectGeometry(1, true) == Geometry::TextFlat &&
           SelectGeometry(2, true) == Geometry::Text);
    assert(SelectGeometry(1, false) == Geometry::None);
    assert(Key(1, 2, 3, false, false) != Key(1, 2, 3, false, true));
    assert(Key(1, 2, 3, false, false) != Key(1, 2, 3, true, false));
    auto scale = TextScale(1920, 1080, 1920, 1080, true);
    assert(scale[0] == 1 && scale[1] == 1);
    scale = TextScale(1024, 768, 1024, 768, true);
    assert(std::abs(scale[1] - .75f) < .0001f && scale[0] == 1);
    scale = TextScale(1024, 768, 512, 512, true);
    assert(scale[0] == 1 && scale[1] == 1);
    scale = TextScale(0, 0, 1024, 768, true);
    assert(scale[0] == 1 && scale[1] == 1);
    auto rect = Fit(1920, 1080, 1024, 768);
    assert(rect.x == 0 && rect.y == 96 && rect.width == 1024 && rect.height == 576);
    rect = Fit(4, 3, 1920, 1080);
    assert(rect.x == 240 && rect.y == 0 && rect.width == 1440 && rect.height == 1080);
    assert(Fit(0, 3, 1920, 1080).width == 0 && Fit(1, 99999, 1, 1).height == 1);
    Viewport view{};
    float negative[4]{0, 1080, 1920, -1080};
    assert(View(negative, view) && view.y == 0 && view.y_sign == 1);
    float positive[4]{0, 0, 1920, 1080};
    assert(View(positive, view) && view.y_sign == -1);
    for (float y : {-1.f, 0.f, 1.f}) { // Vulkan signed viewport vs D3D normalized viewport.
        assert(View(negative, view));
        assert(negative[1] + (y + 1) * negative[3] / 2 ==
               view.y + (1 - y * view.y_sign) * view.height / 2);
        assert(View(positive, view));
        assert(positive[1] + (y + 1) * positive[3] / 2 ==
               view.y + (1 - y * view.y_sign) * view.height / 2);
    }
    positive[2] = std::numeric_limits<float>::quiet_NaN();
    assert(!View(positive, view));
    Scene::Header h{};
    h.ui.targets = 2;
    h.ui.draws = 3;
    h.ui.runs = 1;
    assert(Header(h, Scene::PacketBytes()));
    assert(!Header(h, sizeof(h) - 1));
    h.ui.targets = 33;
    assert(!Header(h, Scene::PacketBytes()));
    h.ui.targets = 2;
    Scene::UiRun run{};
    run.first = 1;
    run.count = 2;
    assert(CommandRange(run, h, Scene::PacketBytes()));
    assert(!CommandRange(run, h, Scene::UiCommandsOffset + 3 * sizeof(Scene::VertexCommand) - 1));
    run.count = 3;
    assert(!CommandRange(run, h, Scene::PacketBytes()));
    run.first = ~0u;
    assert(!CommandRange(run, h, Scene::PacketBytes()));
    assert(!Range(~0ull, ~0ull, 16, 100) && !Range(0, 1, 0, 100));
    Scene::VertexCommand cmd{3, 2, 0, 1};
    assert(Command(cmd, 3));
    cmd.instance_count = 3;
    assert(!Command(cmd, 3));
    cmd.instance_count = 1;
    cmd.first_instance = ~0u;
    assert(!Command(cmd, 3));
    cmd.first_instance = 0;
    cmd.first_vertex = ~0u;
    assert(!Command(cmd, 3));
    cmd.first_vertex = 0;
    cmd.vertex_count = 0;
    assert(!Command(cmd, 3));
    Slots slots;
    std::vector<u32> released;
    auto release = [&](u32 slot) { released.push_back(slot); };
    assert(!slots.Hold(5, 1) && !slots.Hold(0, 0));
    assert(slots.Hold(0, 3) && slots.Hold(1, 5) && !slots.Hold(0, 4));
    slots.Collect(2, release);
    assert(released.empty());
    slots.Collect(3, release);
    assert(released == std::vector<u32>{0});
    assert(slots.Hold(0, 7));
    slots.Collect(5, release);
    assert(slots.Size() == 1 && released.back() == 1);
    slots.DrainAfterIdle(release);
    assert(slots.Size() == 0 && released.back() == 0);
    for (u32 i = 0; i < SceneRing::Slots; ++i)
        assert(slots.Hold(i, 10 + i));
    slots.Collect(99, release);
    assert(slots.Size() == 0);
    std::puts("UI production helpers passed");
}
