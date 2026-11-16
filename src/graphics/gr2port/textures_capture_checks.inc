// SPDX-License-Identifier: GPL-2.0-or-later
// Shared CPU capture implementation; included inside the backend namespace.
bool ValidSwizzle(AmdGpu::CompSwizzle swizzle) {
    const u32 raw = static_cast<u32>(swizzle);
    return raw <= 1 || (raw >= 4 && raw <= 7);
}

bool Tiled(AmdGpu::TileMode tile_mode) {
    return tile_mode != AmdGpu::TileMode::DisplayLinearAligned;
}

bool IsCube(const AmdGpu::Image& sharp) {
    return sharp.GetType() == AmdGpu::ImageType::Cube;
}

// The checks on a T# that need no layout: a single 2D image or a single cube, with a tiling and
// format the renderer converts.
bool SingleImage(const AmdGpu::Image& sharp) {
    const bool single =
        IsCube(sharp) ? sharp.depth == 0
                      : sharp.GetType() == AmdGpu::ImageType::Color2D && sharp.NumLayers() == 1;
    return single && sharp.base_level == 0 && sharp.alt_tile_mode == 0 && sharp.Address() != 0;
}
