// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/logging/log.h"
#include "common/thread.h"
#include "graphics/amdgpu/pixel_format.h"
#include "graphics/amdgpu/resource.h"
#include "graphics/amdgpu/tiling.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/memory.h"
#include "graphics/gr2port/movies.h"
#include "graphics/gr2port/pipeline.h"
#include "graphics/gr2port/scene.h"
#include "graphics/gr2port/scene_ring.h"
#include "graphics/gr2port/settings.h"
#include "graphics/gr2port/textures.h"
#include "graphics/gr2port/upscaler.h"
#include "graphics/host_shaders/tiling_macro_128_comp.h"
#include "graphics/host_shaders/tiling_macro_16_comp.h"
#include "graphics/host_shaders/tiling_macro_32_comp.h"
#include "graphics/host_shaders/tiling_macro_64_comp.h"
#include "graphics/host_shaders/tiling_macro_8_comp.h"
#include "graphics/host_shaders/tiling_micro_128_comp.h"
#include "graphics/host_shaders/tiling_micro_16_comp.h"
#include "graphics/host_shaders/tiling_micro_32_comp.h"
#include "graphics/host_shaders/tiling_micro_64_comp.h"
#include "graphics/host_shaders/tiling_micro_8_comp.h"
#include "graphics/renderer_vulkan/liverpool_to_vk.h"
#include "graphics/texture_cache/tile.h"

