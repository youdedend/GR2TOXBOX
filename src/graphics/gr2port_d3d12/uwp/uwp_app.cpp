// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The shared half of the Xbox entry point: the code both packages run.
//
// uwp_main.cpp owns the console's UWP lifetime (the IFrameworkView, the
// CoreWindow, the app model's activation and suspend/resume) and src/main.cpp
// owns the desktop's command line. Neither of them decides how a game run
// starts or stops; both call into here, so the memory clamp, the chord layer and
// the device lifetime cannot differ between the two packages.
//
// This file compiles for every renderer and every platform: the console parts of
// it are either guarded by GR2_XBOX_UWP or expressed through the interfaces the
// backends share.

#include "graphics/gr2port_d3d12/uwp/uwp_main.h"

#include "common/logging/log.h"
#include "core/uwp_memory.h"

#if defined(GR2_XBOX_UWP)
// The HDMI query. WinRT only exists on the console build; the desktop build
// compiles this file too and must not see these headers.
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Graphics.Display.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/base.h>

#include <cmath>
#endif

namespace Gr2Port::Uwp {

namespace {

/// Set by RunGame, cleared by Shutdown. A second RunGame while the game is up
/// (the app model activating a window again after a suspend) is a no-op rather
/// than a second emulator.
bool g_running = false;
/// The console's core window, kept so NotifyDisplayChanged can resize the
/// swapchain without the caller having to keep it.
void* g_core_window = nullptr;

} // namespace

bool RunGame(void* core_window) {
    if (g_running) {
        LOG_INFO(Graphics, "UWP: RunGame called while the game is already running; ignored.");
        return true;
    }
    g_core_window = core_window;

    // The budget report comes first, before anything large is allocated: the
    // clamp in ClampGuestMemoryRegions only helps if the guest regions are sized
    // after it has been applied, and the App-mode warning explains an
    // ERROR_COMMITMENT_LIMIT that would otherwise look like an emulator bug.
    Core::Xbox::LogStartupReport();

#if defined(GR2_XBOX_UWP)
    if (core_window == nullptr) {
        // A console package with no core window cannot present: the swapchain is
        // a CoreWindow's, and there is no HWND to fall back to.
        LOG_CRITICAL(Graphics, "UWP: RunGame called with no core window on the console build.");
        return false;
    }
#endif

    // The emulator's own loop is started by the caller's window and device
    // plumbing; this function's contract is the part both entry points share --
    // the budget report, the run state and the shutdown order. The per-frame work
    // belongs to the renderer, so RunGame returns once the game has been handed
    // control, and the entry point's loop drives it.
    g_running = true;
    LOG_INFO(Graphics, "UWP: game run started (core window: {}).",
             core_window != nullptr ? "yes" : "no");
    return true;
}

void Shutdown() {
    if (!g_running) {
        return;
    }
    g_running = false;
    g_core_window = nullptr;
    LOG_INFO(Graphics, "UWP: game run stopped.");
}

void NotifyDisplayChanged() {
    if (!g_running) {
        return;
    }
    // The backend recreates the swapchain at the window's new size on the next
    // present; the message only has to reach the log so a resolution change is
    // visible in a report of a frame-pacing problem.
    LOG_INFO(Graphics, "UWP: the display mode changed; the swapchain will be recreated.");
}

DisplayModeQuery QueryDisplayMode() {
    DisplayModeQuery query;

    // The composition scale is what SetMatrixTransform has to cancel. Guard
    // everything: this runs on the view's thread during start-up, where an
    // unhandled exception tears the app down with no window to report it in.
    //
    // WinRT is only available on the console build. This file is compiled for the
    // desktop too (it owns RunGame and Shutdown for both), so the query itself has
    // to sit behind the guard -- guarding only the HDMI half left the desktop build
    // referring to a namespace it does not have.
#if defined(GR2_XBOX_UWP)
    try {
        const auto view = winrt::Windows::ApplicationModel::Core::CoreApplication::MainView();
        if (view) {
            const auto display = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView();
            if (display) {
                // ResolutionScale is an enum whose numeric value is the percentage
                // (100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500).
                const double percent = static_cast<double>(display.ResolutionScale());
                if (percent > 0.0) {
                    query.composition_scale = static_cast<float>(percent / 100.0);
                }
            }
        }
    } catch (const winrt::hresult_error& error) {
        LOG_WARNING(Graphics, "UWP: DisplayInformation unavailable ({}); assuming scale 1.0",
                    winrt::to_string(error.message()));
    }
#endif

    // The panel's own resolution. HdmiDisplayInformation reports the HDMI output
    // mode, which is the panel -- not the composition size -- and that is what a 4K
    // request is measured against. It is Xbox-specific: the type exists on desktop
    // UWP but has no display information for it.
#if defined(GR2_XBOX_UWP)
    try {
        const auto hdmi = winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation::GetForCurrentView();
        if (hdmi) {
            const auto mode = hdmi.GetCurrentDisplayMode();
            if (mode) {
                query.width = mode.ResolutionWidthInRawPixels();
                query.height = mode.ResolutionHeightInRawPixels();
                query.from_hdmi = query.width != 0 && query.height != 0;
            }
        }
    } catch (const winrt::hresult_error& error) {
        // Expected on a factory with no HDMI output (a desktop in this build, or a
        // console mid mode-change). Not an error worth failing start-up over.
        LOG_INFO(Graphics, "UWP: HdmiDisplayInformation unavailable ({}); using the window size",
                 winrt::to_string(error.message()));
    }
#endif

    if (!query.from_hdmi) {
#if defined(GR2_XBOX_UWP)
        // Fall back to the CoreWindow's bounds, which is the composited size. That
        // is smaller than the panel whenever the scale is not 1, which is exactly
        // why the HDMI query is preferred -- but a wrong-by-a-scale-factor number
        // is still better than a zero, and MakePlan clamps it.
        try {
            if (g_core_window != nullptr) {
                auto* core_window = static_cast<::ABI::Windows::UI::Core::ICoreWindow*>(g_core_window);
                const auto bounds = core_window->Bounds;
                if (bounds.Width > 0.0f && bounds.Height > 0.0f) {
                    const float inverse = query.composition_scale > 0.0f
                                              ? 1.0f / query.composition_scale
                                              : 1.0f;
                    // Bounds is in DIPs; multiply back out to physical pixels.
                    query.width = static_cast<unsigned int>(
                        std::lround(static_cast<double>(bounds.Width) * static_cast<double>(inverse)));
                    query.height = static_cast<unsigned int>(
                        std::lround(static_cast<double>(bounds.Height) * static_cast<double>(inverse)));
                }
            }
        } catch (const winrt::hresult_error&) {
            // Leave zeros; MakePlan substitutes 1080p.
        }
#endif
    }

    LOG_INFO(Graphics,
             "UWP: display {}x{} (scale {:.2f}, source {}); the swapchain will plan from this",
             query.width, query.height, static_cast<double>(query.composition_scale),
             query.from_hdmi ? "HDMI" : "window");
    return query;
}

} // namespace Gr2Port::Uwp
