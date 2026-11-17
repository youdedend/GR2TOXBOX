// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <string>
#include "common/logging/log.h"
#include "core/uwp_memory.h"

// Whether the Win32 fallbacks below are the ones to compile.
//
// On a real toolchain that is simply "are we on Windows". The third case is the
// host compile-only check: it defines GR2_HOST_CHECK_WIN32 and puts a stub
// <windows.h>/<psapi.h> on the include path so these branches are parsed on
// Linux. Without it, the check defined GR2_XBOX_UWP on a Linux host and fell
// through to the POSIX path, which needs _SC_PHYS_PAGES -- gcc's <unistd.h>
// happens to define it and clang's does not, so the check passed here and failed
// on the runner twice. Naming the case is better than inferring it from __linux__.
#if defined(_WIN32) || defined(GR2_HOST_CHECK_WIN32)
#define GR2_HAS_DESKTOP_WIN32 1
#else
#define GR2_HAS_DESKTOP_WIN32 0
#endif

#if defined(GR2_HOST_CHECK_WIN32)
// Only the host compile-only check reaches this: it parses the console branch,
// which never includes these on a real toolchain, so the fallbacks below would go
// undeclared. The real includes stay at their own site in the desktop branch.
#include <windows.h>
#include <psapi.h>
#endif

#if defined(GR2_XBOX_UWP)
// Console probes use C++/WinRT projections only when the toolchain provides them.
// The optional LocalState sparse-backing helper also needs Win32 app APIs; its
// limited windows.h inclusion below always sets NOMINMAX so std::min in the
// budget clamp remains well-formed. Host self-tests without projections retain
// the compile-time arithmetic and do not include Windows headers. Both system
// projections are required together: MemoryManager is in Windows.System, and
// AnalyticsInfo is in Windows.System.Profile.
#if __has_include(<winrt/Windows.System.h>) && __has_include(<winrt/Windows.System.Profile.h>)
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Profile.h>
#define GR2_HAS_WINRT_SYSTEM 1
#else
#define GR2_HAS_WINRT_SYSTEM 0
#endif
#if __has_include(<winrt/Windows.Storage.h>) && __has_include(<fileapifromapp.h>)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winrt/Windows.Storage.h>
#include <fileapifromapp.h>
#include <memoryapi.h>
#include <winioctl.h>
#define GR2_HAS_WINRT_STORAGE 1
#else
#define GR2_HAS_WINRT_STORAGE 0
#endif
#elif GR2_HAS_DESKTOP_WIN32
// NOMINMAX before <windows.h>, always. The clamp below is written with std::min,
// and windows.h defines `min` as a two-argument macro unless this is set, which
// turns `std::min(a, b)` into `std((' any two tokens '))` and makes MSVC report
// it as "'take': const object must be initialized". That is unreadable enough
// that it cost a run; the guard belongs here rather than on one include site.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
// MEMORYSTATUSEX lives in <winbase.h> via <windows.h> on a real SDK; the stub tree
// declares it in <psapi.h>, which is where this file already looks.
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
#if GR2_HAS_WINRT_SYSTEM
    bool detected = false;
    try {
        // The Xbox device family string is the only reliable way to tell a
        // console from a Windows 11 UWP desktop. On a console the family is
        // "Windows.Xbox"; on desktop it is "Windows.Desktop" or similar.
        // AnalyticsInfo lives in Windows.System.Profile, not Windows.System: the
        // fully-qualified name is load-bearing and the first CI run that reached
        // this line failed on it.
        const auto family = winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo()
                                .DeviceFamily();
        detected = family == L"Windows.Xbox";
    } catch (...) {
        // A missing analytics contract must not take the emulator down; treat
        // the process as a console build, since GR2_XBOX_UWP is only set for
        // the console package.
        detected = true;
    }
#elif defined(GR2_XBOX_UWP)
    // Built for the console, but this toolchain cannot query the device family.
    // GR2_XBOX_UWP is only defined for the console package, so the answer is the
    // compile-time one.
    constexpr bool detected = true;
#else
    constexpr bool detected = false;
#endif
    g_console_detected.store(detected, std::memory_order_relaxed);
    g_console_probed.store(true, std::memory_order_release);
    return detected;
}

