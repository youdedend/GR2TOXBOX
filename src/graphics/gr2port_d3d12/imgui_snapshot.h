// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Gr2Port::ImGuiRelay {
struct Pixels {
    std::uint64_t revision = 0;
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
struct Vertex { float position[2], uv[2]; std::uint32_t color; };
static_assert(sizeof(Vertex)==20);
struct Draw {
    std::uint32_t count, first_index, vertex_offset;
    std::array<float,4> clip;
    std::shared_ptr<const Pixels> texture;
};
struct Snapshot {
    float position[2]{}, size[2]{}, framebuffer_scale[2]{1,1};
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Draw> commands;
};
// Immutable publication: no ImGui pointers, callbacks or mutable atlas buffers
// cross the producer/render boundary. Texture snapshots survive Forget/reuse.
std::shared_ptr<const Snapshot> Latest();
void ResetSnapshots();
}
