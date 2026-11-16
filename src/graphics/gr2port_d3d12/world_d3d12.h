// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "graphics/gr2port_d3d12/core_state.h"
#include "graphics/gr2port_d3d12/point_lights_d3d12.h"
#include "graphics/gr2port_d3d12/residency_context.h"
#include "graphics/gr2port_d3d12/shadows_d3d12.h"
namespace Gr2Port::D3D12 {
// Shared resources, not pass policy. All resize/destruction requires graphics idle.
struct CoreTargets {
    enum Image {
        Depth,
        Albedo,
        Normal,
        Material,
        Hdr,
        Toned,
        Picture,
        White,
        Black,
        ShadowArray,
        Count
    };
    struct Target {
        ResourcePtr image;
        DescriptorSlot srv;
        D3D12_RESOURCE_STATES state;
    };
    ResidencyContext* context = nullptr;
    std::array<Target, Count> targets;
    ComPtr<ID3D12DescriptorHeap> rtvs, dsvs;
    u32 width = 0, height = 0, pitch = 0;
    ResourcePtr aa;
    DescriptorSlot aa_uav;
    ResourcePtr world_lines;
    DescriptorSlot world_lines_srv, world_lines_uav;
    D3D12_RESOURCE_STATES world_lines_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES aa_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES scratch_state = D3D12_RESOURCE_STATE_COMMON;
    ArenaAllocation meter, zero;
    bool initialized = false;
    bool Start(ResidencyContext& c);
    bool Resize(u32 w, u32 h);
    void Stop();
    void Bind(CommandList& list) const;
    void To(CommandList& list, Image image, D3D12_RESOURCE_STATES state);
    void ScratchTo(CommandList& list, D3D12_RESOURCE_STATES state);
    void Uav(CommandList& list, ID3D12Resource* resource) const;
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(Image image) const;
    D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const;
    u32 Srv(Image image) const {
        return targets[image].srv.index;
    }
};
class WorldRenderer {
    CoreTargets* t_ = nullptr;
    std::array<Pipeline, 10> pipelines_;
    Pipeline world_lines_pipeline_;
    std::array<UploadBuffer, FramesInFlight> uploads_;
    std::array<ArenaAllocation, FramesInFlight> frame_;
    u64 skipped_ = 0;
    u64 recorded_draws_ = 0;
    u64 current_scene_address_ = 0;
    PointLights point_lights_;
    Shadows shadows_;
    u32 world_lines_frame_ = 0;
    void DrawList(CommandList&, const Scene::Header&, const u8*, u64, u32, u32, u32);
    void RecordSurfaces(CommandList&, const Scene::Header&, const u8*, u64, u32);
    void RecordWorldLines(CommandList&, u32);
    void RecordLight(CommandList&, const Scene::Header&, u32);
    void RecordForward(CommandList&, const Scene::Header&, const u8*, u64, u32);

public:
    bool Start(CoreTargets& targets);
    bool Record(CommandList& list, const Scene::Header& h, const u8* data, u64 address, u32 slot);
    void Stop();
    u64 Skipped() const {
        return skipped_;
    }
    u64 RecordedDraws() const {
        return recorded_draws_;
    }
};
} // namespace Gr2Port::D3D12
