// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// SDL's window, display, properties and surface calls, for the Xbox UWP build.
//
// The window is a CoreWindow. There is exactly one, the shell creates it, and it
// covers the screen: so this shim does not create anything, it *adopts* the
// window the UWP host was handed, and the calls that would create, move, resize
// or full-screen a desktop window are answered as the console answers them --
// "it already is what you asked for".
//
// That is not laziness, it is the shape of the platform. A packaged Xbox app
// gets one CoreWindow, in full screen, at the panel's resolution, and cannot
// change any of those things. Saying so in one place beats scattering
// `#if GR2_XBOX_UWP` through window.cpp.

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include <windows.h>

#include "uwp/sdl3_shim/gr2_sdl_internal.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

// The shim's window. SDL_Window is opaque to callers, so this is the whole of it.
struct ShimWindow {
    SDL_WindowID id = 1;
    int width = 0;
    int height = 0;
};

ShimWindow g_window;
bool g_open = false;
std::mutex g_window_mutex;

// SDL's property bags: an ID, and string/number/boolean values in it. The only
// consumer is SDL_CreateWindowWithProperties, which reads the size and the title.
struct PropertyBag {
    std::map<std::string, std::string> strings;
    std::map<std::string, Sint64> numbers;
    std::map<std::string, bool> booleans;
};

std::mutex g_properties_mutex;
std::map<SDL_PropertiesID, PropertyBag> g_properties;
SDL_PropertiesID g_next_properties = 1;

} // namespace

extern "C" void Gr2SdlSetError(const char* message);

// --- The window the UWP host hands over ------------------------------------
//
// Called from the D3D12 device's CreateSwapChainForCoreWindow path, so the shim
// knows the surface size without asking WinRT for it. Kept out of the SDL headers
// on purpose: it is not SDL's business, and a caller that needs it includes no
// SDL at all.
extern "C" void Gr2SdlAdoptWindow(int width, int height) {
    const std::scoped_lock lock{g_window_mutex};
    g_window.width = width;
    g_window.height = height;
    g_open = true;
}

extern "C" void Gr2SdlResizeWindow(int width, int height) {
    const std::scoped_lock lock{g_window_mutex};
    if (g_window.width == width && g_window.height == height) {
        return;
    }
    g_window.width = width;
    g_window.height = height;
    // The emulator watches for this to rebuild its swapchain; on desktop SDL
    // pushes it from its window procedure, and there is no window procedure here.
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    event.window.windowID = g_window.id;
    event.window.data1 = width;
    event.window.data2 = height;
    SDL_PushEvent(&event);
}

// --- Properties -----------------------------------------------------------

extern "C" SDL_PropertiesID SDLCALL SDL_CreateProperties(void) {
    const std::scoped_lock lock{g_properties_mutex};
    const SDL_PropertiesID id = g_next_properties++;
    g_properties[id] = PropertyBag{};
    return id;
}

extern "C" void SDLCALL SDL_DestroyProperties(SDL_PropertiesID props) {
    const std::scoped_lock lock{g_properties_mutex};
    g_properties.erase(props);
}

extern "C" bool SDLCALL SDL_SetStringProperty(SDL_PropertiesID props, const char* name,
                                              const char* value) {
    if (name == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_properties_mutex};
    const auto it = g_properties.find(props);
    if (it == g_properties.end()) {
        return false;
    }
    if (value == nullptr) {
        it->second.strings.erase(name);
    } else {
        it->second.strings[name] = value;
    }
    return true;
}

extern "C" const char* SDLCALL SDL_GetStringProperty(SDL_PropertiesID props, const char* name,
                                                     const char* default_value) {
    const std::scoped_lock lock{g_properties_mutex};
    const auto it = g_properties.find(props);
    if (it == g_properties.end() || name == nullptr) {
        return default_value;
    }
    const auto found = it->second.strings.find(name);
    return found != it->second.strings.end() ? found->second.c_str() : default_value;
}

extern "C" bool SDLCALL SDL_SetNumberProperty(SDL_PropertiesID props, const char* name,
                                              Sint64 value) {
    if (name == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_properties_mutex};
    const auto it = g_properties.find(props);
    if (it == g_properties.end()) {
        return false;
    }
    it->second.numbers[name] = value;
    return true;
}

