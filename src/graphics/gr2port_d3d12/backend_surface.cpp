// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_properties.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "common/logging/log.h"
#include "core/game_check/game_patches.h"
#include "graphics/gr2port_d3d12/ui_d3d12.h"
#include "graphics/gr2port_d3d12/look_d3d12.h"
#include "graphics/gr2port/deformers.h"
#include "graphics/gr2port/movies.h"
#include "graphics/gr2port/backend.h"
#include "graphics/gr2port/frame_stats.h"
#include "graphics/gr2port/window.h"
#include "graphics/gr2port/scene_ring.h"
#include "graphics/gr2port/meshes.h"
#include "graphics/gr2port/textures.h"
#include "graphics/gr2port_d3d12/residency_context.h"
#include "graphics/gr2port_d3d12/arena.h"
#include "graphics/gr2port_d3d12/presenter.h"
#include "graphics/gr2port_d3d12/uwp/uwp_main.h"
#include "graphics/gr2port_d3d12/imgui_gpu.h"
#include "uwp/blackbox_log.h"


#if defined(GR2_XBOX_UWP)
#include "uwp/sdl3_shim/pad_state.h"
#endif

namespace Gr2Port {

namespace {

/// Start is idempotent; the UWP view owns its session, desktop owns a worker.
std::mutex g_state_mutex;
bool g_renderer_started = false;

/// What DeviceLost::Arm was told, and whether the pass trace is on. Kept because
/// the frontend asks for the trace before anything else has run, and the report it
/// produces is written when the device is created -- which the backend can do, even
/// without a swapchain.
std::string g_trace_reason;
bool g_trace_armed = false;

} // namespace

// --- Renderer ---------------------------------------------------------------

namespace Renderer {
namespace {
struct Surface {
    D3D12::Device device;
    D3D12::CommandRing ring;
    D3D12::RootSignature root;
    D3D12::BindlessHeap heap;
    D3D12::ImGuiGpu imgui;
    // Destroy resources before their allocator (members destruct in reverse order).
    D3D12::Allocator allocator;
    D3D12::ArenaSet arenas;
    D3D12::ResidencyContext residency;
    D3D12::UiRenderer ui;
    D3D12::CoreTargets core;
    D3D12::WorldRenderer world;
    D3D12::LookRenderer look;
    D3D12::UiState::Slots retained;
    Scene::Header latest{};
    bool has_picture=false;
    bool attached = false;
    u64 seen = 0;
    u64 captured_packets = 0;
    bool AttachScene() {
        const D3D12::ArenaDesc descriptions[]{
            {D3D12::ArenaKind::Geometry,256_MB,false,false},
            {D3D12::ArenaKind::Scratch,16_MB,true,false},
            {D3D12::ArenaKind::Scene,SceneRing::Slots*SceneRing::SlotBytes(),false,true},
            {D3D12::ArenaKind::Skinning,16_MB,true,false},
        };
        if (!allocator.Create(device) || !arenas.Create(device,allocator,heap,descriptions))return false;
        residency={&device,&allocator,&arenas,&heap,&root,0};
        if(!Meshes::Start(residency)||!Gr2Port::Textures::Start(residency)||
           !ui.Start(residency,device.swapchain_format,GamePatches::AspectRatio())||
           !core.Start(residency)||!world.Start(core)||!look.Start(core))return false;
        const auto storage=arenas.Allocate(D3D12::ArenaKind::Scene,SceneRing::Slots*SceneRing::SlotBytes());
        if (!storage.cpu || storage.size != SceneRing::Slots*SceneRing::SlotBytes()) return false;
        // Serialized scene addresses are offset-low / arena-high, not GPU VAs.
        const u64 address = (u64(storage.ref.arena) << 32) | storage.ref.offset;
        SceneRing::Attach(storage.cpu, address);
        attached = true;
        return true;
    }
    D3D12::ComPtr<ID3D12DescriptorHeap> rtvs;
    std::array<D3D12::ComPtr<ID3D12Resource>,D3D12::FramesInFlight> buffers;
    u64 frames=0;
    bool ready=false;
    bool Targets() {
        auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();
        for(u32 i=0;i<device.buffer_count;++i) {
            if(FAILED(device.swapchain->GetBuffer(i,IID_PPV_ARGS(buffers[i].GetAddressOf())))) return false;
            device.device->CreateRenderTargetView(buffers[i].Get(),nullptr,rtv);
            rtv.ptr+=device.rtv_descriptor_size;
        }
        return true;
    }
    ~Surface() {
        if(device.graphics_queue && device.timeline_fence)
            D3D12::WaitForFence(device,D3D12::SignalFence(device));
        // Shutdown must follow game/capture producer quiescence, as on Vulkan.
        retained.DrainAfterIdle([](u32 slot){SceneRing::Release(slot);});
        if (attached) SceneRing::Attach(nullptr, 0);
        look.Stop(); world.Stop(); core.Stop(); ui.Stop(); Gr2Port::Textures::Stop(); Meshes::Stop();
        imgui.Destroy(); arenas.Destroy(); allocator.Destroy();
        ring.Destroy(); heap.Destroy();
        if(device.timeline_event) CloseHandle(device.timeline_event);
    }
};
std::mutex surface_mutex;
std::unique_ptr<Surface> surface;
std::jthread worker;
std::atomic<bool> resize_requested{false};
}
bool Initialize(void* core_window) {
    const std::scoped_lock lock{surface_mutex};
    if(surface) return surface->ready;
    auto next=std::make_unique<Surface>();
    D3D12::DeviceDesc desc; desc.core_window=core_window;
#if !defined(GR2_XBOX_UWP)
    SDL_Window* window=Window::Show(); if(!window) return false;
    desc.hwnd=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
    const auto size=Window::PixelSize(); desc.width=size.width; desc.height=size.height;
#endif
    if(!D3D12::CreateDevice(desc,next->device)||!D3D12::CreateSwapchain(next->device,desc)) return false;
    D3D12_DESCRIPTOR_HEAP_DESC rtvs{}; rtvs.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvs.NumDescriptors=next->device.buffer_count;
    if(FAILED(next->device.device->CreateDescriptorHeap(&rtvs,IID_PPV_ARGS(next->rtvs.GetAddressOf()))) ||
       !next->Targets() || !next->ring.Create(next->device) ||
       !D3D12::CreateRootSignature(next->device,next->root) || !next->heap.Create(next->device) || !next->heap.ReserveTextureRange() ||
       !next->imgui.Create(next->device,next->root,next->heap) || !next->AttachScene()) return false;
    next->ready=true; surface=std::move(next);
    Blackbox::Milestone("swapchain, backbuffer RTVs, command ring and ImGui PSO ready");
    return true;
}
bool Tick() {
    const std::scoped_lock lock{surface_mutex};
    if(!surface||!surface->ready) return false;
    auto& s=*surface; auto& d=s.device;
#if !defined(GR2_XBOX_UWP)
    const auto size=Window::PixelSize();
    if(!size.width||!size.height) return true;
    if(size.width!=d.swapchain_width||size.height!=d.swapchain_height) resize_requested=true;
#endif
    if(resize_requested.exchange(false)) {
        D3D12::WaitForFence(d,D3D12::SignalFence(d));
        for(auto& buffer:s.buffers) buffer.Reset();
#if defined(GR2_XBOX_UWP)
        // DPI changes also need a new inverse composition transform. Recreate the
        // single swapchain only after all old backbuffer references are released.
        d.swapchain.Reset(); D3D12::DeviceDesc desc; desc.core_window=Uwp::CoreWindowHandle();
        if(!D3D12::CreateSwapchain(d,desc)||!s.Targets()) {s.ready=false;return false;}
#else
        if(!D3D12::ResizeSwapchain(d,size.width,size.height)||!s.Targets()) {s.ready=false;return false;}
#endif
        if(!s.ui.Resize(d.swapchain_format)){s.ready=false;return false;}
        Blackbox::Milestone("swapchain resize completed after GPU idle");
    }
    auto list=s.ring.BeginFrame(); if(!list.IsValid()) {s.ready=false;return false;}
    const u64 completed=d.timeline_fence->GetCompletedValue();
    const u64 submission=d.timeline_value+1; // EndFrame's next actual timeline signal.
    s.residency.completed=completed;
    s.allocator.BeginFrame(completed);
    Meshes::Update(list,submission,completed);
    Gr2Port::Textures::Update(list,submission,completed);
    // Retire before taking: the producer may need these slots immediately.
    s.retained.Collect(completed,[](u32 slot){SceneRing::Release(slot);});
    SceneRing::Packet packet{};
    bool ui_ok=true;
    if (SceneRing::TryTake(s.seen, packet)) {
        s.seen=packet.frame;
        Scene::Header header{};std::memcpy(&header,packet.data,sizeof(header));
        if(D3D12::UiState::Header(header,Scene::PacketBytes())){
            // Even partial recording may read packet memory. Keep the slot on
            // every outcome until EndFrame's actual graphics fence completes.
            if(!s.retained.Hold(packet.slot,submission)){
                SceneRing::Release(packet.slot);ui_ok=false;
            }else{
                ui_ok=s.ui.Prepare(list,header,packet.data,submission);
                Gr2Port::Textures::Refresh(list,submission,std::span{header.dynamic}.first(
                    std::min(header.dynamic_textures,Scene::MaxDynamicTextures)));
                Movies::Collect();
                if(ui_ok&&!header.ui.only){
                    const auto extent=s.ui.WorldExtent(header);
                    if(extent.width){
                        ui_ok=s.core.Resize(extent.width,extent.height);
                        if(ui_ok){
                            Deformers::Update(list,submission,completed,header.frame);
                            ui_ok=s.world.Record(list,header,packet.data,packet.address,s.ring.CurrentFrameIndex());
                        }
                        if(ui_ok){
                            s.look.RecordSky(list,header,packet.address);
                            s.look.RecordPost(list,packet.address);
                            const auto target=s.ui.BeginWorld(list,header);
                            if(target.width)s.look.RecordComposite(list,packet.address,target.width,target.height);
                            s.ui.EndWorld(list);
                        }
                    }
                }
                if(ui_ok)ui_ok=s.ui.Record(list,header,packet.data,packet.address);
                if(ui_ok&&s.ui.HasPicture(header)){s.latest=header;s.has_picture=true;}
            }
        }else SceneRing::Release(packet.slot);
        ++s.captured_packets;
        if(s.captured_packets==1||s.captured_packets%600==0){
            char note[160];std::snprintf(note,sizeof(note),"scene packets=%llu latest=%llu skipped-3D=%llu (device output unverified)",
                static_cast<unsigned long long>(s.captured_packets),static_cast<unsigned long long>(s.seen),
                static_cast<unsigned long long>(s.world.Skipped()));
            Blackbox::Milestone(note);
        }
    }
    const auto index=d.swapchain->GetCurrentBackBufferIndex();
    auto rtv=s.rtvs->GetCPUDescriptorHandleForHeapStart(); rtv.ptr+=size_t(index)*d.rtv_descriptor_size;
    list.ToRenderTarget(s.buffers[index].Get(),D3D12_RESOURCE_STATE_PRESENT);
    list.list->OMSetRenderTargets(1,&rtv,FALSE,nullptr); list.SetFullViewport();
    const float clear[4]={0.018f,0.025f,0.045f,1.f};
    list.list->ClearRenderTargetView(rtv,clear,0,nullptr);
    // Sticky picture is drawn every display tick, including ticks with no packet.
    if(ui_ok&&s.has_picture)ui_ok=s.ui.RecordPresent(list,s.latest,d.swapchain_width,d.swapchain_height);
    const bool drawn=s.imgui.Record(list,u32(s.frames%D3D12::FramesInFlight),ImGuiRelay::Latest());
    list.list->OMSetRenderTargets(0,nullptr,FALSE,nullptr);
    list.Transition(s.buffers[index].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
    if(!s.ring.EndFrame(list)) {s.ready=false;return false;}
    ++s.frames;
    Blackbox::Pass(0xffff0001u);
    const bool presented=D3D12::Present(d,true);
    if(!presented||!drawn||!ui_ok) {s.ready=false;Blackbox::Milestone("UI/present/overlay failed; session stopped");return false;}
    return true;
}
void RequestResize() { resize_requested=true; }
void Shutdown() {
    if(worker.joinable()) {worker.request_stop();worker.join();}
    const std::scoped_lock lock{surface_mutex}; surface.reset();
    ImGuiRelay::ResetSnapshots();
    const std::scoped_lock state{g_state_mutex}; g_renderer_started=false;
}
void Start() {
    const std::scoped_lock lock{g_state_mutex};
    if(g_renderer_started) return;
    g_renderer_started=true;
#if !defined(GR2_XBOX_UWP)
    worker=std::jthread([](std::stop_token stop) {
        if(!Initialize(nullptr)) {Blackbox::Milestone("desktop swapchain initialization failed");return;}
        while(!stop.stop_requested()&&Tick()) std::this_thread::yield();
    });
    std::atexit([]{Shutdown();});
    std::at_quick_exit([]{Shutdown();});
#endif
}
} // namespace Renderer

// --- Pipeline builds --------------------------------------------------------
//
// The Vulkan backend compiles pipelines on a worker pool and records which one is in
// flight so a lost-device report can name it. The D3D12 backend compiles its
// pipelines synchronously on first use, so there is never one "being built" across a
// thread boundary: the honest answer is that this thread is not building any.
std::string PipelineBeingBuilt() {
    return {};
}

// --- Device lost ------------------------------------------------------------

void DeviceLost::Arm(std::string_view reason) {
    const std::scoped_lock lock{g_state_mutex};
    g_trace_armed = true;
    g_trace_reason = std::string{reason};
    LOG_INFO(Graphics, "D3D12: the pass trace is on for this run ({})", g_trace_reason);
}

bool DeviceLost::LastRunLost(const std::filesystem::path& path) {
    // The Vulkan report is a text file the renderer appends to; a run that lost its
    // device leaves a line saying so. Reading the same marker keeps the frontend's
    // behaviour identical on both renderers -- it arms the trace for the next run --
    // and works before the D3D12 backend has a device at all, which is exactly when
    // the frontend asks.
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return false;
    }
    // Opened through the path's native string rather than a stream over the path:
    // this is read before the renderer has a device or a working directory it can
    // rely on, and the narrow form is the one that works on every build.
    std::FILE* const file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    std::string contents;
    char chunk[4096];
    size_t read = 0;
    while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        contents.append(chunk, read);
    }
    std::fclose(file);

    static constexpr std::string_view kMarker = "device lost";
    return std::search(contents.begin(), contents.end(), kMarker.begin(), kMarker.end(),
                       [](const char a, const char b) {
                           return std::tolower(static_cast<unsigned char>(a)) ==
                                  std::tolower(static_cast<unsigned char>(b));
                       }) != contents.end();
}

// --- The game's photos ------------------------------------------------------

bool Photo::Read(std::vector<u8>& pixels, u32& width, u32& height, u32& pitch) {
    // A photo is a copy of the frame's photo target, taken by the screen drawing.
    // UI target readback is not implemented yet; the encoder
    // treats a false return as "not this frame", which is what it does while the
    // game has not reached a photo screen. Nothing is fabricated: a blank image
    // returned as success would be saved as the player's photograph.
    pixels.clear();
    width = 0;
    height = 0;
    pitch = 0;
    return false;
}

} // namespace Gr2Port
