// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Xbox Series X|S entry point.
//
// A UWP application does not own its process. The shell starts it, hands it an
// `ICoreWindow` through the `IFrameworkView` contract, and drives the lifetime
// from the app model: there is no `main`, no `WinMain`, and no way to create the
// window yourself. The port therefore has two entry points that converge on one
// function:
//
//   * `WinMain` (desktop): SDL creates the window, and the emulator runs as it
//     always has.
//   * `IFrameworkView::Run` (console): the shell's core window is realised as a
//     swapchain (`CreateSwapChainForCoreWindow`, see device.cpp), and the
//     emulator runs on the view's thread until the window closes.
//
// Both call `Gr2Port::Uwp::RunGame`, so the game's start-up, the memory budget
// clamp and the chord layer cannot differ between the two packages.
//
// Why the console package needs the framework view at all: an Xbox title is a UWP
// app, so the swapchain must present to a CoreWindow rather than to an HWND
// (CreateSwapChainForHwnd fails with DXGI_ERROR_NOT_CURRENTLY_AVAILABLE on a
// console), and the app must declare an application type of "Game" in its
// manifest to be granted the 5 GiB pool rather than an app's.

#include "graphics/gr2port_d3d12/uwp/uwp_main.h"

#include <string>

#if defined(GR2_XBOX_UWP)

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/base.h>

#include <memory>

#include "common/logging/log.h"
#include "core/uwp_memory.h"
#include "core/uwp_guest_arena.h"
#include "uwp/blackbox_log.h"

// The WindowsApp.lib entry point. A UWP app's /ENTRY is main() from the app
// model's stub, and the stub calls the Application singleton's factory, so this
// is a plain function that the XAML-less app model resolves rather than a
// decorated WinMain.
namespace {

/// The one view the app has. The app model creates it through the factory below,
/// activates it, and then blocks in Run() until the window closes.
class GameView : public winrt::implements<GameView, winrt::Windows::ApplicationModel::Core::IFrameworkView> {
public:
    // IFrameworkView
    void Initialize(const winrt::Windows::ApplicationModel::Core::CoreApplicationView& view) {
        // Activated is how the console reports that the user came back from the
        // dashboard. The game has to re-activate the core window before it can
        // present again; the guest memory was never taken away, so nothing else
        // has to be rebuilt.
        view.Activated({this, &GameView::OnActivated});
    }

    void SetWindow(const winrt::Windows::UI::Core::CoreWindow& window) {
        window_ = window;
        window_.PointerCursor(nullptr);
        // The console's display can change (HDR on and off, a different
        // resolution), and the swapchain has to follow it.
        const auto display = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView();
        display.DpiChanged({this, &GameView::OnDpiChanged});
    }

    void Load(winrt::hstring) {
        // Nothing to load before the window exists: the emulator constructs its
        // device from the window, so the work belongs in Run().
    }

    void Run() {
        // The app model pumps the core window's queue itself, so RunGame
        // processes events per frame instead of owning a message loop the way
        // the desktop presenter does.
        window_.Activate();

        // RunGame blocks for the whole session: it drives the frame loop and
        // returns when the game exits or the window closes, at which point the
        // app model tears the view down through Uninitialize.
        Gr2Port::Blackbox::RunGuarded([](void* window) {
            Gr2Port::Uwp::RunGame(window);
        }, winrt::get_abi(window_));
    }

    void Uninitialize() {
        Gr2Port::Uwp::Shutdown();
    }

private:
    void OnActivated(const winrt::Windows::ApplicationModel::Core::CoreApplicationView&,
                     const winrt::Windows::ApplicationModel::Activation::IActivatedEventArgs&) {
        window_.Activate();
    }

    void OnDpiChanged(const winrt::Windows::Graphics::Display::DisplayInformation&,
                      const winrt::Windows::Foundation::IInspectable&) {
        Gr2Port::Uwp::NotifyDisplayChanged();
    }

    winrt::Windows::UI::Core::CoreWindow window_{nullptr};
};

/// The view's factory: the app model asks for one of these and then for a view.
struct GameViewSource : winrt::implements<GameViewSource,
                                          winrt::Windows::ApplicationModel::Core::IFrameworkViewSource> {
    winrt::Windows::ApplicationModel::Core::IFrameworkView CreateView() {
        return winrt::make<GameView>();
    }
};

} // namespace

/// The application's entry point. On a UWP app the linker resolves /ENTRY to the
/// app model's stub, which calls this.
// The parameter types are spelled out rather than HINSTANCE/PWSTR: neither is in
// the onecore surface this build compiles against, and the app model never passes
// anything the entry point reads. wWinMain's signature is what the linker looks
// for, and these are the same four types it declares (a handle, a handle, a wide
// string and an int).
int __stdcall wWinMain(void* /*hInstance*/, void* /*hPrevInstance*/,
                       wchar_t* /*commandLine*/, int /*showCommand*/) {
    const bool reserved = Core::Xbox::ReserveEarlyGuestArena();
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    Gr2Port::Blackbox::Initialize();
    Gr2Port::Blackbox::RecordGuestArena(reinterpret_cast<void*>(Core::Xbox::EarlyGuestBase),
                                      Core::Xbox::EarlyGuestBytes, reserved);
    if (!reserved) return 1;
    Gr2Port::Blackbox::RunGuarded([](void*) {
        winrt::Windows::ApplicationModel::Core::CoreApplication::Run(GameViewSource{});
    }, nullptr);
    return 0;
}

#else // GR2_XBOX_UWP

// The desktop package uses src/main.cpp. This file still compiles to nothing so a
// build that lists it unconditionally does not have to special-case it.
namespace {
[[maybe_unused]] constexpr int UwpMainDesktopStub = 0;
}

#endif // GR2_XBOX_UWP
