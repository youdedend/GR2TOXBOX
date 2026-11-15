// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "graphics/gr2port_d3d12/descriptors.h"

namespace Gr2Port::D3D12 {

namespace {

D3D12_CPU_DESCRIPTOR_HANDLE AdvanceCpu(D3D12_CPU_DESCRIPTOR_HANDLE base, u32 increment,
                                       u32 index) {
    base.ptr += static_cast<SIZE_T>(increment) * index;
    return base;
}

D3D12_GPU_DESCRIPTOR_HANDLE AdvanceGpu(D3D12_GPU_DESCRIPTOR_HANDLE base, u32 increment,
                                       u32 index) {
    base.ptr += static_cast<UINT64>(increment) * index;
    return base;
}

} // namespace

bool BindlessHeap::Create(Device& device) {
    device_ = device.device.Get();
    if (device_ == nullptr) {
        LOG_CRITICAL(Graphics, "D3D12: bindless heaps need a created device");
        return false;
    }
    srv_increment_ = device.srv_descriptor_size;
    sampler_increment_ = device.sampler_descriptor_size;

    // --- CBV/SRV/UAV heap --------------------------------------------------
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = SrvCapacity;
    // Shader-visible: the descriptor tables in space100..space111 index this heap.
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srv_desc.NodeMask = 0;
    HRESULT hr = device_->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&srv_heap_));
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12: cannot create the {}-slot bindless SRV heap: {}",
                     SrvCapacity, HresultToString(hr));
        return false;
    }
    srv_cpu_base_ = srv_heap_->GetCPUDescriptorHandleForHeapStart();
    srv_gpu_base_ = srv_heap_->GetGPUDescriptorHandleForHeapStart();

    // --- Sampler heap ------------------------------------------------------
    D3D12_DESCRIPTOR_HEAP_DESC sampler_desc{};
    sampler_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_desc.NumDescriptors = SamplerCapacity;
    sampler_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    sampler_desc.NodeMask = 0;
    hr = device_->CreateDescriptorHeap(&sampler_desc, IID_PPV_ARGS(&sampler_heap_));
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12: cannot create the {}-slot sampler heap: {}",
                     SamplerCapacity, HresultToString(hr));
        return false;
    }
    sampler_cpu_base_ = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
    sampler_gpu_base_ = sampler_heap_->GetGPUDescriptorHandleForHeapStart();

    // --- Null descriptors --------------------------------------------------
    // Slot 0 of each table is a null SRV and a default sampler, so a draw that
    // forgot to set a slot reads a defined result (black texture, clamp
    // sampler) instead of whatever the heap happened to hold.
    D3D12_SHADER_RESOURCE_VIEW_DESC null_srv{};
    null_srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    null_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null_srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(nullptr, &null_srv,
                                      AdvanceCpu(srv_cpu_base_, srv_increment_, NullSlot));

    D3D12_SAMPLER_DESC null_sampler{};
    null_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    null_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    null_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    null_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    null_sampler.MaxAnisotropy = 1;
    null_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    null_sampler.MaxLOD = D3D12_FLOAT32_MAX;
    device_->CreateSampler(&null_sampler,
                           AdvanceCpu(sampler_cpu_base_, sampler_increment_, NullSlot));

    LOG_INFO(Graphics, "D3D12: bindless heaps ready ({} SRV/UAV slots, {} sampler slots)",
             SrvCapacity, SamplerCapacity);
    return true;
}

void BindlessHeap::Destroy() {
    srv_heap_.Reset();
    sampler_heap_.Reset();
    device_ = nullptr;
    srv_used_ = NullSlot + 1;
    sampler_used_ = NullSlot + 1;
}

DescriptorSlot BindlessHeap::AllocateSrv(u32 count) {
    std::scoped_lock lock{mutex_};
    if (srv_used_ + count > SrvCapacity) {
        LOG_CRITICAL(Graphics, "D3D12: the bindless SRV heap is exhausted ({}/{})", srv_used_,
                     SrvCapacity);
        return DescriptorSlot{DescriptorSlot::kInvalid, false};
    }
    const u32 index = srv_used_;
    srv_used_ += count;
    return DescriptorSlot{index, false};
}

DescriptorSlot BindlessHeap::AllocateSampler(u32 count) {
    std::scoped_lock lock{mutex_};
    if (sampler_used_ + count > SamplerCapacity) {
        LOG_CRITICAL(Graphics, "D3D12: the bindless sampler heap is exhausted ({}/{})",
                     sampler_used_, SamplerCapacity);
        return DescriptorSlot{DescriptorSlot::kInvalid, true};
    }
    const u32 index = sampler_used_;
    sampler_used_ += count;
    return DescriptorSlot{index, true};
}

void BindlessHeap::Free(DescriptorSlot slot, u32 count) {
    // Bindless slots are intentionally not recycled one at a time. A shader
    // that still holds an index would silently read a different texture after a
    // slot is reused, which is the worst class of visual bug to debug. The whole
    // table is rebuilt when the texture cache is evicted at a level load, which
    // is also when the memory budget is reclaimed.
    (void)slot;
    (void)count;
}

void BindlessHeap::WriteTexture(DescriptorSlot slot, ID3D12Resource* resource,
                                const D3D12_SHADER_RESOURCE_VIEW_DESC& desc) {
    if (!slot || device_ == nullptr) {
        return;
    }
    device_->CreateShaderResourceView(resource, &desc,
                                      AdvanceCpu(srv_cpu_base_, srv_increment_, slot.index));
}

void BindlessHeap::WriteBuffer(DescriptorSlot slot, ID3D12Resource* resource, u64 first_element,
                               u32 num_elements, u32 stride) {
    if (!slot || device_ == nullptr) {
        return;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.FirstElement = first_element;
    desc.Buffer.NumElements = num_elements;
    desc.Buffer.StructureByteStride = stride;
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device_->CreateShaderResourceView(resource, &desc,
                                      AdvanceCpu(srv_cpu_base_, srv_increment_, slot.index));
}

void BindlessHeap::WriteBufferUav(DescriptorSlot slot, ID3D12Resource* resource, u64 first_element,
                                  u32 num_elements, u32 stride) {
    if (!slot || device_ == nullptr) {
        return;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = first_element;
    desc.Buffer.NumElements = num_elements;
    desc.Buffer.StructureByteStride = stride;
    desc.Buffer.CounterOffsetInBytes = 0;
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device_->CreateUnorderedAccessView(resource, nullptr, &desc,
                                       AdvanceCpu(srv_cpu_base_, srv_increment_, slot.index));
}

void BindlessHeap::WriteSampler(DescriptorSlot slot, const D3D12_SAMPLER_DESC& desc) {
    if (!slot || device_ == nullptr) {
        return;
    }
    device_->CreateSampler(&desc, AdvanceCpu(sampler_cpu_base_, sampler_increment_, slot.index));
}

D3D12_GPU_DESCRIPTOR_HANDLE BindlessHeap::SrvTableGpu(u32 offset) const {
    return AdvanceGpu(srv_gpu_base_, srv_increment_, offset);
}

D3D12_GPU_DESCRIPTOR_HANDLE BindlessHeap::SamplerTableGpu(u32 offset) const {
    return AdvanceGpu(sampler_gpu_base_, sampler_increment_, offset);
}

} // namespace Gr2Port::D3D12
