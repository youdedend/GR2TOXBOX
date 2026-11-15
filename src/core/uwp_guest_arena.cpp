// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/uwp_guest_arena.h"
#include <atomic>
#if defined(GR2_XBOX_UWP)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <memoryapi.h>
#endif
namespace Core::Xbox {
namespace { std::atomic<void*> early_arena{nullptr}; }
bool ReserveEarlyGuestArena() noexcept {
#if defined(GR2_XBOX_UWP)
    if (early_arena.load(std::memory_order_acquire)) return true;
    void* const expected_base = reinterpret_cast<void*>(EarlyGuestBase);
    void* const reserved = VirtualAlloc2FromApp(GetCurrentProcess(), expected_base,
        EarlyGuestBytes, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0);
    if (reserved != expected_base) return false;
    early_arena.store(reserved, std::memory_order_release);
    return true;
#else
    return false;
#endif
}
void* TakeEarlyGuestArena() noexcept { return early_arena.exchange(nullptr, std::memory_order_acq_rel); }
}
