// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The SDL entry points the emulator calls, for the Xbox UWP build only.
//
// What this is: SDL 3 has no WinRT/UWP support and never will -- its own header
// carries an #error saying so. The emulator's frontend is SDL throughout, so a
// UWP package has to come from somewhere. The shim supplies the library: this
// file is init, the event queue, the clock and SDL's error string, and its
// siblings are the window (which becomes CoreWindow), gamepad (which becomes
// Windows.Gaming.Input) and audio (which becomes XAudio2).
//
// What this is not: a reimplementation of SDL's headers. Those are the real ones
// from externals/sdl3/include, so every type and every signature here is SDL's,
// and the compiler checks these definitions against them. Only
// SDL_platform_defines.h is replaced, because that is the file holding the
// #error.
//
// Threading. SDL's event queue is thread-safe by contract and this one is too:
// input arrives on whichever thread the WinRT callbacks land on, and the
// emulator's main loop drains it.

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_timer.h>

#include <windows.h>

#include "uwp/sdl3_shim/gr2_sdl_internal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex g_event_mutex;
std::condition_variable g_event_signal;
std::vector<SDL_Event> g_events;
// SDL keeps a small queue and drops rather than grows without bound; the same
// limit is used so a stuck consumer fails the same way it would on desktop
// instead of eating the console's memory.
constexpr size_t MaxQueuedEvents = 65535;

SDL_InitFlags g_initialized = 0;

std::mutex g_error_mutex;
std::string g_error;

struct TimerEntry {
    SDL_TimerID id;
    // Steady-clock milliseconds at which the callback is next due.
    Uint64 due;
    Uint32 interval;
    SDL_TimerCallback callback;
    void* userdata;
};

std::mutex g_timer_mutex;
std::vector<TimerEntry> g_timers;
SDL_TimerID g_next_timer = 1;

Uint64 NowMs() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return static_cast<Uint64>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

} // namespace

// --- SDL's error string. Every shim file sets it through this, so a failed call
// --- reports something specific rather than a stale message.

extern "C" void Gr2SdlSetError(const char* message) {
    const std::scoped_lock lock{g_error_mutex};
    g_error = message != nullptr ? message : "";
}

extern "C" bool SDLCALL SDL_SetError(const char* fmt, ...) {
    // SDL's own signature is variadic with a format string; the shim's callers
    // pass a finished message, so this is a copy. Formatted text is not needed by
    // any call site, and a printf implementation here would be one more thing to
    // get wrong.
    Gr2SdlSetError(fmt);
    return false;
}

extern "C" const char* SDLCALL SDL_GetError(void) {
    // SDL returns a pointer valid until the next call on the same thread. A
    // mutex-guarded string in a function-local keeps that true here without
    // returning a pointer into a container that may reallocate.
    static thread_local std::string message;
    {
        const std::scoped_lock lock{g_error_mutex};
        message = g_error;
    }
    return message.c_str();
}

extern "C" bool SDLCALL SDL_ClearError(void) {
    const std::scoped_lock lock{g_error_mutex};
    g_error.clear();
    return true;
}

// --- Init. The subsystems are recorded so SDL_WasInit answers honestly, and
// --- audio has its own initialisation in the audio shim.

extern "C" bool SDLCALL SDL_Init(SDL_InitFlags flags) {
    g_initialized |= flags;
    return true;
}

extern "C" bool SDLCALL SDL_InitSubSystem(SDL_InitFlags flags) {
    g_initialized |= flags;
    return true;
}

extern "C" void SDLCALL SDL_QuitSubSystem(SDL_InitFlags flags) {
    g_initialized &= ~flags;
}

extern "C" SDL_InitFlags SDLCALL SDL_WasInit(SDL_InitFlags flags) {
    return g_initialized & flags;
}

extern "C" void SDLCALL SDL_Quit(void) {
    g_initialized = 0;
    const std::scoped_lock lock{g_event_mutex};
    g_events.clear();
}

// --- The clock. SDL promises a monotonic millisecond counter from an arbitrary
// --- origin, and a high-resolution counter whose ratio with the frequency is
// --- seconds.

extern "C" Uint64 SDLCALL SDL_GetTicks(void) {
    return NowMs();
}

extern "C" Uint64 SDLCALL SDL_GetPerformanceCounter(void) {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<Uint64>(counter.QuadPart);
}

extern "C" Uint64 SDLCALL SDL_GetPerformanceFrequency(void) {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    // A zero here would divide by zero in every caller; the API's contract
    // guarantees a non-zero answer, and 10 MHz is what Windows returns.
    return frequency.QuadPart != 0 ? static_cast<Uint64>(frequency.QuadPart) : 10000000u;
}

extern "C" void SDLCALL SDL_Delay(Uint32 ms) {
    ::Sleep(ms);
}

// --- The event queue. The emulator reads what it needs and ignores the rest,
// --- exactly as on desktop.

