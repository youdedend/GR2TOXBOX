// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include "graphics/gr2port_d3d12/core_state.h"
using namespace Gr2Port;
using namespace Gr2Port::D3D12;
int main() {
    using namespace CoreState;
    for (float w : {.125f, 1.f, 1000.f}) {
        assert(ReverseZ(-w, w) == w && ReverseZ(w, w) == 0 && ReverseZ(0, w) == w * .5f);
        for (float z : {-1.f, -.5f, 0.f, .5f, 1.f}) {
            const float actual = ReverseZ(z * w, w) / w;
            assert(actual >= 0 && actual <= 1 && std::abs(actual - (1 - z) * .5f) < 1.e-6f);
        }
    }
    const Ref draw = Unpack(Residency::Pack(1, 0x12345678));
    assert(draw.arena == 1 && draw.offset == 0x12345678);
    const WorldRoot world{draw, Unpack(Residency::Pack(1, 32)), Unpack(Residency::Pack(1, 64)), 17};
    u32 words[16]{};
    std::memcpy(words, &world, sizeof(world));
    assert(words[0] == 1 && words[1] == 0x12345678 && words[6] == 17);
    const CharacterRoot character{draw, draw, draw, draw, {.5f, .25f}, draw, draw, 0, 31};
    std::memcpy(words, &character, sizeof(character));
    assert(words[15] == 31);
    const CompositeRoot composite{draw, Unpack(Residency::Pack(4, 16)), 123};
    std::memcpy(words, &composite, sizeof(composite));
    assert(words[2] == 4 && words[3] == 16 && words[4] == 123);
    assert(sizeof(world) <= 64 && sizeof(character) <= 64 && sizeof(composite) <= 64 &&
           sizeof(TextureRoot) <= 16);
    const ShadowRoot shadow{draw, draw, 2, 0x1234};
    std::memcpy(words, &shadow, sizeof(shadow));
    assert(words[4] == 2 && words[5] == 0x1234);
    const auto bias = Bias(3, 2.2f, 16.0f);
    assert(bias.list == 3 && bias.constant == 2 && bias.slope == 1.0f);
    assert(Bias(3, std::numeric_limits<float>::infinity(), 0.f).constant == 0);
    constexpr auto images = DeferredImages(11, 12, 13, 14, 15, 16);
    static_assert(sizeof(images) == 28 && images[0] == 11 && images[1] == 12 && images[2] == 13 &&
                  images[3] == 14 && images[4] == 15 && images[5] == 15 && images[6] == 16);
    Scene::Header h{};
    h.draws = 1;
    h.draw_commands[0] = 1;
    assert(Header(h));
    h.draw_commands[0] = ~0u;
    assert(!Header(h));
    h.draw_commands[0] = 1;
    h.characters = Scene::MaxCharacters + 1;
    assert(!Header(h));
    Scene::Draw d{};
    d.vertices = Residency::Pack(0, 16);
    d.stride = 24;
    d.material = 1;
    Scene::Command cmd{3, 1, 10, 0, 0};
    assert(Ready(d, cmd, 1, 13, 1, true));
    assert(!Ready(d, cmd, 1, 12, 1, true));
    assert(!Ready(d, cmd, 1, 13, 0, true));
    d.vertices = 0;
    assert(!Ready(d, cmd, 1, 13, 1, false));
    d.vertices = Residency::Pack(2, 16);
    assert(Ready(d, cmd, 1, 13, 1, false)); // a checked deformer-arena copy
    d.vertices = Residency::Pack(2, 16_MB - 8);
    assert(!Ready(d, cmd, 1, 13, 1, false));
    d.vertices = 16;
    d.bones = 1;
    assert(!Ready(d, cmd, 1, 13, 1, false));
    d.skin = Residency::Pack(0, 64);
    d.palette = Residency::Pack(1, 128);
    assert(Ready(d, cmd, 1, 13, 1, false));
    d.skin = Residency::Pack(1, 64);
    assert(!Ready(d, cmd, 1, 13, 1, false));
    d.bones = 0;
    cmd.first_instance = 1;
    assert(!Ready(d, cmd, 1, 13, 1, false));
    cmd.first_instance = 0;
    cmd.first_index = ~0u;
    assert(!Ready(d, cmd, 1, 13, 1, false));
    std::puts(
        "Core 3D roots/word order/reverse-Z/geometry, deformer and skin readiness bounds passed");
}
