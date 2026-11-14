// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <utility> // std::move for the arena handles

#include "common/alignment.h"
#include "common/logging/log.h"
#include "graphics/gr2port_d3d12/arena.h"

namespace Gr2Port::D3D12 {

namespace {

constexpr u32 KindIndex(ArenaKind kind) {
    return static_cast<u32>(kind);
}

/// Default capacities, sized for a 4K frame on Series X and a 1080p frame on
/// Series S without either arena being the reason a level fails to load.
/// Together they are 1,088 MiB of the 1,200 MiB heap budget; the remainder is
/// render targets and the transient frame graph.
constexpr ArenaDesc DefaultArenas[] = {
    // Geometry: captured vertex and index streams for the visible scene ring.
    {ArenaKind::Geometry, 512_MB, /*writable=*/false, /*cpu_visible=*/true},
    // Scene: the draw table, materials, decals and lights the capture builds.
    {ArenaKind::Scene, 64_MB, /*writable=*/false, /*cpu_visible=*/true},
    // Skinning: matrices and palettes written by skin.comp.
    {ArenaKind::Skinning, 128_MB, /*writable=*/true, /*cpu_visible=*/true},
    // Particles: GPU particle state, reset per frame.
    {ArenaKind::Particles, 256_MB, /*writable=*/true, /*cpu_visible=*/false},
    // Scratch: per-frame uploads and transient compute I/O, reset per frame.
    {ArenaKind::Scratch, 128_MB, /*writable=*/true, /*cpu_visible=*/true},
};

constexpr const wchar_t* ArenaName(ArenaKind kind) {
    switch (kind) {
    case ArenaKind::Geometry:
        return L"GR2 Arena: Geometry";
    case ArenaKind::Scene:
        return L"GR2 Arena: Scene";
    case ArenaKind::Skinning:
        return L"GR2 Arena: Skinning";
    case ArenaKind::Particles:
        return L"GR2 Arena: Particles";
    case ArenaKind::Scratch:
        return L"GR2 Arena: Scratch";
    default:
        return L"GR2 Arena";
    }
}

} // namespace

bool ArenaSet::Create(Device& device, Allocator& allocator, BindlessHeap& descriptors) {
    device_ = &device;
    allocator_ = &allocator;

    for (const ArenaDesc& desc : DefaultArenas) {
        const u32 index = KindIndex(desc.kind);

        // The arena is a single committed ByteAddressBuffer. On Xbox the UMA
        // heap serves both CPU-visible and GPU-only arenas, so a Default heap
        // resource with a persistent CPU mapping is legal there; the CPU mapping
        // is what makes capture.cpp's writes free.
        const D3D12_HEAP_TYPE heap =
            desc.cpu_visible ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
        const D3D12_RESOURCE_FLAGS flags =
            desc.writable ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                          : D3D12_RESOURCE_FLAG_NONE;
        const D3D12_RESOURCE_STATES state =
            desc.writable && !desc.cpu_visible ? D3D12_RESOURCE_STATE_COMMON
                                               : D3D12_RESOURCE_STATE_GENERIC_READ;

        auto resource = allocator.CreateBuffer(desc.capacity, heap, flags, state, ArenaName(desc.kind));
        if (!resource) {
            LOG_CRITICAL(Graphics,
                         "D3D12: cannot create the {} MiB arena '{}'; the frame graph cannot run",
                         desc.capacity / 1_MB, static_cast<u32>(desc.kind));
            return false;
        }

        // std::move: ResourcePtr is move-only, and leaving `resource` owning the
        // same allocation would release it twice.
        resources_[index] = std::move(resource);
        capacities_[index] = desc.capacity;
        offsets_[index] = 0;
        gpu_base_[index] = resources_[index]->GetGPUVirtualAddress();
        cpu_base_[index] = nullptr;

        if (desc.cpu_visible) {
            void* mapped = nullptr;
            const D3D12_RANGE read_range{0, 0};
            const HRESULT hr = resources_[index]->Map(0, &read_range, &mapped);
            if (FAILED(hr)) {
                LOG_CRITICAL(Graphics, "D3D12: cannot map arena '{}': {}",
                             static_cast<u32>(desc.kind), HresultToString(hr));
                return false;
            }
            cpu_base_[index] = static_cast<u8*>(mapped);
        }

        // One SRV per arena. The descriptor is what a shader's
        // ResourceDescriptorHeap[arena] resolves to; a ByteAddressBuffer view
        // (DXGI_FORMAT_UNKNOWN, stride 0) is the buffer_reference replacement.
        DescriptorSlot srv = descriptors.AllocateSrv(1);
        if (!srv) {
            LOG_CRITICAL(Graphics, "D3D12: no bindless slot for arena '{}'",
                         static_cast<u32>(desc.kind));
            return false;
        }
        descriptors.WriteBuffer(srv, resources_[index].Get(), 0,
                                static_cast<u32>(desc.capacity / 4), 0);
        srv_[index] = srv;

        // Declared outside the branch: the log line below reports the UAV slot
        // for every arena, and a read-only arena reports an invalid slot.
        DescriptorSlot uav{};
        if (desc.writable) {
            // Not a separate heap: D3D12 puts CBV, SRV and UAV descriptors in
            // one heap type, so the UAV slot comes from the same allocator and is
            // simply written with a UAV view instead of an SRV view.
            uav = descriptors.AllocateSrv(1);
            if (!uav) {
                LOG_CRITICAL(Graphics, "D3D12: no bindless UAV slot for arena '{}'",
                             static_cast<u32>(desc.kind));
                return false;
            }
            descriptors.WriteBufferUav(uav, resources_[index].Get(), 0,
                                       static_cast<u32>(desc.capacity / 4), 0);
            uav_[index] = uav;
        }

        LOG_INFO(Graphics, "D3D12 arena '{}': {} MiB, SRV slot {}, UAV slot {}, {}",
                 static_cast<u32>(desc.kind), desc.capacity / 1_MB, srv.index, uav.index,
                 desc.cpu_visible ? "cpu-visible" : "gpu-only");
    }

    LOG_INFO(Graphics, "D3D12: {} MiB of Unified Bindless Arena committed across {} arenas",
             CommittedBytes() / 1_MB, KindIndex(ArenaKind::Count));
    return true;
}

void ArenaSet::Destroy() {
    for (u32 i = 0; i < KindIndex(ArenaKind::Count); ++i) {
        if (resources_[i] && cpu_base_[i] != nullptr) {
            resources_[i]->Unmap(0, nullptr);
            cpu_base_[i] = nullptr;
        }
        resources_[i].Reset();
        capacities_[i] = 0;
        offsets_[i] = 0;
        gpu_base_[i] = 0;
        srv_[i] = DescriptorSlot{};
        uav_[i] = DescriptorSlot{};
    }
}

ArenaAllocation ArenaSet::Allocate(ArenaKind kind, u64 size, u64 alignment) {
    const u32 index = KindIndex(kind);
    if (index >= KindIndex(ArenaKind::Count) || resources_[index] == nullptr) {
        return {};
    }
    // D3D12 buffer SRVs need 4-byte alignment; 16 is the natural alignment of
    // every struct upstream's shaders read (vec4 / mat4).
    alignment = std::max<u64>(alignment, 16);
    if ((alignment & (alignment - 1)) != 0) {
        alignment = 16;
    }

    u64 start = Common::AlignUp(offsets_[index], alignment);
    if (start + size > capacities_[index]) {
        LOG_CRITICAL(Graphics,
                     "D3D12: arena '{}' is full: {} KiB requested at {} MiB of {} MiB; "
                     "the frame's geometry does not fit the console budget",
                     static_cast<u32>(kind), size / 1_KB, start / 1_MB,
                     capacities_[index] / 1_MB);
        return {};
    }
    offsets_[index] = start + size;

    ArenaAllocation allocation{};
    allocation.ref = ArenaRef{static_cast<u32>(kind), static_cast<u32>(start)};
    allocation.size = size;
    allocation.cpu = cpu_base_[index] != nullptr ? cpu_base_[index] + start : nullptr;
    allocation.gpu = gpu_base_[index] + start;
    return allocation;
}

void ArenaSet::Reset(ArenaKind kind) {
    const u32 index = KindIndex(kind);
    if (index < KindIndex(ArenaKind::Count)) {
        offsets_[index] = 0;
    }
}

ArenaRef ArenaSet::RefFromGpuAddress(D3D12_GPU_VIRTUAL_ADDRESS address) const {
    if (address == 0) {
        return ArenaRef::Null();
    }
    for (u32 i = 0; i < KindIndex(ArenaKind::Count); ++i) {
        const D3D12_GPU_VIRTUAL_ADDRESS base = gpu_base_[i];
        if (base != 0 && address >= base && address < base + capacities_[i]) {
            const u64 offset = address - base;
            if (offset <= 0xFFFFFFFFull) {
                return ArenaRef{i, static_cast<u32>(offset)};
            }
        }
    }
    return ArenaRef::Null();
}

u64 ArenaSet::CommittedBytes() const {
    u64 total = 0;
    for (u32 i = 0; i < KindIndex(ArenaKind::Count); ++i) {
        total += capacities_[i];
    }
    return total;
}

ArenaSet::RootArenaTable ArenaSet::BuildRootTable() const {
    RootArenaTable table{};
    for (u32 i = 0; i < KindIndex(ArenaKind::Count); ++i) {
        table.srv_index[i] = srv_[i].index;
    }
    return table;
}

} // namespace Gr2Port::D3D12