namespace Gr2Port::Textures {

namespace {

#include "graphics/gr2port/textures_capture_constants.inc"

// The detile shaders' parameters (host_shaders/tiling.comp). For block-compressed formats the
// pitch and height count blocks.
struct TilingParams {
    u32 bank_swizzle;
    u32 micro_tiled_mips;
    u32 num_mips;
    std::array<VideoCore::MipInfo, VideoCore::MAX_MIPS> mips;
};
static_assert(sizeof(TilingParams) <= ParamsBytes);

#include "graphics/gr2port/textures_capture_state.inc"

// The worker copies texture data from the game's memory into staging buffers, so the render
// thread only records commands. It takes the jobs in order.
enum JobState : u32 {
    Queued,
    Copied,
    Abandoned,
};
struct Job {
    u32 handle;
    u32 generation;
    VAddr data;
    u64 bytes;
    u8* destination;
    std::atomic<u32> state{Queued};
};
struct Worker {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job*> jobs;
};
// Never destroyed: the worker waits on it until the process ends.
Worker* g_worker = nullptr;

// Render thread only.
struct Layout {
    vk::Format format;
    vk::ComponentMapping mapping;
    // The size of the data's first level, and of the image's.
    vk::Extent2D extent;
    vk::Extent2D image_extent;
    // The levels of the data; the data level the image starts at, its offset and the bytes from
    // it on; the image's levels (at most a full chain) and layers.
    u32 data_levels;
    u32 first;
    u32 base;
    u32 bytes;
    u32 levels;
    u32 layers;
    u32 bits;
    bool block;
    AmdGpu::TileMode tile_mode;
    u32 size;
    u32 micro_tiled_mips;
    std::array<VideoCore::MipInfo, VideoCore::MAX_MIPS> mips;
};
struct Retiring {
    u32 handle;
    // The last frame that can sample the texture.
    u64 frame;
};
struct Staging {
    Buffer buffer;
    u64 frame;
};
// An upload into a new image, from the worker's copy to the recorded commands.
struct Upload {
    std::unique_ptr<Job> job;
    Layout layout;
    u32 bank_swizzle;
    Image image;
    Staging staging;
    Staging scratch;
};
// What streaming knows of a texture's image.
struct Residency {
    vk::Format format;
    vk::ComponentMapping mapping;
    vk::Extent2D extent;
    u32 data_levels;
    u32 layers;
    // The data level the image starts at.
    u32 first;
    // The image's bytes from each data level on, from the format alone.
    std::array<u32, VideoCore::MAX_MIPS> bytes_from;
    // The data level the image starts at under each cap.
    std::array<u8, Caps.size()> first_at;
    // Textures of a T# alone keep their levels: nothing tells when the game frees their memory.
    bool streams;
    bool uploading;
    u64 installed_frame;
    // The image a swap replaced, its descriptor and the frame of the swap.
    Image old;
    u32 old_slot;
    u64 old_frame;
};
struct Change {
    u32 handle;
    u32 first;
    u32 used;
};
enum class Begun {
    Started,
    Wait,
    Skip,
};

const Device* g_device = nullptr;
Memory* g_memory = nullptr;
vk::Sampler g_sampler;
vk::Sampler g_clamp_sampler;
vk::DescriptorSetLayout g_set_layout;
vk::DescriptorPool g_pool;
vk::DescriptorSet g_set;
Image g_white;
bool g_white_cleared = false;
vk::DescriptorSetLayout g_detile_set_layout;
vk::PipelineLayout g_detile_layout;
// Indexed by the position in TileModes and in BlockBits.
std::array<std::array<vk::Pipeline, BlockBits.size()>, TileModes.size()> g_detile{};
std::array<Image, Capacity> g_resident;
std::array<Residency, Capacity> g_residency{};
// The movie frame each image holds, 0 for none.
std::array<u32, Capacity> g_refreshed{};
constexpr u32 LoggedKept = 16;
u32 g_logged_kept = 0;
// The handle of each movie surface a frame read, by its slot in Movies.
std::array<u32, Movies::Surfaces> g_movie_handles{};
std::vector<Retiring> g_retiring;
std::vector<Staging> g_staging;
std::deque<Upload> g_uploads;
std::vector<u32> g_replaced;
std::unordered_map<VkFormat, bool> g_format_support;
u32 g_logged_uploads = 0;
Stats g_stats{};
// Streaming: whether the twins exist, one past the highest handle in use, the current quality,
// the textures' budget, the bytes of every image they hold, the data the worker holds, and the
// ratio of the images' bytes to their estimates.
bool g_streaming = false;
u32 g_high = 1;
u32 g_quality = 0;
u64 g_limit = ~u64{0};
u64 g_allocated = 0;
u64 g_in_flight = 0;
double g_scale = 1.0;
std::chrono::steady_clock::time_point g_tick_time{};

namespace LiverpoolToVK = Vulkan::LiverpoolToVK;

// The table lookup of LiverpoolToVK::SurfaceFormat, which asserts on unknown formats.
vk::Format SurfaceFormat(const AmdGpu::Image& sharp) {
    return LiverpoolToVK::surface_format_table[LiverpoolToVK::GetSurfaceFormatTableIndex(
        sharp.GetDataFmt(), sharp.GetNumberFmt())];
}

#include "graphics/gr2port/textures_capture_checks.inc"

bool Convertible(const AmdGpu::Image& sharp) {
    const AmdGpu::TileMode tile_mode = sharp.GetTileMode();
    if (Tiled(tile_mode) && std::ranges::find(TileModes, tile_mode) == TileModes.end()) {
        return false;
    }
    const u32 data_format = static_cast<u32>(sharp.GetDataFmt());
    const u32 number_format = static_cast<u32>(sharp.GetNumberFmt());
    if (data_format >= 64 || number_format >= 16) {
        return false;
    }
    const auto format = SurfaceFormat(sharp);
    const AmdGpu::CompMapping select = sharp.DstSelect();
    return format != vk::Format::eUndefined &&
           std::ranges::find(BlockBits, AmdGpu::NumBitsPerBlock(sharp.GetDataFmt())) !=
               BlockBits.end() &&
           ValidSwizzle(select.r) && ValidSwizzle(select.g) && ValidSwizzle(select.b) &&
           ValidSwizzle(select.a);
}

// The first data level no larger than cap, of data of extent with levels levels.
u32 FirstFor(vk::Extent2D extent, u32 levels, u32 cap) {
    u32 first = 0;
    while (first + 1 < levels && std::max(extent.width, extent.height) >> first > cap) {
        first++;
    }
    return first;
}

// The image's extent and levels from data level first on: at most a full chain.
vk::Extent2D ExtentFrom(vk::Extent2D extent, u32 first) {
    return {std::max(extent.width >> first, 1u), std::max(extent.height >> first, 1u)};
}

u32 LevelsFrom(vk::Extent2D extent, u32 data_levels, u32 first) {
    const vk::Extent2D image = ExtentFrom(extent, first);
    return std::min<u32>(data_levels - first, std::bit_width(std::max(image.width, image.height)));
}

// The layout of the data, and of an image from data level first on.
Layout ReadLayout(const AmdGpu::Image& sharp, u32 first) {
    Layout layout{};
    layout.format = SurfaceFormat(sharp);
    layout.mapping = LiverpoolToVK::ComponentMapping(sharp.DstSelect());
    layout.extent = vk::Extent2D{static_cast<u32>(sharp.width) + 1, static_cast<u32>(sharp.height) + 1};
    layout.data_levels = std::max(sharp.NumLevels(), 1u);
    layout.first = std::min(first, layout.data_levels - 1);
    layout.image_extent = ExtentFrom(layout.extent, layout.first);
    layout.levels = LevelsFrom(layout.extent, layout.data_levels, layout.first);
    layout.layers = IsCube(sharp) ? 6 : 1;
    layout.bits = AmdGpu::NumBitsPerBlock(sharp.GetDataFmt());
    layout.block = AmdGpu::IsBlockCoded(sharp.GetDataFmt());
    layout.tile_mode = sharp.GetTileMode();
    // The data can hold more layers than the image: padding rounds their count to a power of 2.
    VideoCore::ComputeImageSize(sharp.Pitch(), layout.extent.height, 1, layout.data_levels,
                                sharp.NumLayers(), layout.bits, 1, layout.block, sharp.pow2pad != 0,
                                false, layout.tile_mode, AmdGpu::GetArrayMode(layout.tile_mode),
                                &layout.size, &layout.micro_tiled_mips, layout.mips.data());
    layout.base = layout.mips[layout.first].offset;
    layout.bytes = layout.size - layout.base;
    return layout;
}

// Fills what streaming needs of a texture from its layout.
void Describe(Residency& r, const Layout& layout, bool streams) {
    r.format = layout.format;
    r.mapping = layout.mapping;
    r.extent = layout.extent;
    r.data_levels = layout.data_levels;
    r.layers = layout.layers;
    r.streams = streams;
    for (u32 i = 0; i < Caps.size(); i++) {
        r.first_at[i] = static_cast<u8>(FirstFor(layout.extent, layout.data_levels, Caps[i]));
    }
    r.bytes_from.fill(0);
    for (u32 first = 0; first < layout.data_levels && first < r.bytes_from.size(); first++) {
        const vk::Extent2D extent = ExtentFrom(layout.extent, first);
        u64 bytes = 0;
        for (u32 level = 0; level < LevelsFrom(layout.extent, layout.data_levels, first); level++) {
            u32 width = std::max(extent.width >> level, 1u);
            u32 height = std::max(extent.height >> level, 1u);
            if (layout.block) {
                width = (width + 3) / 4;
                height = (height + 3) / 4;
            }
            bytes += u64{width} * height * (layout.bits / 8);
        }
        r.bytes_from[first] = static_cast<u32>(std::min<u64>(bytes * layout.layers, ~u32{0}));
    }
}

#include "graphics/gr2port/textures_capture_hooks.inc"

void Work() {
    Common::SetCurrentThreadName("GR2port:Textures");
    for (;;) {
        Job* job = nullptr;
        {
            std::unique_lock lock{g_worker->mutex};
            g_worker->wake.wait(lock, [] { return !g_worker->jobs.empty(); });
            job = g_worker->jobs.front();
            g_worker->jobs.pop_front();
        }
        JobState state = Copied;
        for (u64 offset = 0; offset < job->bytes; offset += ChunkBytes) {
            std::scoped_lock lock{g_mutex};
            if (g_generations[job->handle] != job->generation) {
                state = Abandoned;
                break;
            }
            std::memcpy(job->destination + offset,
                        reinterpret_cast<const void*>(job->data + offset),
                        std::min(ChunkBytes, job->bytes - offset));
        }
        job->state.store(state, std::memory_order_release);
    }
}

void WriteDescriptor(u32 slot, vk::ImageView view) {
    const vk::DescriptorImageInfo info{
        .imageView = view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::WriteDescriptorSet write{
        .dstSet = g_set,
        .dstBinding = 0,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eSampledImage,
        .pImageInfo = &info,
    };
    g_device->device.updateDescriptorSets(write, {}, g_device->dispatch);
}

void Retire(u64 frame, u64 completed) {
    std::erase_if(g_replaced, [completed](u32 handle) {
        Residency& r = g_residency[handle];
        if (completed < r.old_frame + RetireFrames) {
            return false;
        }
        WriteDescriptor(r.old_slot, g_white.view);
        g_allocated -= r.old.size;
        g_memory->Destroy(r.old);
        return true;
    });
    std::vector<u32> destroyed;
    {
        std::scoped_lock lock{g_mutex};
        destroyed.swap(g_destroyed);
    }
    // The packet of this frame, or the one the capture was writing at the destroy, which the next
    // frame takes at the latest, can still sample the texture.
    for (const u32 handle : destroyed) {
        g_retiring.push_back({handle, frame + 1});
    }
    std::erase_if(g_retiring, [completed](const Retiring& r) {
        Residency& residency = g_residency[r.handle];
        if (completed < r.frame || residency.uploading || residency.old.image) {
            return false;
        }
        Image& image = g_resident[r.handle];
        if (image.image) {
            WriteDescriptor(g_sampled[r.handle].slot.load(std::memory_order_relaxed), g_white.view);
            g_stats.resident_bytes -= image.size;
            g_allocated -= image.size;
            g_memory->Destroy(image);
            g_stats.textures--;
        }
        residency = {};
        std::scoped_lock lock{g_mutex};
        g_free.push_back(r.handle);
        return true;
    });
    std::erase_if(g_staging, [completed](Staging& s) {
        if (s.frame > completed) {
            return false;
        }
        g_memory->Destroy(s.buffer);
        return true;
    });
}

bool FormatSupported(vk::Format format) {
    const auto [it, inserted] = g_format_support.try_emplace(static_cast<VkFormat>(format), false);
    if (inserted) {
        const auto features = g_device->gpu.getFormatProperties(format, g_device->dispatch);
        const auto needed =
            vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
        it->second = (features.optimalTilingFeatures & needed) == needed;
    }
    return it->second;
}

vk::Pipeline DetilePipeline(const Layout& layout) {
    const auto mode = std::ranges::find(TileModes, layout.tile_mode);
    const auto bits = std::ranges::find(BlockBits, layout.bits);
    return g_detile[mode - TileModes.begin()][bits - BlockBits.begin()];
}

// Records the detile of the staging buffer's texture data into scratch.
void RecordDetile(vk::CommandBuffer cmd, const Layout& layout, Buffer& staging, Buffer& scratch,
                  u32 bank_swizzle) {
    TilingParams params{
        .bank_swizzle = bank_swizzle,
        .micro_tiled_mips = layout.micro_tiled_mips >> layout.first,
        .num_mips = layout.data_levels - layout.first,
    };
    // The staging buffer holds the data from the first converted level on.
    for (u32 mip = 0; mip < params.num_mips; mip++) {
        params.mips[mip] = layout.mips[layout.first + mip];
        params.mips[mip].offset -= layout.base;
        if (layout.block) {
            params.mips[mip].pitch = std::max<u16>((params.mips[mip].pitch + 3) / 4, 1);
            params.mips[mip].height = std::max<u16>((params.mips[mip].height + 3) / 4, 1);
        }
    }
    std::memcpy(staging.mapped, &params, sizeof(params));
    const vk::DescriptorBufferInfo tiled{staging.buffer, ParamsBytes, layout.bytes};
    const vk::DescriptorBufferInfo linear{scratch.buffer, 0, layout.bytes};
    const vk::DescriptorBufferInfo uniform{staging.buffer, 0, sizeof(params)};
    const std::array<vk::WriteDescriptorSet, 3> writes{{
        {.dstBinding = 0,
         .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eStorageBuffer,
         .pBufferInfo = &tiled},
        {.dstBinding = 1,
         .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eStorageBuffer,
         .pBufferInfo = &linear},
        {.dstBinding = 2,
         .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eUniformBuffer,
         .pBufferInfo = &uniform},
    }};
    const auto& dl = g_device->dispatch;
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, DetilePipeline(layout), dl);
    cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, g_detile_layout, 0, writes, dl);
    // One invocation per pixel or block, 64 per group; tiled sizes are whole 8x8 tiles.
    cmd.dispatch(layout.bytes / (layout.bits / 8) / 64, 1, 1, dl);
}

bool CreateDetile(const Device& d) {
    const auto& dl = d.dispatch;
    const std::array<vk::DescriptorSetLayoutBinding, 3> bindings{{
        {0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
        {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute},
    }};
    g_detile_set_layout =
        d.device
            .createDescriptorSetLayout(
                {.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                 .bindingCount = static_cast<u32>(bindings.size()),
                 .pBindings = bindings.data()},
                nullptr, dl)
            .value;
    g_detile_layout =
        d.device
            .createPipelineLayout({.setLayoutCount = 1, .pSetLayouts = &g_detile_set_layout},
                                  nullptr, dl)
            .value;
    if (!g_detile_set_layout || !g_detile_layout) {
        return false;
    }
    for (size_t m = 0; m < TileModes.size(); m++) {
        const AmdGpu::TileMode tile_mode = TileModes[m];
        const AmdGpu::ArrayMode array_mode = AmdGpu::GetArrayMode(tile_mode);
        const auto micro_tile_mode = AmdGpu::GetMicroTileMode(tile_mode);
        const bool macro = AmdGpu::IsMacroTiled(array_mode);
        for (size_t b = 0; b < BlockBits.size(); b++) {
            const u32 bits = BlockBits[b];
            // The emulator's specialisation of host_shaders/tiling.comp for one sample, detiling.
            std::array<u32, 12> constants{1, static_cast<u32>(micro_tile_mode),
                                          AmdGpu::GetMicroTileThickness(array_mode), 0};
            if (macro) {
                const auto macro_tile_mode = AmdGpu::CalculateMacrotileMode(tile_mode, bits, 1);
                constants[4] = static_cast<u32>(array_mode);
                constants[5] = static_cast<u32>(AmdGpu::GetPipeConfig(tile_mode));
                constants[6] = AmdGpu::GetBankWidth(macro_tile_mode);
                constants[7] = AmdGpu::GetBankHeight(macro_tile_mode);
                constants[8] = AmdGpu::GetNumBanks(macro_tile_mode);
                constants[9] = std::bit_width(constants[8]) - 1;
                constants[10] =
                    AmdGpu::CalculateTileSplit(tile_mode, array_mode, micro_tile_mode, bits);
                constants[11] = AmdGpu::GetMacrotileAspect(macro_tile_mode);
            }
            std::span<const u32> code;
            switch (bits) {
            case 8:
                code = macro ? std::span<const u32>{TILING_MACRO_8_COMP}
                             : std::span<const u32>{TILING_MICRO_8_COMP};
                break;
            case 16:
                code = macro ? std::span<const u32>{TILING_MACRO_16_COMP}
                             : std::span<const u32>{TILING_MICRO_16_COMP};
                break;
            case 32:
                code = macro ? std::span<const u32>{TILING_MACRO_32_COMP}
                             : std::span<const u32>{TILING_MICRO_32_COMP};
                break;
            case 64:
                code = macro ? std::span<const u32>{TILING_MACRO_64_COMP}
                             : std::span<const u32>{TILING_MICRO_64_COMP};
                break;
            default:
                code = macro ? std::span<const u32>{TILING_MACRO_128_COMP}
                             : std::span<const u32>{TILING_MICRO_128_COMP};
                break;
            }
            std::array<vk::SpecializationMapEntry, 12> entries{};
            for (u32 i = 0; i < entries.size(); i++) {
                entries[i] = vk::SpecializationMapEntry{i, i * static_cast<u32>(sizeof(u32)), sizeof(u32)};
            }
            const u32 count = macro ? 12 : 4;
            const vk::SpecializationInfo specialization{
                .mapEntryCount = count,
                .pMapEntries = entries.data(),
                .dataSize = count * sizeof(u32),
                .pData = constants.data(),
            };
            const vk::ShaderModule module =
                d.device
                    .createShaderModule({.codeSize = code.size_bytes(), .pCode = code.data()},
                                        nullptr, dl)
                    .value;
            const vk::ComputePipelineCreateInfo info{
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                          .module = module,
                          .pName = "main",
                          .pSpecializationInfo = &specialization},
                .layout = g_detile_layout,
            };
            const PipelineBuildNote note{"detile compute", code};
            const auto [result, pipeline] = d.device.createComputePipeline({}, info, nullptr, dl);
            d.device.destroyShaderModule(module, nullptr, dl);
            if (result != vk::Result::eSuccess) {
                LOG_ERROR(Render_Vulkan, "GR2port: no detile pipeline for tile mode {}, {} bits",
                          static_cast<u32>(tile_mode), bits);
                return false;
            }
            g_detile[m][b] = pipeline;
        }
    }
    return true;
}

bool CreateSet(const Device& d) {
    const auto& dl = d.dispatch;
    const auto properties =
        d.gpu.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan12Properties>(
            dl);
    const auto& limits = properties.get<vk::PhysicalDeviceVulkan12Properties>();
    const u32 bound = std::min(limits.maxPerStageDescriptorUpdateAfterBindSampledImages,
                               limits.maxDescriptorSetUpdateAfterBindSampledImages);
    if (bound < Capacity) {
        LOG_ERROR(Render_Vulkan, "GR2port: the GPU binds fewer than {} textures", Capacity);
        return false;
    }
    g_streaming = bound >= Descriptors;
    if (!g_streaming) {
        LOG_WARNING(Render_Vulkan,
                    "GR2port: the GPU binds fewer than {} textures; textures keep every level",
                    Descriptors);
    }
    const u32 descriptors = g_streaming ? Descriptors : Capacity;
    const u32 anisotropy = Settings::Get().graphics.anisotropic_filtering;
    vk::SamplerCreateInfo sampler_info{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        // The game's loader sets this bias and ratio on every texture it loads. A scene drawn
        // below the picture's size for the upscaler takes the upscaler's bias too.
        .mipLodBias = -51.0f / 256.0f + Upscaler::LodBias(),
        // The game's ratio is 2; the graphics settings choose another or none.
        .anisotropyEnable = anisotropy != 0,
        .maxAnisotropy = std::min(static_cast<float>(anisotropy),
                                  d.gpu.getProperties(dl).limits.maxSamplerAnisotropy),
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    g_sampler = d.device.createSampler(sampler_info, nullptr, dl).value;
    // The game clamps its decal textures and cube maps to their edges.
    sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    g_clamp_sampler = d.device.createSampler(sampler_info, nullptr, dl).value;
    constexpr vk::ShaderStageFlags Stages = vk::ShaderStageFlagBits::eVertex |
                                            vk::ShaderStageFlagBits::eFragment |
                                            vk::ShaderStageFlagBits::eCompute;
    const std::array<vk::DescriptorSetLayoutBinding, 3> bindings{{
        {0, vk::DescriptorType::eSampledImage, descriptors, Stages},
        {1, vk::DescriptorType::eSampler, 1, Stages, &g_sampler},
        {2, vk::DescriptorType::eSampler, 1, Stages, &g_clamp_sampler},
    }};
    const std::array<vk::DescriptorBindingFlags, 3> binding_flags{
        vk::DescriptorBindingFlagBits::ePartiallyBound |
            vk::DescriptorBindingFlagBits::eUpdateAfterBind |
            vk::DescriptorBindingFlagBits::eUpdateUnusedWhilePending,
        {},
        {},
    };
    const vk::DescriptorSetLayoutBindingFlagsCreateInfo flags_info{
        .bindingCount = static_cast<u32>(binding_flags.size()),
        .pBindingFlags = binding_flags.data(),
    };
    g_set_layout = d.device
                       .createDescriptorSetLayout(
                           {.pNext = &flags_info,
                            .flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                            .bindingCount = static_cast<u32>(bindings.size()),
                            .pBindings = bindings.data()},
                           nullptr, dl)
                       .value;
    const std::array<vk::DescriptorPoolSize, 2> sizes{{
        {vk::DescriptorType::eSampledImage, descriptors},
        {vk::DescriptorType::eSampler, 2},
    }};
    g_pool = d.device
                 .createDescriptorPool({.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
                                        .maxSets = 1,
                                        .poolSizeCount = static_cast<u32>(sizes.size()),
                                        .pPoolSizes = sizes.data()},
                                       nullptr, dl)
                 .value;
    if (!g_sampler || !g_clamp_sampler || !g_set_layout || !g_pool) {
        return false;
    }
    const auto [result, sets] = d.device.allocateDescriptorSets(
        {.descriptorPool = g_pool, .descriptorSetCount = 1, .pSetLayouts = &g_set_layout}, dl);
    if (result != vk::Result::eSuccess) {
        return false;
    }
    g_set = sets.front();
    return true;
}

// Clears the white texture once, in the first frame.
void ClearWhite(vk::CommandBuffer cmd) {
    const auto& dl = g_device->dispatch;
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    vk::ImageMemoryBarrier2 barrier{
        .dstStageMask = vk::PipelineStageFlagBits2::eClear,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .image = g_white.image,
        .subresourceRange = range,
    };
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier}, dl);
    cmd.clearColorImage(
        g_white.image, vk::ImageLayout::eTransferDstOptimal,
        vk::ClearColorValue{.float32 = std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}}, range, dl);
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eClear;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask =
        vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier}, dl);
}