u64 AppMemoryUsageLimit() noexcept {
#if GR2_HAS_WINRT_SYSTEM
    try {
        return winrt::Windows::System::MemoryManager::AppMemoryUsageLimit();
    } catch (...) {
        return 0;
    }
#elif GR2_HAS_DESKTOP_WIN32
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
#if GR2_HAS_WINRT_SYSTEM
    try {
        return winrt::Windows::System::MemoryManager::AppMemoryUsage();
    } catch (...) {
        return 0;
    }
#elif GR2_HAS_DESKTOP_WIN32
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

std::uintptr_t CreateGuestBackingSection(u64 size) noexcept {
#if defined(GR2_XBOX_UWP) && GR2_HAS_WINRT_STORAGE
    try {
        const auto local = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
        std::wstring path{local.c_str()};
        path += L"\\gr2-guest-dmem.bin";
        CREATEFILE2_EXTENDED_PARAMETERS params{};
        params.dwSize = sizeof(params);
        params.dwFileAttributes = FILE_ATTRIBUTE_TEMPORARY;
        params.dwFileFlags = FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_RANDOM_ACCESS;
        HANDLE file = CreateFile2FromAppW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                          CREATE_ALWAYS, &params);
        if (file == INVALID_HANDLE_VALUE) return 0;
        DWORD returned = 0;
        if (!DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr)) {
            CloseHandle(file);
            return 0;
        }
        // File-backed sections allocate sparse extents only as pages are touched.
        // The section maximum allows distinct PAGE_READWRITE and
        // PAGE_EXECUTE_READ views; neither alias is ever PAGE_EXECUTE_READWRITE.
        HANDLE section = CreateFileMappingFromApp(file, nullptr, PAGE_EXECUTE_READWRITE,
                                                   size, nullptr);
        CloseHandle(file); // DELETE_ON_CLOSE; the section retains the file object.
        return reinterpret_cast<std::uintptr_t>(section);
    } catch (...) {
        return 0;
    }
#else
    (void)size;
    return 0;
#endif
}

std::uintptr_t CreateGuestSwapFile() noexcept {
#if defined(GR2_XBOX_UWP) && GR2_HAS_WINRT_STORAGE
    try {
        const auto local = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
        std::wstring path{local.c_str()};
        path += L"\\gr2-guest-swap.bin";
        CREATEFILE2_EXTENDED_PARAMETERS params{};
        params.dwSize = sizeof(params);
        params.dwFileAttributes = FILE_ATTRIBUTE_TEMPORARY;
        params.dwFileFlags = FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_RANDOM_ACCESS;
        HANDLE file = CreateFile2FromAppW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                          CREATE_ALWAYS, &params);
        if (file == INVALID_HANDLE_VALUE) return 0;
        DWORD returned = 0;
        if (!DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr)) {
            CloseHandle(file);
            return 0;
        }
        return reinterpret_cast<std::uintptr_t>(file);
    } catch (...) {
        return 0;
    }
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

void LogStartupReport(u64 guest_commit_bytes, u64 direct_bytes, u64 flexible_bytes) {
    const u64 limit = AppMemoryUsageLimit();
    const u64 usage = AppMemoryUsage();
    if (IsXboxUwpBuild()) {
        LOG_INFO(Core,
                 "Xbox memory: pool limit {} MiB, usage {} MiB, configured guest commit {} MiB "
                 "(direct {} MiB, flexible {} MiB), guest ceiling {} MiB, D3D12 heap budget {} MiB, "
                 "process commit target {} MiB",
                 limit / 1_MB, usage / 1_MB, guest_commit_bytes / 1_MB,
                 direct_bytes / 1_MB, flexible_bytes / 1_MB, GuestMemoryCeilingBytes / 1_MB,
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
                 "Host memory: limit {} MiB, usage {} MiB, configured guest commit {} MiB "
                 "(direct {} MiB, flexible {} MiB), guest ceiling {} MiB (PS4 Pro devkit), "
                 "D3D12 heap budget {} MiB (desktop; unbounded)",
                 limit / 1_MB, usage / 1_MB, guest_commit_bytes / 1_MB,
                 direct_bytes / 1_MB, flexible_bytes / 1_MB,
                 DesktopGuestCeilingBytes / 1_MB, DesktopD3d12HeapBudgetBytes / 1_MB);
    }
}

} // namespace Core::Xbox
