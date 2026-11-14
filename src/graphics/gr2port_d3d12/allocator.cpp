// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "common/logging/log.h"
#include "graphics/gr2port_d3d12/allocator.h"

namespace Gr2Port::D3D12 {

namespace {

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES props{};
    props.Type = type;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

} // namespace

bool Allocator::Create(Device& device) {
    device_ = &device;
    budget_ = Core::Xbox::EffectiveD3d12HeapBudget();

    allocator_desc_ = {};
    allocator_desc_.pDevice = device.device.Get();
    allocator_desc_.PreferredBlockSize = 64_MB;
    // No FrameInUseCount: this D3D12MA (v3) dropped it from ALLOCATOR_DESC. A
    // block is free once every fence value passed to AllocateMemory has been
    // reached, which this backend drives from its own timeline fence rather than
    // from a frame counter the allocator guesses at.
    //
    // No DXGI adapter either: the device already has one, and passing it would
    // let D3D12MA query the adapter again on a console where that is restricted.
    allocator_desc_.pAdapter = nullptr;
    // The default pools are not zeroed: a zeroed pool would cost a full clear on
    // every 64 MiB block, and this backend writes every byte it reads.
    allocator_desc_.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;

    const D3D12MA::ALLOCATION_CALLBACKS* callbacks = nullptr;
    HRESULT hr = D3D12MA::CreateAllocator(&allocator_desc_, &d3d12ma_);
    (void)callbacks;
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12: D3D12MA allocator creation failed: {}",
                     HresultToString(hr));
        return false;
    }

    LOG_INFO(Graphics,
             "D3D12: allocator ready, budget {} MiB (trim at {}% and {}%, hard stop at 100%)",
             budget_ / 1_MB, static_cast<int>(WarnThreshold * 100),
             static_cast<int>(AggressiveThreshold * 100));
    return true;
}

void Allocator::Destroy() {
    if (d3d12ma_) {
        d3d12ma_.Reset();
    }
    committed_.store(0, std::memory_order_relaxed);
    device_ = nullptr;
}

void ResourceDeleter::operator()(ID3D12Resource* resource) const noexcept {
    if (resource == nullptr) {
        return;
    }
    // Release our reference to the resource first; then release the allocation.
    // After `Allocator::CreateResource` calls `Allocation::SetResource(nullptr)`,
    // the allocation no longer references the resource, so this order cannot
    // double-release. `allocation` holds the only remaining reference to the
    // memory, and dropping it returns the block to D3D12MA.
    resource->Release();
    if (allocation != nullptr) {
        allocation->Release();
    }
    if (released != nullptr) {
        released->fetch_sub(bytes, std::memory_order_relaxed);
    }
}

Allocator::ResourcePtr Allocator::CreateResource(const D3D12_RESOURCE_DESC& desc,
                                                 D3D12_HEAP_TYPE heap_type,
                                                 D3D12_RESOURCE_STATES initial_state,
                                                 const D3D12_CLEAR_VALUE* clear) {
    if (!d3d12ma_) {
        return nullptr;
    }

    // Budget check before the allocation, not after: a failed D3D12
    // allocation inside an AppContainer leaves the device in a state where the
    // next commit also fails, so the refusal has to happen here.
    const D3D12_RESOURCE_ALLOCATION_INFO info =
        device_->device->GetResourceAllocationInfo(0, 1, &desc);
    const u64 size = info.SizeInBytes;
    if (size == UINT64_MAX) {
        LOG_CRITICAL(Graphics, "D3D12: resource allocation info is invalid for this descriptor");
        return nullptr;
    }

    const u64 used = committed_.load(std::memory_order_relaxed);
    if (budget_ != 0 && used + size > budget_) {
        // Try to make room before refusing.
        const u64 needed = used + size - budget_;
        u64 freed = 0;
        if (eviction_) {
            std::scoped_lock lock{mutex_};
            freed = eviction_(needed);
        }
        const u64 now = committed_.load(std::memory_order_relaxed);
        if (now + size > budget_) {
            refused_++;
            LOG_CRITICAL(Graphics,
                         "D3D12: refusing a {} KiB resource: the {} MiB heap budget is full "
                         "({} MiB committed, {} MiB reclaimed)",
                         size / 1_KB, budget_ / 1_MB, now / 1_MB, freed / 1_MB);
            return nullptr;
        }
    }

    D3D12MA::ALLOCATION_DESC alloc_desc{};
    // Let D3D12MA pick the heap: on Xbox the UMA heap is what a Default
    // resource must land in.
    alloc_desc.HeapType = heap_type;

    D3D12MA::Allocation* allocation = nullptr;
    // D3D12MA v3's signature: the heap properties are implied by
    // alloc_desc.HeapType, and the resource is returned through the IID.
    HRESULT hr = d3d12ma_->CreateResource(&alloc_desc, &desc, initial_state, clear, &allocation,
                                          __uuidof(ID3D12Resource), nullptr);
    if (FAILED(hr) || allocation == nullptr) {
        refused_++;
        LOG_CRITICAL(Graphics, "D3D12: CreateResource({} KiB) failed: {}", size / 1_KB,
                     HresultToString(hr));
        return nullptr;
    }

    // Take ownership of the resource, and break the reference cycle by dropping
    // the allocation's reference to it: the Allocation would otherwise keep the
    // resource alive and vice versa. See the Ownership note in allocator.h.
    ID3D12Resource* raw = allocation->GetResource();
    allocation->SetResource(nullptr);

    ResourceDeleter deleter;
    deleter.allocation = allocation;
    deleter.released = &committed_;
    deleter.bytes = size;

    ResourcePtr resource;
    // Attach does not AddRef, and D3D12MA's CreateResource hands back a resource
    // with one reference for us, so there is nothing to add here. WRL's
    // Attach(pointer, deleter) stores the deleter by value -- including the
    // allocation pointer it must release.
    resource.Attach(raw, deleter);

    committed_.fetch_add(size, std::memory_order_relaxed);
    return resource;
}

