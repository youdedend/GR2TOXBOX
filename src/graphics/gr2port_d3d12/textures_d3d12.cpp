// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <span>
#include <vector>
#include "common/logging/log.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/movies.h"
#include "graphics/gr2port/scene.h"
#include "graphics/gr2port/scene_ring.h"
#include "graphics/gr2port/textures.h"
#include "graphics/gr2port_d3d12/residency_context.h"
#include "graphics/gr2port_d3d12/texture_layout.h"

namespace Gr2Port::Textures {
namespace {
using namespace D3D12;
#include "graphics/gr2port/textures_capture_checks.inc"
#include "graphics/gr2port/textures_capture_constants.inc"
#include "graphics/gr2port/textures_capture_state.inc"
bool Convertible(const AmdGpu::Image& sharp) {
    TextureLayout layout;
    return PlanTexture(sharp, 0, layout);
}
#include "graphics/gr2port/textures_capture_hooks.inc"
ResidencyContext* g_context = nullptr;
Pipeline g_detile;
Stats g_stats{};
struct Resident {
    ResourcePtr images[2];
    u64 bytes[2]{};
    D3D12::Residency::Twin twin;
    u32 first = 0, generation = 0;
    u32 movie_generation = 0;
    u64 refreshed = 0;
};
std::array<Resident, Capacity> g_resident;
struct Temporary {
    ResourcePtr source, detiled, repack;
    DescriptorSlot input{D3D12::DescriptorSlot::kInvalid}, output{D3D12::DescriptorSlot::kInvalid};
    u64 fence;
};
std::vector<Temporary> g_temporary;
struct Retiring {
    u32 handle;
    u64 fence;
};
std::vector<Retiring> g_retiring;
ResourcePtr g_white;
bool g_white_uploaded = false;
bool g_squeezed = false;
u64 g_uploaded_this_frame = 0;
std::array<u32, Movies::Surfaces> g_movie_handles{};
std::chrono::steady_clock::time_point g_tick_time{};
constexpr auto ShaderRead = static_cast<D3D12_RESOURCE_STATES>(
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
static_assert(Capacity == D3D12::Residency::TextureCapacity);
static_assert(FirstReserved == D3D12::Residency::FirstReserved);
static_assert(FirstReserved + Capacity == D3D12::Residency::TextureDescriptorEnd);

D3D12_SHADER_RESOURCE_VIEW_DESC View(const TextureLayout& layout) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = layout.desc.Format;
    desc.Shader4ComponentMapping = layout.mapping;
    if (layout.layers == 6) {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MipLevels = layout.levels;
    } else {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = layout.levels;
    }
    return desc;
}
// Called under the same lock that destroys the guest's source memory.
bool Upload(CommandList& cmd, u32 handle, u64 fence, bool refresh) {
    auto& resident = g_resident[handle];
    if (resident.refreshed == fence || (!refresh && !resident.twin.CanRotate(g_context->completed)))
        return false;
    const auto& source = g_sources[handle];
    TextureLayout layout;
    u32 first = refresh ? resident.first : 0;
    while (true) {
        if (!PlanTexture(source.sharp, first, layout)) {
            ++g_unsupported;
            return false;
        }
        const u64 available =
            g_context->allocator->BudgetBytes() -
            std::min(g_context->allocator->BudgetBytes(), g_context->allocator->CommittedBytes());
        if ((layout.source_bytes + layout.copy_bytes <= FinishBytes &&
             (refresh || layout.copy_bytes + 2ull * layout.source_bytes + 64_KB <= available)) ||
            first + 1 == source.sharp.NumLevels())
            break;
        g_squeezed = true;
        ++first;
    }
    if (layout.source_bytes + layout.copy_bytes > FinishBytes) {
        ++g_stats.too_large;
        return false;
    }
    if (g_uploaded_this_frame + layout.source_bytes > FinishBytes)
        return false;
    // Use the SDK's exact footprints on the target device; the portable planner
    // and its tests also enforce row/block and 256/512-byte alignment contracts.
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(layout.copies.size());
    std::vector<UINT> rows(layout.copies.size());
    std::vector<UINT64> row_bytes(layout.copies.size());
    UINT64 total = 0;
    g_context->device->device->GetCopyableFootprints(&layout.desc, 0, layout.copies.size(), 0,
                                                     footprints.data(), rows.data(),
                                                     row_bytes.data(), &total);
    if (!total || total == ~UINT64{0}) {
        ++g_unsupported;
        return false;
    }
    for (size_t i = 0; i < layout.copies.size(); ++i) {
        if (rows[i] != layout.copies[i].rows || row_bytes[i] != layout.copies[i].row_bytes ||
            footprints[i].Offset % 512 || footprints[i].Footprint.RowPitch % 256) {
            ++g_unsupported;
            return false;
        }
        layout.copies[i].footprint = footprints[i];
    }
    layout.copy_bytes = D3D12::Residency::Align(total, 512);
    const auto address = source.sharp.Address();
    if (source.end < address ||
        u64(layout.source_base) + layout.source_bytes > source.end - address) {
        ++g_stats.too_large;
        return false;
    }
    const auto inactive = resident.twin.active ^ 1u;
    // The inactive descriptor's last consumer has completed; only now may its
    // image and descriptor storage be reclaimed/reused.
    if (!refresh && resident.images[inactive]) {
        g_stats.allocated_bytes -= resident.bytes[inactive];
        resident.images[inactive].Reset();
        resident.bytes[inactive] = 0;
    }
    ResourcePtr image;
    if (!refresh)
        image = g_context->allocator->CreateResource(layout.desc, D3D12_HEAP_TYPE_DEFAULT,
                                                     D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource* destination =
        refresh ? resident.images[resident.twin.active].Get() : image.Get();
    if (!destination) {
        g_squeezed = true;
        ++g_stats.failed;
        return false;
    }
    UploadBuffer staging;
    if (layout.tiled) {
        if (!staging.Create(*g_context, layout.source_bytes)) {
            ++g_stats.failed;
            return false;
        }
        std::memcpy(staging.cpu, reinterpret_cast<const void*>(address + layout.source_base),
                    layout.source_bytes);
        staging.Unmap();
    }
    ResourcePtr detiled, repack;
    DescriptorSlot input{D3D12::DescriptorSlot::kInvalid}, output{D3D12::DescriptorSlot::kInvalid};
    if (layout.tiled) {
        detiled = g_context->allocator->CreateBuffer(layout.source_bytes, D3D12_HEAP_TYPE_DEFAULT,
                                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        repack = g_context->allocator->CreateBuffer(layout.copy_bytes, D3D12_HEAP_TYPE_DEFAULT,
                                                    D3D12_RESOURCE_FLAG_NONE,
                                                    D3D12_RESOURCE_STATE_COPY_DEST);
        if (!detiled || !repack) {
            ++g_stats.failed;
            return false;
        }
        input = g_context->heap->AllocateSrv();
        output = g_context->heap->AllocateSrv();
        if (!input || !output) {
            if (input)
                g_context->heap->RecycleSrv(input);
            if (output)
                g_context->heap->RecycleSrv(output);
            ++g_stats.failed;
            return false;
        }
        g_context->heap->WriteBuffer(input, staging.resource.Get(), 0,
                                     (layout.source_bytes + 3) / 4, 0);
        g_context->heap->WriteBufferUav(output, detiled.Get(), 0, (layout.source_bytes + 3) / 4, 0);
    } else {
        UploadBuffer linear;
        if (!linear.Create(*g_context, layout.copy_bytes)) {
            ++g_stats.failed;
            return false;
        }
        // Source uploads are CPU-write-only; copying from guest again avoids
        // reading write-combined memory on this CPU path.
        for (const auto& copy : layout.copies) {
            const auto src =
                reinterpret_cast<const u8*>(address + layout.source_base + copy.source_offset);
            for (u32 row = 0; row < copy.rows; ++row)
                std::memcpy(linear.cpu + copy.footprint.Offset +
                                u64(row) * copy.footprint.Footprint.RowPitch,
                            src + u64(row) * copy.source_pitch, copy.row_bytes);
        }
        // Convert movies into an ordinary owned CPU buffer before row repacking.
        auto movie = Movies::Find(address);
        if (!movie.found) {
            auto kept = Movies::FindKept(address);
            if (kept.source)
                movie = kept.frame;
        }
        if (movie.found && layout.levels == 1 && layout.layers == 1) {
            if (movie.slot < Movies::Surfaces)
                g_movie_handles[movie.slot] = handle;
            const auto& copy = layout.copies[0];
            std::vector<u8> pixels(layout.source_bytes);
            resident.movie_generation = movie.generation;
            Movies::Write(movie, pixels.data(), copy.source_pitch,
                          layout.source_bytes / copy.source_pitch);
            for (u32 row = 0; row < copy.rows; ++row)
                std::memcpy(linear.cpu + copy.footprint.Offset +
                                u64(row) * copy.footprint.Footprint.RowPitch,
                            pixels.data() + u64(row) * copy.source_pitch, copy.row_bytes);
        }
        linear.Unmap();
        repack = std::move(linear.resource);
    }
    auto* raw_source = staging.resource.Get();
    auto* raw_detiled = detiled.Get();
    auto* raw_repack = repack.Get();
    // Every command-referenced temporary is retained before the first dispatch.
    g_temporary.push_back(
        {std::move(staging.resource), std::move(detiled), std::move(repack), input, output, fence});
    if (layout.tiled) {
        for (const auto& copy : layout.copies) {
            if (!DispatchDetile(cmd, g_detile, *g_context->root, *g_context->heap, copy.detile,
                                {input.index, output.index, copy.source_offset, copy.source_offset},
                                layout.source_bytes, layout.source_bytes)) {
                // Even a failed validation after prior dispatches must retain the destination.
                g_temporary.push_back({std::move(image),
                                       {},
                                       {},
                                       {D3D12::DescriptorSlot::kInvalid},
                                       {D3D12::DescriptorSlot::kInvalid},
                                       fence});
                return false;
            }
        }
        cmd.Transition(raw_detiled, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
        for (const auto& copy : layout.copies)
            for (u32 row = 0; row < copy.rows; ++row)
                cmd.list->CopyBufferRegion(
                    raw_repack,
                    copy.footprint.Offset + u64(row) * copy.footprint.Footprint.RowPitch,
                    raw_detiled, copy.source_offset + u64(row) * copy.source_pitch, copy.row_bytes);
        cmd.Transition(raw_repack, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    (void)raw_source;
    // Dynamic linear images keep their descriptor: queue-ordered copy barriers
    // serialize writes after prior draws. No CPU descriptor overwrite is needed.
    if (refresh)
        cmd.Transition(destination, ShaderRead, D3D12_RESOURCE_STATE_COPY_DEST);
    for (const auto& copy : layout.copies) {
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource = raw_repack;
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint = copy.footprint;
        D3D12_TEXTURE_COPY_LOCATION to{};
        to.pResource = destination;
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.SubresourceIndex = copy.subresource;
        const D3D12_BOX box{0, 0, 0, copy.width, copy.height, 1};
        cmd.list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
    }
    cmd.Transition(destination, D3D12_RESOURCE_STATE_COPY_DEST, ShaderRead);
    g_uploaded_this_frame += layout.source_bytes;
    if (refresh) {
        resident.refreshed = fence;
        g_stats.uploaded_bytes += layout.source_bytes;
        return true;
    }
    const auto desc = View(layout);
    const u32 slot = handle + inactive * Capacity;
    g_context->heap->WriteTexture({slot}, image.Get(), desc);
    const bool fresh = !resident.images[resident.twin.active];
    resident.images[inactive] = std::move(image);
    resident.bytes[inactive] = resident.images[inactive].Bytes();
    g_stats.allocated_bytes += resident.bytes[inactive];
    if (fresh)
        ++g_stats.textures;
    resident.twin.Rotate(fence + SceneRing::Slots + 2);
    resident.first = first;
    resident.generation = g_generations[handle];
    resident.refreshed = fence;
    g_sampled[handle].slot.store(slot, std::memory_order_release);
    g_sampled[handle].ready.store(true, std::memory_order_release);
    g_stats.uploaded_bytes += layout.source_bytes;
    g_stats.uploaded_data_bytes += layout.source_bytes;
    g_stats.reduced += fresh && first != 0;
    return true;
}
void Collect(u64 fence, u64 completed) {
    std::erase_if(g_temporary, [completed](Temporary& t) {
        if (!D3D12::Residency::Complete(t.fence, completed))
            return false;
        if (t.input)
            g_context->heap->RecycleSrv(t.input);
        if (t.output)
            g_context->heap->RecycleSrv(t.output);
        return true;
    });
    for (auto h : g_destroyed)
        g_retiring.push_back({h, fence + SceneRing::Slots + 2});
    g_destroyed.clear();
    std::erase_if(g_retiring, [completed](const Retiring& r) {
        if (!D3D12::Residency::Complete(r.fence, completed))
            return false;
        auto& resident = g_resident[r.handle];
        if (resident.images[resident.twin.active])
            --g_stats.textures;
        for (u32 i = 0; i < 2; ++i)
            g_stats.allocated_bytes -= resident.bytes[i];
        resident = {};
        g_free.push_back(r.handle);
        return true;
    });
}
} // namespace
#include "graphics/gr2port/textures_capture_access.inc"
bool Start(D3D12::ResidencyContext& context) {
    g_context = &context;
    if (context.heap->SrvDescriptorsUsed() < D3D12::Residency::TextureDescriptorEnd)
        return false;
    if (!CreateDetilePipeline(context.device->device.Get(), *context.root, g_detile))
        return false;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = 1;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    g_white = context.allocator->CreateResource(desc, D3D12_HEAP_TYPE_DEFAULT,
                                                D3D12_RESOURCE_STATE_COPY_DEST);
    if (!g_white)
        return false;
    TextureLayout layout{};
    layout.desc = desc;
    layout.mapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    layout.levels = layout.layers = 1;
    const auto view = View(layout);
    context.heap->WriteTexture({0}, g_white.Get(), view);
    g_stats.limit_bytes = context.allocator->BudgetBytes();
    return true;
}
void Update(D3D12::CommandList& cmd, u64 fence, u64 completed) {
    if (!g_context)
        return;
    g_context->completed = completed;
    g_uploaded_this_frame = 0;
    std::unique_lock lock{g_mutex};
    Collect(fence, completed);
    if (!g_white_uploaded) {
        UploadBuffer white;
        if (!white.Create(*g_context, 256))
            return;
        std::memset(white.cpu, 255, 256);
        white.Unmap();
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource = white.resource.Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 256};
        D3D12_TEXTURE_COPY_LOCATION to{};
        to.pResource = g_white.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cmd.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        cmd.Transition(g_white.Get(), D3D12_RESOURCE_STATE_COPY_DEST, ShaderRead);
        g_temporary.push_back({std::move(white.resource),
                               {},
                               {},
                               {D3D12::DescriptorSlot::kInvalid},
                               {D3D12::DescriptorSlot::kInvalid},
                               fence});
        g_white_uploaded = true;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - g_tick_time >= TickPeriod) {
        g_tick_time = now;
        g_tick.fetch_add(1);
    }
    const auto pending = g_pending.size();
    u32 attempts = 0;
    const u64 before = g_stats.uploaded_bytes;
    for (size_t i = 0;
         i < pending && attempts < 2 && g_stats.uploaded_bytes - before < FinishBytes / 2; ++i) {
        const auto h = g_pending.front();
        g_pending.pop_front();
        if (!g_sampled[h].object.load() || g_sampled[h].ready.load())
            continue;
        ++attempts;
        if (!Upload(cmd, h, fence, false))
            g_pending.push_back(h);
    }
    g_stats.pending = g_pending.size();
    g_stats.not_2d = g_not_2d;
    g_stats.unsupported = g_unsupported;
    g_stats.missed_destroys = g_missed_destroys;
    g_stats.resident_bytes = g_stats.allocated_bytes;
    lock.unlock();
    Refresh(cmd, fence, g_movie_handles);
}
void Refresh(D3D12::CommandList& cmd, u64 fence, std::span<const u32> descriptors) {
    if (!g_context)
        return;
    std::scoped_lock lock{g_mutex};
    for (auto slot : descriptors) {
        const u32 h = slot & (Capacity - 1);
        if (!h || h >= FirstReserved || !g_sampled[h].ready.load() || !g_textures[h].linear.load())
            continue;
        auto movie = Movies::Find(g_textures[h].address.load());
        if (!movie.found) {
            auto kept = Movies::FindKept(g_textures[h].address.load());
            if (kept.source)
                movie = kept.frame;
        }
        if (movie.found && movie.generation == g_resident[h].movie_generation)
            continue;
        Upload(cmd, h, fence, true);
    }
}
void SetExternal(u32 handle, ID3D12Resource* resource,
                 const D3D12_SHADER_RESOURCE_VIEW_DESC& desc) {
    // Caller owns the resource and must wait for all users before replacing an
    // external view. Reserved UI/ImGui handles do not consume texture twins.
    if (g_context && handle >= FirstReserved && handle < Capacity)
        g_context->heap->WriteTexture({handle}, resource, desc);
}
Stats TakeStats() {
    auto stats = g_stats;
    g_stats.uploaded_bytes = 0;
    g_stats.uploaded_data_bytes = 0;
    return stats;
}
bool Squeezed() {
    return g_squeezed;
}
void Stop() {
    std::scoped_lock lock{g_mutex};
    if (g_context)
        for (auto& t : g_temporary) {
            if (t.input)
                g_context->heap->RecycleSrv(t.input);
            if (t.output)
                g_context->heap->RecycleSrv(t.output);
        }
    g_temporary.clear();
    g_retiring.clear();
    g_pending.clear();
    g_free.clear();
    g_destroyed.clear();
    for (u32 h = 1; h < g_unused; ++h) {
        g_sampled[h].ready = false;
        g_resident[h] = {};
        if (g_sampled[h].object.load())
            g_pending.push_back(h);
        else
            g_free.push_back(h);
    }
    g_white.Reset();
    g_white_uploaded = false;
    g_detile = {};
    g_context = nullptr;
    g_stats = {};
}
} // namespace Gr2Port::Textures
