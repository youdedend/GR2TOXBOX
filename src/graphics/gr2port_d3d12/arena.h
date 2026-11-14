// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <span>
#include <vector>

#include "graphics/gr2port_d3d12/allocator.h"
#include "graphics/gr2port_d3d12/descriptors.h"
#include "graphics/gr2port_d3d12/device.h"

namespace Gr2Port::D3D12 {

/// The Unified Bindless Arena: how this backend replaces GLSL
/// `GL_EXT_buffer_reference` and 64-bit GPU pointers.
///
/// ### The problem
///
/// Upstream's shaders pass raw GPU addresses. `world.vert` reads
///
///     Draws draws = scene_draws;          // a buffer_reference
///     uint64_t vertices = draws.draws[gl_BaseInstance].vertices;
///     vec3 p = LoadPosition(vertices, stride, offset);
///
/// HLSL has no equivalent of `GL_EXT_buffer_reference`: there is no way to
/// dereference an arbitrary 64-bit address held in a struct, and SM 6.6 DXIL
/// deliberately has no generic pointer type. `shaderInt64` buys arithmetic, not
/// dereferencing.
///
/// ### The solution
///
/// Every geometry buffer the frame needs — vertex streams, index buffers, skin
/// matrices, palettes, the draw table itself, particle state — lives in a small
/// number of very large `ByteAddressBuffer`s (the *arenas*). A GPU pointer
/// becomes a pair:
///
///     struct ArenaRef {
///         uint arena;    // which ByteAddressBuffer
///         uint offset;   // byte offset within it
///     };
///
/// and the HLSL accessor is
///
///     uint4 v = ArenaLoad4(arena_ref);
///     // == ResourceDescriptorHeap[arenas[arena_ref.arena]].Load4(ref.offset)
///
/// Because the entire guest GPU memory is under 5 GiB (see
/// `Core::Xbox::GuestMemoryCeilingBytes`, 3,200 MiB of guest memory plus the
/// 1,200 MiB heap budget), a **32-bit byte offset addresses every byte
/// losslessly**. There is no aliasing, no windowing, and no 64-bit math in the
/// shader's hot path — which is also faster than the Vulkan version on GCN
/// hardware, where 64-bit address arithmetic costs extra instructions.
///
/// ### Layout
///
/// | Arena | Contents | Typical size |
/// | --- | --- | --- |
/// | `Geometry` | Vertex/index streams captured by `capture.cpp` | 512 MiB |
/// | `Scene` | Draw table, materials, decals, lights (`scene.h`) | 64 MiB |
/// | `Skinning` | Skin matrices and palettes (`skin.comp` output) | 128 MiB |
/// | `Particles` | GPU particle state (`pfx/gpu_*.comp`) | 256 MiB |
/// | `Scratch` | Per-frame uploads, transient compute I/O | 128 MiB |
///
/// Each arena is one committed `ByteAddressBuffer` plus one SRV slot in the
/// bindless heap, so a shader selects an arena with a single descriptor index
/// and never rebinds anything.
enum class ArenaKind : u32 {
    Geometry = 0,
    Scene = 1,
    Skinning = 2,
    Particles = 3,
    Scratch = 4,
    Count,
};

/// A GPU pointer, expressed as an arena index plus a 32-bit byte offset.
/// This is exactly what the shaders see; the host builds it from the guest's
/// virtual or physical address.
struct ArenaRef {
    u32 arena = 0;
    u32 offset = 0;

