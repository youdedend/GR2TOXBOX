// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics/gr2port_d3d12/ui_d3d12.h"
#include "fullscreen_vert.h"
#include "graphics/gr2port/textures.h"
#include "ui_blur_frag.h"
#include "ui_blur_history_frag.h"
#include "ui_color_control_frag.h"
#include "ui_color_frag.h"
#include "ui_copy_frag.h"
#include "ui_frame_frag.h"
#include "ui_gamma_frag.h"
#include "ui_mask_frag.h"
#include "ui_mask_sprite_frag.h"
#include "ui_masked_frag.h"
#include "ui_master_vert.h"
#include "ui_movie_frag.h"
#include "ui_pass_flat_vert.h"
#include "ui_pass_vert.h"
#include "ui_photo_frag.h"
#include "ui_photo_vert.h"
#include "ui_present_frag.h"
#include "ui_skinned_b_vert.h"
#include "ui_skinned_vert.h"
#include "ui_sprite_frag.h"
#include "ui_stroke_frag.h"
#include "ui_stroke_geom.h"
#include "ui_stroke_vert.h"
#include "ui_text_flat_geom.h"
#include "ui_text_geom.h"
#include "ui_threshold_frag.h"
#include "ui_transform_b_vert.h"
#include "ui_transform_vert.h"

