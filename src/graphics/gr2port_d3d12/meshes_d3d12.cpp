// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "common/logging/log.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/meshes.h"
#include "graphics/gr2port/scene_ring.h"
#include "graphics/gr2port/settings.h"
#include "graphics/gr2port/textures.h"
#include "graphics/gr2port_d3d12/residency_context.h"

namespace Gr2Port::Meshes {
namespace {
using namespace D3D12;
#include "graphics/gr2port/meshes_capture_state.inc"
Stats g_stats{};
std::atomic<u32> g_seconds{0};
const auto g_start = std::chrono::steady_clock::now();
ResidencyContext* g_context = nullptr;
ResourcePtr g_index_buffer;
std::map<u32, u32> g_free_indices;
D3D12::Residency::Ranges g_geometry;
struct Resident {
    u32 offset = 0, bytes = 0, first_index = 0, indices = 0;
    bool skinned = false, cloth = false;
    std::unique_ptr<ChunkBox[]> chunk_tree;
};
std::array<Resident, Capacity> g_resident;
struct Retiring {
    u32 handle;
    u64 fence;
    bool eviction;
};
std::vector<Retiring> g_retiring;
struct Staging {
    ResourcePtr resource;
    u64 fence;
};
std::vector<Staging> g_staging;
bool AllocateIndices(u32 count, u32& first) {
    if (count == 0) {
        first = 0;
        return true;
    }
    for (auto it = g_free_indices.begin(); it != g_free_indices.end(); ++it) {
        if (it->second < count) {
            continue;
        }
        first = it->first;
        const u32 left = it->second - count;
        g_free_indices.erase(it);
        if (left != 0) {
            g_free_indices.emplace(first + count, left);
        }
        g_stats.indices_held += count;
        return true;
    }
    return false;
}

void FreeIndices(u32 first, u32 count) {
    if (count == 0) {
        return;
    }
    g_stats.indices_held -= count;
    auto [it, inserted] = g_free_indices.emplace(first, count);
    const auto next = std::next(it);
    if (next != g_free_indices.end() && it->first + it->second == next->first) {
        it->second += next->second;
        g_free_indices.erase(next);
    }
    if (it != g_free_indices.begin()) {
        const auto previous = std::prev(it);
        if (previous->first + previous->second == it->first) {
            previous->second += it->second;
            g_free_indices.erase(it);
        }
    }
}

#include "graphics/gr2port/meshes_capture_helpers.inc"
void FreeResident(u32 handle) {
    auto& r = g_resident[handle];
    if (r.bytes) {
        g_geometry.Free(r.offset, r.bytes);
        FreeIndices(r.first_index, r.indices);
        g_stats.resident_bytes -= r.bytes + 2ull * r.indices;
        --g_stats.meshes;
        g_stats.skinned -= r.skinned;
        g_stats.cloth -= r.cloth;
        r = {};
    }
}
void Retire(u64 fence, u64 completed) {
    // Preserve the upstream capture grace period as well as the actual queue fence.
    for (u32 h : g_unloaded)
        g_retiring.push_back({h, fence + SceneRing::Slots + 2, false});
    g_unloaded.clear();
    std::erase_if(g_retiring, [completed](const Retiring& r) {
        if (!D3D12::Residency::Complete(r.fence, completed))
            return false;
        auto& mesh = g_meshes[r.handle];
        if (r.eviction && mesh.eviction.load() == Rescued && mesh.shape.load() != 0) {
            mesh.eviction = Kept;
            mesh.ready.store(true, std::memory_order_release);
            return true;
        }
        // An unload can follow an eviction; do not return its handle twice.
        FreeResident(r.handle);
        if (r.eviction)
            mesh.eviction = Evicted;
        else
            g_free.push_back(r.handle);
        return true;
    });
    std::erase_if(g_staging, [completed](const Staging& s) {
        return D3D12::Residency::Complete(s.fence, completed);
    });
}
} // namespace
#include "graphics/gr2port/meshes_capture_get.inc"
#include "graphics/gr2port/meshes_capture_install.inc"

bool Start(D3D12::ResidencyContext& context) {
    g_context = &context;
    const auto capacity =
        context.allocator->BudgetBytes() <= SmallBudget ? SmallIndexCapacity : IndexCapacity;
    g_index_buffer = context.allocator->CreateBuffer(
        capacity * 2, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ, L"GR2 shared R16 index buffer");
    if (!g_index_buffer)
        return false;
    const auto bytes = context.arenas->Capacity(D3D12::ArenaKind::Geometry);
    if (bytes <= 16 || bytes > ~u32{0})
        return false;
    // All-zero is the null pointer, so geometry offset zero is never allocated.
    g_geometry.Reset(16, static_cast<u32>(bytes) - 16);
    g_free_indices.clear();
    g_free_indices.emplace(0, static_cast<u32>(capacity));
    g_stats.index_capacity = capacity;
    return true;
}
void Update(D3D12::CommandList& cmd, u64 fence, u64 completed) {
    if (!g_context)
        return;
    std::scoped_lock lock{g_mutex};
    Retire(fence, completed);
    const auto seconds = static_cast<u32>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_start)
            .count());
    g_seconds = seconds;
    if (fence % ScanFrames == 0 && Textures::Squeezed()) {
        u32 count = 0;
        for (u32 h = 0; h < g_unused && count < EvictsPerScan; ++h) {
            auto& m = g_meshes[h];
            auto& r = g_resident[h];
            if (!m.ready.load() || r.skinned || r.cloth || !r.bytes ||
                seconds - m.used.load() < EvictSeconds)
                continue;
            m.ready = false;
            m.eviction = Evicting;
            g_retiring.push_back({h, fence + SceneRing::Slots + 2, true});
            ++count;
        }
    }
    u64 budget = UploadBudget;
    std::vector<u32> uploaded;
    auto* geometry = g_context->arenas->Resource(D3D12::ArenaKind::Geometry);
    bool copying = false;
    while (!g_pending.empty() && budget) {
        const auto handle = g_pending.front();
        g_pending.pop_front();
        auto& mesh = g_meshes[handle];
        const auto shape = mesh.shape.load();
        if (!shape || g_resident[handle].bytes)
            continue;
        Source source{};
        if (!ReadShape(shape, source) || !source.stride) {
            ++g_stats.skipped;
            continue;
        }
        const u32 vertex_count = source.vertex_bytes / source.stride;
        u32 max_index = 0;
        if (source.index_bytes) {
            const auto* indices = reinterpret_cast<const u16*>(source.indices);
            max_index = *std::max_element(indices, indices + source.index_bytes / 2);
        }
        if (!D3D12::Residency::ValidIndices(max_index, vertex_count,
                                            source.skin ? source.skin_bytes / SkinStride : ~0u) ||
            (source.index_bytes & 1u) ||
            (source.position & 255u) + ((source.position >> 8) == 7 ? 8u : 12u) > source.stride) {
            ++g_stats.skipped;
            ++g_stats.bad_indices;
            continue;
        }
        const bool cloth = source.particles && ValidSoftBody(source, vertex_count);
        g_stats.bad_cloth += source.particles && !cloth;
        const auto align = [](u64 value) { return D3D12::Residency::Align(value, 16); };
        const u64 skin_offset = align(source.vertex_bytes);
        const u64 skin_end = source.skin ? skin_offset + source.skin_bytes : source.vertex_bytes;
        const u64 particles_offset = align(skin_end);
        const u64 constraints_offset = align(particles_offset + source.particle_bytes);
        const u64 map_offset = align(constraints_offset + source.constraint_bytes);
        const u64 region =
            align(cloth ? map_offset + D3D12::Residency::Align(source.map_bytes, 4) : skin_end);
        const u64 bytes = region + D3D12::Residency::Align(source.index_bytes, 4);
        if (bytes > UploadBudget || region > ~u32{0}) {
            ++g_stats.skipped;
            continue;
        }
        if (bytes > budget) {
            g_pending.push_front(handle);
            break;
        }
        u32 first_index = 0, offset = 0;
        if (!AllocateIndices(source.index_bytes / 2, first_index)) {
            g_pending.push_front(handle);
            break;
        }
        if (!g_geometry.Allocate(static_cast<u32>(region), offset)) {
            FreeIndices(first_index, source.index_bytes / 2);
            g_pending.push_front(handle);
            break;
        }
        UploadBuffer upload;
        if (!upload.Create(*g_context, bytes)) {
            FreeIndices(first_index, source.index_bytes / 2);
            g_geometry.Free(offset, region);
            g_pending.push_front(handle);
            break;
        }
        std::memset(upload.cpu, 0, bytes);
        std::memcpy(upload.cpu, reinterpret_cast<const void*>(source.vertices),
                    source.vertex_bytes);
        if (source.skin)
            std::memcpy(upload.cpu + skin_offset, reinterpret_cast<const void*>(source.skin),
                        source.skin_bytes);
        if (cloth) {
            std::memcpy(upload.cpu + particles_offset,
                        reinterpret_cast<const void*>(source.particles), source.particle_bytes);
            std::memcpy(upload.cpu + constraints_offset,
                        reinterpret_cast<const void*>(source.constraints), source.constraint_bytes);
            std::memcpy(upload.cpu + map_offset, reinterpret_cast<const void*>(source.particle_map),
                        source.map_bytes);
        }
        if (source.index_bytes)
            std::memcpy(upload.cpu + region, reinterpret_cast<const void*>(source.indices),
                        source.index_bytes);
        upload.Unmap();
        if (!copying) {
            cmd.Transition(geometry, D3D12_RESOURCE_STATE_GENERIC_READ,
                           D3D12_RESOURCE_STATE_COPY_DEST);
            cmd.Transition(g_index_buffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                           D3D12_RESOURCE_STATE_COPY_DEST);
            copying = true;
        }
        cmd.list->CopyBufferRegion(geometry, offset, upload.resource.Get(), 0, region);
        if (source.index_bytes)
            cmd.list->CopyBufferRegion(g_index_buffer.Get(), 2ull * first_index,
                                       upload.resource.Get(), region, source.index_bytes);
        const u64 address =
            D3D12::Residency::Pack(static_cast<u32>(D3D12::ArenaKind::Geometry), offset);
        mesh.vertices = address;
        mesh.first_index = first_index;
        mesh.indices = source.index_bytes / 2;
        mesh.stride = source.stride;
        mesh.vertex_bytes = source.vertex_bytes;
        mesh.position = source.position;
        mesh.texcoord = source.texcoord;
        mesh.texcoord1 = source.texcoord1;
        mesh.normal = source.normal;
        mesh.tangent = source.tangent;
        mesh.vertex_color = source.vertex_color;
        SetBox(source, vertex_count, mesh);
        SetWeightSum(source, mesh);
        auto tree = BuildChunks(source, mesh, mesh.chunks);
        mesh.chunk_tree = tree.get();
        mesh.skin = source.skin ? address + skin_offset : 0;
        mesh.particles = cloth ? address + particles_offset : 0;
        mesh.constraints = cloth ? address + constraints_offset : 0;
        mesh.particle_map = cloth ? address + map_offset : 0;
        mesh.particle_count = cloth ? source.particle_count : 0;
        g_resident[handle] = {offset,         static_cast<u32>(region), first_index,
                              mesh.indices,   source.skin != 0,         cloth,
                              std::move(tree)};
        g_staging.push_back({std::move(upload.resource), fence});
        ++g_stats.meshes;
        g_stats.skinned += source.skin != 0;
        g_stats.cloth += cloth;
        g_stats.no_texcoord += source.texcoord == 0;
        g_stats.no_normal += source.normal == 0;
        g_stats.uploaded_bytes += bytes;
        g_stats.resident_bytes += region + source.index_bytes;
        budget -= bytes;
        uploaded.push_back(handle);
    }
    if (copying) {
        cmd.Transition(geometry, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
        cmd.Transition(g_index_buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_GENERIC_READ);
    }
    for (auto h : uploaded)
        g_meshes[h].ready.store(true, std::memory_order_release);
    g_stats.pending = g_pending.size();
    g_stats.missed_unloads = g_missed_unloads;
    g_stats.lost_handles = g_lost_handles;
}
ID3D12Resource* IndexBuffer() {
    return g_index_buffer.Get();
}
Stats TakeStats() {
    auto result = g_stats;
    g_stats.uploaded_bytes = 0;
    g_stats.skipped = 0;
    g_stats.bad_indices = 0;
    g_stats.bad_cloth = 0;
    return result;
}
void Stop() {
    std::scoped_lock lock{g_mutex};
    for (u32 h = 0; h < g_unused; ++h) {
        g_meshes[h].ready = false;
        FreeResident(h);
    }
    g_staging.clear();
    g_retiring.clear();
    g_index_buffer.Reset();
    g_context = nullptr;
    // Game-owned handle identities survive a presenter restart; requeue live shapes.
    g_pending.clear();
    g_unloaded.clear();
    g_free.clear();
    for (u32 h = 0; h < g_unused; ++h)
        if (g_meshes[h].shape.load())
            g_pending.push_back(h);
        else
            g_free.push_back(h);
}
} // namespace Gr2Port::Meshes
