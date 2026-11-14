// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include "common/logging/log.h"
#include "core/uwp_memory.h"

#if defined(GR2_XBOX_UWP)
// C++/WinRT headers are only pulled in for the UWP package; the desktop build
// must not depend on them.
#include <winrt/Windows.System.h>
#include <windows.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace Core::Xbox {

namespace {

std::atomic<bool> g_console_probed{false};
std::atomic<bool> g_console_detected{false};

/// The guest ceiling the desktop build keeps: the PS4 Pro devkit figure that
/// upstream derives from these constants (7,936 + 512 MiB). Kept as literals so
/// this file does not depend on the kernel headers just for the size of the
/// desktop default.
constexpr u64 DesktopGuestCeilingBytes = 7936_MB + 512_MB;
/// Desktop D3D12 heap budget: 25% of a 16 GiB machine's VRAM, the upstream
/// renderer's own default when VK_EXT_memory_budget is absent.
constexpr u64 DesktopD3d12HeapBudgetBytes = 6ULL * 1024_MB;

} // namespace

bool IsRunningOnConsole() noexcept {
    if (g_console_probed.load(std::memory_order_acquire)) {
        return g_console_detected.load(std::memory_order_relaxed);
    }
#if defined(GR2_XBOX_UWP)
    bool detected = false;
    try {
        // The Xbox device family string is the only reliable way to tell a
        // console from a Windows 11 UWP desktop. On a console the family is
        // "Windows.Xbox"; on desktop it is "Windows.Desktop" or similar.
        const auto family =
            winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily();
        detected = family == L"Windows.Xbox";
    } catch (...) {
        // A missing analytics contract must not take the emulator down; treat
        // the process as a console build, since GR2_XBOX_UWP is only set for
        // the console package.
        detected = true;
    }
#else
    constexpr bool detected = false;
#endif
    g_console_detected.store(detected, std::memory_order_relaxed);
    g_console_probed.store(true, std::memory_order_release);
    return detected;
}

u64 AppMemoryUsageLimit() noexcept {
#if defined(GR2_XBOX_UWP)
    try {
        return winrt::Windows::System::MemoryManager::AppMemoryUsageLimit();
    } catch (...) {
        return 0;
    }
#elif defined(_WIN32)
    // Desktop: report the system commit limit.
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return status.ullTotalPageFile;
    }
    return 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGESIZE);
    return (pages > 0 && page_size > 0) ? static_cast<u64>(pages) * page_size : 0;
#endif
}

u64 AppMemoryUsage() noexcept {
#if defined(GR2_XBOX_UWP)
    try {
        return winrt::Windows::System::MemoryManager::AppMemoryUsage();
    } catch (...) {
        return 0;
    }
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        return static_cast<u64>(counters.PrivateUsage);
    }
    return 0;
#else
    return 0;
#endif
}

u64 EffectiveGuestMemoryCeiling() noexcept {
    if (IsXboxUwpBuild()) {
        return GuestMemoryCeilingBytes;
    }
    return DesktopGuestCeilingBytes;
}

u64 EffectiveD3d12HeapBudget() noexcept {
    if (IsXboxUwpBuild()) {
        return D3d12HeapBudgetBytes;
    }
    return DesktopD3d12HeapBudgetBytes;
}

bool IsAppModeMemoryPool() noexcept {
    if (!IsXboxUwpBuild()) {
        return false;
    }
    const u64 limit = AppMemoryUsageLimit();
    // Game mode on both consoles reports at least the 5,120 MiB pool. A limit
    // well below that (App mode reports ~1,280 MiB on Series S) means the
    // package is registered as an app.
    return limit != 0 && limit < PlatformPoolBytes;
}

void ClampGuestMemoryRegions(u64& total_size, u64& flexible_size, u64 flexible_base) {
    if (!IsXboxUwpBuild()) {
        return;
    }

    // Work in committed bytes throughout. The flexible base is address space,
    // not memory, so it never counts against the ceiling.
    const u64 before_total = total_size;
    if (total_size < flexible_size) {
        return; // malformed request; leave it to the caller's own checks
    }
    u64 direct = total_size - flexible_size;
    u64 flex = flexible_size - flexible_base;

    if (direct + flex <= GuestMemoryCeilingBytes) {
        return;
    }

    u64 excess = direct + flex - GuestMemoryCeilingBytes;

    // Shrink the flexible pool first, down to a floor that still leaves the
    // GPU's flexible mappings room to breathe.
    constexpr u64 FlexibleFloor = 128_MB;
    if (flex > FlexibleFloor) {
        const u64 take = std::min(excess, flex - FlexibleFloor);
        flex -= take;
        excess -= take;
    }

    // Whatever is left comes out of direct memory. The clamp is hard: a partial
    // clamp would still fail at the first oversized commit.
    direct = excess >= direct ? 0 : direct - excess;
    excess = 0;

    total_size = direct + flexible_base + flex;
    flexible_size = flexible_base + flex;

    LOG_WARNING(Core,
                "Xbox memory clamp: guest commit {} MiB -> {} MiB "
                "(direct {} MiB, flexible {} MiB; ceiling {} MiB, pool {} MiB, App mode: {})",
                (before_total - flexible_base) / 1_MB, (total_size - flexible_base) / 1_MB,
                direct / 1_MB, flex / 1_MB,
                GuestMemoryCeilingBytes / 1_MB, PlatformPoolBytes / 1_MB,
                IsAppModeMemoryPool() ? "yes" : "no");
}

void LogStartupReport() {
    const u64 limit = AppMemoryUsageLimit();
    const u64 usage = AppMemoryUsage();
    if (IsXboxUwpBuild()) {
        LOG_INFO(Core,
                 "Xbox memory: pool limit {} MiB, usage {} MiB, guest ceiling {} MiB, "
                 "D3D12 heap budget {} MiB, process commit target {} MiB",
                 limit / 1_MB, usage / 1_MB, GuestMemoryCeilingBytes / 1_MB,
                 D3d12HeapBudgetBytes / 1_MB, ProcessCommitTargetBytes / 1_MB);
        if (IsAppModeMemoryPool()) {
            LOG_CRITICAL(Core,
                         "Xbox memory: the package is running in App mode (limit {} MiB). "
                         "Gravity Rush 2 needs the ~5 GB Game-mode pool: in Xbox Device Portal "
                         "open the app's settings and change App type from 'App' to 'Game', "
                         "then relaunch. See docs/XBOX_DEV_MODE_GUIDE.md.",
                         limit / 1_MB);
        }
    } else {
        LOG_INFO(Core,
                 "Host memory: limit {} MiB, usage {} MiB, guest ceiling {} MiB (PS4 Pro "
                 "devkit), D3D12 heap budget {} MiB (desktop; unbounded)",
                 limit / 1_MB, usage / 1_MB, DesktopGuestCeilingBytes / 1_MB,
                 DesktopD3d12HeapBudgetBytes / 1_MB);
    }
}

} // namespace Core::Xbox
