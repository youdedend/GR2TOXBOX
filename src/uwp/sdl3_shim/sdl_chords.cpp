// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The chord layer's source, on the Xbox.
//
// src/uwp/chords.h turns an Xbox pad into the DualShock 4 outputs Gravity Rush 2
// reads: the touchpad flicks, the Panther transform, the three gravity styles and
// motion tilt. It is pure logic -- it takes a PadState and gives back
// ChordOutputs -- and until this file it had no caller on the console, because the
// input path that would call it is upstream's SDL event loop.
//
// So this is where the chord layer meets the pad. The pad shim reads the pad each
// frame; this file hands that state to Chords::Update and turns the outputs into
// the SDL events the guest's input handling already understands.
//
// Two details of that translation:
//
//   * A synthetic swipe is not a real touchpad gesture. Upstream models it as a
//     gamepad button of its own (SDL_GAMEPAD_BUTTON_TOUCHPAD_SWIPE_UP, which
//     input_handler.h defines as SDL_GAMEPAD_BUTTON_COUNT + 6 rather than a real
//     SDL button), and input_handler.cpp plays it back as centre-then-direction.
//     Emitting a press on that button is therefore exactly what a DualShock 4
//     flick produces.
//
//   * Motion tilt is not a button at all. The guest reads the pad's gyroscope from
//     a sensor event, so the chord's degrees-per-second tilt is delivered as one:
//     the same event a real DualShock 4's gyro produces. This is why the pad shim
//     answers "no gyro" to SDL_GamepadHasSensor and still sends gyro readings --
//     the pad has none, and the chord layer stands in for it.

// <utility> before input_handler.h: controller.h uses std::to_underlying and relies
// on a transitive include for it, which the console's MSVC build happens to
// provide and the precheck's gcc does not.
#include <utility>

#include "input/input_handler.h"
#include "uwp/sdl3_shim/chords_glue.h"

#if defined(GR2_XBOX_UWP)

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include "uwp/chords.h"

