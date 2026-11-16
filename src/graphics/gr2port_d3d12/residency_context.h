// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "graphics/gr2port_d3d12/arena.h"
#include "graphics/gr2port_d3d12/command.h"
#include "graphics/gr2port_d3d12/pipeline.h"
#include "graphics/gr2port_d3d12/residency_state.h"
namespace Gr2Port::D3D12 {
struct ResidencyContext {
    Device* device = nullptr;
    Allocator* allocator = nullptr;
    ArenaSet* arenas = nullptr;
    BindlessHeap* heap = nullptr;
    RootSignature* root = nullptr;
    u64 completed = 0;
};
struct UploadBuffer {
    ResourcePtr resource;
    u8* cpu = nullptr;
    bool Create(ResidencyContext& context, u64 bytes) {
        resource = context.allocator->CreateBuffer(Residency::Align(bytes, 4),
                                                   D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ);
        void* data = nullptr;
        const D3D12_RANGE range{0, 0};
        if (!resource || FAILED(resource->Map(0, &range, &data)))
            return false;
        cpu = static_cast<u8*>(data);
        return true;
    }
    void Unmap() {
        if (cpu) {
            resource->Unmap(0, nullptr);
            cpu = nullptr;
        }
    }
};
} // namespace Gr2Port::D3D12