extern "C" Sint64 SDLCALL SDL_GetNumberProperty(SDL_PropertiesID props, const char* name,
                                                Sint64 default_value) {
    const std::scoped_lock lock{g_properties_mutex};
    const auto it = g_properties.find(props);
    if (it == g_properties.end() || name == nullptr) {
        return default_value;
    }
    const auto found = it->second.numbers.find(name);
    return found != it->second.numbers.end() ? found->second : default_value;
}

extern "C" bool SDLCALL SDL_SetBooleanProperty(SDL_PropertiesID props, const char* name,
                                               bool value) {
    if (name == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_properties_mutex};
    const auto it = g_properties.find(props);
    if (it == g_properties.end()) {
        return false;
    }
    it->second.booleans[name] = value;
    return true;
}

extern "C" bool SDLCALL SDL_GetBooleanProperty(SDL_PropertiesID props, const char* name,
                                               bool default_value) {
    const std::scoped_lock lock{g_properties_mutex};
    const auto it = g_properties.find(props);
    if (it == g_properties.end() || name == nullptr) {
        return default_value;
    }
    const auto found = it->second.booleans.find(name);
    return found != it->second.booleans.end() ? found->second : default_value;
}

// --- The window -----------------------------------------------------------

extern "C" SDL_Window* SDLCALL SDL_CreateWindowWithProperties(SDL_PropertiesID props) {
    // The size comes from the CoreWindow, which the host already created: the
    // width and height properties are a desktop window's wishes, and on the
    // console the answer is the panel's resolution. Reading them anyway keeps the
    // request visible for a diagnostic, and a non-zero value that disagrees is
    // worth logging -- but it is not honoured, because it cannot be.
    const Sint64 asked_width = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 0);
    const Sint64 asked_height = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 0);
    {
        const std::scoped_lock lock{g_window_mutex};
        if (!g_open) {
            // Nothing has adopted a CoreWindow yet. The D3D12 backend does that
            // before the swapchain, which is before any of this on a console, so
            // reaching here means the platform layer is out of order.
            Gr2SdlSetError("the UWP window was not adopted before SDL_CreateWindowWithProperties");
            return nullptr;
        }
        if (asked_width > 0 && asked_height > 0 &&
            (asked_width != g_window.width || asked_height != g_window.height)) {
            // Not an error: window.cpp asks for the configured size and the
            // console gives the panel's.
        }
        return reinterpret_cast<SDL_Window*>(&g_window);
    }
}

extern "C" SDL_WindowID SDLCALL SDL_GetWindowID(SDL_Window* window) {
    return window != nullptr ? reinterpret_cast<ShimWindow*>(window)->id : 0;
}

extern "C" SDL_Window* SDLCALL SDL_GetWindowFromID(SDL_WindowID id) {
    const std::scoped_lock lock{g_window_mutex};
    return g_open && id == g_window.id ? reinterpret_cast<SDL_Window*>(&g_window) : nullptr;
}

extern "C" bool SDLCALL SDL_GetWindowSizeInPixels(SDL_Window* /*window*/, int* w, int* h) {
    const std::scoped_lock lock{g_window_mutex};
    if (w != nullptr) {
        *w = g_window.width;
    }
    if (h != nullptr) {
        *h = g_window.height;
    }
    return g_open;
}

extern "C" float SDLCALL SDL_GetWindowDisplayScale(SDL_Window* /*window*/) {
    // A console renders at the panel's resolution and the UI is laid out for it;
    // there is no DPI scaling to follow.
    return 1.0f;
}

extern "C" float SDLCALL SDL_GetWindowPixelDensity(SDL_Window* /*window*/) {
    return 1.0f;
}

extern "C" SDL_WindowFlags SDLCALL SDL_GetWindowFlags(SDL_Window* /*window*/) {
    // Full screen, focused, and never hidden: a CoreWindow is shown by the shell
    // and owns the console's single input focus. window.cpp tests only
    // SDL_WINDOW_FULLSCREEN, but the rest are true and cost nothing.
    return SDL_WINDOW_FULLSCREEN | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_KEYBOARD_GRABBED;
}

extern "C" bool SDLCALL SDL_SetWindowMinimumSize(SDL_Window* /*window*/, int /*w*/, int /*h*/) {
    // The window cannot be resized by the app, so a minimum is satisfied by the
    // only size there is.
    return true;
}

extern "C" bool SDLCALL SDL_SetWindowIcon(SDL_Window* /*window*/, SDL_Surface* /*icon*/) {
    return true;
}

