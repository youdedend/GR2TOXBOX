// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics/gr2port_d3d12/point_lights_d3d12.h"
#include "graphics/gr2port_d3d12/world_d3d12.h"
#include "light_grid_comp.h"
namespace Gr2Port::D3D12 {
bool PointLights::Start(CoreTargets& t) {
    t_ = &t;
    auto& c = *t.context;
    grid_ = c.arenas->Allocate(ArenaKind::Scratch,
                               u64(Scene::LightGridColumns) * Scene::LightGridRows *
                                   Scene::LightGridSlices * Scene::LightsPerCell * 4);
    return grid_.size && CreateComputePipeline(c.device->device.Get(), *c.root,
                                               Shaders::light_grid_comp_span, pipeline_);
}
void PointLights::Record(CommandList& list, const Scene::Header& h, u64 address) {
    if (!h.point_lights || h.point_lights > Scene::MaxPointLights)
        return;
    auto& t = *t_;
    t.Bind(list);
    t.ScratchTo(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const CoreState::Ref root[]{CoreState::Unpack(address + Scene::LightOffset),
                                CoreState::Unpack(address + Scene::PointLightsOffset), Grid()};
    list.SetPipeline(pipeline_);
    list.SetDrawConstants(root, sizeof(root));
    list.Dispatch(Scene::LightGridColumns, Scene::LightGridRows, Scene::LightGridSlices);
    t.Uav(list, t.context->arenas->Resource(ArenaKind::Scratch));
    t.ScratchTo(list,
                static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
}
} // namespace Gr2Port::D3D12
