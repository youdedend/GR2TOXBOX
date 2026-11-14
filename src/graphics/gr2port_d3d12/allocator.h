// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/uwp_memory.h"
#include "graphics/gr2port_d3d12/d3d12ma/D3D12MemAlloc.h"
#include "graphics/gr2port_d3d12/device.h"

namespace Gr2Port::D3D12 {

/// Resource allocation with the console memory budget enforced.
///
/// D3D12MA handles placement and defragmentation; this class owns the *policy*:
///
///   * a hard ceiling of `Core::Xbox::EffectiveD3d12HeapBudget()` (1,200 MiB on
///     Xbox, 6 GiB on desktop), so the D3D12 heap can never grow into the guest
///     memory the emulator needs for the game itself;
///   * LRU eviction of evictable resources at 80% of the budget (a warning and
///     a proactive trim) and at 90% (aggressive trim, dropping every cache entry
///     the renderer can rebuild);
///   * a hard refusal at 100%, because over-committing inside a 5,120 MiB
///     AppContainer pool fails the allocation with `E_OUTOFMEMORY` and takes the
///     whole frame with it.
///
/// The eviction policy is what makes the 3,200 MiB guest + 1,200 MiB heap split
/// workable: Gravity Rush 2's texture cache wants far more than 1,200 MiB at 4K,
/// so the cache has to be a soft cache, not a hard one.
///
/// ### Ownership
///
/// D3D12MA ties the memory to the `D3D12MA::Allocation`, not to the resource:
/// `Allocation::GetResource()` is a *borrowed* pointer, and releasing the resource
/// alone leaks every byte. But keeping the `Allocation` alive through a
/// `ComPtr` with a custom deleter creates a cycle, because the `Allocation`
/// itself holds a reference to the resource.
///
/// The cycle is broken the way D3D12MA intends: once the resource has been
/// created and a reference taken, `Allocation::SetResource(nullptr)` drops the
/// allocation's reference to it. The resource is then owned by the caller's
/// `ComPtr`, the allocation is owned by that `ComPtr`'s deleter, and releasing
/// the resource destroys the allocation and returns the memory. Nothing leaks and
/// nothing dangles.
///
/// ### Eviction contract
///
/// A resource is either **evictable** (the renderer can rebuild it from the
/// guest's memory: textures, static geometry, the PSO cache) or **resident**
/// (render targets, the swapchain, the arenas, anything a frame in flight is
/// reading). Only evictable resources are ever dropped, and only when the GPU is
/// not using them — the callback receives the frame number the resource was last
/// used in and refuses anything newer than the last completed frame.
///
/// ### The type handed out
///
/// `ResourcePtr` is the only thing `Allocator` returns. It owns the resource *and*
/// the D3D12MA allocation behind it, because releasing the resource alone does not
/// return the memory (see Ownership above). It is a small explicit type rather than
/// a `ComPtr` with a custom deleter for two reasons: WRL's `ComPtr` has no
/// two-argument `Attach` in every SDK, and spelling the ownership out here is what
/// makes it reviewable.
///
/// Move-only: two handles to one allocator allocation would double-release it.
class ResourcePtr {
public:
    ResourcePtr() = default;
    /// Lets a creation path say `return nullptr` and still produce an empty
    /// handle, which reads better than `return {}` at four call sites.
    ResourcePtr(std::nullptr_t) noexcept {}
    ~ResourcePtr() {
        Reset();
    }

    ResourcePtr(const ResourcePtr&) = delete;
    ResourcePtr& operator=(const ResourcePtr&) = delete;

    ResourcePtr(ResourcePtr&& other) noexcept {
        MoveFrom(other);
    }
    ResourcePtr& operator=(ResourcePtr&& other) noexcept {
        if (this != &other) {
            Reset();
            MoveFrom(other);
        }
        return *this;
    }

    /// Takes ownership of `resource`'s existing reference and of `allocation`.
    /// `bytes` is returned to `*committed` when the last handle goes away.
    void Adopt(ID3D12Resource* resource, D3D12MA::Allocation* allocation, u64 bytes,
               std::atomic<u64>* committed) noexcept {
        Reset();
        resource_.Attach(resource);
        allocation_ = allocation;
        bytes_ = bytes;
        committed_ = committed;
    }