extern "C" bool SDLCALL SDL_ShowWindow(SDL_Window* /*window*/) {
    return true;
}

extern "C" bool SDLCALL SDL_SyncWindow(SDL_Window* /*window*/) {
    return true;
}

extern "C" bool SDLCALL SDL_SetWindowFullscreen(SDL_Window* /*window*/, bool /*fullscreen*/) {
    // Always full screen. Accepting the call and doing nothing is correct here:
    // the alternative is failing, which window.cpp treats as fatal.
    return true;
}

extern "C" bool SDLCALL SDL_SetWindowFullscreenMode(SDL_Window* /*window*/,
                                                    const SDL_DisplayMode* /*mode*/) {
    return true;
}

extern "C" bool SDLCALL SDL_SetWindowRelativeMouseMode(SDL_Window* /*window*/, bool /*enabled*/) {
    // No pointer to capture. Mouse input on the console arrives from the pad,
    // through the emulator's own layer, so the flag has nothing to control.
    return true;
}

// --- Text input -----------------------------------------------------------
// Software keyboards are a shell concern and this package does not raise one: the
// console's text comes from the pad-driven prompt layer. The calls are accepted so
// the paths that bracket text entry stay linear.

extern "C" bool SDLCALL SDL_StartTextInput(SDL_Window* /*window*/) {
    return true;
}

extern "C" bool SDLCALL SDL_StopTextInput(SDL_Window* /*window*/) {
    return true;
}

extern "C" bool SDLCALL SDL_SetTextInputArea(SDL_Window* /*window*/, const SDL_Rect* /*rect*/,
                                             int /*cursor*/) {
    return true;
}

// --- Displays -------------------------------------------------------------
// One display, the panel. The ID is 1 and every query about it is answered from
// the CoreWindow's size, which the host reported.

extern "C" SDL_DisplayID* SDLCALL SDL_GetDisplays(int* count) {
    static SDL_DisplayID ids[1] = {1};
    if (count != nullptr) {
        *count = 1;
    }
    return ids;
}

extern "C" SDL_DisplayID SDLCALL SDL_GetPrimaryDisplay(void) {
    return 1;
}

extern "C" SDL_DisplayID SDLCALL SDL_GetDisplayForWindow(SDL_Window* /*window*/) {
    return 1;
}

extern "C" SDL_DisplayID SDLCALL SDL_GetDisplayForPoint(const SDL_Point* /*point*/) {
    return 1;
}

extern "C" const char* SDLCALL SDL_GetDisplayName(SDL_DisplayID displayID) {
    return displayID == 1 ? "Xbox" : nullptr;
}

extern "C" bool SDLCALL SDL_GetDisplayBounds(SDL_DisplayID displayID, SDL_Rect* rect) {
    if (displayID != 1 || rect == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_window_mutex};
    *rect = SDL_Rect{0, 0, g_window.width, g_window.height};
    return true;
}

namespace {

// The panel's one mode. Filled from the adopted CoreWindow, and rewritten every
// time it is asked for so a resize reaches the caller.
SDL_DisplayMode g_display_mode{};

} // namespace

extern "C" const SDL_DisplayMode* SDLCALL SDL_GetCurrentDisplayMode(SDL_DisplayID displayID) {
    if (displayID != 1) {
        return nullptr;
    }
    const std::scoped_lock lock{g_window_mutex};
    g_display_mode = SDL_DisplayMode{};
    g_display_mode.displayID = 1;
    g_display_mode.w = g_window.width;
    g_display_mode.h = g_window.height;
    // The panel presents at 60 Hz, and the swapchain is what actually sets the
    // rate; nothing in the port branches on this beyond a settings comparison.
    g_display_mode.refresh_rate = 60.0f;
    g_display_mode.format = SDL_PIXELFORMAT_RGBA32;
    return &g_display_mode;
}

extern "C" bool SDLCALL SDL_GetClosestFullscreenDisplayMode(
    SDL_DisplayID displayID, int /*w*/, int /*h*/, float /*refresh_rate*/,
    bool /*include_high_density_modes*/, SDL_DisplayMode* closest) {
    // There is one mode, the panel's, so it is the closest to anything asked for.
    const SDL_DisplayMode* const current = SDL_GetCurrentDisplayMode(displayID);
    if (current == nullptr || closest == nullptr) {
        return false;
    }
    *closest = *current;
    return true;
}