namespace {

// The instance ID the pad shim uses. The chord's events have to name the pad the
// guest opened, or input_handler.cpp cannot find the controller they belong to.
constexpr SDL_JoystickID kPadId = 1;

using Gr2Sdl::Pad::Button;

// The chords' outputs as SDL reports them, in one table so the two halves of the
// translation cannot drift apart. Upstream's own input_handler.h defines these
// touchpad names; they are not SDL's.
// The button field is an int, not SDL_GamepadButton.
//
// The synthetic touchpad and style names are not SDL enum values: input_handler.h
// defines them as `SDL_GAMEPAD_BUTTON_COUNT + n` macros, and the style ones as
// 0x08000001 and up, which is far past anything SDL's enum names. Casting them
// into the enum is the obvious fix and it is wrong: clang rejects a constant cast
// that lands outside the enumeration's range (-Wenum-constexpr-conversion), which
// is how the first attempt at this failed. The values are ints by construction, so
// the field keeps them as ints and PushButton takes them as ints.
struct SyntheticButton {
    bool Uwp::Chords::ChordOutputs::*output;
    int button;
};

constexpr SyntheticButton kSyntheticButtons[] = {
    {&Uwp::Chords::ChordOutputs::touchpad_swipe_up,
     SDL_GAMEPAD_BUTTON_TOUCHPAD_SWIPE_UP},
    {&Uwp::Chords::ChordOutputs::touchpad_swipe_down,
     SDL_GAMEPAD_BUTTON_TOUCHPAD_SWIPE_DOWN},
    {&Uwp::Chords::ChordOutputs::touchpad_swipe_left,
     SDL_GAMEPAD_BUTTON_TOUCHPAD_SWIPE_LEFT},
    {&Uwp::Chords::ChordOutputs::touchpad_swipe_right,
     SDL_GAMEPAD_BUTTON_TOUCHPAD_SWIPE_RIGHT},
    {&Uwp::Chords::ChordOutputs::touchpad_center,
     SDL_GAMEPAD_BUTTON_TOUCHPAD_CENTER},
    {&Uwp::Chords::ChordOutputs::normal_style,
     GR2_NORMAL_STYLE},
    {&Uwp::Chords::ChordOutputs::lunar_style,
     GR2_LUNAR_STYLE},
    {&Uwp::Chords::ChordOutputs::jupiter_style,
     GR2_JUPITER_STYLE},
};

// The buttons a chord can borrow, and the swallow flag that says it did. Face
// buttons, the D-Pad and the two stick clicks are the ones the default chord map
// uses; keeping the table to them is what makes "which flag applies to this
// button" a lookup rather than a guess.
struct SwallowGroup {
    Button buttons[4];
    bool Uwp::Chords::ChordOutputs::*flag;
};

constexpr SwallowGroup kSwallowGroups[] = {
    {{Button::DpadUp, Button::DpadDown, Button::DpadLeft, Button::DpadRight},
     &Uwp::Chords::ChordOutputs::swallow_dpad},
    {{Button::South, Button::East, Button::West, Button::North},
     &Uwp::Chords::ChordOutputs::swallow_face},
    {{Button::LeftStick, Button::RightStick, Button::Count, Button::Count},
     &Uwp::Chords::ChordOutputs::swallow_sticks},
};

bool Swallowed(const Button button, const Uwp::Chords::ChordOutputs& outputs) {
    for (const SwallowGroup& group : kSwallowGroups) {
        for (const Button candidate : group.buttons) {
            if (candidate == button) {
                return outputs.*(group.flag);
            }
        }
    }
    return false;
}

SDL_GamepadButton ToSdlButton(const Button button) {
    switch (button) {
    case Button::South:
        return SDL_GAMEPAD_BUTTON_SOUTH;
    case Button::East:
        return SDL_GAMEPAD_BUTTON_EAST;
    case Button::West:
        return SDL_GAMEPAD_BUTTON_WEST;
    case Button::North:
        return SDL_GAMEPAD_BUTTON_NORTH;
    case Button::Back:
        return SDL_GAMEPAD_BUTTON_BACK;
    case Button::Start:
        return SDL_GAMEPAD_BUTTON_START;
    case Button::LeftStick:
        return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    case Button::RightStick:
        return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    case Button::LeftShoulder:
        return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    case Button::RightShoulder:
        return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    case Button::DpadUp:
        return SDL_GAMEPAD_BUTTON_DPAD_UP;
    case Button::DpadDown:
        return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    case Button::DpadLeft:
        return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    case Button::DpadRight:
        return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    case Button::Misc1:
        return SDL_GAMEPAD_BUTTON_MISC1;
    case Button::Guide:
    case Button::Count:
    default:
        return SDL_GAMEPAD_BUTTON_INVALID;
    }
}

Uwp::Chords::PadState ToPadState(const Gr2Sdl::Pad::State& state) {
    const auto down = [&state](const Button button) {
        return state.buttons[static_cast<int>(button)];
    };
    Uwp::Chords::PadState pad{};
    pad.view = down(Button::Back);
    pad.menu = down(Button::Start);
    pad.left_bumper = down(Button::LeftShoulder);
    pad.right_bumper = down(Button::RightShoulder);
    pad.dpad_up = down(Button::DpadUp);
    pad.dpad_down = down(Button::DpadDown);
    pad.dpad_left = down(Button::DpadLeft);
    pad.dpad_right = down(Button::DpadRight);
    pad.a = down(Button::South);
    pad.b = down(Button::East);
    pad.x = down(Button::West);
    pad.y = down(Button::North);
    pad.left_stick = down(Button::LeftStick);
    pad.right_stick = down(Button::RightStick);
    pad.right_stick_x = state.axes[static_cast<int>(Gr2Sdl::Pad::Axis::RightX)];
    pad.right_stick_y = state.axes[static_cast<int>(Gr2Sdl::Pad::Axis::RightY)];
    return pad;
}

void PushButton(const int button, const bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.which = kPadId;
    event.gbutton.button = static_cast<Uint8>(button);
    event.gbutton.down = down;
    SDL_PushEvent(&event);
}

// The last state the chord layer saw, so a chord's rising edge fires once.
Uwp::Chords::PadState g_previous_pad{};
// The previous frame's outputs, so a swallowed press can be replayed when the
// chord that swallowed it ends.
Uwp::Chords::ChordOutputs g_previous_outputs{};

// What the guest currently believes about the borrowed buttons. Kept separately
// from the pad's real state because a chord makes the two disagree: the pad says
// "D-Pad up is down", the guest must see "it was never pressed".
bool g_reported[static_cast<int>(Button::Count)]{};

void EmitTilt(const Uwp::Chords::ChordOutputs& outputs, const bool was_active) {
    if (!outputs.motion_active && !was_active) {
        return;
    }
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE;
    event.gsensor.which = kPadId;
    event.gsensor.sensor = SDL_SENSOR_GYRO;
    if (outputs.motion_active) {
        // Degrees per second to radians per second, which is what
        // SDL_GamepadSensorEvent carries and what UpdateSensorGyro expects. Pitch
        // is a rotation about X and roll about Z, matching the DualShock 4's axes.
        constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
        event.gsensor.data[0] = outputs.motion_tilt_pitch * kDegreesToRadians;
        event.gsensor.data[1] = 0.0f;
        event.gsensor.data[2] = outputs.motion_tilt_roll * kDegreesToRadians;
    }
    // An all-zero reading on release is sent on purpose: it is how the guest
    // learns the pad stopped tilting.
    SDL_PushEvent(&event);
}

} // namespace

