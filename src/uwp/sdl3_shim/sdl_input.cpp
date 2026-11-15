// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// SDL's keyboard, mouse, hint and camera calls, for the Xbox UWP build.
//
// Four small groups in one file because they have the same shape: an Xbox has no
// keyboard, no mouse and no camera the app may open, so each of these is a
// well-defined *empty* platform rather than an unimplemented one. The distinction
// matters, and it is the reason this file is written out instead of stubbed at the
// last moment: an emulator that reports "no mouse" correctly lets the guest's
// code take its keyboard-and-mouse-free path, while one that crashes or lies does
// not.
//
// The console's input is the pad, and src/uwp/sdl3_shim/sdl_gamepad.cpp owns it.
// What is here exists so that the *other* inputs report their absence honestly.
//
// Hints are the exception: they are not a device, they are configuration, and SDL
// stores them itself. They are implemented for real, because GR2port sets hints
// that change its own behaviour and silently ignoring them would be a bug.

#include <SDL3/SDL_camera.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <cstdlib>
#include <map>
#include <mutex>
#include <string>

extern "C" void Gr2SdlSetError(const char* message);

namespace {

// --- Keyboard --------------------------------------------------------------
//
// SDL's keyboard state is an array indexed by scancode, owned by SDL and updated
// from the platform's key events. There are no keys, so the array is all zero and
// stays that way. It is still a real array of SDL_SCANCODE_COUNT entries because
// callers index it directly -- src/graphics/gr2port/window.cpp reads
// SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_PRINTSCREEN] without a length check.

bool g_keyboard_state[SDL_SCANCODE_COUNT]{};

} // namespace

extern "C" const bool* SDLCALL SDL_GetKeyboardState(int* numkeys) {
    if (numkeys != nullptr) {
        *numkeys = SDL_SCANCODE_COUNT;
    }
    return g_keyboard_state;
}

extern "C" SDL_Window* SDLCALL SDL_GetKeyboardFocus(void) {
    // The window has focus: a console app's CoreWindow is the only one there is,
    // and it is active whenever the app is running. Returning it keeps a caller
    // that gates input on "is my window focused" on its normal path instead of its
    // paused one.
    return SDL_GetWindowFromID(1);
}

extern "C" const char* SDLCALL SDL_getenv(const char* name) {
    // The shim's stand-in for SDL's environment lookup. A packaged app has no
    // environment to read, so every query answers "unset" rather than calling the
    // CRT's getenv and reading whatever the shell happened to pass in.
    (void)name;
    return nullptr;
}

extern "C" SDL_Keymod SDLCALL SDL_GetModState(void) {
    // No keyboard, so no modifiers. SDL_KMOD_NONE is zero.
    return SDL_KMOD_NONE;
}

// --- Mouse -----------------------------------------------------------------
//
// Same reasoning as the keyboard: the console has no pointer. The calls still have
// to behave, because the guest's mouse library opens the device, enumerates it and
// then polls it, and each of those steps has a defined answer for "no device".

extern "C" SDL_MouseID* SDLCALL SDL_GetMice(int* count) {
    // An empty list rather than null: SDL distinguishes "no mice" from "the call
    // failed", and mouse.cpp logs the difference.
    if (count != nullptr) {
        *count = 0;
    }
    return static_cast<SDL_MouseID*>(malloc(1));
}

extern "C" const char* SDLCALL SDL_GetMouseNameForID(SDL_MouseID instance_id) {
    (void)instance_id;
    return nullptr;
}

extern "C" SDL_MouseButtonFlags SDLCALL SDL_GetMouseState(float* x, float* y) {
    if (x != nullptr) {
        *x = 0.0f;
    }
    if (y != nullptr) {
        *y = 0.0f;
    }
    return 0;
}

extern "C" SDL_MouseButtonFlags SDLCALL SDL_GetGlobalMouseState(float* x, float* y) {
    return SDL_GetMouseState(x, y);
}

extern "C" SDL_MouseButtonFlags SDLCALL SDL_GetRelativeMouseState(float* x, float* y) {
    // The relative position is a delta since the last call, and a console has no
    // pointer to move it. input_mouse.cpp integrates this into its virtual cursor,
    // so zero is the correct answer and means "the mouse did not move".
    if (x != nullptr) {
        *x = 0.0f;
    }
    if (y != nullptr) {
        *y = 0.0f;
    }
    return 0;
}

extern "C" void SDLCALL SDL_WarpMouseInWindow(SDL_Window* window, float x, float y) {
    // Nothing to warp: the cursor does not exist and no guest can see it. The call
    // is accepted so a caller that hides a virtual cursor by recentring it does
    // not take an error path.
    (void)window;
    (void)x;
    (void)y;
}

extern "C" bool SDLCALL SDL_CaptureMouse(bool enabled) {
    // Capture is about receiving events outside the window. A console app already
    // owns every event there is.
    (void)enabled;
    return true;
}

