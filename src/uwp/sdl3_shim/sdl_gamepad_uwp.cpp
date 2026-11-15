// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The pad's platform edge on the Xbox: Windows.Gaming.Input.
//
// This file is the only place the shim touches WinRT, and it is the only file the
// precheck cannot check, because there is no Windows SDK in the check's
// environment. That is deliberate: everything that can be reasoned about without
// a console -- the SDL mapping, the event edges, the chord plumbing -- lives in
// sdl_gamepad.cpp, which the precheck does compile. Keep this file thin.
//
// Why Windows.Gaming.Input rather than SDL's own XInput backend: XInput is still
// in the onecore surface a UWP app links against, but SDL 3 has no UWP build, and
// Windows.Gaming.Input is the API Microsoft documents for a packaged Xbox title.
// It also reports the Series pad's Share button and its precise triggers, which
// XInput's 8-bit buttons do not.

#include "uwp/sdl3_shim/pad_state.h"

#if defined(GR2_XBOX_UWP)

#include <winrt/Windows.Foundation.h>
// IVectorView's Size/GetAt/First are in the Collections header, not in
// Windows.Foundation.h. Without it GetAt is a template whose return type has not
// been deduced yet, which reads as "cannot be used before it is defined" and
// says nothing about a missing include.
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/base.h>

#include <algorithm>
#include <mutex>

namespace Gr2Sdl::Pad {

namespace {

std::mutex g_mutex;
winrt::Windows::Gaming::Input::IGameController g_controller{nullptr};
winrt::Windows::Gaming::Input::Gamepad g_gamepad{nullptr};
/// Set by the Added and Removed handlers. The handlers run on a pool thread, so
/// the flag is read under the same lock the state is read under.
bool g_dirty = true;

/// Reads the 0..1 doubles the API reports into the -1..1 sticks SDL expects.
float ToSigned(const double value) {
    // The API already writes the stick's own range; the clamp is for the small
    // overshoot a worn pad produces at the extremes.
    return static_cast<float>(std::clamp(value * 2.0 - 1.0, -1.0, 1.0));
}

float ToTrigger(const double value) {
    return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

/// Picks up the pad that was added. The console has one, so the first one found is
/// the one the guest uses.
void AdoptGamepadLocked() {
    const auto pads = winrt::Windows::Gaming::Input::Gamepad::Gamepads();
    if (pads.Size() == 0) {
        g_controller = nullptr;
        g_gamepad = nullptr;
        return;
    }
    g_gamepad = pads.GetAt(0);
    g_controller = g_gamepad;
}

void OnGamepadAdded(const winrt::Windows::Foundation::IInspectable&,
                    const winrt::Windows::Gaming::Input::Gamepad&) {
    const std::scoped_lock lock{g_mutex};
    g_dirty = true;
}

void OnGamepadRemoved(const winrt::Windows::Foundation::IInspectable&,
                      const winrt::Windows::Gaming::Input::Gamepad&) {
    const std::scoped_lock lock{g_mutex};
    g_dirty = true;
}

} // namespace

State Read() {
    const std::scoped_lock lock{g_mutex};

    if (g_dirty || g_gamepad == nullptr) {
        g_dirty = false;
        AdoptGamepadLocked();
    }
    if (g_gamepad == nullptr) {
        return State{};
    }

    const auto reading = g_gamepad.GetCurrentReading();

    State state{};
    state.connected = true;

    const auto& buttons = reading.Buttons;
    state.buttons[static_cast<int>(Button::South)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::A) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::East)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::B) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::West)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::X) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::North)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::Y) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::Back)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::View) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::Start)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::Menu) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    // The Xbox button is the shell's: a packaged app is not allowed to consume
    // it and GamepadButtons has no member for it, so it stays false. The chord
    // layer never uses it as a modifier -- View is the default -- so nothing is
    // lost.
    state.buttons[static_cast<int>(Button::LeftStick)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::LeftThumbstick) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::RightStick)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::RightThumbstick) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::LeftShoulder)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::LeftShoulder) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::RightShoulder)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::RightShoulder) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::DpadUp)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::DPadUp) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::DpadDown)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::DPadDown) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::DpadLeft)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::DPadLeft) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    state.buttons[static_cast<int>(Button::DpadRight)] =
        (buttons & winrt::Windows::Gaming::Input::GamepadButtons::DPadRight) !=
        winrt::Windows::Gaming::Input::GamepadButtons::None;
    // Share arrives with the Series pad; the enum member is absent on older SDKs,
    // so it is read through the numeric value Microsoft documents rather than a
    // name that may not compile.
    state.buttons[static_cast<int>(Button::Misc1)] =
        (static_cast<unsigned int>(buttons) & 0x2000u) != 0u;

    state.axes[static_cast<int>(Axis::LeftX)] = ToSigned(reading.LeftThumbstickX);
    state.axes[static_cast<int>(Axis::LeftY)] = ToSigned(reading.LeftThumbstickY);
    state.axes[static_cast<int>(Axis::RightX)] = ToSigned(reading.RightThumbstickX);
    state.axes[static_cast<int>(Axis::RightY)] = ToSigned(reading.RightThumbstickY);
    state.axes[static_cast<int>(Axis::LeftTrigger)] = ToTrigger(reading.LeftTrigger);
    state.axes[static_cast<int>(Axis::RightTrigger)] = ToTrigger(reading.RightTrigger);

    return state;
}

bool Rumble(float low_frequency, float high_frequency) {
    // GamepadVibration takes 0..1 doubles for the four motors. The triggers are
    // reported as a separate pair and the pad ignores them unless it has impulse
    // triggers, so both are driven from the two motors SDL offers.
    winrt::Windows::Gaming::Input::GamepadVibration vibration{};
    vibration.LeftMotor = std::clamp(static_cast<double>(low_frequency), 0.0, 1.0);
    vibration.RightMotor = std::clamp(static_cast<double>(high_frequency), 0.0, 1.0);
    vibration.LeftTrigger = std::clamp(static_cast<double>(low_frequency), 0.0, 1.0);
    vibration.RightTrigger = std::clamp(static_cast<double>(high_frequency), 0.0, 1.0);

    const std::scoped_lock lock{g_mutex};
    if (g_gamepad == nullptr) {
        return false;
    }
    try {
        g_gamepad.Vibration(vibration);
        return true;
    } catch (const winrt::hresult_error&) {
        // A rumble on a pad that has just been removed throws. SDL's contract is
        // that this returns false rather than that it cannot throw.
        return false;
    }
}

bool SetLight(unsigned char /*red*/, unsigned char /*green*/, unsigned char /*blue*/) {
    // The player indicator is set by the shell from the app's declared player
    // count; a packaged app cannot address it. controller.cpp uses the LED for
    // per-controller colour, which the console paints itself instead.
    return false;
}

} // namespace Gr2Sdl::Pad

#else // GR2_XBOX_UWP

// Every other build links this file to nothing: the shim is only on the include
// path for the Xbox package, so nothing calls into it, and the precheck compiles
// it to prove that.
namespace Gr2Sdl::Pad {

State Read() {
    return State{};
}

bool Rumble(float /*low_frequency*/, float /*high_frequency*/) {
    return false;
}

bool SetLight(unsigned char /*red*/, unsigned char /*green*/, unsigned char /*blue*/) {
    return false;
}

} // namespace Gr2Sdl::Pad

#endif // GR2_XBOX_UWP
