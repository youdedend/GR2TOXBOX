// SPDX-License-Identifier: GPL-2.0-or-later
// Shared CPU capture implementation; included inside the backend namespace.
// InitTextureFromGnf FUN_01618ca0(texture, gnf, srgb) fills a texture object from a GNF blob that
// it keeps; DestroyTexture FUN_016182a0(texture) frees the blob.
constexpr Hooks::Entry Init{0x1618ca0, 6, Hooks::FramePrologue};
constexpr Hooks::Entry Destroy{0x16182a0, 6, Hooks::FramePrologue};

// Texture object: T# at +0x30, the renderer's handle at +0x6c, the GNF blob at +0x70.
constexpr u64 TextureSharp = 0x30;
constexpr u64 TextureHandle = 0x6c;
constexpr u64 TextureBlob = 0x70;
// GNF blob: a header {magic, contents size}, then the contents, whose stream size is at +4; the
// texture data stream follows the contents.
constexpr u64 BlobContentsSize = 0x4;
constexpr u64 BlobStreamSize = 0xc;
constexpr u64 BlobContents = 0x8;

// The tilings the detile shaders convert, and the bits per pixel or block of each shader.
constexpr std::array<AmdGpu::TileMode, 4> TileModes{
    AmdGpu::TileMode::Display1DThin,
    AmdGpu::TileMode::Display2DThin,
    AmdGpu::TileMode::Thin1DThin,
    AmdGpu::TileMode::Thin2DThin,
};
constexpr std::array<u32, 5> BlockBits{8, 16, 32, 64, 128};

// Each handle has two descriptors, its own and a twin Capacity above it. A texture whose levels
// change gets its new image in the one no frame samples, so frames in flight keep the old image.
constexpr u32 Twin = Capacity;
constexpr u32 Descriptors = 2 * Capacity;
// An extent larger than any image's.
constexpr u32 LargeExtent = 1u << 16;
constexpr u32 LoggedTextures = 8;
// The detile parameters lead each staging buffer; the texture data follows.
constexpr u64 ParamsBytes = 256;

// Streaming keeps the textures within a budget of device memory. The clock ticks every TickPeriod;
// a texture a capture read within the last UsedTicks is in use, and one read within the last
// ShownTicks gets larger levels when the quality allows them.
constexpr auto TickPeriod = std::chrono::milliseconds{125};
constexpr u32 UsedTicks = 16;
constexpr u32 ShownTicks = 2;
// A texture no draw reads for this long, a minute, is parked even when the budget has room.
constexpr u32 ParkTicks = 480;
// The largest levels a texture may keep. Out of use, a texture keeps at most Caps[Parked] while
// the budget asks for it, and once no draw has read it for ParkTicks.
constexpr std::array<u32, 8> Caps{LargeExtent, 4096, 2048, 1024, 512, 256, 128, 64};
constexpr u8 Parked = 6;
// The qualities, best first: the caps of the textures in use and of the others.
struct Quality {
    u8 used;
    u8 unused;
};
// The cap a texture last read age ticks ago takes at a quality.
constexpr u8 CapOf(const Quality& quality, u32 age) {
    if (age <= UsedTicks) {
        return quality.used;
    }
    return age > ParkTicks ? std::max(quality.unused, Parked) : quality.unused;
}
constexpr std::array<Quality, 9> Qualities{{
    {0, 0},
    {0, Parked},
    {1, Parked},
    {2, Parked},
    {3, Parked},
    {4, Parked},
    {5, Parked},
    {6, Parked},
    {7, 7},
}};
// Device memory the textures leave free beside the headroom, for meshes and targets to grow into;
// the room a better quality must leave besides; the least budget the textures get.
constexpr u64 SpareBytes = 128_MB;
constexpr u64 RaiseMargin = 64_MB;
constexpr u64 LeastBudget = 64_MB;
// Per frame at most: shrinks recorded, new textures looked at, grows started and bytes of copied
// uploads recorded. The data the worker holds for uploads not yet recorded stays within
// InFlightBytes.
constexpr u32 ShrinksPerFrame = 64;
constexpr u32 FreshPerFrame = 512;
constexpr u32 GrowsPerFrame = 64;
constexpr u64 FinishBytes = 64_MB;
constexpr u64 InFlightBytes = 64_MB;
// The worker copies a texture's data in chunks, each under the lock the texture destroy takes.
constexpr u64 ChunkBytes = 256_KB;
// Frames from a swap until no frame samples the replaced image: the packets the ring holds and the
// one the capture is writing.
constexpr u64 RetireFrames = SceneRing::Slots + 2;
// Images at least this large get device memory of their own, which a shrink returns at once.
constexpr u64 DedicatedBytes = 1_MB;
