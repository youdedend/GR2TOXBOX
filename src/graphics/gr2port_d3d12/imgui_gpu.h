// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "graphics/gr2port_d3d12/command.h"
#include "graphics/gr2port_d3d12/imgui_snapshot.h"
#include <map>
namespace Gr2Port::D3D12 {
class ImGuiGpu {
public:
    bool Create(Device& device, const RootSignature& root, BindlessHeap& heap);
    // Called after CommandRing::BeginFrame has waited for this slot's fence.
    bool Record(CommandList& list, u32 slot, const std::shared_ptr<const ImGuiRelay::Snapshot>& frame);
    void Destroy(); // only after GPU idle
private:
    struct Texture { ComPtr<ID3D12Resource> resource; DescriptorSlot srv; u64 last_use=0; };
    struct Frame { ComPtr<ID3D12Resource> vertices, indices; std::vector<ComPtr<ID3D12Resource>> uploads; };
    Device* device_=nullptr;
    const RootSignature* root_=nullptr;
    BindlessHeap* heap_=nullptr;
    Pipeline pipeline_;
    std::array<Frame,FramesInFlight> frames_;
    std::map<u64,Texture> textures_;
    std::vector<DescriptorSlot> free_slots_;
    std::array<DescriptorSlot,FramesInFlight> vertex_slots_{};
    u64 serial_=0;
};
}
