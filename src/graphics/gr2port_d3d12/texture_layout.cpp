// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics/gr2port_d3d12/texture_layout.h"
namespace Gr2Port::D3D12 {
DXGI_FORMAT TextureFormat(AmdGpu::DataFormat data, AmdGpu::NumberFormat number) {
    using D = AmdGpu::DataFormat;
    using N = AmdGpu::NumberFormat;
    const bool srgb = number == N::Srgb, unorm = number == N::Unorm, snorm = number == N::Snorm,
               fp = number == N::Float;
    switch (data) {
    case D::FormatBc1:
        return srgb    ? DXGI_FORMAT_BC1_UNORM_SRGB
               : unorm ? DXGI_FORMAT_BC1_UNORM
                       : DXGI_FORMAT_UNKNOWN;
    case D::FormatBc2:
        return srgb    ? DXGI_FORMAT_BC2_UNORM_SRGB
               : unorm ? DXGI_FORMAT_BC2_UNORM
                       : DXGI_FORMAT_UNKNOWN;
    case D::FormatBc3:
        return srgb    ? DXGI_FORMAT_BC3_UNORM_SRGB
               : unorm ? DXGI_FORMAT_BC3_UNORM
                       : DXGI_FORMAT_UNKNOWN;
    case D::FormatBc4:
        return snorm ? DXGI_FORMAT_BC4_SNORM : unorm ? DXGI_FORMAT_BC4_UNORM : DXGI_FORMAT_UNKNOWN;
    case D::FormatBc5:
        return snorm ? DXGI_FORMAT_BC5_SNORM : unorm ? DXGI_FORMAT_BC5_UNORM : DXGI_FORMAT_UNKNOWN;
    case D::FormatBc6:
        return snorm ? DXGI_FORMAT_BC6H_SF16 : unorm ? DXGI_FORMAT_BC6H_UF16 : DXGI_FORMAT_UNKNOWN;
    case D::FormatBc7:
        return srgb    ? DXGI_FORMAT_BC7_UNORM_SRGB
               : unorm ? DXGI_FORMAT_BC7_UNORM
                       : DXGI_FORMAT_UNKNOWN;
    case D::Format8_8_8_8:
        return srgb    ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
               : snorm ? DXGI_FORMAT_R8G8B8A8_SNORM
               : unorm ? DXGI_FORMAT_R8G8B8A8_UNORM
                       : DXGI_FORMAT_UNKNOWN;
    case D::Format8:
        return snorm ? DXGI_FORMAT_R8_SNORM : unorm ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_UNKNOWN;
    case D::Format8_8:
        return snorm   ? DXGI_FORMAT_R8G8_SNORM
               : unorm ? DXGI_FORMAT_R8G8_UNORM
                       : DXGI_FORMAT_UNKNOWN;
    case D::Format16:
        return fp      ? DXGI_FORMAT_R16_FLOAT
               : snorm ? DXGI_FORMAT_R16_SNORM
               : unorm ? DXGI_FORMAT_R16_UNORM
                       : DXGI_FORMAT_UNKNOWN;
    case D::Format16_16:
        return fp      ? DXGI_FORMAT_R16G16_FLOAT
               : snorm ? DXGI_FORMAT_R16G16_SNORM
               : unorm ? DXGI_FORMAT_R16G16_UNORM
                       : DXGI_FORMAT_UNKNOWN;
    case D::Format16_16_16_16:
        return fp      ? DXGI_FORMAT_R16G16B16A16_FLOAT
               : snorm ? DXGI_FORMAT_R16G16B16A16_SNORM
               : unorm ? DXGI_FORMAT_R16G16B16A16_UNORM
                       : DXGI_FORMAT_UNKNOWN;
    case D::Format32:
        return fp ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_UNKNOWN;
    case D::Format32_32:
        return fp ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_UNKNOWN;
    case D::Format32_32_32_32:
        return fp ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_UNKNOWN;
    case D::Format10_10_10_2:
        return unorm ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_UNKNOWN;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}
bool PlanTexture(const AmdGpu::Image& s, u32 first, TextureLayout& out) {
    using namespace AmdGpu;
    out = {};
    const bool cube = s.GetType() == ImageType::Cube;
    const u32 levels = s.NumLevels(), layers = s.NumLayers();
    const auto tile = s.GetTileMode();
    const bool tiled = s.IsTiled();
    const bool allowed = tile == TileMode::Display1DThin || tile == TileMode::Display2DThin ||
                         tile == TileMode::Thin1DThin || tile == TileMode::Thin2DThin ||
                         tile == TileMode::DisplayLinearAligned;
    const auto format = TextureFormat(s.GetDataFmt(), s.GetNumberFmt());
    if (!allowed || format == DXGI_FORMAT_UNKNOWN || s.alt_tile_mode || s.base_level ||
        !s.Address() || s.NumSamples() != 1 || first >= levels || levels > VideoCore::MAX_MIPS ||
        (cube ? s.depth != 0 : s.GetType() != ImageType::Color2D || layers != 1))
        return false;
    const u32 width = s.width + 1, height = s.height + 1;
    if (s.Pitch() < width || levels > std::bit_width(std::max(width, height)))
        return false;
    const u32 bits = NumBitsPerBlock(s.GetDataFmt()), bytes = bits / 8;
    const bool block = IsBlockCoded(s.GetDataFmt());
    if (!bytes || u64(s.Pitch()) * height * layers * bytes * 2 > 0xffffffffull)
        return false;
    std::array<VideoCore::MipInfo, VideoCore::MAX_MIPS> mips{};
    u32 size = 0, micro = 0;
    VideoCore::ComputeImageSize(s.Pitch(), height, 1, levels, layers, bits, 1, block,
                                s.pow2pad != 0, false, tile, GetArrayMode(tile), &size, &micro,
                                mips.data());
    out.first = first;
    out.levels = levels - first;
    out.layers = cube ? 6 : 1;
    out.source_base = mips[first].offset;
    out.source_bytes = size - out.source_base;
    out.tiled = tiled;
    out.desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    out.desc.Width = std::max(1u, width >> first);
    out.desc.Height = std::max(1u, height >> first);
    out.desc.DepthOrArraySize = out.layers;
    out.desc.MipLevels = out.levels;
    out.desc.Format = format;
    out.desc.SampleDesc.Count = 1;
    out.desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    const auto select = s.DstSelect();
    u32 mapping = 1u << 12;
    for (u32 i = 0; i < 4; ++i) {
        const u32 sw = static_cast<u32>(select.array[i]);
        if (sw > 1 && (sw < 4 || sw > 7))
            return false;
        const u32 component = sw == 0 ? 4 : sw == 1 ? 5 : sw - 4;
        mapping |= component << (i * 3);
    }
    out.mapping = mapping;
    // A BGRA memory view is equivalent to the R/B destination selection.
    if (s.GetDataFmt() == DataFormat::Format8_8_8_8 && select.r == CompSwizzle::Blue &&
        select.g == CompSwizzle::Green && select.b == CompSwizzle::Red &&
        select.a == CompSwizzle::Alpha &&
        (s.GetNumberFmt() == NumberFormat::Unorm || s.GetNumberFmt() == NumberFormat::Srgb)) {
        out.desc.Format = s.GetNumberFmt() == NumberFormat::Srgb ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                                                                 : DXGI_FORMAT_B8G8R8A8_UNORM;
        out.mapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    }
    for (u32 slice = 0; slice < out.layers; ++slice)
        for (u32 level = first; level < levels; ++level) {
            const auto& mip = mips[level];
            const u32 slice_bytes = mip.size / layers;
            const u32 pitch = block ? std::max(1u, (u32(mip.pitch) + 3) / 4) : mip.pitch;
            const u32 rows = block ? std::max(1u, (u32(mip.height) + 3) / 4) : mip.height;
            if (u64(pitch) * rows * bytes > slice_bytes)
                return false;
            TextureCopy copy{};
            copy.subresource = level - first + slice * out.levels;
            copy.width = std::max(1u, width >> level);
            copy.height = std::max(1u, height >> level);
            const auto footprint =
                Residency::Place(copy.width, copy.height, bytes, block, out.copy_bytes);
            copy.footprint.Offset = footprint.offset;
            copy.footprint.Footprint = {
                out.desc.Format, block ? u32(Residency::Align(copy.width, 4)) : copy.width,
                block ? u32(Residency::Align(copy.height, 4)) : copy.height, 1, footprint.pitch};
            copy.rows = footprint.rows;
            copy.row_bytes = footprint.row_bytes;
            copy.source_offset = mip.offset - out.source_base + slice * slice_bytes;
            copy.source_pitch = pitch * bytes;
            copy.source_bytes = slice_bytes;
            if (tiled) {
                const bool macro = IsMacroTiled(GetArrayMode(tile)) && !(micro & (1u << level));
                auto& p = copy.detile;
                p = {pitch,
                     rows,
                     1,
                     pitch * rows,
                     bytes,
                     static_cast<u32>(GetMicroTileMode(tile)),
                     1,
                     1,
                     macro ? 4u : 2u,
                     0,
                     1,
                     1,
                     2,
                     256,
                     1,
                     s.GetBankSwizzle()};
                if (macro) {
                    const auto mode = CalculateMacrotileMode(tile, bits, 1);
                    p.pipe_config = static_cast<u32>(GetPipeConfig(tile));
                    p.bank_width = GetBankWidth(mode);
                    p.bank_height = GetBankHeight(mode);
                    p.num_banks = GetNumBanks(mode);
                    p.tile_split =
                        CalculateTileSplit(tile, GetArrayMode(tile), GetMicroTileMode(tile), bits);
                    p.macro_aspect = GetMacrotileAspect(mode);
                }
                if (!ValidateDetile(p, {1, 2, copy.source_offset, copy.source_offset}))
                    return false;
            }
            if (u64(copy.source_offset) + slice_bytes > out.source_bytes ||
                copy.row_bytes > copy.source_pitch || copy.rows > rows)
                return false;
            out.copy_bytes = footprint.offset + footprint.bytes;
            out.copies.push_back(copy);
        }
    return !out.copies.empty();
}
} // namespace Gr2Port::D3D12
