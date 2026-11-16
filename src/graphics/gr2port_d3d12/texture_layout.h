// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <vector>
#include <d3d12.h>
#include "graphics/amdgpu/resource.h"
#include "graphics/gr2port_d3d12/detile.h"
#include "graphics/gr2port_d3d12/residency_state.h"
#include "graphics/texture_cache/tile.h"
namespace Gr2Port::D3D12 {
DXGI_FORMAT TextureFormat(AmdGpu::DataFormat data, AmdGpu::NumberFormat number);
struct TextureCopy {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    u32 subresource = 0, width = 0, height = 0, rows = 0, row_bytes = 0;
    u32 source_offset = 0, source_pitch = 0, source_bytes = 0;
    DetileConstants detile{};
};
struct TextureLayout {
    D3D12_RESOURCE_DESC desc{};
    u32 source_base = 0, source_bytes = 0, first = 0, levels = 0, layers = 0, mapping = 0;
    u64 copy_bytes = 0;
    bool tiled = false;
    std::vector<TextureCopy> copies;
};
bool PlanTexture(const AmdGpu::Image& sharp, u32 first, TextureLayout& out);
} // namespace Gr2Port::D3D12