    [[nodiscard]] bool IsNull() const noexcept {
        return arena == 0 && offset == 0;
    }
    /// A null reference is all-zeroes, which also makes it safe to memset.
    static constexpr ArenaRef Null() noexcept {
        return ArenaRef{0, 0};
    }
};

/// A sub-allocation inside one arena.
struct ArenaAllocation {
    ArenaRef ref{};
    u64 size = 0;
    /// The CPU address the host writes through (the arena is persistently
    /// mapped through D3D12MA), or nullptr for shared-only arenas.
    u8* cpu = nullptr;
    /// The GPU virtual address, used for the few places that need one
    /// (ExecuteIndirect argument buffers, constant-buffer views).
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
};

/// Configuration for one arena.
struct ArenaDesc {
    ArenaKind kind = ArenaKind::Geometry;
    u64 capacity = 0;
    /// The GPU writes to this arena (skinning output, particle state), so it
    /// needs both an SRV and a UAV descriptor.
    bool writable = false;
    /// Keep the CPU mapping for direct writes (capture paths).
    bool cpu_visible = true;
};

/// The arena set. One instance per device; the renderer's frame graph
/// allocates out of it and resets the transient kinds per frame.
class ArenaSet {
public:
    /// Creates every arena at the sizes the preset asks for. Called once the
    /// device, allocator and bindless heap exist.
    bool Create(Device& device, Allocator& allocator, BindlessHeap& descriptors);

    void Destroy();

    /// Allocates `size` bytes, aligned to `alignment` (power of two), from an
    /// arena. Returns a null allocation when the arena is full, which the
    /// caller must treat as a budget failure, not a programming error.
    ArenaAllocation Allocate(ArenaKind kind, u64 size, u64 alignment = 16);

    /// Resets a transient arena (Scratch, and Particles between frames) back to
    /// empty. Only legal when the GPU has finished consuming it, which the
    /// frame graph guarantees by waiting on the arena's fence value.
    void Reset(ArenaKind kind);

    /// The `ByteAddressBuffer` SRV slot for a kind, the value shaders index
    /// `ResourceDescriptorHeap[]` with.
    [[nodiscard]] DescriptorSlot Srv(ArenaKind kind) const {
        return srv_[static_cast<u32>(kind)];
    }
    /// The `RWByteAddressBuffer` UAV slot for a writable arena.
    [[nodiscard]] DescriptorSlot Uav(ArenaKind kind) const {
        return uav_[static_cast<u32>(kind)];
    }
    [[nodiscard]] ID3D12Resource* Resource(ArenaKind kind) const {
        return resources_[static_cast<u32>(kind)].Get();
    }
    /// Builds an ArenaRef from a kind and an offset, the only legal way to make
    /// one for a shader.
    [[nodiscard]] ArenaRef MakeRef(ArenaKind kind, u64 offset) const {
        return ArenaRef{static_cast<u32>(kind), static_cast<u32>(offset)};
    }
    /// Translates a guest GPU virtual address into an ArenaRef, or a null
    /// reference when the address is not inside an arena (the caller then
    /// reports the same "missing geometry" diagnostic the Vulkan backend does).
    [[nodiscard]] ArenaRef RefFromGpuAddress(D3D12_GPU_VIRTUAL_ADDRESS address) const;

    [[nodiscard]] u64 Capacity(ArenaKind kind) const {
        return capacities_[static_cast<u32>(kind)];
    }
    [[nodiscard]] u64 CommittedBytes() const;

    /// The root-constant layout the shaders expect for the arena table: the
    /// SRV index of each arena, packed as 5 consecutive uints at b0.
    struct RootArenaTable {
        u32 srv_index[static_cast<u32>(ArenaKind::Count)];
    };
    [[nodiscard]] RootArenaTable BuildRootTable() const;

private:
    ComPtr<ID3D12Resource> resources_[static_cast<u32>(ArenaKind::Count)];
    D3D12_GPU_VIRTUAL_ADDRESS gpu_base_[static_cast<u32>(ArenaKind::Count)]{};
    u8* cpu_base_[static_cast<u32>(ArenaKind::Count)]{};
    u64 capacities_[static_cast<u32>(ArenaKind::Count)]{};
    u64 offsets_[static_cast<u32>(ArenaKind::Count)]{};
    DescriptorSlot srv_[static_cast<u32>(ArenaKind::Count)]{};
    DescriptorSlot uav_[static_cast<u32>(ArenaKind::Count)]{};
    Allocator* allocator_ = nullptr;
    Device* device_ = nullptr;
};

} // namespace Gr2Port::D3D12
