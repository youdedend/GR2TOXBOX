// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// SDL's gamepad calls, for the Xbox UWP build.
//
// SDL's gamepad model has three parts: a list of pads, a handle per open pad, and
// events when a pad's state changes. All three are here; the pad itself is read by
// pad_state.h's platform side (Windows.Gaming.Input on the console, nothing
// elsewhere), and this file is the part that turns readings into what SDL's API
// promises.
//
// The Xbox has exactly one pad and it is always present, so the list is either
// empty or has one entry. Opening it does not do anything the reading does not
// already do: SDL's handle is the instance ID, because there is only ever one.

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_guid.h>
#include <SDL3/SDL_joystick.h>

#include <cstdlib>
#include <iterator>

#include "uwp/sdl3_shim/chords_glue.h"
#include "uwp/sdl3_shim/pad_state.h"

extern "C" void Gr2SdlSetError(const char* message);

namespace {

// The single pad's instance ID. SDL promises nothing about the value, only that
// an open handle and its ID agree, so any non-zero constant works. controller.cpp
// stores it and compares against SDL_GetGamepadID later.
constexpr SDL_JoystickID kPadId = 1;

// Whether the caller has opened the pad. SDL's Open returns a handle that must be
// closed, and a closed pad sees no events; controller.cpp opens on the first frame
// and closes on teardown.
bool g_open = false;

// The previous reading, kept so a button or axis change becomes exactly one event
// rather than a stream of them. SDL's contract: a button event on the edge, an
// axis event when the value changes.
Gr2Sdl::Pad::State g_previous{};

// The pad's name, as SDL_GetGamepadName returns it. The Xbox has one controller
// and it is the console's own, so the name is the console's.
constexpr const char* kPadName = "Xbox Wireless Controller";

// SDL's GUID for an Xbox pad. The bytes are SDL's own encoding: a bus, then the
// vendor and product, then a name hash. This is what SDL_GetGamepadGUIDForID
// returns on a real Xbox pad on a desktop, so a guest that prints the GUID -- or a
// remapping tool that keys off it -- sees the same value on the console.
constexpr SDL_GUID kXboxGuid{0x03, 0x00, 0x00, 0x00, 0x5E, 0x04, 0x00, 0x00,
                             0xCC, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// SDL's gamepad button order for the buttons this shim reports. pad_state.h's
// enum is a copy of this order, so the loop below is an identity -- but it is
// written out rather than assumed, because a wrong index would silently swap A
// and B, which is exactly the kind of bug that survives to a playtest.
constexpr SDL_GamepadButton kButtons[] = {
    SDL_GAMEPAD_BUTTON_SOUTH,   SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_WEST,    SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK,    SDL_GAMEPAD_BUTTON_GUIDE,
    SDL_GAMEPAD_BUTTON_START,   SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_MISC1,
};

constexpr SDL_GamepadAxis kAxes[] = {
    SDL_GAMEPAD_AXIS_LEFTX,        SDL_GAMEPAD_AXIS_LEFTY,
    SDL_GAMEPAD_AXIS_RIGHTX,       SDL_GAMEPAD_AXIS_RIGHTY,
    SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

static_assert(std::size(kButtons) == static_cast<size_t>(Gr2Sdl::Pad::Button::Count),
              "every pad button the shim reads needs an SDL button to report it as");
static_assert(std::size(kAxes) == static_cast<size_t>(Gr2Sdl::Pad::Axis::Count),
              "every pad axis the shim reads needs an SDL axis to report it as");

// Sticks are -1..1 floats and SDL reports Sint16, so the conversion has to be
// exact at the ends: -1 maps to -32768, not -32767, or a full left stick never
// reads as full and every analogue threshold in the guest is off by a step.
Sint16 ToSdlAxis(const Gr2Sdl::Pad::Axis axis, float value) {
    if (value > 1.0f) {
        value = 1.0f;
    } else if (value < -1.0f) {
        value = -1.0f;
    }
    if (axis == Gr2Sdl::Pad::Axis::LeftTrigger ||
        axis == Gr2Sdl::Pad::Axis::RightTrigger) {
        // Triggers are 0..1 and SDL reports them as a positive Sint16 from 0.
        return static_cast<Sint16>(value * 32767.0f);
    }
    // 32767 rather than 32768 at the top so the range stays symmetric and the
    // cast cannot overflow; the bottom uses all 15 bits plus the sign.
    return static_cast<Sint16>(value < 0.0f ? value * 32768.0f : value * 32767.0f);
}

// The current reading, refreshed by UpdateGamepads. Kept separately from
// g_previous so a caller that polls an accessor twice in a frame sees one state.
Gr2Sdl::Pad::State g_current{};

void PushButtonEvent(const Gr2Sdl::Pad::Button button, const bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.which = kPadId;
    event.gbutton.button = static_cast<Uint8>(kButtons[static_cast<int>(button)]);
    event.gbutton.down = down;
    SDL_PushEvent(&event);
}

void PushAxisEvent(const Gr2Sdl::Pad::Axis axis, const Sint16 value) {
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.which = kPadId;
    event.gaxis.axis = static_cast<Uint8>(kAxes[static_cast<int>(axis)]);
    event.gaxis.value = value;
    SDL_PushEvent(&event);
}

} // namespace

// --- The pad's lifecycle ---------------------------------------------------

extern "C" void SDLCALL SDL_UpdateGamepads(void) {
    const Gr2Sdl::Pad::State now = Gr2Sdl::Pad::Read();
    const bool was_connected = g_current.connected;
    g_current = now;

    if (!g_open) {
        // controller.cpp polls only after opening; a closed pad emits nothing, so
        // the caller's own reconnect logic is the one that decides when to look.
        g_previous = g_current;
        return;
    }

    if (now.connected != was_connected) {
        SDL_Event event{};
        event.type = now.connected ? SDL_EVENT_GAMEPAD_ADDED : SDL_EVENT_GAMEPAD_REMOVED;
        event.gdevice.which = kPadId;
        SDL_PushEvent(&event);
    }

    if (!now.connected) {
        // The pad is gone: take back the buttons and tilt the chord layer was
        // standing in for, or the guest holds a key nobody is pressing.
        Gr2Sdl::Chords::Detach();
        g_previous = g_current;
        return;
    }

    // One event per change. SDL's own backends do this from the OS's callbacks;
    // here it is a comparison against the last frame, which is why the state is
    // doubled up rather than read straight from the platform.
    for (int i = 0; i < static_cast<int>(Gr2Sdl::Pad::Button::Count); ++i) {
        if (now.buttons[i] != g_previous.buttons[i]) {
            PushButtonEvent(static_cast<Gr2Sdl::Pad::Button>(i), now.buttons[i]);
        }
    }
    for (int i = 0; i < static_cast<int>(Gr2Sdl::Pad::Axis::Count); ++i) {
        const Sint16 current = ToSdlAxis(static_cast<Gr2Sdl::Pad::Axis>(i), now.axes[i]);
        if (current != ToSdlAxis(static_cast<Gr2Sdl::Pad::Axis>(i), g_previous.axes[i])) {
            PushAxisEvent(static_cast<Gr2Sdl::Pad::Axis>(i), current);
        }
    }
    g_previous = now;

    // The chord layer runs after the pad's own events are queued, so the D-Pad
    // press it may want to swallow has already been sent and can be corrected.
    Gr2Sdl::Chords::Attach(now);
}

extern "C" void SDLCALL SDL_UpdateJoysticks(void) {
    // The shim has one device and it is a gamepad; SDL's joystick layer is the
    // lower half of the same device, so updating one updates both.
    SDL_UpdateGamepads();
}

extern "C" SDL_JoystickID* SDLCALL SDL_GetGamepads(int* count) {
    // SDL allocates this and the caller frees it with SDL_free, so it is a plain
    // malloc -- but SDL_free in this shim is libc's free, so that works.
    const bool connected = Gr2Sdl::Pad::Read().connected;
    SDL_JoystickID* const ids =
        static_cast<SDL_JoystickID*>(malloc(connected ? sizeof(SDL_JoystickID) : 1));
    if (ids == nullptr) {
        if (count != nullptr) {
            *count = 0;
        }
        return nullptr;
    }
    if (connected) {
        ids[0] = kPadId;
    }
    if (count != nullptr) {
        *count = connected ? 1 : 0;
    }
    return ids;
}

extern "C" SDL_JoystickID* SDLCALL SDL_GetJoysticks(int* count) {
    return SDL_GetGamepads(count);
}

extern "C" SDL_Gamepad* SDLCALL SDL_OpenGamepad(SDL_JoystickID instance_id) {
    if (instance_id != kPadId || !Gr2Sdl::Pad::Read().connected) {
        Gr2SdlSetError("no gamepad with that instance ID");
        return nullptr;
    }
    g_open = true;
    return reinterpret_cast<SDL_Gamepad*>(static_cast<intptr_t>(kPadId));
}

extern "C" SDL_Gamepad* SDLCALL SDL_GetGamepadFromID(SDL_JoystickID instance_id) {
    return g_open && instance_id == kPadId
               ? reinterpret_cast<SDL_Gamepad*>(static_cast<intptr_t>(kPadId))
               : nullptr;
}

extern "C" SDL_Joystick* SDLCALL SDL_GetJoystickFromID(SDL_JoystickID instance_id) {
    return reinterpret_cast<SDL_Joystick*>(SDL_GetGamepadFromID(instance_id));
}

extern "C" void SDLCALL SDL_CloseGamepad(SDL_Gamepad* gamepad) {
    if (gamepad != nullptr) {
        g_open = false;
    }
}

extern "C" void SDLCALL SDL_CloseJoystick(SDL_Joystick* joystick) {
    SDL_CloseGamepad(reinterpret_cast<SDL_Gamepad*>(joystick));
}

// --- Identity --------------------------------------------------------------

extern "C" SDL_JoystickID SDLCALL SDL_GetGamepadID(SDL_Gamepad* gamepad) {
    return gamepad != nullptr ? kPadId : 0;
}

extern "C" SDL_JoystickID SDLCALL SDL_GetJoystickID(SDL_Joystick* joystick) {
    return SDL_GetGamepadID(reinterpret_cast<SDL_Gamepad*>(joystick));
}

extern "C" SDL_GUID SDLCALL SDL_GetGamepadGUIDForID(SDL_JoystickID instance_id) {
    // selected_pad.cpp prints this to identify the pad in its settings; an ID the
    // shim does not know gets SDL's zero GUID, which its API allows.
    return instance_id == kPadId ? kXboxGuid : SDL_GUID{};
}

extern "C" SDL_GUID SDLCALL SDL_GetGamepadGUID(SDL_Gamepad* gamepad) {
    return gamepad != nullptr ? kXboxGuid : SDL_GUID{};
}

extern "C" void SDLCALL SDL_GUIDToString(SDL_GUID guid, char* pszGUID, int cbGUID) {
    if (pszGUID == nullptr || cbGUID < 33) {
        return;
    }
    // SDL's format: 32 hex characters, no separators, lower case.
    static constexpr char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        pszGUID[i * 2] = kHex[guid.data[i] >> 4];
        pszGUID[i * 2 + 1] = kHex[guid.data[i] & 0x0F];
    }
    pszGUID[32] = '\0';
}

extern "C" const char* SDLCALL SDL_GetGamepadName(SDL_Gamepad* gamepad) {
    return gamepad != nullptr ? kPadName : nullptr;
}

extern "C" const char* SDLCALL SDL_GetGamepadNameForID(SDL_JoystickID instance_id) {
    return instance_id == kPadId ? kPadName : nullptr;
}

extern "C" SDL_GamepadType SDLCALL SDL_GetGamepadType(SDL_Gamepad* gamepad) {
    return gamepad != nullptr ? SDL_GAMEPAD_TYPE_XBOXONE : SDL_GAMEPAD_TYPE_UNKNOWN;
}

extern "C" SDL_GamepadType SDLCALL SDL_GetGamepadTypeForID(SDL_JoystickID instance_id) {
    return instance_id == kPadId ? SDL_GAMEPAD_TYPE_XBOXONE : SDL_GAMEPAD_TYPE_UNKNOWN;
}

extern "C" Uint16 SDLCALL SDL_GetGamepadVendorForID(SDL_JoystickID instance_id) {
    // Microsoft. prompt_packs.cpp reads the vendor and product to decide which
    // glyphs a prompt pack should use, and the console's own pad has to identify
    // itself as the Xbox controller it is or the prompts would fall back to
    // generic ones.
    return instance_id == kPadId ? 0x045E : 0;
}

extern "C" Uint16 SDLCALL SDL_GetGamepadProductForID(SDL_JoystickID instance_id) {
    // The Xbox Series X|S controller's product ID. Anything else the shim does not
    // claim to be.
    return instance_id == kPadId ? 0x0B12 : 0;
}

extern "C" const char* SDLCALL SDL_GetGamepadStringForType(SDL_GamepadType type) {
    // SDL's own spelling, which prompt_packs.cpp uses as a map key.
    return type == SDL_GAMEPAD_TYPE_XBOXONE ? "xboxone" : "unknown";
}

extern "C" SDL_Gamepad* SDLCALL SDL_GetGamepadFromPlayerIndex(int player_index) {
    // The console has one pad and the guest's first player is player 0.
    return player_index == 0 ? SDL_GetGamepadFromID(kPadId) : nullptr;
}

extern "C" int SDLCALL SDL_GetGamepadPlayerIndex(SDL_Gamepad* gamepad) {
    return gamepad != nullptr ? 0 : -1;
}

// --- State -----------------------------------------------------------------

extern "C" bool SDLCALL SDL_GetGamepadButton(SDL_Gamepad* gamepad, SDL_GamepadButton button) {
    if (gamepad == nullptr) {
        return false;
    }
    for (int i = 0; i < static_cast<int>(Gr2Sdl::Pad::Button::Count); ++i) {
        if (kButtons[i] == button) {
            return g_current.buttons[i];
        }
    }
    // The pad has no axis click beyond L3/R3 and no touchpad, so any other button
    // is genuinely not present -- which is not the same as being up.
    return false;
}

extern "C" bool SDLCALL SDL_GamepadHasButton(SDL_Gamepad* gamepad, SDL_GamepadButton button) {
    if (gamepad == nullptr) {
        return false;
    }
    for (int i = 0; i < static_cast<int>(Gr2Sdl::Pad::Button::Count); ++i) {
        if (kButtons[i] == button) {
            return true;
        }
    }
    return false;
}

extern "C" Sint16 SDLCALL SDL_GetGamepadAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) {
    if (gamepad == nullptr) {
        return 0;
    }
    for (int i = 0; i < static_cast<int>(Gr2Sdl::Pad::Axis::Count); ++i) {
        if (kAxes[i] == axis) {
            return ToSdlAxis(static_cast<Gr2Sdl::Pad::Axis>(i), g_current.axes[i]);
        }
    }
    return 0;
}

