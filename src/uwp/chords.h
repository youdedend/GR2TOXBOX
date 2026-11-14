// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>
#include <string_view>

#include "common/types.h"

/// Controller chords that restore the DualShock 4's touchpad and motion inputs on
/// an Xbox pad.
///
/// Gravity Rush 2 uses the DualShock 4 in two ways an Xbox controller cannot
/// reproduce directly:
///
///   * the touchpad, for **flick** attacks (up/down/left/right) and the
///     **Panther** transform (hold the touchpad);
///   * six-axis motion, for the gravity-slide and photo-mode tilt.
///
/// Upstream already models both as controller *outputs*
/// (`touchpad_swipe_up/down/left/right`, `touchpad_center`, `motion_tilt_*`)
/// and binds the game's actions to them (`gr2_flick_up`, ... in
/// input/game_functions.cpp). What is missing on Xbox is a *source*: something
/// has to produce those outputs from an Xbox pad.
///
/// This module is that source. It watches the raw Xbox pad state and emits
/// synthetic outputs when a chord is held:
///
/// | Chord | Output | Game action |
/// | --- | --- | --- |
/// | View + D-Pad Up | `touchpad_swipe_up` | `gr2_flick_up` |
/// | View + D-Pad Down | `touchpad_swipe_down` | `gr2_flick_down` |
/// | View + D-Pad Left | `touchpad_swipe_left` | `gr2_flick_left` |
/// | View + D-Pad Right | `touchpad_swipe_right` | `gr2_flick_right` |
/// | View + R3 (hold) | `touchpad_center` | `gr2_panther` |
/// | View + LB | `gr2_lunar_style` | Lunar style |
/// | View + RB | `gr2_jupiter_style` | Jupiter style |
/// | View + B | `gr2_normal_style` | Normal style |
/// | View + Y (hold) | motion-tilt from the right stick | gravity-slide tilt |
///
/// Every chord is remappable through the launcher and can be disabled, because
/// a player who does not use flicks should not lose the D-Pad.
namespace Uwp::Chords {

/// A stable identifier for each chord, used by the settings file.
enum class Chord : u32 {
    FlickUp = 0,
    FlickDown,
    FlickLeft,
    FlickRight,
    Panther,       // hold for touchpad click
    NormalStyle,
    LunarStyle,
    JupiterStyle,
    MotionTilt,    // hold to drive motion from the right stick
    Count,
};

/// The Xbox buttons a chord can use as its modifier.
enum class Modifier : u32 {
    View = 0,      // the "View" / Back button
    Menu,
    LeftBumper,
    RightBumper,
    None,
    Count,
};

/// The button pressed together with the modifier.
enum class TriggerButton : u32 {
    DpadUp = 0,
    DpadDown,
    DpadLeft,
    DpadRight,
    A,
    B,
    X,
    Y,
    LeftStick,   // L3
    RightStick,  // R3
    LeftBumper,  // LB
    RightBumper, // RB
    Count,
};

struct Binding {
    Modifier modifier = Modifier::View;
    TriggerButton trigger = TriggerButton::DpadUp;
    bool enabled = false;
};

/// The full chord map. Defaults are the table above; the launcher edits this and
/// saves it next to the game settings.
struct Bindings {
    std::array<Binding, static_cast<u32>(Chord::Count)> chords{};
};

/// Stores and applies the bindings in the guest's controller outputs.
void Apply(const Bindings& bindings);
[[nodiscard]] Bindings Current();
[[nodiscard]] bool UsesXboxPrompts();
void SetUsesXboxPrompts(bool enabled);

/// Xbox pad state as the chord layer needs it.
struct PadState {
    bool view = false;
    bool menu = false;
    bool left_bumper = false;
    bool right_bumper = false;
    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;
    bool a = false;
    bool b = false;
    bool x = false;
    bool y = false;
    bool left_stick = false;  // L3 click
    bool right_stick = false; // R3 click
    /// Right stick, -1..1, for the motion-tilt chord.
    float right_stick_x = 0.0f;
    float right_stick_y = 0.0f;
};

/// The synthetic outputs a frame should emit.
struct ChordOutputs {
    bool touchpad_swipe_up = false;
    bool touchpad_swipe_down = false;
    bool touchpad_swipe_left = false;
    bool touchpad_swipe_right = false;
    bool touchpad_center = false;
    bool normal_style = false;
    bool lunar_style = false;
    bool jupiter_style = false;
    /// Degrees per second of tilt, derived from the right stick.
    float motion_tilt_pitch = 0.0f;
    float motion_tilt_roll = 0.0f;
    bool motion_active = false;
    /// Chords consumed the D-Pad / face button this frame, so the guest must not
    /// also see the raw press (otherwise "View + D-Pad Up" would also open the
    /// photo-mode menu).
    bool swallow_dpad = false;
    bool swallow_face = false;
    bool swallow_sticks = false;
};

/// Computes the outputs for this frame. `previous` is the state the last call
/// saw, so swipes are edge-triggered rather than repeated every frame.
ChordOutputs Update(const PadState& current, const PadState& previous);

/// Loads and saves the bindings (JSON, under the port's own settings file).
bool Load(Bindings& out, std::string_view json_text);
std::string Save(const Bindings& bindings);

} // namespace Uwp::Chords
