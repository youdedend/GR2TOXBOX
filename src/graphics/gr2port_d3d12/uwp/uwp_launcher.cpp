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
        if(!checked) {
            game.reset(); status=error;
            Blackbox::Milestone("selected folder refused: no valid Gravity Rush 2 v1.11 game and update pair");
            return false;
        }
        game=std::move(checked); selected=candidate;
        Blackbox::SetGameRoot(game->root.wstring().c_str());
        Blackbox::Milestone("selected dump passed console-only validation");
        status="Gravity Rush 2 v1.11 validated. D-pad selects Launch or Choose another folder.";
        return true;
    };
    try {
        if(access.ContainsItem(Token)) {
            pending=access.GetFolderAsync(Token); restoring=true;
            status="Restoring the saved USB folder permission.";
            Blackbox::Milestone("restoring the saved folder permission from the FutureAccessList");
        } else {
            Blackbox::Milestone("no saved folder permission exists; checking explicit USB roots");
        }
    } catch(const winrt::hresult_error&) {
        status="Saved USB permission unavailable: choose the folder again.";
        Blackbox::Milestone("saved folder permission could not be restored; choose a folder again");
    }
    // A saved path alone does not grant permission; restore its capability first.
    // Only probe explicit removable-drive roots. Never scan the package install path.
    if(!pending) {
        std::ifstream file(state/"gr2-dump-path.txt"); std::string path;
        if(std::getline(file,path)&&!path.empty()) {
            if(!validate(std::filesystem::u8path(path)))
                Blackbox::Milestone("saved folder path is no longer a valid dump; checking explicit USB roots");
        } else {
            Blackbox::Milestone("no saved folder path is available; checking explicit USB roots");
        }
    }
    bool previous_a=false,previous_b=false,previous_x=false,previous_start=false;
    bool previous_up=false,previous_down=false;
    bool picker_cancel_requested=false;
    bool pad_disconnected_reported=false;
    int focus=0;
    bool scan_candidates=true;
    std::optional<GameCheck::Game> launch;
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
                                if(!file) Blackbox::Milestone("could not save the selected folder path; FutureAccessList permission remains active");
                            } else if(restoring) {
                                access.Remove(Token);
                                Blackbox::Milestone("saved folder token no longer identifies a valid dump; token removed");
                            }
                        } else {
                            status="Folder selection returned no folder. Press A to try again.";
                            Blackbox::Milestone("system folder picker returned no folder");
                        }
                    } else if(pending.Status()==AsyncStatus::Canceled) {
                        status="Folder picker canceled. Press A to choose again.";
                        if(restoring) {
                            access.Remove(Token);
                            Blackbox::Milestone("saved folder permission restore was canceled or expired; select the folder again");
                        } else if(!picker_cancel_requested) {
                            Blackbox::Milestone("system folder picker canceled without a folder selection");
                        }
                    } else {
                        status="USB permission or folder picker failed. Press A to choose again.";
                        Blackbox::Milestone("USB folder permission or picker operation failed");
                        if(restoring) access.Remove(Token);
                    }
                } catch(const winrt::hresult_error&) {
                    status="USB folder unavailable: choose the folder again.";
                    Blackbox::Milestone("saved USB folder capability is no longer available; select the folder again");
                    if(restoring) access.Remove(Token);
                }
                pending=nullptr; restoring=false; picker_cancel_requested=false;
            }
            if(scan_candidates && !pending && !game) {
                scan_candidates=false;
                bool found_candidate=false;
                for(const wchar_t* drive:{L"E:\\",L"D:\\",L"F:\\"}) {
                    std::error_code root_error;
                    if(!std::filesystem::is_directory(drive,root_error)) {
                        Blackbox::Milestone(drive[0]==L'E' ? "E:\\ is not mounted; skipping that USB root" :
                            drive[0]==L'D' ? "D:\\ is not mounted; skipping that USB root" :
                                             "F:\\ is not mounted; skipping that USB root");
                        continue;
                    }
                    for(const wchar_t* suffix:{L"GR2",L"Gravity Rush 2",L""}) {
                        std::error_code ec;
                        const auto path=std::filesystem::path(drive)/suffix;
                        if(std::filesystem::is_directory(path,ec) && GameCheck::HoldsGame(path)) {
                            found_candidate=true;
                            if(validate(path)) break;
                        }
                    }
                    if(game) break;
                }
                if(!game && !found_candidate)
                    Blackbox::Milestone("no valid dump was found under E:\\, D:\\, or F:\\; use Choose folder");
            }
            const auto pad=Gr2Sdl::Pad::Read();
            if(!pad.connected && !pad_disconnected_reported) {
                Blackbox::Milestone("no Xbox controller is connected; launcher button input is unavailable");
                pad_disconnected_reported=true;
            }
            const bool a=pad.buttons[int(Gr2Sdl::Pad::Button::South)];
            const bool b=pad.buttons[int(Gr2Sdl::Pad::Button::East)];
            const bool x=pad.buttons[int(Gr2Sdl::Pad::Button::West)];
            const bool start=pad.buttons[int(Gr2Sdl::Pad::Button::Start)];
            const bool up=pad.buttons[int(Gr2Sdl::Pad::Button::DpadUp)];
            const bool down=pad.buttons[int(Gr2Sdl::Pad::Button::DpadDown)];
            const bool a_pressed=a&&!previous_a, b_pressed=b&&!previous_b;
            const bool x_pressed=x&&!previous_x, start_pressed=start&&!previous_start;
            if(game && ((up&&!previous_up)||(down&&!previous_down))) focus=1-focus;
            if(pending && pending.Status()==AsyncStatus::Started && b_pressed) {
                pending.Cancel(); picker_cancel_requested=true;
                status="Folder picker canceled. Press A to choose again.";
                Blackbox::Milestone("player canceled the system folder picker with B");
            }
            const bool choose_folder=x_pressed || (a_pressed && (!game || focus==1));
            const bool launch_game=game && (start_pressed || (a_pressed && focus==0));
            if(!pending && choose_folder) {
                try {
                    Pickers::FolderPicker picker;
                    picker.SuggestedStartLocation(Pickers::PickerLocationId::ComputerFolder);
                    picker.FileTypeFilter().Append(L"*");
                    pending=picker.PickSingleFolderAsync();
                    restoring=false;
                    status="Choose the folder holding the game and update. Press B to cancel the system picker.";
                    Blackbox::Milestone("system folder picker opened for an explicit user selection");
                } catch(const winrt::hresult_error&) {
                    status="Folder picker unavailable. Check the package's file-system capabilities.";
                    Blackbox::Milestone("system folder picker refused to open; package access capability may be missing");
                }
            } else if(!pending && launch_game) {
                if(GameCheck::HoldUserFolder()) {
                    Blackbox::Milestone("user data folder lock acquired; handing selected dump to emulator boot");
                    launch=*game; break;
                }
                status="Another instance holds the user folder.";
                Blackbox::Milestone("launch refused because another process holds the user data folder");
            }
            previous_a=a;previous_b=b;previous_x=x;previous_start=start;
            previous_up=up;previous_down=down;
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
            ImGui::Separator();
            ImGui::Text("D-pad focus:");
            if(game) {
                ImGui::TextColored(focus==0?ImVec4(1.0f,0.85f,0.25f,1.0f):ImVec4(0.8f,0.8f,0.8f,1.0f),
                                   "%s Launch",focus==0?">":" ");
                ImGui::TextColored(focus==1?ImVec4(1.0f,0.85f,0.25f,1.0f):ImVec4(0.8f,0.8f,0.8f,1.0f),
                                   "%s Choose another folder",focus==1?">":" ");
            } else {
                ImGui::TextColored(ImVec4(1.0f,0.85f,0.25f,1.0f),"> Choose folder");
            }
            ImGui::TextWrapped("Up/Down: move focus   A: activate   X: choose another folder   Menu: launch   B: cancel picker");
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
        if(!BootConsoleGame(*launch))
            Blackbox::Milestone("console emulator refused the selected dump before guest startup");
    }
    return true;
}
}
#endif
