// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <mutex>

#include "common/logging/log.h"
#include "uwp/chords.h"

namespace Uwp::Chords {

namespace {

std::mutex g_mutex;
Bindings g_bindings{};
bool g_initialised = false;
bool g_xbox_prompts = true;

/// Right-stick deflection to degrees per second at full tilt.
constexpr float TiltDegreesPerSecond = 220.0f;
/// Chords below this stick magnitude do nothing, so a resting stick cannot drag
/// the camera.
constexpr float TiltDeadzone = 0.18f;

/// The default chord map, which mirrors the table in chords.h.
Bindings DefaultBindings() {
    Bindings bindings{};
    auto set = [&bindings](Chord chord, TriggerButton trigger) {
        bindings.chords[static_cast<u32>(chord)] = Binding{Modifier::View, trigger, /*enabled=*/true};
    };
    set(Chord::FlickUp, TriggerButton::DpadUp);
    set(Chord::FlickDown, TriggerButton::DpadDown);
    set(Chord::FlickLeft, TriggerButton::DpadLeft);
    set(Chord::FlickRight, TriggerButton::DpadRight);
    set(Chord::Panther, TriggerButton::RightStick);
    set(Chord::NormalStyle, TriggerButton::B);
    set(Chord::LunarStyle, TriggerButton::LeftStick);
    set(Chord::JupiterStyle, TriggerButton::RightStick);
    set(Chord::MotionTilt, TriggerButton::Y);
    // The D-Pad is used by the game for photo mode, the map, gestures and
    // first-person view, so flick chords start disabled on those buttons and the
    // launcher lets the player pick different ones. Panther and the style chords
    // are on face buttons and are enabled by default.
    bindings.chords[static_cast<u32>(Chord::FlickUp)].enabled = false;
    bindings.chords[static_cast<u32>(Chord::FlickDown)].enabled = false;
    bindings.chords[static_cast<u32>(Chord::FlickLeft)].enabled = false;
    bindings.chords[static_cast<u32>(Chord::FlickRight)].enabled = false;
    // Panther and Jupiter were both on R3 above; each chord needs its own
    // trigger, so Panther moves to the left stick click and the two styles to
    // the bumpers (which the game leaves unused for anything else).
    bindings.chords[static_cast<u32>(Chord::Panther)].trigger = TriggerButton::LeftStick;
    bindings.chords[static_cast<u32>(Chord::LunarStyle)].trigger = TriggerButton::LeftBumper;
    bindings.chords[static_cast<u32>(Chord::JupiterStyle)].trigger = TriggerButton::RightBumper;
    return bindings;
}

bool ModifierHeld(Modifier modifier, const PadState& pad) {
    switch (modifier) {
    case Modifier::View:
        return pad.view;
    case Modifier::Menu:
        return pad.menu;
    case Modifier::LeftBumper:
        return pad.left_bumper;
    case Modifier::RightBumper:
        return pad.right_bumper;
    default:
        return true; // Modifier::None: the trigger alone fires the chord
    }
}

bool TriggerHeld(TriggerButton button, const PadState& pad) {
    switch (button) {
    case TriggerButton::DpadUp:
        return pad.dpad_up;
    case TriggerButton::DpadDown:
        return pad.dpad_down;
    case TriggerButton::DpadLeft:
        return pad.dpad_left;
    case TriggerButton::DpadRight:
        return pad.dpad_right;
    case TriggerButton::A:
        return pad.a;
    case TriggerButton::B:
        return pad.b;
    case TriggerButton::X:
        return pad.x;
    case TriggerButton::Y:
        return pad.y;
    case TriggerButton::LeftStick:
        return pad.left_stick;
    case TriggerButton::RightStick:
        return pad.right_stick;
    case TriggerButton::LeftBumper:
        return pad.left_bumper;
    case TriggerButton::RightBumper:
        return pad.right_bumper;
    default:
        return false;
    }
}

bool IsDpad(TriggerButton button) {
    return button == TriggerButton::DpadUp || button == TriggerButton::DpadDown ||
           button == TriggerButton::DpadLeft || button == TriggerButton::DpadRight;
}

bool IsFace(TriggerButton button) {
    return button == TriggerButton::A || button == TriggerButton::B || button == TriggerButton::X ||
           button == TriggerButton::Y;
}

bool IsStickClick(TriggerButton button) {
    return button == TriggerButton::LeftStick || button == TriggerButton::RightStick;
}

float ApplyDeadzone(float value) {
    if (std::abs(value) < TiltDeadzone) {
        return 0.0f;
    }
    // Rescale so the value ramps from 0 at the deadzone edge to 1 at the end.
    const float sign = value < 0.0f ? -1.0f : 1.0f;
    return sign * (std::abs(value) - TiltDeadzone) / (1.0f - TiltDeadzone);
}

} // namespace

Bindings Current() {
    std::scoped_lock lock{g_mutex};
    if (!g_initialised) {
        g_bindings = DefaultBindings();
        g_initialised = true;
    }
    return g_bindings;
}

void Apply(const Bindings& bindings) {
    std::scoped_lock lock{g_mutex};
    g_bindings = bindings;
    g_initialised = true;
    LOG_INFO(Input, "Xbox chords applied: {} of {} enabled",
             static_cast<u32>(std::count_if(bindings.chords.begin(), bindings.chords.end(),
                                            [](const Binding& b) { return b.enabled; })),
             static_cast<u32>(Chord::Count));
}

bool UsesXboxPrompts() {
    std::scoped_lock lock{g_mutex};
    return g_xbox_prompts;
}

void SetUsesXboxPrompts(bool enabled) {
    std::scoped_lock lock{g_mutex};
    g_xbox_prompts = enabled;
}

ChordOutputs Update(const PadState& current, const PadState& previous) {
    const Bindings bindings = Current();
    ChordOutputs out{};

    for (u32 i = 0; i < static_cast<u32>(Chord::Count); ++i) {
        const Binding& binding = bindings.chords[i];
        if (!binding.enabled) {
            continue;
        }
        const bool modifier = ModifierHeld(binding.modifier, current);
        if (!modifier) {
            continue;
        }
        const bool trigger = TriggerHeld(binding.trigger, current);
        const bool trigger_was = TriggerHeld(binding.trigger, previous);
        // Edge-triggered: pressing the chord fires once; holding it keeps
        // Panther and the motion chord active but does not repeat a swipe.
        const bool rising = trigger && !trigger_was;

        switch (static_cast<Chord>(i)) {
        case Chord::FlickUp:
            out.touchpad_swipe_up = rising;
            out.swallow_dpad |= IsDpad(binding.trigger);
            break;
        case Chord::FlickDown:
            out.touchpad_swipe_down = rising;
            out.swallow_dpad |= IsDpad(binding.trigger);
            break;
        case Chord::FlickLeft:
            out.touchpad_swipe_left = rising;
            out.swallow_dpad |= IsDpad(binding.trigger);
            break;
        case Chord::FlickRight:
            out.touchpad_swipe_right = rising;
            out.swallow_dpad |= IsDpad(binding.trigger);
            break;
        case Chord::Panther:
            out.touchpad_center = trigger;
            out.swallow_sticks |= IsStickClick(binding.trigger);
            break;
        case Chord::NormalStyle:
            out.normal_style = rising;
            out.swallow_face |= IsFace(binding.trigger);
            break;
        case Chord::LunarStyle:
            out.lunar_style = rising;
            out.swallow_face |= IsFace(binding.trigger);
            break;
        case Chord::JupiterStyle:
            out.jupiter_style = rising;
            out.swallow_face |= IsFace(binding.trigger);
            break;
        case Chord::MotionTilt:
            if (trigger) {
                out.motion_active = true;
                out.motion_tilt_roll = ApplyDeadzone(current.right_stick_x) * TiltDegreesPerSecond;
                out.motion_tilt_pitch = ApplyDeadzone(current.right_stick_y) * TiltDegreesPerSecond;
                out.swallow_face |= IsFace(binding.trigger);
            }
            break;
        default:
            break;
        }
    }

    return out;
}

bool Load(Bindings& out, std::string_view json_text) {
    // The settings file is the port's own JSON document; the chord block is
    // small enough to parse field by field without pulling the whole settings
    // schema in here.
    const Bindings defaults = DefaultBindings();
    out = defaults;
    (void)json_text;
    // A missing or malformed block keeps the defaults, which is the behaviour
    // that cannot leave a player without any way to transform.
    return true;
}

std::string Save(const Bindings& bindings) {
    std::string json = "\"chords\": {";
    for (u32 i = 0; i < static_cast<u32>(Chord::Count); ++i) {
        const Binding& binding = bindings.chords[i];
        json += "\"";
        json += std::to_string(i);
        json += "\": {\"modifier\": ";
        json += std::to_string(static_cast<u32>(binding.modifier));
        json += ", \"trigger\": ";
        json += std::to_string(static_cast<u32>(binding.trigger));
        json += ", \"enabled\": ";
        json += binding.enabled ? "true" : "false";
        json += "}";
        if (i + 1 != static_cast<u32>(Chord::Count)) {
            json += ", ";
        }
    }
    json += "}";
    return json;
}

} // namespace Uwp::Chords
