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
    allocator_desc_.FrameInUseCount = FramesInFlight;
    // No DXGI adapter is needed: the device has one, and passing it would let
    // D3D12MA query the adapter again on a console where that is restricted.
    allocator_desc_.pAdapter = nullptr;
    allocator_desc_.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;

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

ComPtr<ID3D12Resource> Allocator::CreateResource(const D3D12_RESOURCE_DESC& desc,
                                                 D3D12_HEAP_TYPE heap_type,
                                                 D3D12_RESOURCE_STATES initial_state,
                                                 const D3D12_CLEAR_VALUE* clear) {
    if (!d3d12ma_) {
        return nullptr;
    }

    // Budget check before the allocation, not after: a failed D3D12
    // allocation inside an AppContainer leaves the device in a state where the
    // next commit also fails, so the refusal has to happen here.
    const D3D12_RESOURCE_ALLOCATION_INFO info = device_->device->GetResourceAllocationInfo(0, 1, &desc);
    u64 size = info.SizeInBytes;
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
        if (budget_ != 0 && now + size > budget_) {
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

    ComPtr<D3D12MA::Allocation> allocation;
    const D3D12_HEAP_PROPERTIES props = HeapProperties(heap_type);
    HRESULT hr = d3d12ma_->CreateResource(
        &alloc_desc, &props, D3D12_HEAP_FLAG_NONE, &desc, initial_state, clear, &allocation,
        nullptr);
    if (FAILED(hr)) {
        refused_++;
        LOG_CRITICAL(Graphics, "D3D12: CreateResource({} KiB) failed: {}", size / 1_KB,
                     HresultToString(hr));
        return nullptr;
    }

    committed_.fetch_add(size, std::memory_order_relaxed);
    // The Allocation owns the resource; D3D12MA keeps the resource alive as long
    // as the allocation exists. The caller only sees the resource, and the
    // allocation's lifetime is tied to it by D3D12MA's own bookkeeping in
    // ReleaseAllocation when the resource's final reference drops.
    ComPtr<ID3D12Resource> resource;
    allocation->GetResource(IID_PPV_ARGS(&resource));
    return resource;
}

ComPtr<ID3D12Resource> Allocator::CreateBuffer(u64 size, D3D12_HEAP_TYPE heap_type,
                                               D3D12_RESOURCE_FLAGS flags,
                                               D3D12_RESOURCE_STATES initial_state,
                                               const wchar_t* name, bool evictable) {
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