// Copies the image of the texture at source into the image of handle, as the game's DMA between
// their memories does on the GPU; false when source has no image of the same shape, or when its
// image does not hold movie, the last job into source.
bool CopyKept(vk::CommandBuffer cmd, u32 handle, VAddr source, const Movies::Frame& movie) {
    const auto& dl = g_device->dispatch;
    const u32 from = FindAddress(source);
    const Image& to = g_resident[handle];
    if (from == 0 || from >= FirstReserved || !g_resident[from].image ||
        !g_sampled[from].ready.load(std::memory_order_acquire) ||
        g_resident[from].extent != to.extent || g_resident[from].format != to.format ||
        (movie.found && g_refreshed[from] != movie.generation)) {
        return false;
    }
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    const auto samplers =
        vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader;
    std::array<vk::ImageMemoryBarrier2, 2> barriers{{
        {
            .srcStageMask = samplers,
            .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .image = g_resident[from].image,
            .subresourceRange = range,
        },
        {
            .srcStageMask = samplers,
            .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = to.image,
            .subresourceRange = range,
        },
    }};
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = static_cast<u32>(barriers.size()),
                          .pImageMemoryBarriers = barriers.data()},
                         dl);
    const vk::ImageCopy copy{
        .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .extent = {to.extent.width, to.extent.height, 1},
    };
    cmd.copyImage(g_resident[from].image, vk::ImageLayout::eTransferSrcOptimal, to.image,
                  vk::ImageLayout::eTransferDstOptimal, copy, dl);
    for (auto& barrier : barriers) {
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
        barrier.srcAccessMask = barrier.dstAccessMask;
        barrier.dstStageMask = samplers;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
        std::swap(barrier.oldLayout, barrier.newLayout);
    }
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = static_cast<u32>(barriers.size()),
                          .pImageMemoryBarriers = barriers.data()},
                         dl);
    return true;
}