extern "C" bool SDLCALL SDL_GamepadHasAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) {
    if (gamepad == nullptr) {
        return false;
    }
    for (int i = 0; i < static_cast<int>(Gr2Sdl::Pad::Axis::Count); ++i) {
        if (kAxes[i] == axis) {
            return true;
        }
    }
    return false;
}

// --- Rumble, light, sensors -------------------------------------------------
//
// The Xbox pad has no motion sensors and no player light the app can set, and
// SDL's API distinguishes "not supported" from "failed" by returning false from
// the setter and false from the query. controller.cpp already tolerates both: it
// logs once and stops asking.

extern "C" bool SDLCALL SDL_RumbleGamepad(SDL_Gamepad* gamepad, Uint16 low_frequency_rumble,
                                          Uint16 high_frequency_rumble, Uint32 /*duration_ms*/) {
    if (gamepad == nullptr) {
        return false;
    }
    // Windows.Gaming.Input's rumble takes 0..1 doubles. The duration is SDL's own
    // convention: the caller re-issues the call each frame it wants vibration, and
    // the platform keeps it until told otherwise.
    return Gr2Sdl::Pad::Rumble(static_cast<float>(low_frequency_rumble) / 65535.0f,
                               static_cast<float>(high_frequency_rumble) / 65535.0f);
}

