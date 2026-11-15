// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics/gr2port_d3d12/detile.h"
#include <limits>
namespace Gr2Port::D3D12 {
namespace {
bool Power(std::uint32_t x) { return x && !(x & (x - 1)); }
}
bool ValidateDetile(const DetileConstants& p, const DetileTextures& t) {
    if (!p.pitch || !p.height || p.depth != 1 || p.pitch % 8 || p.height % 8 ||
        p.samples != 1 || p.thickness != 1 || p.micro_mode > 1 ||
        (p.bytes != 1 && p.bytes != 2 && p.bytes != 4 && p.bytes != 8 && p.bytes != 12 && p.bytes != 16) ||
        (p.micro_mode == 0 && p.bytes == 12) || (p.array_mode != 2 && p.array_mode != 4) ||
        (t.input_offset & 3) || (t.output_offset & 3) || !t.input || !t.output) return false;
    const std::uint64_t count = std::uint64_t(p.pitch) * p.height;
    if (count != p.texel_count || count * p.bytes > std::numeric_limits<std::uint32_t>::max()) return false;
    if (p.array_mode == 2) return true;
    if ((p.pipe_config != 0 && p.pipe_config != 10 && p.pipe_config != 12) ||
        !Power(p.num_banks) || p.num_banks < 2 || p.num_banks > 16 ||
        !Power(p.bank_width) || p.bank_width > 8 || !Power(p.bank_height) || p.bank_height > 8 ||
        !Power(p.macro_aspect) || p.macro_aspect > p.num_banks ||
        !Power(p.tile_split) || p.tile_split < 64 || p.tile_split > 4096 ||
        p.bank_swizzle >= p.num_banks) return false;
    const auto pipes = p.pipe_config == 0 ? 2u : 8u;
    const auto macro_pitch = 8 * p.bank_width * pipes * p.macro_aspect;
    const auto macro_height = 8 * p.bank_height * p.num_banks / p.macro_aspect;
    // Pipe/bank interleave is 256 bytes. Small mip levels must be padded to
    // complete interleave groups or use the micro-tiled fallback instead.
    const auto interleave = 256ull * pipes * p.num_banks;
    return p.pitch % macro_pitch == 0 && p.height % macro_height == 0 &&
           (count * p.bytes) % interleave == 0;
}
}
