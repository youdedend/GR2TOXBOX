// SPDX-License-Identifier: GPL-2.0-or-later
#include <SDL3/SDL_events.h>
#include "uwp/sdl3_shim/gr2_sdl_internal.h"
#include <cassert>
#include <chrono>
#include <thread>
void Gr2SdlRunEventWatches(SDL_Event*) {}
int main() {
    SDL_Event event{};
    assert(!SDL_WaitEventTimeout(&event,0));
    assert(!SDL_WaitEventTimeout(nullptr,1));
    SDL_Event first{},second{};
    first.type=SDL_EVENT_QUIT;second.type=SDL_EVENT_GAMEPAD_ADDED;
    assert(SDL_PushEvent(&first));assert(SDL_PushEvent(&second));
    assert(SDL_WaitEventTimeout(nullptr,0)); // peek, not removal
    assert(SDL_WaitEventTimeout(&event,0)&&event.type==first.type);
    assert(SDL_WaitEventTimeout(&event,1)&&event.type==second.type);
    assert(!SDL_WaitEventTimeout(&event,1));
    for(int timeout : {2000,-1}) {
        std::thread producer([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            assert(SDL_PushEvent(&first));
        });
        assert(SDL_WaitEventTimeout(&event,timeout)&&event.type==first.type);
        producer.join();
    }
    assert(!SDL_WaitEventTimeout(&event,0));
}
