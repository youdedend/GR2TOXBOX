// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include "common/error.h"
#include "common/logging/log.h"
#include "core/uwp_wx.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace Core::Wx {

namespace {
std::atomic<u64> g_seal_count{0};
std::atomic<u64> g_open_for_patch_count{0};
std::atomic<u64> g_bytes_sealed{0};

/// Aligns a range outwards to the host's allocation granularity so that a
/// protection change never covers a neighbouring allocation.
struct AlignedRange {
    u8* begin;
    u64 size;
};

[[nodiscard]] AlignedRange AlignOut(void* addr, u64 size) {
    if (size == 0) {
        return {static_cast<u8*>(addr), 0};
    }
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const u64 page = info.dwPageSize;
    const u64 granularity = info.dwAllocationGranularity;
#else
    const u64 page = static_cast<u64>(sysconf(_SC_PAGESIZE));
    const u64 granularity = page;
#endif
    const u64 base = reinterpret_cast<u64>(addr);
    const u64 aligned_begin = base & ~(granularity - 1);
    const u64 end = base + size;
    const u64 aligned_end = (end + page - 1) & ~(page - 1);
    return {reinterpret_cast<u8*>(aligned_begin), aligned_end - aligned_begin};
}
} // namespace

bool HostForbidsRwx() noexcept {
#ifdef GR2_XBOX_UWP
    return true;
#else
    return false;
#endif
}

void FlushInstructionCacheRange(void* addr, u64 size) {
    if (size == 0) {
        return;
    }
#ifdef _WIN32
    ::FlushInstructionCache(GetCurrentProcess(), addr, static_cast<SIZE_T>(size));
#else
    const auto range = AlignOut(addr, size);
    __builtin___clear_cache(reinterpret_cast<char*>(range.begin),
                            reinterpret_cast<char*>(range.begin + range.size));
#endif
}

bool CommitWritable(void* addr, u64 size) {
    if (size == 0) {
        return true;
    }
#ifdef _WIN32
#ifdef GR2_XBOX_UWP
    // AppContainer-safe commit. VirtualAllocFromApp refuses RWX by policy, so
    // this is always a plain read-write commit.
    void* ret = VirtualAllocFromApp(addr, size, MEM_COMMIT, PAGE_READWRITE);
#else
    void* ret = VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE);
#endif
    if (ret == nullptr) {
        LOG_CRITICAL(Core, "W^X: failed to commit writable code at {}: {}",
                     fmt::ptr(addr), Common::GetLastErrorMsg());
        return false;
    }
    return true;
#else
    void* ret = mmap(addr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (ret == MAP_FAILED) {
        LOG_CRITICAL(Core, "W^X: failed to map writable code at {}", fmt::ptr(addr));
        return false;
    }
    return true;
#endif
}

bool SealExecutable(void* addr, u64 size) {
    if (size == 0) {
        return true;
    }
    // The instruction cache must be coherent with the writes before the host
    // is allowed to fetch from the range.
    FlushInstructionCacheRange(addr, size);

#ifdef _WIN32
    const auto range = AlignOut(addr, size);
    DWORD old_protect{};
#ifdef GR2_XBOX_UWP
    if (!VirtualProtectFromApp(range.begin, range.size, PAGE_EXECUTE_READ, &old_protect)) {
        LOG_CRITICAL(Core, "W^X: failed to seal code at {}: {}", fmt::ptr(range.begin),
                     Common::GetLastErrorMsg());
        return false;
    }
#else
    if (!VirtualProtect(range.begin, range.size, PAGE_EXECUTE_READ, &old_protect)) {
        LOG_CRITICAL(Core, "W^X: failed to seal code at {}: {}", fmt::ptr(range.begin),
                     Common::GetLastErrorMsg());
        return false;
    }
#endif
    g_seal_count.fetch_add(1, std::memory_order_relaxed);
    g_bytes_sealed.fetch_add(size, std::memory_order_relaxed);
    return true;
#else
    const auto range = AlignOut(addr, size);
    if (mprotect(range.begin, range.size, PROT_READ | PROT_EXEC) != 0) {
        LOG_CRITICAL(Core, "W^X: failed to seal code at {}", fmt::ptr(range.begin));
        return false;
    }
    g_seal_count.fetch_add(1, std::memory_order_relaxed);
    g_bytes_sealed.fetch_add(size, std::memory_order_relaxed);
    return true;
#endif
}

