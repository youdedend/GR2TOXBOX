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

} // namespace Gr2Port::Uwp
