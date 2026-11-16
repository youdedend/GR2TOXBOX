// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics/gr2port/meshes.h"
#include "graphics/gr2port/settings.h"
#include "graphics/gr2port_d3d12/shadows_d3d12.h"
#include "graphics/gr2port_d3d12/world_d3d12.h"
#include "shadow_alpha_frag.h"
#include "shadow_alpha_vert.h"
#include "shadow_vert.h"
namespace Gr2Port::D3D12 {
namespace {
constexpr auto Rest = static_cast<D3D12_RESOURCE_STATES>(
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}
bool Shadows::Start(CoreTargets& t) {
    t_ = &t;
    auto& c = *t.context;
    const auto quality = Settings::Get().graphics.shadows;
    constexpr u32 sides[3][2]{{2048, 4096}, {2048, 2048}, {1024, 2048}};
    const auto q = std::min(u32(quality), 2u);
    sizes_ = {sides[q][0], sides[q][1]};
    draw_ = quality != Settings::Quality::Off;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = Scene::Cascades;
    if (FAILED(c.device->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(dsvs_.GetAddressOf()))))
        return false;
    const auto step = c.device->dsv_descriptor_size;
    for (u32 i = 0; i < 2; ++i) {
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = d.Height = sizes_[i];
        d.DepthOrArraySize = i == 0 ? 2 : 1;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R16_TYPELESS;
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        maps_[i] = c.allocator->CreateResource(d, D3D12_HEAP_TYPE_DEFAULT, Rest);
        srvs_[i] = c.heap->AllocateSrv();
        if (!maps_[i] || !srvs_[i])
            return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R16_UNORM;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.ViewDimension =
            i == 0 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
        if (i == 0) {
            srv.Texture2DArray.MipLevels = 1;
            srv.Texture2DArray.ArraySize = 2;
        } else
            srv.Texture2D.MipLevels = 1;
        c.heap->WriteTexture(srvs_[i], maps_[i].Get(), srv);
    }
    for (u32 cascade = 0; cascade < Scene::Cascades; ++cascade) {
        D3D12_DEPTH_STENCIL_VIEW_DESC d{};
        d.Format = DXGI_FORMAT_D16_UNORM;
        d.ViewDimension =
            cascade < 2 ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY : D3D12_DSV_DIMENSION_TEXTURE2D;
        if (cascade < 2) {
            d.Texture2DArray.ArraySize = 1;
            d.Texture2DArray.FirstArraySlice = cascade;
        }
        auto handle = dsvs_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += cascade * step;
        c.device->device->CreateDepthStencilView(maps_[cascade < 2 ? 0 : 1].Get(), &d, handle);
    }
    state_ = Rest;
    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sampler.MaxAnisotropy = 1;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    c.heap->WriteSampler({4}, sampler);
    return true;
}
Pipeline* Shadows::Get(CoreState::ShadowKey key) {
    auto& c = *t_->context;
    Cached* free = nullptr;
    const u64 fence = c.device->timeline_value + 1;
    for (auto& entry : cache_) {
        if (entry.pipeline.IsValid() && entry.key == key) {
            entry.fence = fence;
            return &entry.pipeline;
        }
        if (!free && (!entry.pipeline.IsValid() || entry.fence <= c.completed))
            free = &entry;
    }
    // Never evict a PSO referenced by this list or by in-flight submissions.
    if (!free)
        return nullptr;
    GraphicsPipelineDesc d;
    d.vs = key.list >= 2 ? Shaders::shadow_alpha_vert_span : Shaders::shadow_vert_span;
    if (key.list >= 2)
        d.ps = Shaders::shadow_alpha_frag_span;
    d.depth_format = DXGI_FORMAT_D16_UNORM;
    d.depth_test = d.depth_write = true;
    d.depth_compare = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    d.front_counter_clockwise = true;
    d.cull = (key.list % 2) == 0 ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    d.depth_bias = true;
    d.depth_bias_constant = key.constant;
    d.depth_bias_slope = key.slope;
    Pipeline next;
    if (!CreateGraphicsPipeline(c.device->device.Get(), *c.root, d, next))
        return nullptr;
    free->pipeline = std::move(next);
    free->key = key;
    free->fence = fence;
    return &free->pipeline;
}
void Shadows::Record(CommandList& cmd, const Scene::Header& h, const u8* data, u64 address) {
    auto& t = *t_;
    auto& c = *t.context;
    t.Bind(cmd);
    for (auto& map : maps_)
        cmd.Transition(map.Get(), state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto* index = Meshes::IndexBuffer();
    const u64 indices = index ? index->GetDesc().Width / 2 : 0;
    const CoreState::Limits limits{c.arenas->Capacity(ArenaKind::Geometry),
                                   c.arenas->Capacity(ArenaKind::Scene),
                                   c.arenas->Capacity(ArenaKind::Skinning)};
    if (index) {
        const D3D12_INDEX_BUFFER_VIEW ib{index->GetGPUVirtualAddress(), u32(indices * 2),
                                         DXGI_FORMAT_R16_UINT};
        cmd.SetIndexBuffer(ib);
    }
    const auto step = c.device->dsv_descriptor_size;
    for (u32 cascade = 0; cascade < Scene::Cascades; ++cascade) {
        auto dsv = dsvs_->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += cascade * step;
        cmd.list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
        cmd.list->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        cmd.SetViewport(sizes_[cascade < 2 ? 0 : 1], sizes_[cascade < 2 ? 0 : 1]);
        if (!draw_ || !index || h.casters > Scene::g_room.casters ||
            !(h.cascades & (1u << cascade)))
            continue;
        for (u32 list = 0; list < Scene::CasterLists; ++list) {
            const u32 count = h.caster_commands[cascade][list];
            if (!count || count > Scene::CasterCommands())
                continue;
            auto* pipeline =
                Get(CoreState::Bias(list, h.depth_bias[cascade][0], h.depth_bias[cascade][1]));
            if (!pipeline)
                continue;
            cmd.SetPipeline(*pipeline);
            const u64 first = Scene::FirstCasterCommand(h, cascade, list);
            if (first + count >
                u64(Scene::Cascades) * Scene::CasterRegions() * Scene::CasterCommands())
                continue;
            for (u32 i = 0; i < count; ++i) {
                Scene::Command draw{};
                std::memcpy(&draw,
                            data + Scene::CasterCommandsOffset() + (first + i) * sizeof(draw),
                            sizeof(draw));
                if (!draw.index_count || draw.first_index > indices ||
                    draw.index_count > indices - draw.first_index ||
                    draw.first_instance >= h.casters ||
                    draw.instance_count > h.casters - draw.first_instance)
                    continue;
                for (u32 j = 0; j < draw.instance_count; ++j) {
                    const u32 base = draw.first_instance + j;
                    Scene::Caster caster{};
                    std::memcpy(&caster, data + Scene::CastersOffset() + u64(base) * sizeof(caster),
                                sizeof(caster));
                    if (caster.table >= Scene::CasterTables ||
                        !CoreState::Streams(caster.vertices, caster.stride, caster.skin,
                                            caster.palette, caster.bones, limits))
                        continue;
                    const CoreState::ShadowRoot root{
                        CoreState::Unpack(address + Scene::CastersOffset()),
                        CoreState::Unpack(address + Scene::ShadowsOffset), cascade, base};
                    cmd.SetDrawConstants(&root, sizeof(root));
                    cmd.list->DrawIndexedInstanced(draw.index_count, 1, draw.first_index,
                                                   draw.vertex_offset, 0);
                }
            }
        }
    }
    cmd.list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    for (auto& map : maps_)
        cmd.Transition(map.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, Rest);
    state_ = Rest;
}
void Shadows::Stop() {
    for (auto& entry : cache_)
        entry = {};
    for (auto& map : maps_)
        map.Reset();
    dsvs_.Reset();
    t_ = nullptr;
}
} // namespace Gr2Port::D3D12
