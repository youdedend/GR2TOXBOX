// SPDX-License-Identifier: GPL-2.0-or-later
#include "character_frag.h"
#include "character_vert.h"
#include "deferred_frag.h"
#include "fullscreen_vert.h"
#include "graphics/gr2port/deformers.h"
#include "graphics/gr2port/meshes.h"
#include "graphics/gr2port_d3d12/world_d3d12.h"
#include "interior_frag.h"
#include "lambert_frag.h"
#include "lambert_vert.h"
#include "vision_frag.h"
#include "world_alpha_frag.h"
#include "world_decal_frag.h"
#include "world_decal_vert.h"
#include "world_frag.h"
#include "world_vert.h"
namespace Gr2Port::D3D12 {
namespace {
constexpr auto Rest = static_cast<D3D12_RESOURCE_STATES>(
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
constexpr DXGI_FORMAT Surfaces[]{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R16G16_FLOAT,
                                 DXGI_FORMAT_R8G8B8A8_UNORM};
constexpr DXGI_FORMAT HdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr u32 UploadBytes = 4096;
} // namespace
bool CoreTargets::Start(ResidencyContext& c) {
    context = &c;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = Count;
    if (FAILED(c.device->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(rtvs.GetAddressOf()))))
        return false;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = 1;
    if (FAILED(c.device->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(dsvs.GetAddressOf()))))
        return false;
    for (auto& target : targets) {
        target.srv = c.heap->AllocateSrv();
        if (!target.srv)
            return false;
    }
    aa_uav = c.heap->AllocateSrv();
    if (!aa_uav)
        return false;
    meter = c.arenas->Allocate(ArenaKind::Scratch, 16);
    zero = c.arenas->Allocate(ArenaKind::Scratch, 16);
    // Bind safe comparison and point samplers before the per-scene shadow pass
    // replaces the comparison state with its forward-Z LESS_EQUAL sampler.
    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxAnisotropy = 1;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    c.heap->WriteSampler({4}, sampler);
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    c.heap->WriteSampler({2}, sampler);
    return meter.size && zero.size && zero.ref.offset == meter.ref.offset + 16;
}
D3D12_CPU_DESCRIPTOR_HANDLE CoreTargets::Rtv(Image i) const {
    auto h = rtvs->GetCPUDescriptorHandleForHeapStart();
    h.ptr += size_t(i) * context->device->rtv_descriptor_size;
    return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE CoreTargets::Dsv() const {
    return dsvs->GetCPUDescriptorHandleForHeapStart();
}
void CoreTargets::Bind(CommandList& list) const {
    list.SetRootSignature(*context->root);
    ID3D12DescriptorHeap* heaps[]{context->heap->SrvHeap(), context->heap->SamplerHeap()};
    list.list->SetDescriptorHeaps(2, heaps);
    list.SetArenaTable(*context->arenas, *context->heap);
    list.SetDescriptorTables(*context->heap);
    list.SetViewport(width, height);
    list.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
void CoreTargets::To(CommandList& list, Image i, D3D12_RESOURCE_STATES state) {
    auto& t = targets[i];
    if (t.state != state) {
        list.Transition(t.image.Get(), t.state, state);
        t.state = state;
    }
}
void CoreTargets::ScratchTo(CommandList& list, D3D12_RESOURCE_STATES state) {
    if (scratch_state != state) {
        list.Transition(context->arenas->Resource(ArenaKind::Scratch), scratch_state, state);
        scratch_state = state;
    }
}
void CoreTargets::Uav(CommandList& list, ID3D12Resource* resource) const {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = resource;
    list.list->ResourceBarrier(1, &b);
}
bool CoreTargets::Resize(u32 w, u32 h) {
    if (!w || !h || w > 8192 || h > 8192)
        return false;
    if (width == w && height == h)
        return true;
    WaitForFence(*context->device, context->device->timeline_value);
    // Shared stable handles may be rewritten only after every old user retires.
    for (u32 i = 0; i < Count; ++i) {
        const auto image = static_cast<Image>(i);
        auto& t = targets[i];
        DXGI_FORMAT format = i == Depth               ? DXGI_FORMAT_R32_TYPELESS
                             : i <= Material          ? Surfaces[i - 1]
                             : i == Hdr || i == Black ? HdrFormat
                             : i == Picture           ? DXGI_FORMAT_R8G8B8A8_TYPELESS
                             : i >= White             ? DXGI_FORMAT_R32_FLOAT
                                                      : DXGI_FORMAT_R8G8B8A8_UNORM;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = i >= White ? 1 : w;
        desc.Height = i >= White ? 1 : h;
        desc.DepthOrArraySize = i == ShadowArray ? 2 : 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = i == Depth     ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                     : i == Picture ? D3D12_RESOURCE_FLAG_NONE
                                    : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        auto next = context->allocator->CreateResource(desc, D3D12_HEAP_TYPE_DEFAULT, Rest);
        if (!next)
            return false;
        t.image = std::move(next);
        t.state = Rest;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = i == Depth     ? DXGI_FORMAT_R32_FLOAT
                     : i == Picture ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                    : format;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.ViewDimension =
            i == ShadowArray ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
        if (i == ShadowArray) {
            srv.Texture2DArray.MipLevels = 1;
            srv.Texture2DArray.ArraySize = 2;
        } else
            srv.Texture2D.MipLevels = 1;
        context->heap->WriteTexture(t.srv, t.image.Get(), srv);
        if (i == Depth) {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
            dsv.Format = DXGI_FORMAT_D32_FLOAT;
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            context->device->device->CreateDepthStencilView(t.image.Get(), &dsv, Dsv());
        } else if (i != Picture)
            context->device->device->CreateRenderTargetView(t.image.Get(), nullptr, Rtv(image));
    }
    pitch = u32(Residency::Align(u64(w) * 4, 256));
    aa = context->allocator->CreateBuffer(u64(pitch) * h, D3D12_HEAP_TYPE_DEFAULT,
                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!aa)
        return false;
    context->heap->WriteBufferUav(aa_uav, aa.Get(), 0, pitch * h / 4, 0);
    aa_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    width = w;
    height = h;
    return true;
}
void CoreTargets::Stop() {
    for (auto& t : targets)
        t.image.Reset();
    aa.Reset();
    rtvs.Reset();
    dsvs.Reset();
    context = nullptr;
    width = height = 0;
}
bool WorldRenderer::Start(CoreTargets& targets) {
    t_ = &targets;
    auto& c = *targets.context;
    for (u32 i = 0; i < FramesInFlight; ++i) {
        frame_[i] = c.arenas->Allocate(ArenaKind::Scratch, sizeof(Scene::Light) + 32);
        if (!frame_[i].size || !uploads_[i].Create(c, UploadBytes))
            return false;
    }
    static_assert(sizeof(Scene::Light) + 64 < UploadBytes);
    for (u32 i = 0; i < pipelines_.size(); ++i) {
        GraphicsPipelineDesc d;
        d.front_counter_clockwise = true;
        d.depth_compare = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        d.depth_test = d.depth_write = i < 4 || (i >= 5 && i <= 8);
        d.depth_format = d.depth_test ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
        if (i < 4) {
            d.vs = Shaders::world_vert_span;
            d.ps = i < 2 ? Shaders::world_frag_span : Shaders::world_alpha_frag_span;
            d.color_formats = Surfaces;
            d.cull = (i & 1) ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
        } else if (i == 4) {
            d.vs = Shaders::world_decal_vert_span;
            d.ps = Shaders::world_decal_frag_span;
            d.color_formats = Surfaces;
            d.cull = D3D12_CULL_MODE_FRONT;
            d.alpha_blend = true;
            d.keep_target_alpha = true;
        } else {
            d.color_formats = {&HdrFormat, 1};
            d.vs = i == 7   ? Shaders::lambert_vert_span
                   : i == 9 ? Shaders::fullscreen_vert_span
                            : Shaders::character_vert_span;
            d.ps = i == 5   ? Shaders::character_frag_span
                   : i == 6 ? Shaders::interior_frag_span
                   : i == 7 ? Shaders::lambert_frag_span
                   : i == 8 ? Shaders::vision_frag_span
                            : Shaders::deferred_frag_span;
            d.cull = i == 9 ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
        }
        if (!CreateGraphicsPipeline(c.device->device.Get(), *c.root, d, pipelines_[i]))
            return false;
    }
    return point_lights_.Start(targets) && shadows_.Start(targets) && Deformers::Start(targets);
}
bool WorldRenderer::Record(CommandList& list, const Scene::Header& h, const u8* data, u64 address,
                           u32 slot) {
    if (!CoreState::Header(h) || slot >= FramesInFlight || !Meshes::IndexBuffer())
        return false;
    auto& t = *t_;
    auto& c = *t.context;
    auto& upload = uploads_[slot];
    auto* scratch = c.arenas->Resource(ArenaKind::Scratch);
    std::memset(upload.cpu, 0, UploadBytes);
    const float initial[4]{0, 1, 1, 0};
    std::memcpy(upload.cpu, initial, 16);
    Scene::Light light{};
    std::memcpy(&light, data + Scene::LightOffset, sizeof(light));
    light.point_lights = h.point_lights;
    light.shadow_trace = 0;
    std::memcpy(upload.cpu + 32, &light, sizeof(light));
    const auto record = CoreState::DeferredImages(
        t.Srv(CoreTargets::Depth), t.Srv(CoreTargets::Albedo), t.Srv(CoreTargets::Normal),
        t.Srv(CoreTargets::Material), t.Srv(CoreTargets::Black), t.Srv(CoreTargets::White));
    std::memcpy(upload.cpu + 32 + sizeof(light), record.data(), sizeof(record));
    t.ScratchTo(list, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!t.initialized) {
        list.list->CopyBufferRegion(scratch, t.meter.ref.offset, upload.resource.Get(), 0, 32);
        t.initialized = true;
    }
    list.list->CopyBufferRegion(scratch, frame_[slot].ref.offset, upload.resource.Get(), 32,
                                sizeof(light) + sizeof(record));
    t.ScratchTo(list, Rest);
    t.Bind(list);
    for (auto image : {CoreTargets::White, CoreTargets::Black, CoreTargets::ShadowArray}) {
        t.To(list, image, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float v = image == CoreTargets::Black ? 0.f : 1.f;
        const float clear[]{v, v, v, v};
        list.list->ClearRenderTargetView(t.Rtv(image), clear, 0, nullptr);
        t.To(list, image, Rest);
    }
    Deformers::Record(list, h, address);
    point_lights_.Record(list, h, address);
    shadows_.Record(list, h, data, address);
    RecordSurfaces(list, h, data, address, slot);
    RecordLight(list, h, slot);
    RecordForward(list, h, data, address, slot);
    return true;
}
void WorldRenderer::DrawList(CommandList& list, const Scene::Header& h, const u8* data, u64 address,
                             u32 slot, u32 list_index, u32 pipeline) {
    auto& t = *t_;
    const CoreState::Ref light_ref{frame_[slot].ref.arena, frame_[slot].ref.offset};
    const auto draws = CoreState::Unpack(address + Scene::DrawsOffset),
               characters = CoreState::Unpack(address + Scene::CharactersOffset);
    auto* index = Meshes::IndexBuffer();
    D3D12_INDEX_BUFFER_VIEW ib{index->GetGPUVirtualAddress(), u32(index->GetDesc().Width),
                               DXGI_FORMAT_R16_UINT};
    list.SetIndexBuffer(ib);
    list.SetPipeline(pipelines_[pipeline]);
    const u32 first = Scene::FirstCommand(h, static_cast<Scene::DrawList>(list_index));
    for (u32 i = 0; i < h.draw_commands[list_index]; ++i) {
        Scene::Command cmd{};
        std::memcpy(&cmd, data + Scene::CommandsOffset() + u64(first + i) * sizeof(cmd),
                    sizeof(cmd));
        if (cmd.first_instance >= h.draws) {
            ++skipped_;
            continue;
        }
        Scene::Draw draw{};
        std::memcpy(&draw, data + Scene::DrawsOffset + u64(cmd.first_instance) * sizeof(draw),
                    sizeof(draw));
        if (!CoreState::Ready(draw, cmd, h.draws, ib.SizeInBytes / 2, h.characters, pipeline >= 5,
                              {t.context->arenas->Capacity(ArenaKind::Geometry),
                               t.context->arenas->Capacity(ArenaKind::Scene),
                               t.context->arenas->Capacity(ArenaKind::Skinning)}) ||
            (pipeline < 4 && draw.material > h.materials)) {
            ++skipped_;
            continue;
        }
        if (pipeline < 4) {
            const CoreState::WorldRoot root{
                draws, CoreState::Unpack(address + Scene::MaterialsOffset()),
                CoreState::Unpack(address + Scene::LayerMaterialsOffset()), cmd.first_instance};
            list.SetDrawConstants(&root, sizeof(root));
        } else if (pipeline == 7) {
            const CoreState::LambertRoot root{draws, characters, cmd.first_instance};
            list.SetDrawConstants(&root, sizeof(root));
        } else {
            const CoreState::CharacterRoot root{
                draws,
                characters,
                CoreState::Unpack(address + Scene::DecalsOffset),
                light_ref,
                {1.f / t.width, 1.f / t.height},
                CoreState::Unpack(current_scene_address_ + Scene::PointLightsOffset),
                point_lights_.Grid(),
                0,
                cmd.first_instance};
            list.SetDrawConstants(&root, sizeof(root));
        }
        list.list->DrawIndexedInstanced(cmd.index_count, 1, cmd.first_index, cmd.vertex_offset, 0);
    }
}
void WorldRenderer::RecordSurfaces(CommandList& list, const Scene::Header& h, const u8* data,
                                   u64 address, u32 slot) {
    auto& t = *t_;
    const CoreState::Ref light_ref{frame_[slot].ref.arena, frame_[slot].ref.offset};
    D3D12_CPU_DESCRIPTOR_HANDLE colors[]{t.Rtv(CoreTargets::Albedo), t.Rtv(CoreTargets::Normal),
                                         t.Rtv(CoreTargets::Material)};
    for (auto image : {CoreTargets::Albedo, CoreTargets::Normal, CoreTargets::Material}) {
        t.To(list, image, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float clear[4]{};
        list.list->ClearRenderTargetView(t.Rtv(image), clear, 0, nullptr);
    }
    t.To(list, CoreTargets::Depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    const auto depth = t.Dsv();
    list.list->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 0, 0, 0, nullptr);
    list.list->OMSetRenderTargets(3, colors, FALSE, &depth);
    for (u32 i = 0; i < 4; ++i)
        DrawList(list, h, data, address, slot, i, i);
    list.list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    t.To(list, CoreTargets::Depth, Rest);
    if (h.world_decals) {
        list.list->OMSetRenderTargets(3, colors, FALSE, nullptr);
        list.SetPipeline(pipelines_[4]);
        const CoreState::DecalRoot root{CoreState::Unpack(address + Scene::WorldDecalsOffset),
                                        light_ref};
        list.SetDrawConstants(&root, sizeof(root));
        const u32 depth_srv = t.Srv(CoreTargets::Depth);
        list.SetPassConstants(&depth_srv, 4);
        list.list->DrawInstanced(36, h.world_decals, 0, 0);
    }
    list.list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    for (auto image : {CoreTargets::Albedo, CoreTargets::Normal, CoreTargets::Material})
        t.To(list, image, Rest);
}
void WorldRenderer::RecordLight(CommandList& list, const Scene::Header& h, u32 slot) {
    auto& t = *t_;
    const CoreState::Ref light_ref{frame_[slot].ref.arena, frame_[slot].ref.offset};
    t.To(list, CoreTargets::Hdr, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto hdr = t.Rtv(CoreTargets::Hdr);
    const float background[]{h.background[0], h.background[1], h.background[2], 0};
    list.list->ClearRenderTargetView(hdr, background, 0, nullptr);
    list.list->OMSetRenderTargets(1, &hdr, FALSE, nullptr);
    list.SetPipeline(pipelines_[9]);
    const CoreState::DeferredRoot deferred{
        light_ref,
        CoreState::Unpack(current_scene_address_ + Scene::PointLightsOffset),
        point_lights_.Grid(),
        {light_ref.arena, light_ref.offset + u32(sizeof(Scene::Light))}};
    list.SetDrawConstants(&deferred, sizeof(deferred));
    const CoreState::TextureRoot textures = shadows_.Textures();
    list.SetPassConstants(&textures, sizeof(textures));
    list.DrawFullscreen();
}
void WorldRenderer::RecordForward(CommandList& list, const Scene::Header& h, const u8* data,
                                  u64 address, u32 slot) {
    auto& t = *t_;
    const auto hdr = t.Rtv(CoreTargets::Hdr), depth = t.Dsv();
    t.To(list, CoreTargets::Depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    list.list->OMSetRenderTargets(1, &hdr, FALSE, &depth);
    for (auto pair : {std::pair{6u, 7u}, std::pair{7u, 8u}, std::pair{4u, 5u}, std::pair{5u, 6u}}) {
        const CoreState::TextureRoot forward = shadows_.Textures();
        list.SetPassConstants(&forward, sizeof(forward));
        DrawList(list, h, data, address, slot, pair.first, pair.second);
    }
}
void WorldRenderer::Stop() {
    Deformers::Stop();
    shadows_.Stop();
    point_lights_.Stop();
    for (auto& upload : uploads_) {
        upload.Unmap();
        upload.resource.Reset();
    }
    for (auto& p : pipelines_)
        p = {};
    t_ = nullptr;
}
} // namespace Gr2Port::D3D12