Allocator::ResourcePtr Allocator::CreateBuffer(u64 size, D3D12_HEAP_TYPE heap_type,
                                               D3D12_RESOURCE_FLAGS flags,
                                               D3D12_RESOURCE_STATES initial_state,
                                               const wchar_t* name, bool evictable) {
    // Evictability is a renderer-side property: an evictable buffer is one the
    // renderer is willing to drop and rebuild, which the frame graph tracks. The
    // allocator only needs to know the byte size, so it records nothing here --
    // but the parameter stays in the signature because the alternative is every
    // caller forgetting which of its buffers may be reclaimed.
    (void)evictable;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;

    auto resource = CreateResource(desc, heap_type, initial_state, nullptr);
    if (resource && name != nullptr) {
        resource->SetName(name);
    }
    return resource;
}

void Allocator::BeginFrame(u64 completed_frame) {
    frame_ = completed_frame;
    const double utilisation = Utilisation();
    if (utilisation >= AggressiveThreshold) {
        LOG_DEBUG(Graphics, "D3D12: heap at {}% of budget, trimming aggressively",
                  static_cast<int>(utilisation * 100));
        TrimTo(0.70, completed_frame);
    } else if (utilisation >= WarnThreshold) {
        LOG_DEBUG(Graphics, "D3D12: heap at {}% of budget, trimming",
                  static_cast<int>(utilisation * 100));
        TrimTo(0.75, completed_frame);
    }
}

bool Allocator::TrimTo(double target_utilisation, u64 completed_frame) {
    (void)completed_frame;
    if (!eviction_ || budget_ == 0) {
        return false;
    }
    const u64 target = static_cast<u64>(static_cast<double>(budget_) * target_utilisation);
    const u64 used = committed_.load(std::memory_order_relaxed);
    if (used <= target) {
        return false;
    }

    const u64 needed = used - target;
    std::scoped_lock lock{mutex_};
    const u64 freed = eviction_(needed);
    evicted_bytes_ += freed;
    evictions_++;
    LOG_INFO(Graphics, "D3D12: trimmed {} MiB of evictable resources (target {}% of budget)",
             freed / 1_MB, static_cast<int>(target_utilisation * 100));
    return freed != 0;
}

Allocator::Stats Allocator::GetStats() const {
    return Stats{
        .committed = committed_.load(std::memory_order_relaxed),
        .budget = budget_,
        .evictions = evictions_,
        .evicted_bytes = evicted_bytes_,
        .refused = refused_,
    };
}

void Allocator::LogStats() const {
    const Stats stats = GetStats();
    LOG_INFO(Graphics, "D3D12 heap: {} MiB / {} MiB ({}%), {} evictions ({} MiB), {} refusals",
             stats.committed / 1_MB, stats.budget / 1_MB,
             stats.budget == 0 ? 0 : static_cast<int>(100 * stats.committed / stats.budget),
             stats.evictions, stats.evicted_bytes / 1_MB, stats.refused);
}

} // namespace Gr2Port::D3D12