// --- Surfaces -------------------------------------------------------------
// SDL_CreateSurfaceFrom wraps pixels the caller owns; nothing here owns memory, so
// both calls only have to hand back a describable surface. window.cpp uses it for
// the window icon, which a CoreWindow does not show.

namespace {

struct ShimSurface {
    int width;
    int height;
    SDL_PixelFormat format;
    void* pixels;
    int pitch;
};

} // namespace

extern "C" SDL_Surface* SDLCALL SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format,
                                                     void* pixels, int pitch) {
    if (width <= 0 || height <= 0) {
        Gr2SdlSetError("SDL_CreateSurfaceFrom needs a positive size");
        return nullptr;
    }
    auto* surface = new ShimSurface{width, height, format, pixels, pitch};
    return reinterpret_cast<SDL_Surface*>(surface);
}

extern "C" void SDLCALL SDL_DestroySurface(SDL_Surface* surface) {
    delete reinterpret_cast<ShimSurface*>(surface);
}

// SDL_PointInRect needs no definition: SDL_rect.h defines it inline, so the header
// already carries the implementation and writing one here would be a redefinition.

// --- Event watches ---------------------------------------------------------
//
// SDL lets a caller see every event before the queue does, without consuming it.
// gr2port's window.cpp uses it to notice a Print Screen press, and the desktop's
// SDL provides it; here the shim has to, because SDL_PushEvent is the shim's own
// function and a watch that is never called is a feature that silently stops
// working.

namespace {

struct EventWatch {
    SDL_EventFilter filter;
    void* userdata;
};

std::mutex g_watch_mutex;
std::vector<EventWatch> g_watches;

} // namespace

extern "C" bool SDLCALL SDL_AddEventWatch(SDL_EventFilter filter, void* userdata) {
    if (filter == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_watch_mutex};
    g_watches.push_back(EventWatch{filter, userdata});
    return true;
}

extern "C" void SDLCALL SDL_RemoveEventWatch(SDL_EventFilter filter, void* userdata) {
    const std::scoped_lock lock{g_watch_mutex};
    const auto it = std::find_if(g_watches.begin(), g_watches.end(),
                                 [filter, userdata](const EventWatch& watch) {
                                     return watch.filter == filter && watch.userdata == userdata;
                                 });
    if (it != g_watches.end()) {
        g_watches.erase(it);
    }
}

void Gr2SdlRunEventWatches(SDL_Event* event) {
    // A copy of the list, because a filter is allowed to remove itself and the
    // vector cannot be mutated while it is being walked.
    std::vector<EventWatch> watches;
    {
        const std::scoped_lock lock{g_watch_mutex};
        watches = g_watches;
    }
    for (const EventWatch& watch : watches) {
        // In SDL 3 a filter's return value says whether the event should reach the
        // queue's *users*, not whether the remaining filters run; SDL itself keeps
        // calling them, and a watch that dropped the rest would make the order of
        // registration matter. So the result is deliberately ignored.
        (void)watch.filter(watch.userdata, event);
    }
}

// --- Window properties and title -------------------------------------------

extern "C" SDL_PropertiesID SDLCALL SDL_GetWindowProperties(SDL_Window* window) {
    // The shim's one window owns a property bag of its own, created on demand.
    static SDL_PropertiesID window_properties = 0;
    if (window == nullptr) {
        return 0;
    }
    if (window_properties == 0) {
        // Deliberately empty except for the handle: the property callers read off
        // a window is the one naming the platform window it wraps, and on the
        // console there is no such handle to hand out. An absent property is the
        // honest answer, and SDL's API is built for it.
        window_properties = SDL_CreateProperties();
    }
    return window_properties;
}

extern "C" const char* SDLCALL SDL_GetWindowTitle(SDL_Window* window) {
    (void)window;
    // The title the caller asked for, as a CoreWindow has no title bar to show it
    // in -- but a caller that reads it back expects what it set.
    return "Gravity Rush 2";
}

extern "C" const char* SDLCALL SDL_GetPixelFormatName(SDL_PixelFormat format) {
    // The only caller is a diagnostic, and the console's surface format is the
    // swapchain's. Naming the one format this shim reports is enough to know what
    // a log line meant.
    return format == SDL_PIXELFORMAT_RGBA32 ? "SDL_PIXELFORMAT_RGBA32" : "SDL_PIXELFORMAT_UNKNOWN";
}
