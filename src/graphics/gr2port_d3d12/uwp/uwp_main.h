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

/// Tells the backend that the display's mode or DPI changed, so the swapchain is
/// recreated at the new size rather than presenting a stretched frame.
void NotifyDisplayChanged();

} // namespace Gr2Port::Uwp
