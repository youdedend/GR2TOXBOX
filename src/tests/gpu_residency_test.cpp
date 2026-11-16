// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include "graphics/gr2port_d3d12/residency_state.h"
#include "graphics/gr2port_d3d12/texture_layout.h"
void assert_fail_impl() {
    std::abort();
}
[[noreturn]] void unreachable_impl() {
    std::abort();
}
namespace R = Gr2Port::D3D12::Residency;
using namespace Gr2Port::D3D12;
AmdGpu::Image Sharp(AmdGpu::DataFormat format, AmdGpu::TileMode tile, u32 width = 64,
                    u32 height = 64) {
    AmdGpu::Image s{};
    s.base_address = 0x100;
    s.type = static_cast<u32>(AmdGpu::ImageType::Color2D);
    s.data_format = static_cast<u32>(format);
    s.num_format = static_cast<u32>(AmdGpu::NumberFormat::Unorm);
    s.width = width - 1;
    s.height = height - 1;
    s.pitch = width - 1;
    s.tiling_index = static_cast<u32>(tile);
    s.dst_sel_x = 4;
    s.dst_sel_y = 5;
    s.dst_sel_z = 6;
    s.dst_sel_w = 7;
    return s;
}
int main() {
    assert(R::Pack(0, 0) == 0);
    assert(R::Arena(R::Pack(4, 0xfffffff0u)) == 4 &&
           R::Offset(R::Pack(4, 0xfffffff0u)) == 0xfffffff0u);
    R::Ranges vertices, indices;
    vertices.Reset(16, 1024);
    indices.Reset(0, 64);
    u32 a, b, c;
    assert(vertices.Allocate(128, a) && a == 16);
    assert(vertices.Allocate(256, b) && b == 144);
    vertices.Free(a, 128);
    vertices.Free(b, 256);
    assert(vertices.Allocate(1024, c) && c == 16);
    assert(!vertices.Allocate(1, a));
    vertices.Free(c, 1024);
    assert(indices.Allocate(32, a) && a == 0);
    assert(indices.Allocate(32, b) && b == 32);
    assert(!indices.Allocate(1, c));
    indices.Free(a, 32);
    indices.Free(b, 32);
    assert(indices.Allocate(64, c) && c == 0);
    assert(R::ValidIndices(3, 4, 4));
    assert(!R::ValidIndices(4, 4, 8));
    assert(!R::ValidIndices(3, 4, 3));
    assert(!R::ValidIndices(0, 0));
    auto resource = std::make_shared<int>(42);
    std::weak_ptr<int> live = resource;
    struct Retired {
        std::shared_ptr<int> value;
        u64 fence;
    };
    std::vector<Retired> retired;
    retired.push_back({resource, 15});
    resource.reset();
    auto collect = [&](u64 completed) {
        std::erase_if(retired,
                      [&](const auto& item) { return R::Complete(item.fence, completed); });
    };
    collect(14);
    assert(!live.expired());
    collect(15);
    assert(live.expired());
    R::Twin twin;
    assert(twin.CanRotate(0));
    twin.Rotate(9);
    assert(twin.active == 1 && !twin.CanRotate(8));
    assert(twin.CanRotate(9));
    twin.Rotate(20);
    assert(twin.active == 0 && !twin.CanRotate(19));
    assert(R::FirstReserved + R::TextureCapacity == R::TextureDescriptorEnd);
    assert(R::TextureDescriptorEnd + 288 == 65536);
    const auto footprint = R::Place(17, 9, 4, false, 1);
    assert(footprint.offset == 512 && footprint.pitch == 256 && footprint.row_bytes == 68 &&
           footprint.rows == 9);
    const auto bc = R::Place(17, 9, 8, true, footprint.offset + footprint.bytes);
    assert(bc.pitch == 256 && bc.rows == 3 && bc.row_bytes == 40 && bc.offset % 512 == 0);
    using D = AmdGpu::DataFormat;
    using N = AmdGpu::NumberFormat;
    using T = AmdGpu::TileMode;
    assert(TextureFormat(D::FormatBc1, N::Srgb) == DXGI_FORMAT_BC1_UNORM_SRGB);
    assert(TextureFormat(D::FormatBc6, N::Snorm) == DXGI_FORMAT_BC6H_SF16);
    assert(TextureFormat(D::FormatBc7, N::Unorm) == DXGI_FORMAT_BC7_UNORM);
    assert(TextureFormat(D::Format16_16_16_16, N::Float) == DXGI_FORMAT_R16G16B16A16_FLOAT);
    assert(TextureFormat(D::Format32, N::Float) == DXGI_FORMAT_R32_FLOAT);
    assert(TextureFormat(D::Format8_8_8_8, N::Uint) == DXGI_FORMAT_UNKNOWN);
    TextureLayout plan;
    for (auto mode : {T::Display1DThin, T::Display2DThin, T::Thin1DThin, T::Thin2DThin}) {
        for (auto format :
             {D::Format8, D::Format8_8, D::Format8_8_8_8, D::FormatBc1, D::FormatBc7}) {
            auto sharp = Sharp(format, mode, 256, 256);
            sharp.last_level = 4;
            assert(PlanTexture(sharp, 0, plan));
            assert(plan.levels == 5 && plan.copies.size() == 5);
            for (const auto& copy : plan.copies) {
                assert(copy.footprint.Offset % 512 == 0 &&
                       copy.footprint.Footprint.RowPitch % 256 == 0);
                assert(copy.row_bytes <= copy.footprint.Footprint.RowPitch);
                assert(ValidateDetile(copy.detile, {1, 2, copy.source_offset, copy.source_offset}));
                assert(u64(copy.source_offset) + copy.source_bytes <= plan.source_bytes);
            }
            assert(PlanTexture(sharp, 2, plan));
            assert(plan.first == 2 && plan.levels == 3 && plan.source_base > 0);
        }
    }
    auto sharp = Sharp(D::Format8_8_8_8, T::DisplayLinearAligned, 1920, 1080);
    assert(PlanTexture(sharp, 0, plan) && !plan.tiled);
    sharp.dst_sel_x = 6;
    sharp.dst_sel_z = 4;
    assert(PlanTexture(sharp, 0, plan) && plan.desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM);
    sharp = Sharp(D::FormatBc1, T::Thin2DThin, 128, 128);
    sharp.type = static_cast<u32>(AmdGpu::ImageType::Cube);
    sharp.pow2pad = 1;
    sharp.last_level = 3;
    assert(PlanTexture(sharp, 0, plan) && plan.layers == 6 && plan.copies.size() == 24);
    assert(plan.copies[4].source_offset > 0); // Cube source mips carry eight padded slices.
    sharp.alt_tile_mode = 1;
    assert(!PlanTexture(sharp, 0, plan));
    sharp.alt_tile_mode = 0;
    sharp.tiling_index = static_cast<u32>(T::DisplayLinearGeneral);
    assert(!PlanTexture(sharp, 0, plan));
    sharp = Sharp(D::FormatInvalid, T::Thin1DThin);
    assert(!PlanTexture(sharp, 0, plan));
    std::cout << "GPU residency: encoded arenas/ranges, mesh bounds, fence lifetimes, "
                 "BC/linear/cube footprints and twin rotation passed\n";
}
