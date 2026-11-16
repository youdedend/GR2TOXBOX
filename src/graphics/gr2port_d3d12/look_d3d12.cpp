// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics/gr2port_d3d12/look_d3d12.h"
#include "antialias_off_comp.h"
#include "composite_frag.h"
#include "exposure_comp.h"
#include "fullscreen_vert.h"
#include "luminance_comp.h"
#include "sky_frag.h"
#include "tonemap_frag.h"
namespace Gr2Port::D3D12 {
namespace {
constexpr auto Rest = static_cast<D3D12_RESOURCE_STATES>(
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}
bool LookRenderer::Start(CoreTargets& t) {
    t_ = &t;
    auto& c = *t.context;
    auto graphics = [&](Pipeline& out, std::span<const u8> ps, DXGI_FORMAT format, bool sky) {
        GraphicsPipelineDesc d;
        d.vs = Shaders::fullscreen_vert_span;
        d.ps = ps;
        d.color_formats = {&format, 1};
        d.depth_test = sky;
        d.depth_write = false;
        d.depth_compare = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        if (sky)
            d.depth_format = DXGI_FORMAT_D32_FLOAT;
        return CreateGraphicsPipeline(c.device->device.Get(), *c.root, d, out);
    };
    return graphics(sky_, Shaders::sky_frag_span, DXGI_FORMAT_R16G16B16A16_FLOAT, true) &&
           graphics(tonemap_, Shaders::tonemap_frag_span, DXGI_FORMAT_R8G8B8A8_UNORM, false) &&
           graphics(composite_, Shaders::composite_frag_span, DXGI_FORMAT_R8G8B8A8_UNORM, false) &&
           CreateComputePipeline(c.device->device.Get(), *c.root, Shaders::luminance_comp_span,
                                 luminance_) &&
           CreateComputePipeline(c.device->device.Get(), *c.root, Shaders::exposure_comp_span,
                                 exposure_) &&
           CreateComputePipeline(c.device->device.Get(), *c.root, Shaders::antialias_off_comp_span,
                                 antialias_);
}
void LookRenderer::RecordSky(CommandList& list, const Scene::Header& h, u64 address) {
    if (!h.sky)
        return;
    t_->Bind(list);
    list.SetPipeline(sky_);
    const auto ref = CoreState::Unpack(address + Scene::SkyOffset);
    const u32 root[]{ref.arena, ref.offset, 0, 0, 0};
    list.SetDrawConstants(root, sizeof(root));
    list.DrawFullscreen();
}
void LookRenderer::RecordPost(CommandList& list, u64 address) {
    auto& t = *t_;
    t.Bind(list);
    list.list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    t.To(list, CoreTargets::Depth, Rest);
    t.To(list, CoreTargets::Hdr, Rest);
    const CoreState::Ref meter{t.meter.ref.arena, t.meter.ref.offset};
    const auto grade = CoreState::Unpack(address + Scene::GradeOffset);
    t.ScratchTo(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const CoreState::MeterRoot sample{meter, {t.width, t.height}, {0, 0}};
    list.SetPipeline(luminance_);
    list.SetDrawConstants(&sample, sizeof(sample));
    const u32 scene = t.Srv(CoreTargets::Hdr);
    list.SetPassConstants(&scene, 4);
    list.Dispatch((t.width + 63) / 64, (t.height + 63) / 64, 1);
    auto* scratch = t.context->arenas->Resource(ArenaKind::Scratch);
    t.Uav(list, scratch);
    const float tiles = float((t.width + 7) / 8) * float((t.height + 7) / 8);
    const CoreState::ExposureRoot exposure{grade, meter, 1.f / tiles};
    list.SetPipeline(exposure_);
    list.SetDrawConstants(&exposure, sizeof(exposure));
    list.Dispatch(1, 1, 1);
    t.Uav(list, scratch);
    t.ScratchTo(list, Rest);
    t.To(list, CoreTargets::Toned, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto toned = t.Rtv(CoreTargets::Toned);
    list.list->OMSetRenderTargets(1, &toned, FALSE, nullptr);
    list.SetPipeline(tonemap_);
    const CoreState::ToneRoot tone{grade, meter};
    list.SetDrawConstants(&tone, sizeof(tone));
    const u32 textures[]{scene, t.Srv(CoreTargets::Black)};
    list.SetPassConstants(textures, sizeof(textures));
    list.DrawFullscreen();
    list.list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    t.To(list, CoreTargets::Toned, Rest);
    if (t.aa_state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        list.Transition(t.aa.Get(), t.aa_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        t.aa_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    list.SetPipeline(antialias_);
    const u32 aa[]{t.width, t.height, t.pitch, 0};
    list.SetDrawConstants(aa, sizeof(aa));
    const u32 handles[]{t.Srv(CoreTargets::Toned), t.aa_uav.index, 0, 0};
    list.SetPassConstants(handles, sizeof(handles));
    list.Dispatch((t.width + 7) / 8, (t.height + 7) / 8, 1);
    t.Uav(list, t.aa.Get());
    list.Transition(t.aa.Get(), t.aa_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    t.aa_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    t.To(list, CoreTargets::Picture, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource = t.targets[CoreTargets::Picture].image.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = t.aa.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, t.width, t.height, 1, t.pitch};
    list.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    t.To(list, CoreTargets::Picture, Rest);
}
void LookRenderer::RecordComposite(CommandList& list, u64 address, u32 width, u32 height) {
    auto& t = *t_;
    t.Bind(list);
    list.SetViewport(width, height);
    list.SetPipeline(composite_);
    const CoreState::CompositeRoot root{CoreState::Unpack(address + Scene::GradeOffset),
                                        {t.meter.ref.arena, t.meter.ref.offset},
                                        t.Srv(CoreTargets::Picture)};
    list.SetDrawConstants(&root, sizeof(root));
    list.DrawFullscreen();
}
void LookRenderer::Stop() {
    sky_ = {};
    luminance_ = {};
    exposure_ = {};
    tonemap_ = {};
    antialias_ = {};
    composite_ = {};
    t_ = nullptr;
}
} // namespace Gr2Port::D3D12
