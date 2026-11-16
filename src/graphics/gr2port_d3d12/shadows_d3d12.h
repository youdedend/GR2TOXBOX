// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "graphics/gr2port_d3d12/command.h"
#include "graphics/gr2port_d3d12/core_state.h"
#include "graphics/gr2port_d3d12/pipeline.h"
namespace Gr2Port::D3D12 {
struct CoreTargets;
class Shadows {
    struct Cached {
        CoreState::ShadowKey key{};
        Pipeline pipeline;
        u64 fence = 0;
    };
    CoreTargets* t_ = nullptr;
    std::array<ResourcePtr, 2> maps_;
    std::array<DescriptorSlot, 2> srvs_;
    ComPtr<ID3D12DescriptorHeap> dsvs_;
    std::array<Cached, 48> cache_;
    std::array<u32, 2> sizes_{};
    bool draw_ = true;
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_COMMON;
    Pipeline* Get(CoreState::ShadowKey);

public:
    bool Start(CoreTargets&);
    void Record(CommandList&, const Scene::Header&, const u8*, u64);
    CoreState::TextureRoot Textures() const {
        return {srvs_[0].index, srvs_[1].index, srvs_[0].index, srvs_[1].index};
    }
    void Stop();
};
} // namespace Gr2Port::D3D12