bool OpenForPatch(void* addr, u64 size) {
    if (size == 0) {
        return true;
    }
#ifdef _WIN32
    const auto range = AlignOut(addr, size);
    DWORD old_protect{};
#ifdef GR2_XBOX_UWP
    // PAGE_EXECUTE_READWRITE does not exist in an AppContainer. Drop execute
    // while the write happens: the thread performing the patch is running host
    // code, so the range does not need to be executable during the rewrite.
    // The patching thread's own return path never executes from this range.
    if (!VirtualProtectFromApp(range.begin, range.size, PAGE_READWRITE, &old_protect)) {
        LOG_CRITICAL(Core, "W^X: failed to open code for patching at {}: {}",
                     fmt::ptr(range.begin), Common::GetLastErrorMsg());
        return false;
    }
#else
    if (!VirtualProtect(range.begin, range.size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        LOG_CRITICAL(Core, "W^X: failed to open code for patching at {}: {}",
                     fmt::ptr(range.begin), Common::GetLastErrorMsg());
        return false;
    }
#endif
    g_open_for_patch_count.fetch_add(1, std::memory_order_relaxed);
    return true;
#else
    const auto range = AlignOut(addr, size);
    if (mprotect(range.begin, range.size, PROT_READ | PROT_WRITE) != 0) {
        LOG_CRITICAL(Core, "W^X: failed to open code for patching at {}", fmt::ptr(range.begin));
        return false;
    }
    g_open_for_patch_count.fetch_add(1, std::memory_order_relaxed);
    return true;
#endif
}

bool CloseAfterPatch(void* addr, u64 size) {
    return SealExecutable(addr, size);
}

u32 ProtForCodeInitial(u32 guest_prot, bool is_exec) noexcept {
#ifdef _WIN32
    constexpr u32 kNoAccess = PAGE_NOACCESS;
    constexpr u32 kReadOnly = PAGE_READONLY;
    constexpr u32 kReadWrite = PAGE_READWRITE;
    constexpr u32 kExecuteRead = PAGE_EXECUTE_READ;
#else
    constexpr u32 kNoAccess = PROT_NONE;
    constexpr u32 kReadOnly = PROT_READ;
    constexpr u32 kReadWrite = PROT_READ | PROT_WRITE;
    constexpr u32 kExecuteRead = PROT_READ | PROT_EXEC;
#endif

    const bool read = (guest_prot & 1) != 0 || (guest_prot & 16) != 0;  // CpuRead | GpuRead
    const bool write = (guest_prot & 2) != 0 || (guest_prot & 32) != 0; // CpuWrite | GpuWrite

    // A guest segment that is not executable but writable stays writable.
    if (!is_exec) {
        if (write) {
            return kReadWrite;
        }
        return read ? kReadOnly : kNoAccess;
    }

    // An executable guest segment is committed writable for population and
    // sealed to PAGE_EXECUTE_READ afterwards. Writing here is expected: the
    // loader, the relocator and the static patcher all run before sealing.
    return write || IsStrictWxRequired() ? kReadWrite : kExecuteRead;
}

Stats GetStats() noexcept {
    return Stats{
        .seal_count = g_seal_count.load(std::memory_order_relaxed),
        .open_for_patch_count = g_open_for_patch_count.load(std::memory_order_relaxed),
        .bytes_sealed = g_bytes_sealed.load(std::memory_order_relaxed),
    };
}

void LogStartupReport() {
    if (IsStrictWxRequired()) {
        LOG_INFO(Core, "W^X: strict mode (AppContainer): guest code is committed "
                       "PAGE_READWRITE, populated, flushed and sealed PAGE_EXECUTE_READ; "
                       "PAGE_EXECUTE_READWRITE is never requested");
    } else {
        LOG_INFO(Core, "W^X: desktop mode: page guards available, execute-write transitions "
                       "go through VirtualProtect/mprotect");
    }
}

} // namespace Core::Wx