// The stages that sample textures.
constexpr vk::PipelineStageFlags2 Samplers = vk::PipelineStageFlagBits2::eVertexShader |
                                             vk::PipelineStageFlagBits2::eFragmentShader |
                                             vk::PipelineStageFlagBits2::eComputeShader;

// Puts image, which holds the texture's data from level first on, in use: in the handle's own
// descriptor for its first image, else in the twin no frame samples. The replaced image is
// destroyed once no frame can sample it.
void PutInUse(u32 handle, const Image& image, u32 first, u64 frame) {
    Residency& r = g_residency[handle];
    Sampled& t = g_sampled[handle];
    Image& current = g_resident[handle];
    r.installed_frame = frame;
    g_stats.resident_bytes += image.size;
    if (!current.image) {
        current = image;
        r.first = first;
        WriteDescriptor(handle, image.view);
        g_refreshed[handle] = 0;
        // The handle may be that of an earlier texture, a movie's surface.
        std::ranges::replace(g_movie_handles, handle, 0u);
        g_stats.textures++;
        t.slot.store(static_cast<u16>(handle), std::memory_order_relaxed);
        t.ready.store(true, std::memory_order_release);
        return;
    }
    const u32 slot = t.slot.load(std::memory_order_relaxed) ^ Twin;
    WriteDescriptor(slot, image.view);
    g_stats.resident_bytes -= current.size;
    r.old = current;
    r.old_slot = slot ^ Twin;
    r.old_frame = frame;
    g_replaced.push_back(handle);
    current = image;
    r.first = first;
    t.slot.store(static_cast<u16>(slot), std::memory_order_release);
}

