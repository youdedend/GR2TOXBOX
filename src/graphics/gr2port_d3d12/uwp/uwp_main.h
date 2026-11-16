// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/// The Xbox Series X|S entry point, and the app lifetime the port has to obey.
///
/// The console package is a UWP app, so the port does not own its process: the
/// shell activates it, hands it a CoreWindow through IFrameworkView, and ends it
/// by closing that window. uwp_main.cpp implements that contract and calls
/// `RunGame`, which is the same function the desktop entry point calls, so the
/// two packages cannot diverge in how they start the emulator.
///
/// Only the console build defines GR2_XBOX_UWP, so this header is safe to include
/// from shared code.
namespace Gr2Port::Uwp {

/// Starts the emulator and blocks until the game exits.
///
/// `core_window` is the console's `ICoreWindow*`, which the D3D12 backend turns
/// into a swapchain with `CreateSwapChainForCoreWindow`. It is null on the
/// desktop build, where SDL owns the window instead.
///
/// Returns false when the game could not start, in which case the caller should
/// let the app close rather than spin in a frame loop over a dead device.
bool RunGame(void* core_window);

/// Stops the emulator and releases the device. Called from the view's
/// Uninitialize, and after the game's own shutdown on desktop.
void Shutdown();
void RequestStop();
bool SessionRunning();
void* CoreWindowHandle();
// UI-thread pump, shared by the launcher and the emulator WindowSDL loop.
bool PumpConsoleEvents();
bool ConsoleSession();

/// Tells the backend that the display's mode or DPI changed, so the swapchain is
/// recreated at the new size rather than presenting a stretched frame.
void NotifyDisplayChanged();

/// Asks the console what its HDMI output is currently doing, and returns the
/// mode reduced to the numbers the swapchain needs.
///
/// On the console this reads HdmiDisplayInformation for the current view -- the
/// API that reports the *panel's* resolution, which on an Xbox is usually 3840x2160
/// even when the title presents 1920x1080 and lets the scaler do the rest. The
/// composition scale comes from DisplayInformation, because a CoreWindow swapchain
/// is composited at the window's logical size and has to be told to cancel that
/// scaling.
///
/// Returns a zero-sized Mode on a build or a moment where neither API answers,
/// which Display::MakePlan turns into a safe 1080p fallback.
///
/// Declared here rather than in a WinRT header so the D3D12 backend can call it
/// without including C++/WinRT: device.cpp is compiled for the desktop build too.
struct DisplayModeQuery {
    unsigned int width = 0;
    unsigned int height = 0;
    float composition_scale = 1.0f;
    /// True when HdmiDisplayInformation answered, false when only the
    /// composition scale was available.
    bool from_hdmi = false;
};
DisplayModeQuery QueryDisplayMode();

} // namespace Gr2Port::Uwp
