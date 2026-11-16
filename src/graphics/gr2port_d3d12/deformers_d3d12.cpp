// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include "common/logging/log.h"
#include "graphics/gr2port/deformers.h"
#include "graphics/gr2port/meshes.h"
#include "graphics/gr2port_d3d12/deformers_d3d12.h"

#include "effector_comp.h"
#include "graphics/gr2port/scene.h"
#include "skin_comp.h"
#include "soft_body_comp.h"
#include "soft_body_write_comp.h"

namespace Gr2Port::Deformers {

namespace {

// Deformed instance: SObj kind at +0xa, shape at +0x10, vertex buffer object at +0x18, handle at
// +0x13c in the gap after the vertex buffer object.
constexpr u64 InstanceKind = 0xa;
constexpr u16 KindDeformed = 0x19;
constexpr u64 InstanceShape = 0x10;
constexpr u64 InstanceVertexBuffer = 0x18;
constexpr u64 InstanceHandle = 0x13c;
// Shape: vertex buffer object at +0x60.
constexpr u64 ShapeVertexBuffer = 0x60;
// Vertex buffer object: stride +0x18, element count +0x1c, vertex count +0x20, the element of
// each semantic from +0x24, and the elements, 4 bytes each, from +0x34.
constexpr u64 VertexStride = 0x18;
constexpr u64 VertexElements = 0x1c;
constexpr u64 VertexCount = 0x20;
constexpr u64 VertexLayout = 0x24;
constexpr u64 VertexElementInfo = 0x34;
constexpr u32 MaxElements = 16;
// An instance unused for this many game frames loses its copy.
constexpr u64 RetireFrames = 600;
// The copies share one buffer the renderer creates at its start, before textures fill the
// device's memory; a copy starts at a multiple of CopyAlignment.
constexpr u64 ArenaBytes = 16_MB;
constexpr u64 CopyAlignment = 256;
// A cloth instance's state follows its vertex data: a header, then a position and previous
// position per particle.
constexpr u64 StateHeaderBytes = 32;
constexpr u64 ParticleStateBytes = 32;
constexpr u32 GridSlices = 10;
constexpr u64 GridBytes = u64{Scene::GridCells} * Scene::GridEntries * 4;
constexpr u32 LoggedInstances = 4;
static_assert(Capacity <= 0x80000000u);

template <typename T>
T Load(VAddr address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

u64 Aligned(u64 bytes) {
    return (bytes + CopyAlignment - 1) & ~(CopyAlignment - 1);
}

void Stamp(VAddr object, u32 value) {
    std::memcpy(reinterpret_cast<void*>(object + InstanceHandle), &value, sizeof(value));
}

// The handle field of an instance whose layout is not its shape's: at or above Capacity and
// derived from the shape, so that a later instance of another shape at the address is checked.
u32 UnsupportedStamp(VAddr shape) {
    return 0x80000000u | static_cast<u32>(shape >> 4);
}

struct Instance {
    std::atomic<VAddr> object{0};
    std::atomic<VAddr> shape{0};
    // The last game frame the capture used the instance.
    std::atomic<u64> used{0};
    // Written by the capture before it publishes object.
    const Meshes::Mesh* mesh = nullptr;
    Copy copy;
};

struct Range {
    u64 offset = 0;
    u64 size = 0;
};

// Guards the handle lists and the arena's free ranges: offset to size.
std::mutex g_mutex;
std::vector<u32> g_free;
u32 g_unused = 0;
std::vector<u32> g_pending;
std::map<u64, u64> g_free_ranges;
std::array<Instance, Capacity> g_instances;
std::array<Range, Capacity> g_ranges;
std::atomic<u32> g_unsupported{0};
std::atomic<u32> g_no_room{0};

// Render thread only.
struct Retiring {
    u32 handle;
    // The last frame that can draw the copy.
    u64 frame;
};
D3D12::CoreTargets* g_targets = nullptr;
u64 g_base = 0;
D3D12::UploadBuffer g_zero;
D3D12_RESOURCE_STATES g_state = D3D12_RESOURCE_STATE_COMMON;
D3D12::Pipeline g_skin;
D3D12::Pipeline g_effector;
D3D12::Pipeline g_soft_body;
D3D12::Pipeline g_soft_body_write;
D3D12::ArenaAllocation g_grid;
std::vector<u32> g_live;
std::vector<Retiring> g_retiring;
Stats g_stats{};
u32 g_logged = 0;

bool Allocate(u64 size, u64& offset) {
    for (auto it = g_free_ranges.begin(); it != g_free_ranges.end(); ++it) {
        if (it->second < size) {
            continue;
        }
        offset = it->first;
        const u64 left = it->second - size;
        g_free_ranges.erase(it);
        if (left != 0) {
            g_free_ranges.emplace(offset + size, left);
        }
        return true;
    }
    return false;
}

void Release(u64 offset, u64 size) {
    auto [it, inserted] = g_free_ranges.emplace(offset, size);
    const auto next = std::next(it);
    if (next != g_free_ranges.end() && it->first + it->second == next->first) {
        it->second += next->second;
        g_free_ranges.erase(next);
    }
    if (it != g_free_ranges.begin()) {
        const auto previous = std::prev(it);
        if (previous->first + previous->second == it->first) {
            previous->second += it->second;
            g_free_ranges.erase(it);
        }
    }
}

// A deformed instance copies its shape's vertex buffer: same stride, vertex count and elements.
bool SameLayout(VAddr vertex_buffer, VAddr shape, const Meshes::Mesh& mesh) {
    if (!mesh.vertex_bytes || !mesh.stride)
        return false;
    const VAddr own = shape + ShapeVertexBuffer;
    const u32 elements = Load<u16>(vertex_buffer + VertexElements);
    const u32 vertices = Load<u32>(vertex_buffer + VertexCount);
    if (Load<u16>(vertex_buffer + VertexStride) != mesh.stride ||
        elements != Load<u16>(own + VertexElements) || elements > MaxElements ||
        vertices != Load<u32>(own + VertexCount) ||
        u64{vertices} * mesh.stride > mesh.vertex_bytes) {
        return false;
    }
    return std::memcmp(reinterpret_cast<const void*>(vertex_buffer + VertexLayout),
                       reinterpret_cast<const void*>(own + VertexLayout),
                       VertexElementInfo - VertexLayout + 4 * elements) == 0;
}

u64 VertexBytes(const Meshes::Mesh& mesh) {
    return Aligned(mesh.vertex_bytes);
}

u64 StateBytes(const Meshes::Mesh& mesh, u64 state_bytes) {
    return mesh.particle_count != 0
               ? Aligned(StateHeaderBytes + u64{mesh.particle_count} * ParticleStateBytes)
               : Aligned(state_bytes);
}

// Gives an instance its copy at once: the frame being captured can already use it, because the
// render thread fills the copy before it runs that frame's jobs and draws.
Copy Register(VAddr object, VAddr vertex_buffer, VAddr shape, const Meshes::Mesh& mesh, u64 frame,
              u64 extra_state_bytes) {
    if (Load<u16>(object + InstanceKind) != KindDeformed ||
        Load<VAddr>(object + InstanceShape) != shape || !SameLayout(vertex_buffer, shape, mesh)) {
        Stamp(object, UnsupportedStamp(shape));
        g_unsupported.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    const u64 vertex_bytes = VertexBytes(mesh);
    const u64 state_bytes = StateBytes(mesh, extra_state_bytes);
    Range range{.size = vertex_bytes + state_bytes};
    std::scoped_lock lock{g_mutex};
    if (g_free.empty() && g_unused == Capacity) {
        return {};
    }
    if (!Allocate(range.size, range.offset)) {
        g_no_room.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    u32 handle = Capacity;
    if (!g_free.empty()) {
        handle = g_free.back();
        g_free.pop_back();
    } else {
        handle = g_unused++;
    }
    g_ranges[handle] = range;
    const u64 address = g_base + range.offset;
    Instance& instance = g_instances[handle];
    instance.mesh = &mesh;
    instance.copy = {
        .vertices = address,
        .state = state_bytes != 0 ? address + vertex_bytes : 0,
    };
    instance.shape.store(shape, std::memory_order_relaxed);
    instance.used.store(frame, std::memory_order_relaxed);
    instance.object.store(object, std::memory_order_release);
    Stamp(object, handle);
    g_pending.push_back(handle);
    return instance.copy;
}

bool Unused(const Instance& instance, u64 game_frame) {
    return instance.used.load(std::memory_order_relaxed) + RetireFrames < game_frame;
}

void Retire(u64 frame, u64 completed, u64 game_frame) {
    // The packet of this frame, or the one the capture is writing, which the next frame takes at
    // the latest, can still draw a copy.
    std::erase_if(g_live, [frame, game_frame](u32 handle) {
        Instance& instance = g_instances[handle];
        if (!Unused(instance, game_frame)) {
            return false;
        }
        instance.object.store(0, std::memory_order_release);
        g_retiring.push_back({handle, frame + 1});
        g_stats.retired++;
        return true;
    });
    std::erase_if(g_retiring, [completed](const Retiring& r) {
        if (completed < r.frame) {
            return false;
        }
        std::scoped_lock lock{g_mutex};
        Range& range = g_ranges[r.handle];
        g_stats.resident_bytes -= range.size;
        g_stats.instances--;
        Release(range.offset, range.size);
        range = {};
        g_free.push_back(r.handle);
        return true;
    });
}

} // namespace

Copy Get(VAddr vertex_buffer, VAddr shape, const Meshes::Mesh& mesh, u64 frame, u64 state_bytes) {
    if (!g_base)
        return {};
    const VAddr object = vertex_buffer - InstanceVertexBuffer;
    const u32 handle = Load<u32>(object + InstanceHandle);
    if (handle < Capacity) {
        Instance& instance = g_instances[handle];
        if (instance.object.load(std::memory_order_acquire) == object &&
            instance.shape.load(std::memory_order_relaxed) == shape) {
            instance.used.store(frame, std::memory_order_relaxed);
            return instance.copy;
        }
    } else if (handle == UnsupportedStamp(shape)) {
        return {};
    }
    return Register(object, vertex_buffer, shape, mesh, frame, state_bytes);
}

namespace {
constexpr auto Rest = static_cast<D3D12_RESOURCE_STATES>(
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
void To(D3D12::CommandList& cmd, D3D12_RESOURCE_STATES state) {
    if (g_state != state) {
        cmd.Transition(g_targets->context->arenas->Resource(D3D12::ArenaKind::Skinning), g_state,
                       state);
        g_state = state;
    }
}
} // namespace
bool Start(D3D12::CoreTargets& targets) {
    using namespace D3D12;
    g_targets = &targets;
    auto& c = *targets.context;
    g_grid = c.arenas->Allocate(D3D12::ArenaKind::Scratch, GridBytes);
    if (!g_grid.size || !g_zero.Create(c, 64 * 1024))
        return false;
    std::memset(g_zero.cpu, 0, 64 * 1024);
    if (!CreateComputePipeline(c.device->device.Get(), *c.root, Shaders::skin_comp_span, g_skin) ||
        !CreateComputePipeline(c.device->device.Get(), *c.root, Shaders::effector_comp_span,
                               g_effector) ||
        !CreateComputePipeline(c.device->device.Get(), *c.root, Shaders::soft_body_comp_span,
                               g_soft_body) ||
        !CreateComputePipeline(c.device->device.Get(), *c.root, Shaders::soft_body_write_comp_span,
                               g_soft_body_write))
        return false;
    g_free_ranges.emplace(0, ArenaBytes);
    g_base = D3D12::Residency::Pack(static_cast<u32>(D3D12::ArenaKind::Skinning), 0);
    return c.arenas->Capacity(D3D12::ArenaKind::Skinning) >= ArenaBytes;
}
void Update(D3D12::CommandList& cmd, u64 frame, u64 completed, u64 game_frame) {
    Retire(frame, completed, game_frame);
    std::vector<u32> created;
    {
        std::scoped_lock lock{g_mutex};
        created.swap(g_pending);
    }
    if (created.empty())
        return;
    auto& c = *g_targets->context;
    auto* output = c.arenas->Resource(D3D12::ArenaKind::Skinning);
    To(cmd, D3D12_RESOURCE_STATE_COPY_DEST);
    for (u32 handle : created) {
        auto& instance = g_instances[handle];
        const auto& mesh = *instance.mesh;
        const auto range = g_ranges[handle];
        g_stats.instances++;
        g_stats.resident_bytes += range.size;
        if (!mesh.ready.load(std::memory_order_acquire) ||
            mesh.shape.load() != instance.shape.load()) {
            instance.object.store(0, std::memory_order_release);
            g_retiring.push_back({handle, frame + 1});
            continue;
        }
        cmd.list->CopyBufferRegion(output, range.offset,
                                   c.arenas->Resource(D3D12::ArenaKind::Geometry),
                                   D3D12::Residency::Offset(mesh.vertices), mesh.vertex_bytes);
        for (u64 offset = VertexBytes(mesh); offset < range.size;) {
            const u64 bytes = std::min<u64>(64 * 1024, range.size - offset);
            cmd.list->CopyBufferRegion(output, range.offset + offset, g_zero.resource.Get(), 0,
                                       bytes);
            offset += bytes;
        }
        g_live.push_back(handle);
    }
    To(cmd, Rest);
}
void Record(D3D12::CommandList& cmd, const Scene::Header& h, u64 address) {
    using namespace D3D12;
    if (h.skin_jobs > Scene::MaxSkinJobs || h.soft_body_jobs > Scene::MaxSoftBodyJobs ||
        h.skin_groups > 65535 || h.soft_body_groups > 65535 || h.write_groups > 65535)
        return;
    const bool cloth = h.soft_body_jobs && h.soft_body_groups && h.write_groups;
    const bool skin = h.skin_jobs && h.skin_groups;
    const bool grid = cloth || h.grass != 0 || h.branch_jobs != 0;
    if (!skin && !grid)
        return;
    auto& t = *g_targets;
    t.Bind(cmd);
    if (skin || cloth)
        To(cmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto barrier = [&] {
        t.Uav(cmd, t.context->arenas->Resource(D3D12::ArenaKind::Skinning));
    };
    const D3D12::CoreState::Ref roots[]{
        D3D12::CoreState::Unpack(address + Scene::SoftBodyJobsOffset),
        D3D12::CoreState::Unpack(address + Scene::EffectorsOffset),
        {g_grid.ref.arena, g_grid.ref.offset}};
    if (grid) {
        t.ScratchTo(cmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd.SetPipeline(g_effector);
        cmd.SetDrawConstants(roots, sizeof(roots));
        cmd.Dispatch(1, 1, GridSlices);
        t.Uav(cmd, t.context->arenas->Resource(D3D12::ArenaKind::Scratch));
    }
    if (skin) {
        cmd.SetPipeline(g_skin);
        const auto jobs = D3D12::CoreState::Unpack(address + Scene::SkinJobsOffset);
        cmd.SetDrawConstants(&jobs, sizeof(jobs));
        cmd.Dispatch(h.skin_groups, h.skin_jobs, 1);
        barrier();
    }
    if (cloth) {
        cmd.SetDrawConstants(roots, sizeof(roots));
        cmd.SetPipeline(g_soft_body);
        cmd.Dispatch(h.soft_body_groups, h.soft_body_jobs, 1);
        barrier();
        cmd.SetPipeline(g_soft_body_write);
        cmd.Dispatch(h.write_groups, h.soft_body_jobs, 1);
        barrier();
    }
    if (grid)
        t.ScratchTo(cmd, Rest);
    if (skin || cloth)
        To(cmd, Rest);
}
u64 GridAddress() {
    return D3D12::Residency::Pack(g_grid.ref.arena, g_grid.ref.offset);
}
Stats TakeStats() {
    Stats s = g_stats;
    s.pending = g_no_room.exchange(0);
    s.unsupported = g_unsupported.exchange(0);
    g_stats.retired = 0;
    return s;
}
void Stop() {
    std::scoped_lock lock{g_mutex};
    g_base = 0;
    g_targets = nullptr;
    g_skin = {};
    g_effector = {};
    g_soft_body = {};
    g_soft_body_write = {};
    g_zero.Unmap();
    g_zero.resource.Reset();
    g_free.clear();
    g_pending.clear();
    g_free_ranges.clear();
    g_live.clear();
    g_retiring.clear();
    g_unused = 0;
    for (auto& i : g_instances) {
        i.object.store(0);
        i.shape.store(0);
        i.used.store(0);
        i.mesh = nullptr;
        i.copy = {};
    }
    g_ranges = {};
    g_stats = {};
    g_grid = {};
    g_state = D3D12_RESOURCE_STATE_COMMON;
}
} // namespace Gr2Port::Deformers