// Queues the copy of a texture's data from level first on for an upload into a new image. A first
// upload describes the texture for streaming.
Begun StartUpload(u32 handle, const Source& source, u32 generation, u32 first, bool fresh,
                  bool streams) {
    const Layout layout = ReadLayout(source.sharp, first);
    const VAddr data = source.sharp.Address();
    if (data + layout.size > source.end || layout.size == 0) {
        g_stats.too_large++;
        if (g_logged_skips < LoggedTextures) {
            g_logged_skips++;
            LOG_INFO(Render_Vulkan,
                     "GR2port: texture {:#x} not converted: a layout of {} bytes, {} in the GNF "
                     "data",
                     g_sampled[handle].object.load(std::memory_order_relaxed), layout.size,
                     source.end - std::min(data, source.end));
        }
        return Begun::Skip;
    }
    if (!FormatSupported(layout.format)) {
        g_stats.failed++;
        return Begun::Skip;
    }
    if (g_in_flight != 0 && g_in_flight + layout.bytes > InFlightBytes) {
        return Begun::Wait;
    }
    const bool tiled = Tiled(layout.tile_mode);
    Upload upload{
        .job = std::make_unique<Job>(),
        .layout = layout,
        .bank_swizzle = source.sharp.GetBankSwizzle(),
        .staging = {g_memory->CreateBuffer(ParamsBytes + layout.bytes,
                                           vk::BufferUsageFlagBits::eStorageBuffer |
                                               vk::BufferUsageFlagBits::eUniformBuffer |
                                               vk::BufferUsageFlagBits::eTransferSrc,
                                           Placement::Staging),
                    0},
    };
    if (tiled) {
        upload.scratch.buffer = g_memory->CreateBuffer(layout.bytes,
                                                       vk::BufferUsageFlagBits::eStorageBuffer |
                                                           vk::BufferUsageFlagBits::eTransferSrc,
                                                       Placement::Device);
    }
    upload.image =
        g_memory->CreateTexture(layout.format, layout.image_extent, layout.levels, layout.mapping,
                                layout.layers != 1, layout.bytes >= DedicatedBytes);
    if (upload.staging.buffer.mapped == nullptr || (tiled && !upload.scratch.buffer.buffer) ||
        !upload.image.image) {
        g_memory->Destroy(upload.staging.buffer);
        g_memory->Destroy(upload.scratch.buffer);
        g_memory->Destroy(upload.image);
        g_stats.failed++;
        return Begun::Skip;
    }
    Residency& r = g_residency[handle];
    if (fresh) {
        Describe(r, layout, streams);
    }
    r.uploading = true;
    g_in_flight += layout.bytes;
    g_allocated += upload.image.size;
    Job& job = *upload.job;
    job.handle = handle;
    job.generation = generation;
    job.data = data + layout.base;
    job.bytes = layout.bytes;
    job.destination = upload.staging.buffer.mapped + ParamsBytes;
    g_uploads.push_back(std::move(upload));
    {
        std::scoped_lock lock{g_worker->mutex};
        g_worker->jobs.push_back(&job);
    }
    g_worker->wake.notify_one();
    if (g_logged_uploads < LoggedTextures) {
        g_logged_uploads++;
        LOG_INFO(Render_Vulkan,
                 "GR2port: texture {:#x}: {}x{} {}, {} levels from level {}, tile mode {}, {} of "
                 "{} bytes",
                 g_sampled[handle].object.load(std::memory_order_relaxed),
                 layout.image_extent.width, layout.image_extent.height,
                 vk::to_string(layout.format), layout.levels, layout.first,
                 static_cast<u32>(layout.tile_mode), layout.bytes, layout.size);
    }
    g_stats.uploaded_bytes += layout.bytes;
    g_stats.uploaded_data_bytes += layout.size;
    return Begun::Started;
}

// Records the uploads whose data the worker copied, in order, and puts their images in use.
void FinishUploads(vk::CommandBuffer cmd, u64 frame) {
    const auto& dl = g_device->dispatch;
    u32 count = 0;
    u64 bytes = 0;
    while (count < g_uploads.size()) {
        const Upload& upload = g_uploads[count];
        if (upload.job->state.load(std::memory_order_acquire) == Queued ||
            (count != 0 && bytes + upload.layout.bytes > FinishBytes)) {
            break;
        }
        bytes += upload.layout.bytes;
        count++;
    }
    if (count == 0) {
        return;
    }
    // An upload of a texture the game destroyed since is dropped.
    std::vector<bool> kept(count);
    {
        std::scoped_lock lock{g_mutex};
        for (u32 i = 0; i < count; i++) {
            const Job& job = *g_uploads[i].job;
            kept[i] = job.state.load(std::memory_order_relaxed) == Copied &&
                      g_generations[job.handle] == job.generation;
        }
    }
    std::vector<vk::ImageMemoryBarrier2> barriers;
    barriers.reserve(count);
    for (u32 i = 0; i < count; i++) {
        Upload& upload = g_uploads[i];
        g_in_flight -= upload.layout.bytes;
        g_residency[upload.job->handle].uploading = false;
        if (!kept[i]) {
            g_allocated -= upload.image.size;
            g_memory->Destroy(upload.image);
            g_memory->Destroy(upload.staging.buffer);
            g_memory->Destroy(upload.scratch.buffer);
            continue;
        }
        if (Tiled(upload.layout.tile_mode)) {
            RecordDetile(cmd, upload.layout, upload.staging.buffer, upload.scratch.buffer,
                         upload.bank_swizzle);
        }
        barriers.push_back({
            .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = upload.image.image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, upload.layout.levels, 0,
                                 upload.layout.layers},
        });
    }
    if (!barriers.empty()) {
        const vk::MemoryBarrier2 detiled{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        };
        cmd.pipelineBarrier2({.memoryBarrierCount = 1,
                              .pMemoryBarriers = &detiled,
                              .imageMemoryBarrierCount = static_cast<u32>(barriers.size()),
                              .pImageMemoryBarriers = barriers.data()},
                             dl);
        std::vector<vk::BufferImageCopy> copies;
        for (u32 i = 0; i < count; i++) {
            if (!kept[i]) {
                continue;
            }
            const Upload& upload = g_uploads[i];
            const Layout& layout = upload.layout;
            const bool tiled = Tiled(layout.tile_mode);
            const Buffer& source = tiled ? upload.scratch.buffer : upload.staging.buffer;
            const u64 source_offset = tiled ? 0 : ParamsBytes;
            copies.clear();
            for (u32 mip = 0; mip < layout.levels; mip++) {
                const u32 level = layout.first + mip;
                const VideoCore::MipInfo& info = layout.mips[level];
                const u32 width = std::max(layout.extent.width >> level, 1u);
                const u32 height = std::max(layout.extent.height >> level, 1u);
                copies.push_back({
                    .bufferOffset = source_offset + info.offset - layout.base,
                    .bufferRowLength = info.pitch,
                    .bufferImageHeight = info.height,
                    .imageSubresource = {vk::ImageAspectFlagBits::eColor, mip, 0, layout.layers},
                    .imageExtent = {info.pitch != 0 ? std::min<u32>(info.pitch, width) : width,
                                    info.height != 0 ? std::min<u32>(info.height, height) : height,
                                    1},
                });
            }
            cmd.copyBufferToImage(source.buffer, upload.image.image,
                                  vk::ImageLayout::eTransferDstOptimal, copies, dl);
        }
        for (vk::ImageMemoryBarrier2& barrier : barriers) {
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstStageMask = Samplers;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = static_cast<u32>(barriers.size()),
                              .pImageMemoryBarriers = barriers.data()},
                             dl);
    }
    for (u32 i = 0; i < count; i++) {
        Upload& upload = g_uploads[i];
        if (!kept[i]) {
            continue;
        }
        const u32 handle = upload.job->handle;
        if (g_resident[handle].image) {
            g_stats.grows++;
            g_stats.grow_bytes += upload.layout.bytes;
        }
        PutInUse(handle, upload.image, upload.layout.first, frame);
        upload.staging.frame = frame;
        g_staging.push_back(upload.staging);
        if (upload.scratch.buffer.buffer) {
            upload.scratch.frame = frame;
            g_staging.push_back(upload.scratch);
        }
    }
    g_uploads.erase(g_uploads.begin(), g_uploads.begin() + count);
}

