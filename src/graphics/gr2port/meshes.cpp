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
#include "graphics/gr2port/memory.h"
#include "graphics/gr2port/meshes.h"
#include "graphics/gr2port/settings.h"
#include "graphics/gr2port/textures.h"

namespace Gr2Port::Meshes {

namespace {

#include "graphics/gr2port/meshes_capture_state.inc"

// Render thread only.
struct Resident {
    Buffer vertices;
    u32 first_index = 0;
    u32 indices = 0;
    bool skinned = false;
    bool cloth = false;
    // Mesh::chunk_tree, which lives as long as the mesh's buffers.
    std::unique_ptr<ChunkBox[]> chunk_tree;
};
struct Retiring {
    u32 handle;
    // The last frame that can draw the mesh.
    u64 frame;
};
struct Staging {
    Buffer buffer;
    u64 frame;
};
struct Eviction {
    u32 handle;
    // The last frame that can draw the mesh, and the memory it held then.
    u64 frame;
    VmaAllocation allocation;
};
const Device* g_device = nullptr;
Memory* g_memory = nullptr;
Buffer g_index_buffer;
std::map<u32, u32> g_free_indices;
std::array<Resident, Capacity> g_resident;
std::vector<Retiring> g_retiring;
std::vector<Staging> g_staging;
Stats g_stats{};
std::vector<Eviction> g_evictions;
// Seconds since the start, which draws stamp into the meshes they ask for.
std::atomic<u32> g_seconds{0};
const auto g_start = std::chrono::steady_clock::now();

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

void Retire(u64 frame, u64 completed) {
    std::vector<u32> unloaded;
    {
        std::scoped_lock lock{g_mutex};
        unloaded.swap(g_unloaded);
    }
    // The packet of this frame, or the one the capture was writing at the unload, which the next
    // frame takes at the latest, can still draw the mesh.
    for (const u32 handle : unloaded) {
        g_retiring.push_back({handle, frame + 1});
    }
    std::erase_if(g_retiring, [completed](const Retiring& r) {
        if (completed < r.frame) {
            return false;
        }
        Resident& resident = g_resident[r.handle];
        if (resident.vertices.allocation != nullptr) {
            g_stats.resident_bytes -= resident.vertices.size + 2ull * resident.indices;
            g_memory->Destroy(resident.vertices);
            FreeIndices(resident.first_index, resident.indices);
            g_stats.meshes--;
            g_stats.skinned -= resident.skinned;
            g_stats.cloth -= resident.cloth;
        }
        resident = {};
        std::scoped_lock lock{g_mutex};
        g_free.push_back(r.handle);
        return true;
    });
    // An evicted mesh gives its memory back once no frame can draw it, unless a draw wanted it
    // again meanwhile.
    std::erase_if(g_evictions, [completed](const Eviction& e) {
        if (completed < e.frame) {
            return false;
        }
        Mesh& mesh = g_meshes[e.handle];
        Resident& resident = g_resident[e.handle];
        u8 state = Evicting;
        if (resident.vertices.allocation != e.allocation) {
            return true;
        }
        if (!mesh.eviction.compare_exchange_strong(state, Evicted, std::memory_order_acq_rel)) {
            if (state == Rescued) {
                mesh.eviction.store(Kept, std::memory_order_relaxed);
                mesh.ready.store(true, std::memory_order_release);
            }
            return true;
        }
        g_stats.resident_bytes -= resident.vertices.size + 2ull * resident.indices;
        g_memory->Destroy(resident.vertices);
        FreeIndices(resident.first_index, resident.indices);
        g_stats.meshes--;
        resident = {};
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

} // namespace

#include "graphics/gr2port/meshes_capture_install.inc"

void CopyVertices(vk::CommandBuffer cmd, const Mesh& mesh, vk::Buffer destination, u64 offset) {
    const Resident& resident = g_resident[&mesh - g_meshes.data()];
    const vk::BufferCopy copy{.dstOffset = offset, .size = mesh.vertex_bytes};
    cmd.copyBuffer(resident.vertices.buffer, destination, copy, g_device->dispatch);
}

#include "graphics/gr2port/meshes_capture_get.inc"

bool Start(const Device& device, Memory& memory) {
    g_device = &device;
    g_memory = &memory;
    memory.Refresh(0);
    // Areas that show their detailed models from further away hold more meshes at once: at four
    // times the margin a flight through either city held 79M indices, at twice 49M.
    const u64 margin = std::max(Settings::Get().graphics.far_environment_detail, 100u);
    const u64 base =
        memory.DeviceBudget().budget <= SmallBudget ? SmallIndexCapacity : IndexCapacity;
    const u64 capacity = base + base * (margin - 100) / 300;
    g_index_buffer = memory.CreateBuffer(
        capacity * 2, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        Placement::Device);
    if (!g_index_buffer.buffer) {
        return false;
    }
    g_free_indices.emplace(0u, static_cast<u32>(capacity));
    g_stats.index_capacity = capacity;
    return true;
}

void Update(vk::CommandBuffer cmd, u64 frame, u64 completed) {
    Retire(frame, completed);
    const u32 seconds = static_cast<u32>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_start)
            .count());
    g_seconds.store(seconds, std::memory_order_relaxed);
    if (frame % ScanFrames == 0 && Textures::Squeezed()) {
        u32 evicts = 0;
        const u32 high = [] {
            std::scoped_lock lock{g_mutex};
            return g_unused;
        }();
        for (u32 handle = 0; handle < high && evicts < EvictsPerScan; handle++) {
            Mesh& mesh = g_meshes[handle];
            const Resident& resident = g_resident[handle];
            if (!mesh.ready.load(std::memory_order_relaxed) || resident.skinned || resident.cloth ||
                resident.vertices.allocation == nullptr ||
                seconds - mesh.used.load(std::memory_order_relaxed) < EvictSeconds) {
                continue;
            }
            mesh.ready.store(false, std::memory_order_relaxed);
            mesh.eviction.store(Evicting, std::memory_order_release);
            // The packet of this frame, or the one the capture is writing, can still draw it.
            g_evictions.push_back({handle, frame + 1, resident.vertices.allocation});
            evicts++;
        }
    }

    std::vector<u32> uploaded;
    u64 budget = UploadBudget;
    {
        std::scoped_lock lock{g_mutex};
        while (!g_pending.empty() && budget != 0) {
            const u32 handle = g_pending.front();
            g_pending.pop_front();
            Mesh& mesh = g_meshes[handle];
            const VAddr shape = mesh.shape.load(std::memory_order_relaxed);
            // A handle freed and reused before its first upload is queued twice.
            if (shape == 0 || g_resident[handle].vertices.buffer) {
                continue;
            }
            Source source;
            u32 first_index = 0;
            if (!ReadShape(shape, source) ||
                !AllocateIndices(source.index_bytes / 2, first_index)) {
                g_stats.skipped++;
                continue;
            }
            // The skin stream, then the cloth data, follow the vertex data in the mesh's buffer.
            const u32 vertex_count = source.vertex_bytes / std::max(source.stride, 1u);
            const bool cloth = source.particles != 0 && source.stride != 0;
            const bool valid_cloth = cloth && ValidSoftBody(source, vertex_count);
            g_stats.bad_cloth += cloth && !valid_cloth;
            const auto align = [](u32 offset) { return (offset + 15) & ~15u; };
            const u32 skin_offset = align(source.vertex_bytes);
            const u32 skin_end =
                source.skin != 0 ? skin_offset + source.skin_bytes : source.vertex_bytes;
            const u32 particles_offset = align(skin_end);
            const u32 constraints_offset = align(particles_offset + source.particle_bytes);
            const u32 map_offset = align(constraints_offset + source.constraint_bytes);
            // The shaders read the map as u32s.
            const u32 vertex_region =
                valid_cloth ? map_offset + ((source.map_bytes + 3) & ~3u) : skin_end;
            const u64 bytes = u64{vertex_region} + source.index_bytes;
            if (!g_memory->Reserve(vertex_region)) {
                FreeIndices(first_index, source.index_bytes / 2);
                g_pending.push_front(handle);
                break;
            }
            Staging staging{
                .buffer = g_memory->CreateBuffer(bytes, vk::BufferUsageFlagBits::eTransferSrc,
                                                 Placement::Staging),
                .frame = frame,
            };
            Buffer vertices = g_memory->CreateBuffer(vertex_region,
                                                     vk::BufferUsageFlagBits::eStorageBuffer |
                                                         vk::BufferUsageFlagBits::eTransferSrc |
                                                         vk::BufferUsageFlagBits::eTransferDst,
                                                     Placement::Device);
            if (staging.buffer.mapped == nullptr || !vertices.buffer) {
                FreeIndices(first_index, source.index_bytes / 2);
                g_memory->Destroy(staging.buffer);
                g_memory->Destroy(vertices);
                g_stats.skipped++;
                continue;
            }
            std::memcpy(staging.buffer.mapped, reinterpret_cast<const void*>(source.vertices),
                        source.vertex_bytes);
            if (source.skin != 0) {
                std::memcpy(staging.buffer.mapped + skin_offset,
                            reinterpret_cast<const void*>(source.skin), source.skin_bytes);
            }
            if (valid_cloth) {
                std::memcpy(staging.buffer.mapped + particles_offset,
                            reinterpret_cast<const void*>(source.particles), source.particle_bytes);
                std::memcpy(staging.buffer.mapped + constraints_offset,
                            reinterpret_cast<const void*>(source.constraints),
                            source.constraint_bytes);
                std::memcpy(staging.buffer.mapped + map_offset,
                            reinterpret_cast<const void*>(source.particle_map), source.map_bytes);
            }
            // Every index must name a vertex inside the copied vertex data. The game's copy is
            // read, as the staging memory is write-combined.
            u32 max_index = 0;
            if (source.index_bytes != 0) {
                std::memcpy(staging.buffer.mapped + vertex_region,
                            reinterpret_cast<const void*>(source.indices), source.index_bytes);
                const auto* index_data = reinterpret_cast<const u16*>(source.indices);
                max_index = *std::max_element(index_data, index_data + source.index_bytes / 2);
            }
            if (g_stats.uploads_logged < LoggedUploads) {
                g_stats.uploads_logged++;
                LOG_INFO(Render_Vulkan,
                         "GR2port: mesh {:#x}: {} vertices of {} bytes, position format {} at {}, "
                         "{} indices up to {}",
                         shape, vertex_count, source.stride, source.position >> 8,
                         source.position & 0xff, source.index_bytes / 2, max_index);
            }
            if (source.stride == 0 || max_index >= vertex_count ||
                (source.skin != 0 && max_index >= source.skin_bytes / SkinStride)) {
                FreeIndices(first_index, source.index_bytes / 2);
                g_memory->Destroy(staging.buffer);
                g_memory->Destroy(vertices);
                g_stats.skipped++;
                g_stats.bad_indices++;
                continue;
            }
            const vk::BufferCopy vertex_copy{.size = vertex_region};
            cmd.copyBuffer(staging.buffer.buffer, vertices.buffer, vertex_copy, g_device->dispatch);
            if (source.index_bytes != 0) {
                const vk::BufferCopy index_copy{
                    .srcOffset = vertex_region,
                    .dstOffset = 2ull * first_index,
                    .size = source.index_bytes & ~1u,
                };
                cmd.copyBuffer(staging.buffer.buffer, g_index_buffer.buffer, index_copy,
                               g_device->dispatch);
            }
            mesh.vertices = vertices.address;
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
            std::unique_ptr<ChunkBox[]> chunk_tree = BuildChunks(source, mesh, mesh.chunks);
            mesh.chunk_tree = chunk_tree.get();
            mesh.skin = source.skin != 0 ? vertices.address + skin_offset : 0;
            mesh.particles = valid_cloth ? vertices.address + particles_offset : 0;
            mesh.constraints = valid_cloth ? vertices.address + constraints_offset : 0;
            mesh.particle_map = valid_cloth ? vertices.address + map_offset : 0;
            mesh.particle_count = valid_cloth ? source.particle_count : 0;
            if (valid_cloth && g_stats.cloth < LoggedCloth) {
                LOG_INFO(Render_Vulkan,
                         "GR2port: cloth of mesh {:#x}: {} particles, {} constraints, {} map "
                         "entries for {} vertices",
                         shape, source.particle_count, source.constraint_bytes / 4,
                         source.map_bytes / 2, vertex_count);
            }
            g_stats.no_texcoord += source.texcoord == 0;
            g_stats.no_normal += source.normal == 0;
            g_stats.skinned += source.skin != 0;
            g_stats.cloth += valid_cloth;
            g_resident[handle] = {
                .vertices = vertices,
                .first_index = first_index,
                .indices = source.index_bytes / 2,
                .skinned = source.skin != 0,
                .cloth = valid_cloth,
                .chunk_tree = std::move(chunk_tree),
            };
            g_staging.push_back(staging);
            g_stats.meshes++;
            g_stats.uploaded_bytes += bytes;
            g_stats.resident_bytes += bytes;
            uploaded.push_back(handle);
            budget = bytes >= budget ? 0 : budget - bytes;
        }
        g_stats.pending = static_cast<u32>(g_pending.size());
        g_stats.missed_unloads = g_missed_unloads;
        g_stats.lost_handles = g_lost_handles;
    }
    if (uploaded.empty()) {
        return;
    }
    const vk::MemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader |
                        vk::PipelineStageFlagBits2::eIndexInput |
                        vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eIndexRead,
    };
    cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier},
                         g_device->dispatch);
    for (const u32 handle : uploaded) {
        g_meshes[handle].ready.store(true, std::memory_order_release);
    }
}

vk::Buffer IndexBuffer() {
    return g_index_buffer.buffer;
}

Stats TakeStats() {
    Stats stats = g_stats;
    g_stats.uploaded_bytes = 0;
    g_stats.skipped = 0;
    g_stats.bad_indices = 0;
    g_stats.bad_cloth = 0;
    return stats;
}

} // namespace Gr2Port::Meshes
