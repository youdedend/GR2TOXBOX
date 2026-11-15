// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/// Padding, as the SDL3 shim sees it.
///
/// The shim's job here is small and specific: SDL's gamepad API is a polled view
/// of pad state plus events when that state changes, and Windows.Gaming.Input is
/// also a polled view of pad state. So the shim reads the pad on the platform
/// thread and keeps the last frame's reading; the chord layer and controller.cpp
/// ask for it through the SDL functions they already call, and no SDL-side state
/// machine is needed.
///
/// Why this header exists at all: Windows.Gaming.Input is WinRT, which cannot be
/// compiled where the shim is unit-checked (tools/ci/local_precheck.sh compiles
/// each shim file against the real SDL headers with no Windows SDK). The pad's
/// *platform* edge therefore lives in sdl_gamepad_uwp.cpp behind this interface,
/// and everything that can be reasoned about without a console -- the mapping
/// from pad buttons to SDL's gamepad enums, the event synthesis, the chord
/// plumbing -- lives in sdl_gamepad.cpp, which needs nothing but SDL.
///
/// On any build that is not the Xbox package the platform side is an empty pad,
/// so the desktop build of these files still compiles and the precheck covers the
/// code that ships.
namespace Gr2Sdl::Pad {

/// The pad's buttons, as the shim reports them.
///
/// A one-to-one copy of SDL's gamepad button order for the buttons an Xbox pad
/// has, so the mapping in sdl_gamepad.cpp is an identity and cannot drift. The
/// names are SDL's, not Microsoft's: SDL calls the bottom face button *south*
/// because the same enum is A on an Xbox pad and X on a Nintendo one.
enum class Button : int {
    South = 0,      // A
    East,           // B
    West,           // X
    North,          // Y
    Back,           // View
    Guide,          // The Xbox button; no SDL gamepad button by default
    Start,          // Menu
    LeftStick,      // L3
    RightStick,     // R3
    LeftShoulder,   // LB
    RightShoulder,  // RB
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Misc1,          // Share on a Series pad
    Count,
};

/// The pad's axes, in SDL's order.
enum class Axis : int {
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count,
};

/// One reading of the pad.
///
/// Buttons are booleans rather than SDL's own storage because the platform side
/// is where a press becomes an edge: a Windows.Gaming.Input reading says "this
/// button is down", and the shim turns that into an event only when the previous
/// reading disagreed.
struct State {
    bool connected = false;
    /// Indexed by Button.
    bool buttons[static_cast<int>(Button::Count)]{};
    /// Indexed by Axis. Sticks run -1..1, triggers 0..1.
    float axes[static_cast<int>(Axis::Count)]{};
    /// The pad's serial number, or empty when the platform does not expose one.
    /// controller.cpp does not read it, but the SDL API offers it and the shim
    /// must not invent one.
    const char* serial = "";
};

/// Reads the pad now. Called once per frame by the shim's update entry point.
///
/// The Xbox package implements this over Windows.Gaming.Input; every other build
/// returns a disconnected state. Declared rather than defined here so the two
/// implementations can be selected by the build, and so the header carries no
/// WinRT.
State Read();

/// The pad's rumble motor speeds, 0..1. Platform-specific, so it lives here
/// beside Read rather than in the SDL-shaped code.
///
/// Returns false when the pad cannot vibrate, which is what SDL_RumbleGamepad
/// reports to its caller.
bool Rumble(float low_frequency, float high_frequency);

/// The console's player indicator light. Returns false when the pad has none,
/// which is what SDL_SetGamepadLED reports.
bool SetLight(unsigned char red, unsigned char green, unsigned char blue);

} // namespace Gr2Sdl::Pad