namespace Gr2Sdl::Chords {

void Attach(const Gr2Sdl::Pad::State& state) {
    const Uwp::Chords::PadState current = ToPadState(state);
    const Uwp::Chords::ChordOutputs outputs = Uwp::Chords::Update(current, g_previous_pad);
    g_previous_pad = current;

    // Each synthetic button is a press and a matching release: down on the frame
    // the flag rises, up on the frame it falls. input_handler.cpp's edge detection
    // needs both, or the guest treats the button as held forever.
    for (const SyntheticButton& synthetic : kSyntheticButtons) {
        const bool now = outputs.*(synthetic.output);
        const bool before = g_previous_outputs.*(synthetic.output);
        if (now != before) {
            PushButton(synthetic.button, now);
        }
    }

    EmitTilt(outputs, g_previous_outputs.motion_active);

    // A chord eats the button it borrowed, so "View + D-Pad Up" flicks without
    // also walking. The pad shim has already pushed the real press by the time the
    // chord layer has seen it, so the correction is a release when the guest was
    // told "down" and must now believe "up" -- and a fresh press when the chord
    // ends with the button still held, so the guest sees the D-Pad key it is
    // actually holding.
    for (int i = 0; i < static_cast<int>(Button::Count); ++i) {
        const Button button = static_cast<Button>(i);
        const SDL_GamepadButton sdl = ToSdlButton(button);
        if (sdl == SDL_GAMEPAD_BUTTON_INVALID) {
            continue;
        }
        const bool held = state.buttons[i];
        const bool hide = held && Swallowed(button, outputs);
        const bool wanted = held && !hide;
        if (wanted == g_reported[i]) {
            continue;
        }
        PushButton(sdl, wanted);
        g_reported[i] = wanted;
    }

    g_previous_outputs = outputs;
}

void Detach() {
    // The pad went away. Emit the releases the guest is owed for anything the
    // chord layer said was held, so a chord cannot leave a key stuck.
    for (int i = 0; i < static_cast<int>(Button::Count); ++i) {
        const SDL_GamepadButton sdl = ToSdlButton(static_cast<Button>(i));
        if (sdl != SDL_GAMEPAD_BUTTON_INVALID && g_reported[i]) {
            PushButton(sdl, false);
            g_reported[i] = false;
        }
    }
    g_previous_pad = Uwp::Chords::PadState{};
    g_previous_outputs = Uwp::Chords::ChordOutputs{};
}

} // namespace Gr2Sdl::Chords

#else // GR2_XBOX_UWP

// The chord layer's source is only needed where the pad comes from
// Windows.Gaming.Input, so every other build compiles this file to nothing. The
// precheck still builds it, which is how the logic above stays honest.
namespace Gr2Sdl::Chords {

void Attach(const Gr2Sdl::Pad::State& /*state*/) {}

void Detach() {}

} // namespace Gr2Sdl::Chords

#endif // GR2_XBOX_UWP