// Records copies of the levels each texture keeps into a smaller image, and puts those in use.
void RecordShrinks(vk::CommandBuffer cmd, u64 frame, std::span<const Change> changes) {
    if (changes.empty()) {
        return;
    }
    const auto& dl = g_device->dispatch;
    std::vector<Image> images(changes.size());
    std::vector<vk::ImageMemoryBarrier2> barriers;
    barriers.reserve(2 * changes.size());
    const vk::ImageSubresourceRange all{vk::ImageAspectFlagBits::eColor, 0, vk::RemainingMipLevels,
                                        0, vk::RemainingArrayLayers};
    for (size_t i = 0; i < changes.size(); i++) {
        const Residency& r = g_residency[changes[i].handle];
        const u32 first = changes[i].first;
        images[i] = g_memory->CreateTexture(r.format, ExtentFrom(r.extent, first),
                                            LevelsFrom(r.extent, r.data_levels, first), r.mapping,
                                            r.layers != 1, r.bytes_from[first] >= DedicatedBytes);
        if (!images[i].image) {
            continue;
        }
        barriers.push_back({
            .srcStageMask = Samplers,
            .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .image = g_resident[changes[i].handle].image,
            .subresourceRange = all,
        });
        barriers.push_back({
            .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = images[i].image,
            .subresourceRange = all,
        });
    }
    if (barriers.empty()) {
        return;
    }
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = static_cast<u32>(barriers.size()),
                          .pImageMemoryBarriers = barriers.data()},
                         dl);
    std::vector<vk::ImageCopy> copies;
    for (size_t i = 0; i < changes.size(); i++) {
        if (!images[i].image) {
            continue;
        }
        const Residency& r = g_residency[changes[i].handle];
        const u32 first = changes[i].first;
        copies.clear();
        for (u32 level = 0; level < LevelsFrom(r.extent, r.data_levels, first); level++) {
            const vk::Extent2D extent = ExtentFrom(r.extent, first + level);
            copies.push_back({
                .srcSubresource = {vk::ImageAspectFlagBits::eColor, first - r.first + level, 0,
                                   r.layers},
                .dstSubresource = {vk::ImageAspectFlagBits::eColor, level, 0, r.layers},
                .extent = {extent.width, extent.height, 1},
            });
        }
        cmd.copyImage(g_resident[changes[i].handle].image, vk::ImageLayout::eTransferSrcOptimal,
                      images[i].image, vk::ImageLayout::eTransferDstOptimal, copies, dl);
    }
    for (u32 i = 0; i < barriers.size(); i += 2) {
        vk::ImageMemoryBarrier2& source = barriers[i];
        source.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
        source.srcAccessMask = vk::AccessFlagBits2::eNone;
        source.dstStageMask = Samplers;
        source.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
        std::swap(source.oldLayout, source.newLayout);
        vk::ImageMemoryBarrier2& target = barriers[i + 1];
        target.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
        target.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        target.dstStageMask = Samplers;
        target.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
        target.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        target.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = static_cast<u32>(barriers.size()),
                          .pImageMemoryBarriers = barriers.data()},
                         dl);
    for (size_t i = 0; i < changes.size(); i++) {
        if (!images[i].image) {
            continue;
        }
        g_allocated += images[i].size;
        g_stats.shrinks++;
        PutInUse(changes[i].handle, images[i], changes[i].first, frame);
    }
}

// Queues the first uploads of new textures, in the order the game made them.
void StartNew() {
    struct Fresh {
        u32 handle;
        Source source;
        u32 generation;
        bool streams;
        bool used;
    };
    static std::vector<Fresh> fresh;
    fresh.clear();
    const u32 tick = g_tick.load(std::memory_order_relaxed);
    {
        std::scoped_lock lock{g_mutex};
        for (const u32 handle : g_pending) {
            if (fresh.size() == FreshPerFrame) {
                break;
            }
            const Texture& t = g_textures[handle];
            // A handle freed and reused before its first upload is queued twice.
            if (g_sampled[handle].object.load(std::memory_order_relaxed) == 0 ||
                g_resident[handle].image || g_residency[handle].uploading) {
                fresh.push_back({.handle = 0});
                continue;
            }
            fresh.push_back({
                .handle = handle,
                .source = g_sources[handle],
                .generation = g_generations[handle],
                .streams = !t.sharp_only.load(std::memory_order_relaxed),
                .used = tick - g_sampled[handle].used.load(std::memory_order_relaxed) <= UsedTicks,
            });
        }
        g_high = g_unused;
    }
    const Quality quality = Qualities[g_quality];
    // New textures may take the spare memory; a lower quality makes room in the next tick.
    const u64 limit = g_limit > ~u64{0} - SpareBytes ? g_limit : g_limit + SpareBytes;
    size_t done = 0;
    for (; done < fresh.size(); done++) {
        const Fresh& f = fresh[done];
        if (f.handle == 0) {
            continue;
        }
        const Source& source = f.source;
        const vk::Extent2D extent{static_cast<u32>(source.sharp.width) + 1,
                                  static_cast<u32>(source.sharp.height) + 1};
        const u32 first = f.streams ? FirstFor(extent, std::max(source.sharp.NumLevels(), 1u),
                                               Caps[f.used ? quality.used : quality.unused])
                                    : 0;
        if (g_allocated > limit) {
            break;
        }
        if (StartUpload(f.handle, source, f.generation, first, true, f.streams) == Begun::Wait) {
            break;
        }
    }
    std::scoped_lock lock{g_mutex};
    g_pending.erase(g_pending.begin(), g_pending.begin() + done);
    g_stats.pending = static_cast<u32>(g_pending.size());
    g_stats.not_2d = g_not_2d;
    g_stats.unsupported = g_unsupported;
    g_stats.missed_destroys = g_missed_destroys;
}