// Cursor calls. The console has no pointer, so every cursor is the same: a handle
// that is never drawn. SDL_CreateSystemCursor still returns non-null, because
// callers treat null as a failure and log it.

extern "C" SDL_Cursor* SDLCALL SDL_CreateSystemCursor(SDL_SystemCursor id) {
    return reinterpret_cast<SDL_Cursor*>(static_cast<intptr_t>(id) + 1);
}

extern "C" bool SDLCALL SDL_SetCursor(SDL_Cursor* cursor) {
    (void)cursor;
    return true;
}

extern "C" void SDLCALL SDL_DestroyCursor(SDL_Cursor* cursor) {
    (void)cursor;
}

extern "C" bool SDLCALL SDL_ShowCursor(void) {
    return true;
}

extern "C" bool SDLCALL SDL_HideCursor(void) {
    return true;
}

extern "C" bool SDLCALL SDL_CursorVisible(void) {
    // The cursor is hidden, which is what window.cpp wants of a window that holds
    // the mouse.
    return false;
}

// --- Hints -----------------------------------------------------------------
//
// SDL keeps hints in a global table with priorities, where a higher priority beats
// a lower one and SDL_HINT_OVERRIDE beats everything. The emulator sets a handful
// (the app name, the gamecontroller ignore list, background joystick events) and
// reads them back; ignoring them would change its behaviour silently, so this is a
// real implementation.

namespace {

std::mutex g_hints_mutex;
std::map<std::string, std::pair<std::string, SDL_HintPriority>> g_hints;

} // namespace

extern "C" bool SDLCALL SDL_SetHintWithPriority(const char* name, const char* value,
                                                SDL_HintPriority priority) {
    if (name == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_hints_mutex};
    if (value == nullptr) {
        g_hints.erase(name);
        return true;
    }
    const auto it = g_hints.find(name);
    // A lower priority cannot overwrite a higher one; SDL_HINT_OVERRIDE always
    // wins, which is what a caller uses when it means it.
    if (it != g_hints.end() && priority != SDL_HINT_OVERRIDE && it->second.second > priority) {
        return false;
    }
    g_hints[name] = {value, priority};
    return true;
}

extern "C" bool SDLCALL SDL_SetHint(const char* name, const char* value) {
    return SDL_SetHintWithPriority(name, value, SDL_HINT_NORMAL);
}

extern "C" bool SDLCALL SDL_ResetHint(const char* name) {
    if (name == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_hints_mutex};
    g_hints.erase(name);
    return true;
}

extern "C" const char* SDLCALL SDL_GetHint(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    const std::scoped_lock lock{g_hints_mutex};
    const auto it = g_hints.find(name);
    return it != g_hints.end() ? it->second.first.c_str() : nullptr;
}

extern "C" bool SDLCALL SDL_GetHintBoolean(const char* name, bool default_value) {
    const char* const value = SDL_GetHint(name);
    if (value == nullptr) {
        return default_value;
    }
    // SDL's own spelling of a true hint. Anything else is false, which is what its
    // documentation says and what a caller that writes "0" expects.
    return value[0] == '1' || value[0] == 't' || value[0] == 'T';
}

// --- Camera ----------------------------------------------------------------
//
// The guest's camera library enumerates devices and opens one. The console has no
// camera the app may open, so the list is empty and opening fails with a message
// rather than a null pointer from nowhere. camera.cpp already handles an empty
// list: it logs "no camera" and the photo modes that need one report themselves.

extern "C" SDL_CameraID* SDLCALL SDL_GetCameras(int* count) {
    if (count != nullptr) {
        *count = 0;
    }
    return static_cast<SDL_CameraID*>(malloc(1));
}

extern "C" const char* SDLCALL SDL_GetCurrentCameraDriver(void) {
    return "none";
}

extern "C" const char* SDLCALL SDL_GetCameraName(SDL_CameraID instance_id) {
    (void)instance_id;
    return nullptr;
}

extern "C" SDL_Camera* SDLCALL SDL_OpenCamera(SDL_CameraID instance_id,
                                              const SDL_CameraSpec* spec) {
    (void)instance_id;
    (void)spec;
    Gr2SdlSetError("the Xbox build has no camera device");
    return nullptr;
}

extern "C" void SDLCALL SDL_CloseCamera(SDL_Camera* camera) {
    (void)camera;
}

extern "C" bool SDLCALL SDL_GetCameraFormat(SDL_Camera* camera, SDL_CameraSpec* spec) {
    (void)camera;
    (void)spec;
    return false;
}

extern "C" SDL_Surface* SDLCALL SDL_AcquireCameraFrame(SDL_Camera* camera, Uint64* timestampNS) {
    (void)camera;
    if (timestampNS != nullptr) {
        *timestampNS = 0;
    }
    return nullptr;
}

extern "C" void SDLCALL SDL_ReleaseCameraFrame(SDL_Camera* camera, SDL_Surface* frame) {
    (void)camera;
    (void)frame;
}