    /// Releases the resource, then the allocation, and decrements the committed
    /// counter. The order matters: `Allocator` clears the allocation's reference
    /// to the resource at creation, so the resource must die first.
    void Reset() noexcept;

    [[nodiscard]] ID3D12Resource* Get() const noexcept {
        return resource_.Get();
    }
    ID3D12Resource* operator->() const noexcept {
        return resource_.Get();
    }
    ID3D12Resource** GetAddressOf() noexcept {
        return resource_.GetAddressOf();
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return resource_ != nullptr;
    }
    /// Comparisons against nullptr, so a caller can write the emptiness test the
    /// way it reads at the call site.
    friend bool operator==(const ResourcePtr& ptr, std::nullptr_t) noexcept {
        return ptr.resource_ == nullptr;
    }
    friend bool operator!=(const ResourcePtr& ptr, std::nullptr_t) noexcept {
        return ptr.resource_ != nullptr;
    }

    /// The bytes this resource holds, for the allocator's statistics.
    [[nodiscard]] u64 Bytes() const noexcept {
        return bytes_;
    }

private:
    void MoveFrom(ResourcePtr& other) noexcept {
        resource_.Attach(other.resource_.Detach());
        allocation_ = other.allocation_;
        bytes_ = other.bytes_;
        committed_ = other.committed_;
        other.allocation_ = nullptr;
        other.bytes_ = 0;
        other.committed_ = nullptr;
    }

    ComPtr<ID3D12Resource> resource_;
    D3D12MA::Allocation* allocation_ = nullptr;
    u64 bytes_ = 0;
    std::atomic<u64>* committed_ = nullptr;
};

class Allocator {
public:
    /// Called when the budget passes a trim threshold. The renderer registers
    /// this and drops cache entries until its own accounting falls back under
    /// the target. Returns how many bytes were released.
    using EvictionCallback = std::function<u64(u64 bytes_needed)>;

    static constexpr double WarnThreshold = 0.80;
    static constexpr double AggressiveThreshold = 0.90;

    bool Create(Device& device);
    void Destroy();

    /// Creates a committed resource inside the budget. `kind` is the D3D12MA
    /// heap class: Default (GPU-only), Upload (CPU-write), Readback.
    ResourcePtr CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_HEAP_TYPE heap_type,
                               D3D12_RESOURCE_STATES initial_state,
                               const D3D12_CLEAR_VALUE* clear = nullptr);

    /// A committed buffer with a known size, the common case for the arenas and
    /// for upload staging.
    ResourcePtr CreateBuffer(u64 size, D3D12_HEAP_TYPE heap_type, D3D12_RESOURCE_FLAGS flags,
                             D3D12_RESOURCE_STATES initial_state, const wchar_t* name = nullptr,
                             bool evictable = false);

    /// The renderer installs its texture-cache trimmer here.
    void SetEvictionCallback(EvictionCallback callback) {
        eviction_ = std::move(callback);
    }

    /// Checks the budget and trims if a threshold was crossed. Called once per
    /// frame, before the frame graph allocates.
    void BeginFrame(u64 completed_frame);

    /// Total bytes this allocator has committed to D3D12 heaps.
    [[nodiscard]] u64 CommittedBytes() const {
        return committed_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] u64 BudgetBytes() const {
        return budget_;
    }
    [[nodiscard]] double Utilisation() const {
        const u64 budget = budget_;
        return budget == 0 ? 0.0 : static_cast<double>(CommittedBytes()) / static_cast<double>(budget);
    }

    /// Per-frame statistics for the debug overlay and the log.
    struct Stats {
        u64 committed;
        u64 budget;
        u64 evictions;
        u64 evicted_bytes;
        u64 refused;
    };
    [[nodiscard]] Stats GetStats() const;
    void LogStats() const;

private:
    bool TrimTo(double target_utilisation, u64 completed_frame);

    ComPtr<D3D12MA::Allocator> d3d12ma_;
    D3D12MA::ALLOCATOR_DESC allocator_desc_{};
    Device* device_ = nullptr;
    u64 budget_ = 0;
    std::atomic<u64> committed_{0};
    mutable std::mutex mutex_;
    EvictionCallback eviction_;
    u64 evictions_ = 0;
    u64 evicted_bytes_ = 0;
    u64 refused_ = 0;
    u64 frame_ = 0;
};

} // namespace Gr2Port::D3D12
