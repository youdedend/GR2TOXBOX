// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace Gr2Port::D3D12::Residency {
constexpr std::uint64_t Pack(std::uint32_t arena, std::uint32_t offset) {
    return (std::uint64_t(arena) << 32) | offset;
}
constexpr std::uint32_t Arena(std::uint64_t address) {
    return address >> 32;
}
constexpr std::uint32_t Offset(std::uint64_t address) {
    return std::uint32_t(address);
}
constexpr std::uint64_t Align(std::uint64_t n, std::uint64_t a) {
    return (n + a - 1) & ~(a - 1);
}
class Ranges {
    std::map<std::uint32_t, std::uint32_t> free_;

public:
    void Reset(std::uint32_t first, std::uint32_t count) {
        free_.clear();
        if (count)
            free_[first] = count;
    }
    bool Allocate(std::uint32_t count, std::uint32_t& first) {
        if (!count) {
            first = 0;
            return true;
        }
        for (auto i = free_.begin(); i != free_.end(); ++i)
            if (i->second >= count) {
                first = i->first;
                const auto left = i->second - count;
                free_.erase(i);
                if (left)
                    free_.emplace(first + count, left);
                return true;
            }
        return false;
    }
    void Free(std::uint32_t first, std::uint32_t count) {
        if (!count)
            return;
        auto [i, ok] = free_.emplace(first, count);
        if (!ok)
            return;
        auto next = std::next(i);
        if (next != free_.end() && std::uint64_t(i->first) + i->second == next->first) {
            i->second += next->second;
            free_.erase(next);
        }
        if (i != free_.begin()) {
            auto prev = std::prev(i);
            if (std::uint64_t(prev->first) + prev->second == i->first) {
                prev->second += i->second;
                free_.erase(i);
            }
        }
    }
};
constexpr bool ValidIndices(std::uint32_t max_index, std::uint32_t vertices,
                            std::uint32_t skin_vertices = ~0u) {
    return vertices && max_index < vertices && max_index < skin_vertices;
}
// CPU tests exercise exactly this fence gate used by the resource collectors.
constexpr bool Complete(std::uint64_t required, std::uint64_t completed) {
    return required <= completed;
}
struct Twin {
    std::uint32_t active = 0;
    std::uint64_t busy[2]{};
    bool CanRotate(std::uint64_t completed) const {
        return Complete(busy[active ^ 1], completed);
    }
    void Rotate(std::uint64_t safe_after) {
        busy[active] = safe_after;
        active ^= 1;
    }
};
constexpr std::uint32_t TextureCapacity = 32768;
constexpr std::uint32_t FirstReserved = 32480;
constexpr std::uint32_t TextureDescriptorEnd =
    TextureCapacity + FirstReserved; // 65248, leaves 288.
static_assert(TextureDescriptorEnd < 65536);
struct Footprint {
    std::uint64_t offset = 0, bytes = 0;
    std::uint32_t pitch = 0, rows = 0, row_bytes = 0;
};
inline Footprint Place(std::uint32_t width, std::uint32_t height, std::uint32_t bytes, bool bc,
                       std::uint64_t start = 0) {
    const auto columns = bc ? std::max(1u, (width + 3) / 4) : width;
    const auto rows = bc ? std::max(1u, (height + 3) / 4) : height;
    const auto row = std::uint64_t(columns) * bytes;
    if (!width || !height || !bytes || row > std::numeric_limits<std::uint32_t>::max() - 255)
        return {};
    return {Align(start, 512), Align(row, 256) * rows, std::uint32_t(Align(row, 256)), rows,
            std::uint32_t(row)};
}
} // namespace Gr2Port::D3D12::Residency