namespace Gr2Port::D3D12 {
namespace {
constexpr DXGI_FORMAT TargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr D3D12_RESOURCE_STATES Rest = static_cast<D3D12_RESOURCE_STATES>(
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
static_assert(Scene::UiTargetHandles >= Residency::FirstReserved);
static_assert(Scene::UiTargetHandles + Scene::MaxUiTargets <= Textures::Capacity);
std::span<const u8> Vertex(u32 n) {
    switch (n) {
    case 0:
        return Shaders::ui_pass_vert_span;
    case 1:
        return Shaders::ui_pass_flat_vert_span;
    case 2:
        return Shaders::ui_master_vert_span;
    case 3:
        return Shaders::ui_transform_b_vert_span;
    case 4:
        return Shaders::ui_transform_vert_span;
    case 5:
        return Shaders::ui_skinned_vert_span;
    case 6:
        return Shaders::ui_skinned_b_vert_span;
    case 7:
        return Shaders::ui_photo_vert_span;
    case 8:
        return Shaders::ui_stroke_vert_span;
    default:
        return {};
    }
}
std::span<const u8> Pixel(u32 n) {
    switch (n) {
    case 0:
        return Shaders::ui_color_frag_span;
    case 1:
        return Shaders::ui_copy_frag_span;
    case 2:
        return Shaders::ui_gamma_frag_span;
    case 3:
        return Shaders::ui_threshold_frag_span;
    case 4:
        return Shaders::ui_movie_frag_span;
    case 5:
        return Shaders::ui_sprite_frag_span;
    case 6:
        return Shaders::ui_frame_frag_span;
    case 7:
        return Shaders::ui_masked_frag_span;
    case 8:
        return Shaders::ui_blur_frag_span;
    case 9:
        return Shaders::ui_blur_history_frag_span;
    case 10:
        return Shaders::ui_color_control_frag_span;
    case 11:
        return Shaders::ui_photo_frag_span;
    case 12:
        return Shaders::ui_mask_frag_span;
    case 13:
        return Shaders::ui_mask_sprite_frag_span;
    case 14:
        return Shaders::ui_stroke_frag_span;
    default:
        return {};
    }
}
std::span<const u8> Geometry(u32 v, bool narrow) {
    switch (UiState::SelectGeometry(v, narrow)) {
    case UiState::Geometry::Stroke:
        return Shaders::ui_stroke_geom_span;
    case UiState::Geometry::Text:
        return Shaders::ui_text_geom_span;
    case UiState::Geometry::TextFlat:
        return Shaders::ui_text_flat_geom_span;
    default:
        return {};
    }
}
} // namespace
bool UiRenderer::Start(ResidencyContext& context, DXGI_FORMAT format, float aspect) {
    context_ = &context;
    narrow_ = std::abs(aspect - 16.f / 9.f) > 0.01f;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = Scene::MaxUiTargets;
    if (FAILED(context.device->device->CreateDescriptorHeap(&heap,
                                                            IID_PPV_ARGS(rtvs_.GetAddressOf()))))
        return false;
    for (u32 i = 0; i < 4; ++i) {
        const auto sampler = UiState::Sampler(i);
        context.heap->WriteSampler({16 + i}, sampler);
        // ui_present samples sampler 3, independently of the game's sampler bits.
        if (i == 1)
            context.heap->WriteSampler({3}, sampler);
    }
    return Resize(format);
}
bool UiRenderer::Resize(DXGI_FORMAT format) {
    if (present_.IsValid() && present_format_ == format)
        return true;
    GraphicsPipelineDesc desc;
    desc.vs = Shaders::fullscreen_vert_span;
    desc.ps = Shaders::ui_present_frag_span;
    desc.color_formats = {&format, 1};
    desc.depth_test = false;
    desc.depth_write = false;
    Pipeline next;
    if (!CreateGraphicsPipeline(context_->device->device.Get(), *context_->root, desc, next))
        return false;
    present_ = std::move(next);
    present_format_ = format;
    return true;
}
D3D12_CPU_DESCRIPTOR_HANDLE UiRenderer::Rtv(u32 index) const {
    auto handle = rtvs_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += size_t(index) * context_->device->rtv_descriptor_size;
    return handle;
}
void UiRenderer::Bind(CommandList& list) {
    list.SetRootSignature(*context_->root);
    ID3D12DescriptorHeap* heaps[]{context_->heap->SrvHeap(), context_->heap->SamplerHeap()};
    list.list->SetDescriptorHeaps(2, heaps);
    list.SetDescriptorTables(*context_->heap);
    list.SetArenaTable(*context_->arenas, *context_->heap);
}
bool UiRenderer::Prepare(CommandList& list, const Scene::Header& h, const u8* packet,
                         u64 submission) {
    if (!UiState::Header(h, Scene::PacketBytes()))
        return false;
    std::array<u32, Scene::MaxUiDynamicTextures> dynamic{};
    std::memcpy(dynamic.data(), packet + Scene::UiDynamicOffset, h.ui.dynamic * sizeof(u32));
    Textures::Refresh(list, submission, std::span{dynamic}.first(h.ui.dynamic));
    std::array<Scene::UiTarget, Scene::MaxUiTargets> descriptions{};
    std::memcpy(descriptions.data(), packet + Scene::UiTargetsOffset,
                h.ui.targets * sizeof(Scene::UiTarget));
    // Reserved descriptors are shared by all in-flight lists. Replacing either
    // their resource or SRV is only legal once previous submissions are idle.
    bool needs_idle = false;
    for (u32 i = 0; i < h.ui.targets; ++i) {
        const auto& d = descriptions[i];
        const auto& t = targets_[i];
        if (!d.width || !d.height || d.width > 16384 || d.height > 16384)
            return false;
        if (t.image && (t.width != d.width || t.height != d.height || t.base != d.base))
            needs_idle = true;
    }
    if (needs_idle)
        WaitForFence(*context_->device, context_->device->timeline_value);
    for (u32 i = 0; i < h.ui.targets; ++i) {
        const auto& d = descriptions[i];
        auto& t = targets_[i];
        const bool replace =
            !t.image || t.width != d.width || t.height != d.height || t.base != d.base;
        if (replace) {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = d.width;
            desc.Height = d.height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = TargetFormat;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE clear{};
            clear.Format = TargetFormat;
            clear.Color[3] = 1.f;
            auto image =
                context_->allocator->CreateResource(desc, D3D12_HEAP_TYPE_DEFAULT, Rest, &clear);
            if (!image)
                return false;
            t.image = std::move(image);
            t.width = d.width;
            t.height = d.height;
            t.base = d.base;
            context_->device->device->CreateRenderTargetView(t.image.Get(), nullptr, Rtv(i));
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = TargetFormat;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1;
            Textures::SetExternal(Scene::UiTargetHandles + i, t.image.Get(), srv);
        }
        // fresh_targets requests reliable packet delivery, not a discard. As
        // in Vulkan, existing target contents survive kept/accumulation draws.
        if (replace) {
            list.Transition(t.image.Get(), Rest, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const float black[4]{0, 0, 0, 1};
            list.list->ClearRenderTargetView(Rtv(i), black, 0, nullptr);
            list.Transition(t.image.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, Rest);
        }
    }
    return true;
}
Pipeline* UiRenderer::PipelineFor(const Scene::UiRun& run) {
    const auto key = UiState::Key(run.vertex, run.pixel, run.blend, narrow_,
                                  run.topology == Scene::UiPrimitiveStrip);
    if (auto found = pipelines_.find(key); found != pipelines_.end())
        return &found->second;
    GraphicsPipelineDesc desc;
    desc.vs = Vertex(run.vertex);
    desc.ps = Pixel(run.pixel);
    desc.gs = Geometry(run.vertex, narrow_);
    desc.color_formats = {&TargetFormat, 1};
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull = D3D12_CULL_MODE_NONE;
    desc.custom_blend = true;
    desc.blend_target = UiState::Blend(run.blend);
    Pipeline pipeline;
    if (!CreateGraphicsPipeline(context_->device->device.Get(), *context_->root, desc, pipeline))
        return nullptr;
    return &pipelines_.emplace(key, std::move(pipeline)).first->second;
}
bool UiRenderer::Record(CommandList& list, const Scene::Header& h, const u8* packet, u64 address) {
    if (!UiState::Header(h, Scene::PacketBytes()))
        return false;
    Bind(list);
    const auto& screen = targets_[h.ui.picture < targets_.size() ? h.ui.picture : 0];
    u32 open = Scene::MaxUiTargets;
    auto close = [&] {
        if (open != Scene::MaxUiTargets) {
            list.list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
            list.Transition(targets_[open].image.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, Rest);
            open = Scene::MaxUiTargets;
        }
    };
    for (u32 i = 0; i < h.ui.runs; ++i) {
        Scene::UiRun run{};
        std::memcpy(&run, packet + Scene::UiRunsOffset + u64(i) * sizeof(run), sizeof(run));
        UiState::Viewport view{};
        if (run.target >= h.ui.targets || !targets_[run.target].image ||
            !UiState::Pairs(run.vertex, run.pixel) ||
            !UiState::CommandRange(run, h, Scene::PacketBytes()) ||
            !UiState::View(run.viewport, view))
            continue;
        auto* pipeline = PipelineFor(run);
        if (!pipeline) {
            close();
            return false;
        }
        auto& target = targets_[run.target];
        if (open != run.target) {
            close();
            open = run.target;
            list.Transition(target.image.Get(), Rest, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const auto rtv = Rtv(open);
            list.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        }
        list.SetPipeline(*pipeline);
        const float zero[4]{};
        list.list->OMSetBlendFactor(zero);
        list.list->IASetPrimitiveTopology(run.topology == Scene::UiPrimitiveStrip
                                              ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
                                              : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_VIEWPORT viewport{view.x, view.y, view.width, view.height, 0, 1};
        list.list->RSSetViewports(1, &viewport);
        const D3D12_RECT scissor{0, 0, LONG(target.width), LONG(target.height)};
        list.SetScissor(scissor);
        const auto scale =
            UiState::TextScale(screen.width, screen.height, target.width, target.height, narrow_);
        const float raster[4]{view.y_sign, 0, 0, 0};
        list.SetPassConstants(raster, sizeof(raster));
        for (u32 j = 0; j < run.count; ++j) {
            Scene::VertexCommand cmd{};
            std::memcpy(&cmd, packet + Scene::UiCommandsOffset + u64(run.first + j) * sizeof(cmd),
                        sizeof(cmd));
            if (!UiState::Command(cmd, h.ui.draws))
                continue;
            const auto root = UiState::Root(address + Scene::UiDrawsOffset, cmd.first_instance,
                                            scale[0], scale[1]);
            list.SetDrawConstants(&root, sizeof(root));
            list.list->DrawInstanced(cmd.vertex_count, cmd.instance_count, cmd.first_vertex, 0);
        }
    }
    close();
    return true;
}
bool UiRenderer::HasPicture(const Scene::Header& h) const {
    return h.ui.picture < h.ui.targets && h.ui.picture < targets_.size() &&
           bool(targets_[h.ui.picture].image);
}
bool UiRenderer::RecordPresent(CommandList& list, const Scene::Header& h, u32 width, u32 height) {
    if (!HasPicture(h) || !width || !height)
        return true;
    const auto& source = targets_[h.ui.picture];
    const auto area = UiState::Fit(source.width, source.height, width, height);
    Bind(list);
    list.SetPipeline(present_);
    const u32 picture = Scene::UiTargetHandles + h.ui.picture;
    list.SetPassConstants(&picture, sizeof(picture));
    const D3D12_VIEWPORT viewport{
        float(area.x), float(area.y), float(area.width), float(area.height), 0, 1};
    const D3D12_RECT scissor{LONG(area.x), LONG(area.y), LONG(area.x + area.width),
                             LONG(area.y + area.height)};
    list.list->RSSetViewports(1, &viewport);
    list.SetScissor(scissor);
    list.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list.list->DrawInstanced(3, 1, 0, 0);
    return true;
}
void UiRenderer::Stop() {
    // Surface has waited for the graphics queue before this call.
    pipelines_.clear();
    present_ = {};
    for (auto& target : targets_)
        target = Target{};
    rtvs_.Reset();
    context_ = nullptr;
    present_format_ = DXGI_FORMAT_UNKNOWN;
}
} // namespace Gr2Port::D3D12