extern "C" bool SDLCALL SDL_RumbleGamepadTriggers(SDL_Gamepad* gamepad, Uint16 /*left_rumble*/,
                                                  Uint16 /*right_rumble*/, Uint32 /*duration_ms*/) {
    // Impulse triggers are a Series controller feature Windows.Gaming.Input does
    // not expose separately.
    (void)gamepad;
    return false;
}

extern "C" bool SDLCALL SDL_SetGamepadLED(SDL_Gamepad* gamepad, Uint8 red, Uint8 green, Uint8 blue) {
    if (gamepad == nullptr) {
        return false;
    }
    return Gr2Sdl::Pad::SetLight(red, green, blue);
}

extern "C" bool SDLCALL SDL_SetGamepadSensorEnabled(SDL_Gamepad* gamepad, SDL_SensorType type,
                                                    bool /*enabled*/) {
    // No gyroscope or accelerometer on an Xbox pad, so there is nothing to enable
    // and the honest answer is no. controller.cpp's chord layer is what restores
    // motion input here, from the right stick.
    (void)gamepad;
    (void)type;
    return false;
}

extern "C" float SDLCALL SDL_GetGamepadSensorDataRate(SDL_Gamepad* gamepad, SDL_SensorType type) {
    (void)gamepad;
    (void)type;
    return 0.0f;
}

extern "C" bool SDLCALL SDL_GamepadHasSensor(SDL_Gamepad* gamepad, SDL_SensorType /*type*/) {
    (void)gamepad;
    return false;
}

extern "C" bool SDLCALL SDL_GetGamepadSensorData(SDL_Gamepad* gamepad, SDL_SensorType type,
                                                 float* data, int /*num_values*/) {
    (void)gamepad;
    (void)type;
    (void)data;
    return false;
}