// Chooses the best quality whose images fit the textures' budget: what the device's budget leaves
// beside the rest of the process, the headroom and the spare memory.
// Textures Plan found out of use for ParkTicks with more than the parked levels.
u32 g_parkable = 0;

void Plan() {
    const Memory::Budget budget = g_memory->DeviceBudget();
    const u64 other = budget.usage > g_allocated ? budget.usage - g_allocated : 0;
    const u64 kept = other + g_memory->Headroom() + SpareBytes;
    g_limit = std::max(budget.budget > kept ? budget.budget - kept : 0, LeastBudget);
    const u32 tick = g_tick.load(std::memory_order_relaxed);
    std::array<u64, Qualities.size()> wanted{};
    u64 estimated = 0;
    u32 used_textures = 0;
    u64 used_bytes = 0;
    u64 full_bytes = 0;
    u32 reduced = 0;
    u32 parkable = 0;
    for (u32 handle = 1; handle < g_high; handle++) {
        const Residency& r = g_residency[handle];
        if (!g_resident[handle].image) {
            continue;
        }
        estimated += r.bytes_from[r.first];
        full_bytes += r.bytes_from[0];
        if (!r.streams) {
            for (u64& bytes : wanted) {
                bytes += r.bytes_from[r.first];
            }
            continue;
        }
        reduced += r.first != 0;
        const u32 age = tick - g_sampled[handle].used.load(std::memory_order_relaxed);
        const bool used = age <= UsedTicks;
        parkable += age > ParkTicks && r.first < r.first_at[Parked];
        used_textures += used;
        used_bytes += used ? r.bytes_from[0] : 0;
        for (u32 q = 0; q < Qualities.size(); q++) {
            wanted[q] += r.bytes_from[r.first_at[CapOf(Qualities[q], age)]];
        }
    }
    // The estimates leave out the driver's alignment and padding.
    g_scale = estimated != 0 && g_stats.resident_bytes > estimated
                  ? static_cast<double>(g_stats.resident_bytes) / static_cast<double>(estimated)
                  : 1.0;
    u32 quality = Qualities.size() - 1;
    for (u32 q = 0; q < Qualities.size(); q++) {
        const u64 margin = q < g_quality ? RaiseMargin : 0;
        if (static_cast<double>(wanted[q]) * g_scale + static_cast<double>(margin) <=
            static_cast<double>(g_limit)) {
            quality = q;
            break;
        }
    }
    if (!g_streaming) {
        quality = 0;
    }
    if (quality != g_quality) {
        LOG_INFO(Render_Vulkan,
                 "GR2port textures: quality {} (textures in use up to {} px, others {} px): "
                 "budget {} MB, held {} MB, wanted {:.0f} MB, {} MB of the process's {} MB is "
                 "not textures",
                 quality, Caps[Qualities[quality].used], Caps[Qualities[quality].unused],
                 g_limit >> 20, g_allocated >> 20,
                 static_cast<double>(wanted[quality]) * g_scale / (1 << 20), other >> 20,
                 budget.usage >> 20);
        g_quality = quality;
    }
    g_stats.limit_bytes = g_limit;
    g_stats.allocated_bytes = g_allocated;
    g_stats.quality = g_quality;
    g_stats.used_textures = used_textures;
    g_stats.used_bytes = used_bytes;
    g_stats.full_bytes = full_bytes;
    g_stats.reduced = reduced;
    g_parkable = parkable;
}

// Moves each texture toward the levels the quality gives it: smaller images by copies on the GPU,
// larger ones by uploads, the textures read most recently first, while the budget has room.
void Converge(vk::CommandBuffer cmd, u64 frame) {
    // With every level of every texture and none to park, nothing changes.
    if (!g_streaming || (g_quality == 0 && g_stats.reduced == 0 && g_parkable == 0)) {
        return;
    }
    static std::vector<Change> shrinks;
    static std::vector<Change> grows;
    shrinks.clear();
    grows.clear();
    const u32 tick = g_tick.load(std::memory_order_relaxed);
    const Quality quality = Qualities[g_quality];
    for (u32 handle = 1; handle < g_high; handle++) {
        const Residency& r = g_residency[handle];
        const Texture& t = g_textures[handle];
        if (!r.streams || r.uploading || r.old.image || r.installed_frame == frame ||
            !g_resident[handle].image ||
            g_sampled[handle].object.load(std::memory_order_relaxed) == 0) {
            continue;
        }
        const u32 used = g_sampled[handle].used.load(std::memory_order_relaxed);
        const u32 age = tick - used;
        const u32 first = r.first_at[CapOf(quality, age)];
        // A texture out of use but not shown gets larger levels only when the others may.
        if (first > r.first && shrinks.size() < ShrinksPerFrame) {
            shrinks.push_back({handle, first, used});
        } else if (first < r.first && (age <= ShownTicks || r.first_at[quality.unused] < r.first)) {
            grows.push_back({handle, first, used});
        }
    }
    RecordShrinks(cmd, frame, shrinks);
    if (grows.empty()) {
        return;
    }
    std::ranges::sort(grows, [](const Change& a, const Change& b) {
        const u64 a_bytes = g_residency[a.handle].bytes_from[a.first];
        const u64 b_bytes = g_residency[b.handle].bytes_from[b.first];
        return a.used != b.used ? a.used > b.used : a_bytes < b_bytes;
    });
    if (grows.size() > GrowsPerFrame) {
        grows.resize(GrowsPerFrame);
    }
    struct Grow {
        Source source;
        u32 generation;
        bool live;
    };
    static std::vector<Grow> sources;
    sources.clear();
    {
        std::scoped_lock lock{g_mutex};
        for (const Change& grow : grows) {
            sources.push_back({
                g_sources[grow.handle],
                g_generations[grow.handle],
                g_sampled[grow.handle].object.load(std::memory_order_relaxed) != 0,
            });
        }
    }
    for (size_t i = 0; i < grows.size(); i++) {
        const Residency& r = g_residency[grows[i].handle];
        const double bytes = static_cast<double>(r.bytes_from[grows[i].first]) * g_scale;
        if (static_cast<double>(g_allocated) + bytes > static_cast<double>(g_limit)) {
            break;
        }
        if (!sources[i].live) {
            continue;
        }
        if (StartUpload(grows[i].handle, sources[i].source, sources[i].generation, grows[i].first,
                        false, true) == Begun::Wait) {
            break;
        }
    }
}

} // namespace

