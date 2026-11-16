// SPDX-License-Identifier: GPL-2.0-or-later
#if defined(GR2_XBOX_UWP)
#include "graphics/gr2port_d3d12/uwp/uwp_main.h"
#include "graphics/gr2port_d3d12/uwp/console_boot.h"
#include "graphics/gr2port_d3d12/presenter.h"
#include "graphics/gr2port_d3d12/imgui_snapshot.h"
#include "graphics/gr2port/backend.h"
#include "core/game_check/game_check.h"
#include "uwp/sdl3_shim/pad_state.h"
#include "uwp/blackbox_log.h"
#include "common/logging/log.h"
#include "imgui/imgui_texture.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.UI.Core.h>
#include <chrono>
#include <fstream>
#include <optional>
#include <string>

namespace Gr2Port::Uwp {
using namespace winrt::Windows::Storage;
using winrt::Windows::Foundation::AsyncStatus;
namespace {
constexpr wchar_t Token[]=L"gr2-dump";
using FolderOperation=winrt::Windows::Foundation::IAsyncOperation<StorageFolder>;
struct LauncherContext {
    ImGuiContext* context=ImGui::CreateContext();
    ~LauncherContext() { ImGui::DestroyContext(context); }
};
}
bool PumpConsoleEvents() {
    const auto window=winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
    window.Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);
    SDL_UpdateGamepads();
    if(!SessionRunning()) return false;
    if(!Renderer::Tick()) {RequestStop();return false;}
    return true;
}
bool ConsoleSession() {
    const auto window=winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
    const auto closed=window.Closed(winrt::auto_revoke,[](const auto&,const auto&) {
        RequestStop(); SDL_Event quit{}; quit.type=SDL_EVENT_QUIT; SDL_PushEvent(&quit);
    });
    const auto state=std::filesystem::path{ApplicationData::Current().LocalFolder().Path().c_str()};
    InitializeConsoleSettings(state);
    Blackbox::Milestone("file logging initialized; console settings loaded from LocalState");
    if(!Renderer::Initialize(CoreWindowHandle())) {
        Blackbox::Milestone("CoreWindow renderer initialization failed before first present");
        return false;
    }
    Blackbox::Milestone("ConsoleSession ready; CoreWindow swapchain initialized");
    auto access=AccessCache::StorageApplicationPermissions::FutureAccessList();
    FolderOperation pending{nullptr};
    bool restoring=false;
    std::optional<GameCheck::Game> game;
    std::filesystem::path selected;
    std::string status="UWP activated. Local diagnostics and logging are open. ConsoleSession is ready; connect the USB drive containing your decrypted Gravity Rush 2 v1.11 dump.";
    auto validate=[&](const std::filesystem::path& candidate) {
        std::string error;
        auto checked=GameCheck::ValidateFolder(candidate,error);
        if(!checked) {game.reset();status=error;return false;}
        game=std::move(checked); selected=candidate;
        Blackbox::SetGameRoot(game->root.wstring().c_str());
        status="Gravity Rush 2 v1.11 validated. Press A or Menu to launch. Press X to choose another folder.";
        return true;
    };
    try {
        if(access.ContainsItem(Token)) {
            pending=access.GetFolderAsync(Token); restoring=true;
        }
    } catch(const winrt::hresult_error& e) {status="Saved USB permission unavailable: "+winrt::to_string(e.message());}
    // A saved path alone does not grant permission; restore its capability first.
    // Only probe accessible paths. Never assume a removable drive letter exists.
    if(!pending) {
        std::ifstream file(state/"gr2-dump-path.txt"); std::string path;
        if(std::getline(file,path)&&!path.empty()) validate(std::filesystem::u8path(path));
    }
    bool previous_a=false,previous_x=false,previous_start=false;
    bool scan_candidates=true;
    std::optional<std::filesystem::path> launch;
    {
        LauncherContext gui;
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
        io.BackendRendererName="GR2port D3D12 launcher";
        io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset|ImGuiBackendFlags_RendererHasTextures;
        io.FontGlobalScale=2.f;
        while(SessionRunning()) {
            if(pending && pending.Status()!=AsyncStatus::Started) {
                try {
                    if(pending.Status()==AsyncStatus::Completed) {
                        const auto folder=pending.GetResults();
                        if(folder) {
                            // Keep the StorageFolder capability, not just a drive-letter string.
                            const std::filesystem::path path{folder.Path().c_str()};
                            if(validate(path)) {
                                access.AddOrReplace(Token,folder);
                                std::ofstream file(state/"gr2-dump-path.txt",std::ios::trunc);
                                const auto utf8=selected.u8string(); file.write(reinterpret_cast<const char*>(utf8.data()),std::streamsize(utf8.size()));
                            }
                        } else status="Folder selection canceled. Press A to try again.";
                    } else status="USB permission/picker failed. Press A to choose the folder again.";
                } catch(const winrt::hresult_error& e) {
                    status="USB folder unavailable: "+winrt::to_string(e.message());
                    if(restoring) access.Remove(Token);
                }
                pending=nullptr; restoring=false;
            }
            if(scan_candidates && !pending && !game) {
                scan_candidates=false;
                for(const wchar_t* drive:{L"D:\\",L"E:\\",L"F:\\"}) {
                    for(const wchar_t* suffix:{L"Gravity Rush 2",L"GR2",L""}) {
                        std::error_code ec; const auto path=std::filesystem::path(drive)/suffix;
                        if(std::filesystem::is_directory(path,ec) && GameCheck::HoldsGame(path) && validate(path)) break;
                    }
                    if(game) break;
                }
            }
            const auto pad=Gr2Sdl::Pad::Read();
            const bool a=pad.buttons[int(Gr2Sdl::Pad::Button::South)];
            const bool x=pad.buttons[int(Gr2Sdl::Pad::Button::West)];
            const bool start=pad.buttons[int(Gr2Sdl::Pad::Button::Start)];
            const bool choose=(!game && a&&!previous_a)||(x&&!previous_x);
            if(!pending && choose) {
                try {
                    Pickers::FolderPicker picker;
                    picker.SuggestedStartLocation(Pickers::PickerLocationId::ComputerFolder);
                    picker.FileTypeFilter().Append(L"*");
                    // Called on the CoreWindow UI thread. Never block it with .get().
                    pending=picker.PickSingleFolderAsync();
                    status="Choose the folder holding the game and update. Press B to cancel the system picker.";
                } catch(const winrt::hresult_error& e) {status="Folder picker failed: "+winrt::to_string(e.message());}
            } else if(!pending && game && ((a&&!previous_a)||(start&&!previous_start))) {
                if(GameCheck::HoldUserFolder()) {launch=game->root;break;}
                status="Another instance holds the user folder.";
            }
            previous_a=a;previous_x=x;previous_start=start;
            // A fixed logical launcher canvas is scaled to the actual RTV by the relay.
            io.DisplaySize=ImVec2(1920.f,1080.f);
            io.DeltaTime=1.f/60.f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(80,80),ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x-160,io.DisplaySize.y-160),ImGuiCond_Always);
            ImGui::Begin("Gravity Rush 2 - Xbox Dev Mode",nullptr,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize);
            ImGui::TextWrapped("%s",status.c_str());
            if(!selected.empty()) ImGui::TextWrapped("Folder: %s",selected.string().c_str());
            ImGui::Separator();
            ImGui::TextWrapped("A: choose folder / launch     X: choose another folder     Menu: launch");
            ImGui::TextWrapped("Development renderer: game scene capture/frame graph is not complete. A successful boot is not playable-rendering proof.");
            ImGui::End(); ImGui::Render(); ImGuiRelay::Capture(*ImGui::GetDrawData());
            // Capture before Tick so the very first swapchain present carries the
            // startup state instead of an empty/black frame. Pumping still occurs
            // every iteration, and its one-frame input latency is bounded at 60 Hz.
            if(!PumpConsoleEvents()) break;
        }
        if(pending && pending.Status()==AsyncStatus::Started) pending.Cancel();
    }
    ImGuiRelay::ResetSnapshots();
    if(launch && SessionRunning()) {
        Blackbox::Milestone("validated dump handed to Core::Emulator::Run (in process)");
        BootConsoleGame(*launch);
    }
    return true;
}
}
#endif
