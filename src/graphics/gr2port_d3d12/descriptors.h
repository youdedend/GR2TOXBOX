// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include <utility>
#include <vector>

#include "graphics/gr2port_d3d12/device.h"

namespace Gr2Port::D3D12 {

/// A descriptor slot in the global bindless tables.
///
/// The backend never binds descriptor tables per draw. Instead every resource
/// gets a slot once, at creation, and the shaders index the heaps directly:
///
///   Texture2D t = ResourceDescriptorHeap[srv];
///   SamplerState s = SamplerDescriptorHeap[smp];
///
/// `srv`/`smp` are 32-bit values the backend writes into the draw's constants.
/// This is the D3D12 equivalent of upstream's Vulkan
/// `descriptorBindingPartiallyBound` + `runtimeDescriptorArray` combination, and
/// it is what makes a 1,000-draw frame cheap: there is no per-draw descriptor
/// copy and no root signature churn.
///
/// Slots are allocated in blocks from a free list, so a texture that comes and
/// goes (a streaming mip, a movie frame) does not fragment the heap.
struct DescriptorSlot {
    u32 index = 0;
    /// Set when the slot refers to a sampler table rather than an SRV table.
    bool sampler = false;

    explicit operator bool() const noexcept {
        return index != kInvalid;
    }
    static constexpr u32 kInvalid = 0xFFFFFFFFu;
};

/// The bindless heap set: one CBV/SRV/UAV heap and one sampler heap, both
/// shader-visible, both large enough for the whole frame plus the texture cache.
///
/// Sizes follow the shader-visible limits of FL 12_1 (1,000,000 descriptors of
/// CBV/SRV/UAV, 2,048 samplers). The backend allocates far less than the limit:
/// 65,536 SRV slots and 256 sampler slots keep the heap small enough to stay
/// resident while allowing every texture the port uploads to have a stable slot.
class BindlessHeap {
public:
    static constexpr u32 SrvCapacity = 65536;
    static constexpr u32 SamplerCapacity = 256;
    /// Slot 0 of both tables is a null descriptor, so a zero index in a draw
    /// yields a defined (black / clamp) result instead of reading garbage.
    static constexpr u32 NullSlot = 0;

    bool Create(Device& device);
    void Destroy();

    /// Reserves `count` consecutive SRV slots. Returns kInvalid when the heap
    /// is full, which the caller must treat as a fatal out-of-descriptors error.
    DescriptorSlot AllocateSrv(u32 count = 1);
    DescriptorSlot AllocateSampler(u32 count = 1);
    void Free(DescriptorSlot slot, u32 count = 1);

    /// Writes a texture SRV into an allocated slot.
    void WriteTexture(DescriptorSlot slot, ID3D12Resource* resource,
                      const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);
    /// Writes a buffer SRV (ByteAddressBuffer for the unified arena) into a slot.
    void WriteBuffer(DescriptorSlot slot, ID3D12Resource* resource, u64 first_element,
                     u32 num_elements, u32 stride);
    /// Writes a buffer UAV (RWByteAddressBuffer) into a slot.
    void WriteBufferUav(DescriptorSlot slot, ID3D12Resource* resource, u64 first_element,
                        u32 num_elements, u32 stride);
    void WriteSampler(DescriptorSlot slot, const D3D12_SAMPLER_DESC& desc);

    /// The GPU handles the root signature expects, for the rare draw that must
    /// bind a table explicitly (ExecuteIndirect signatures, debug overlays).
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE SrvTableGpu(u32 offset = 0) const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE SamplerTableGpu(u32 offset = 0) const;

    [[nodiscard]] ID3D12DescriptorHeap* SrvHeap() const {
        return srv_heap_.Get();
    }
    [[nodiscard]] ID3D12DescriptorHeap* SamplerHeap() const {
        return sampler_heap_.Get();
    }
    [[nodiscard]] u32 SrvDescriptorsUsed() const {
        return srv_used_;
    }
    [[nodiscard]] u32 SrvDescriptorCapacity() const {
        return SrvCapacity;
    }

private:
    /// The device that created the heaps, used to write descriptors.
    /// Not owning: the Device outlives the heap.
    ID3D12Device8* device_ = nullptr;
    ComPtr<ID3D12DescriptorHeap> srv_heap_;
    ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_base_{};
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_base_{};
    D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_base_{};
    D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_base_{};
    u32 srv_increment_ = 0;
    u32 sampler_increment_ = 0;
    u32 srv_used_ = NullSlot + 1;
    u32 sampler_used_ = NullSlot + 1;
    std::mutex mutex_;
};

} // namespace Gr2Port::D3D12
