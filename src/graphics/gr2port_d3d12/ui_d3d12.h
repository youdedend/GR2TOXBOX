// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "graphics/gr2port_d3d12/residency_context.h"
#include "graphics/gr2port_d3d12/ui_state.h"
#include <unordered_map>
namespace Gr2Port::D3D12 {
// Render-thread only. Stop and swapchain-format changes require GPU idle.
class UiRenderer {
  public:
    bool Start(ResidencyContext& context, DXGI_FORMAT present_format, float layout_aspect);
    bool Resize(DXGI_FORMAT present_format);
    bool Prepare(CommandList& list, const Scene::Header& header, const u8* packet, u64 submission);
    bool Record(CommandList& list, const Scene::Header& header, const u8* packet, u64 address);
    bool RecordPresent(CommandList& list, const Scene::Header& header, u32 width, u32 height);
    struct Extent {u32 width=0,height=0;};
    Extent WorldExtent(const Scene::Header& header) const;
    Extent BeginWorld(CommandList& list,const Scene::Header& header);
    void EndWorld(CommandList& list);
    bool HasPicture(const Scene::Header& header) const;
    void Stop();

  private:
    struct Target {
        ResourcePtr image;
        u32 width = 0, height = 0;
        u64 base = 0;
    };
    ResidencyContext* context_ = nullptr;
    std::array<Target, Scene::MaxUiTargets> targets_;
    ComPtr<ID3D12DescriptorHeap> rtvs_;
    std::unordered_map<u64, Pipeline> pipelines_;
    Pipeline present_;
    DXGI_FORMAT present_format_ = DXGI_FORMAT_UNKNOWN;
    bool narrow_ = false;
    u32 open_world_=Scene::MaxUiTargets;
    void Bind(CommandList& list);
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(u32 index) const;
    Pipeline* PipelineFor(const Scene::UiRun& run);
};
} // namespace Gr2Port::D3D12