#include "graphics/gr2port/textures_capture_access.inc"

void Refresh(vk::CommandBuffer cmd, u64 frame, std::span<const u32> read) {
    const auto& dl = g_device->dispatch;
    // A movie surface a frame read before takes each frame the game's job copies into it, whether
    // a frame reads it then or later.
    constexpr u32 MaxRead = std::max(Scene::MaxUiDynamicTextures, Scene::MaxDynamicTextures);
    std::array<u32, MaxRead + Movies::Surfaces> handles;
    const auto first = std::min<std::size_t>(read.size(), MaxRead);
    // The frames name textures by descriptor: a handle or its twin.
    std::transform(read.begin(), read.begin() + first, handles.begin(),
                   [](u32 slot) { return slot & (Capacity - 1); });
    u32 count = static_cast<u32>(first);
    for (u32 slot = 0; slot < Movies::Surfaces; slot++) {
        const u32 handle = g_movie_handles[slot];
        if (handle == 0) {
            continue;
        }
        const Movies::Job job = Movies::Peek(slot);
        if (job.generation == g_refreshed[handle] ||
            g_textures[handle].address.load(std::memory_order_relaxed) != job.surface) {
            continue;
        }
        if (std::find(handles.begin(), handles.begin() + count, handle) ==
            handles.begin() + count) {
            handles[count++] = handle;
        }
    }
    for (const u32 handle : std::span{handles}.first(count)) {
        const Image& image = g_resident[handle];
        if (handle == 0 || handle >= FirstReserved || !image.image ||
            !g_sampled[handle].ready.load(std::memory_order_acquire)) {
            continue;
        }
        AmdGpu::Image sharp;
        {
            std::scoped_lock lock{g_mutex};
            sharp = g_sources[handle].sharp;
        }
        const Layout layout = ReadLayout(sharp, 0);
        const VideoCore::MipInfo& info = layout.mips[0];
        if (Tiled(layout.tile_mode) || layout.levels != 1 || layout.layers != 1 ||
            layout.image_extent != image.extent ||
            u64{info.pitch} * image.extent.height > layout.bytes) {
            continue;
        }
        // A movie's surface holds a byte per texel. A texture of another kind at a surface's
        // address is one the game put there after the movie, and takes its own memory.
        const bool surface = layout.bits == 8 && !layout.block;
        // A surface that keeps a movie's frame takes, once per DMA, the copy of the frame the DMA
        // copied, taken when the game kept it, as only the GPU writes its memory. Without a copy
        // it takes the image of the source when that holds the source's last job.
        const Movies::Kept kept = surface ? Movies::FindKept(sharp.Address()) : Movies::Kept{};
        Movies::Frame movie{};
        if (kept.source != 0) {
            if (kept.generation == g_refreshed[handle]) {
                continue;
            }
            const bool copied =
                !kept.frame.found && CopyKept(cmd, handle, kept.source, Movies::Find(kept.source));
            if (g_logged_kept < LoggedKept) {
                g_logged_kept++;
                LOG_INFO(Render_Vulkan, "GR2port movies: kept surface {:#x} filled {}",
                         sharp.Address(),
                         kept.frame.found ? "from the frame copied at the keep"
                                          : (copied ? "from its source's image" : "later"));
            }
            if (copied) {
                g_refreshed[handle] = kept.generation;
                continue;
            }
            if (!kept.frame.found) {
                continue;
            }
            movie = kept.frame;
        } else if (surface) {
            // A movie surface takes the copy of each frame the game's job copies into it.
            movie = Movies::Find(sharp.Address());
            if (movie.found) {
                g_movie_handles[movie.slot] = handle;
            }
            if (movie.found && movie.generation == g_refreshed[handle]) {
                continue;
            }
        }
        Staging staging{
            .buffer = g_memory->CreateBuffer(layout.bytes, vk::BufferUsageFlagBits::eTransferSrc,
                                             Placement::Staging),
            .frame = frame,
        };
        if (staging.buffer.mapped == nullptr) {
            g_memory->Destroy(staging.buffer);
            continue;
        }
        if (movie.found) {
            Movies::Write(movie, staging.buffer.mapped, info.pitch, image.extent.height);
            // Two new frames of the surface during the copy reuse its buffer: the next refresh
            // takes the newest.
            const u32 now = kept.source != 0 ? Movies::FindKept(sharp.Address()).generation
                                             : Movies::Find(sharp.Address()).generation;
            if (now != movie.generation) {
                g_memory->Destroy(staging.buffer);
                continue;
            }
            g_refreshed[handle] = movie.generation;
        } else {
            std::memcpy(staging.buffer.mapped,
                        reinterpret_cast<const void*>(sharp.Address() + layout.base), layout.bytes);
        }
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        const auto samplers =
            vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader;
        vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = samplers,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = image.image,
            .subresourceRange = range,
        };
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier}, dl);
        const vk::BufferImageCopy copy{
            .bufferOffset = info.offset - layout.base,
            .bufferRowLength = info.pitch,
            .bufferImageHeight = info.height,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageExtent = {image.extent.width, image.extent.height, 1},
        };
        cmd.copyBufferToImage(staging.buffer.buffer, image.image,
                              vk::ImageLayout::eTransferDstOptimal, copy, dl);
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = samplers;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier}, dl);
        g_staging.push_back(staging);
    }
}

void SetExternal(u32 handle, vk::ImageView view) {
    WriteDescriptor(handle, view);
}

bool Start(const Device& device, Memory& memory) {
    g_device = &device;
    g_memory = &memory;
    if (!CreateSet(device) || !CreateDetile(device)) {
        LOG_ERROR(Render_Vulkan, "GR2port: the texture array or the detile pipelines failed");
        return false;
    }
    g_white = memory.CreateTexture(vk::Format::eR8G8B8A8Unorm, {1, 1}, 1, {});
    if (!g_white.image) {
        return false;
    }
    WriteDescriptor(0, g_white.view);
    g_worker = new Worker;
    std::thread{Work}.detach();
    return true;
}

void Update(vk::CommandBuffer cmd, u64 frame, u64 completed) {
    if (!g_white_cleared) {
        g_white_cleared = true;
        ClearWhite(cmd);
    }
    const auto now = std::chrono::steady_clock::now();
    const bool ticked = now - g_tick_time >= TickPeriod;
    if (ticked) {
        g_tick_time = now;
        g_tick.fetch_add(1, std::memory_order_relaxed);
    }
    Retire(frame, completed);
    FinishUploads(cmd, frame);
    if (ticked) {
        Plan();
    }
    StartNew();
    Converge(cmd, frame);
}

vk::DescriptorSetLayout SetLayout() {
    return g_set_layout;
}

vk::DescriptorSet Set() {
    return g_set;
}

Stats TakeStats() {
    Stats stats = g_stats;
    g_stats.uploaded_bytes = 0;
    g_stats.uploaded_data_bytes = 0;
    g_stats.shrinks = 0;
    g_stats.grows = 0;
    g_stats.grow_bytes = 0;
    return stats;
}

bool Squeezed() {
    return g_streaming && Qualities[g_quality].used != 0;
}

} // namespace Gr2Port::Textures