extern "C" bool SDLCALL SDL_PushEvent(SDL_Event* event) {
    if (event == nullptr) {
        return false;
    }
    // The event watches see it first, as they do on desktop: SDL's platform layer
    // calls them from the OS's pump before the queue, and here this function is
    // that pump.
    Gr2SdlRunEventWatches(event);
    {
        const std::scoped_lock lock{g_event_mutex};
        if (g_events.size() >= MaxQueuedEvents) {
            // SDL refuses and says why. Returning true here would let a caller
            // believe an input reached the queue when it did not.
            Gr2SdlSetError("SDL event queue is full");
            return false;
        }
        g_events.push_back(*event);
    }
    g_event_signal.notify_one();
    return true;
}

extern "C" bool SDLCALL SDL_WaitEvent(SDL_Event* event) {
    if (event == nullptr) {
        return false;
    }
    std::unique_lock lock{g_event_mutex};
    // A timeout rather than an unbounded wait: the app has to be able to leave
    // its loop when the shell suspends it, and UWP's suspension arrives as a
    // WinRT callback on another thread rather than as an event.
    g_event_signal.wait_for(lock, std::chrono::milliseconds{100},
                            [] { return !g_events.empty(); });
    if (g_events.empty()) {
        return false;
    }
    *event = g_events.front();
    g_events.erase(g_events.begin());
    return true;
}

extern "C" bool SDLCALL SDL_RunOnMainThread(SDL_MainThreadCallback callback, void* userdata,
                                            bool /*wait_complete*/) {
    // The emulator's main loop *is* the main thread, and every caller of this uses
    // it to get back onto that thread. On UWP the shim's window and input
    // callbacks already run there (CoreWindow's events are raised on the UI
    // thread), so the work is done immediately rather than queued.
    if (callback == nullptr) {
        return false;
    }
    callback(userdata);
    return true;
}

// --- Timers. The emulator uses one to poll the gamepad; the shim runs them from
// --- a thread of its own rather than from the UI thread, so a slow callback
// --- cannot stall input.

namespace {

std::thread g_timer_thread;
std::atomic<bool> g_timers_running{false};

void TimerThread() {
    while (g_timers_running.load(std::memory_order_relaxed)) {
        std::vector<TimerEntry> due;
        {
            const std::scoped_lock lock{g_timer_mutex};
            const Uint64 now = NowMs();
            for (auto it = g_timers.begin(); it != g_timers.end();) {
                if (it->due <= now) {
                    due.push_back(*it);
                    it->due = now + it->interval;
                }
                ++it;
            }
        }
        for (const TimerEntry& timer : due) {
            // SDL's contract: a callback returning 0 cancels the timer.
            if (timer.callback(timer.userdata, timer.id, timer.interval) == 0) {
                const std::scoped_lock lock{g_timer_mutex};
                std::erase_if(g_timers, [&](const TimerEntry& entry) { return entry.id == timer.id; });
            }
        }
        ::Sleep(1);
    }
}

} // namespace

extern "C" SDL_TimerID SDLCALL SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback,
                                            void* userdata) {
    if (callback == nullptr || interval == 0) {
        Gr2SdlSetError("SDL_AddTimer needs a callback and a non-zero interval");
        return 0;
    }
    SDL_TimerID id;
    {
        const std::scoped_lock lock{g_timer_mutex};
        id = g_next_timer++;
        g_timers.push_back({id, NowMs() + interval, interval, callback, userdata});
    }
    if (!g_timers_running.exchange(true)) {
        g_timer_thread = std::thread{TimerThread};
    }
    return id;
}

extern "C" bool SDLCALL SDL_RemoveTimer(SDL_TimerID id) {
    const std::scoped_lock lock{g_timer_mutex};
    const auto before = g_timers.size();
    std::erase_if(g_timers, [id](const TimerEntry& entry) { return entry.id == id; });
    return g_timers.size() != before;
}

// --- A message box. UWP has no modal Win32 box; a dialog is a XAML content
// --- dialog, which the shim does not have. Reporting through the emulator's log
// --- is the honest substitute: the message reaches the same place every other
// --- diagnostic does, and no caller depends on the user's answer.

extern "C" bool SDLCALL SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags /*flags*/, const char* title,
                                                 const char* message, SDL_Window* /*window*/) {
    std::fprintf(stderr, "[%s] %s\n", title != nullptr ? title : "", message != nullptr ? message : "");
    return true;
}

extern "C" bool SDLCALL SDL_ShowMessageBox(const SDL_MessageBoxData* data, int* buttonid) {
    if (data != nullptr) {
        std::fprintf(stderr, "[%s] %s\n", data->title != nullptr ? data->title : "",
                     data->message != nullptr ? data->message : "");
    }
    // The first button is the default, and SDL's contract is that buttonid is set
    // even when the box is dismissed.
    if (buttonid != nullptr) {
        *buttonid = 0;
    }
    return true;
}
