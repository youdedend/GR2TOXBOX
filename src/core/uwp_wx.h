// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string_view>
#include "common/types.h"

/// Strict W^X for native x86-64 guest execution.
///
/// Xbox Series X|S Retail Dev Mode runs the title in an AppContainer. The
/// container's security policy forbids `PAGE_EXECUTE_READWRITE`, and Windows
/// will refuse the allocation even when the package declares the
/// `codeGeneration` restricted capability. Guest code therefore goes through
/// the following lifecycle on every Xbox UWP build:
///
///   1. Reserve        : MEM_RESERVE, PAGE_NOACCESS (never readable).
///   2. Commit writable: PAGE_READWRITE through `VirtualAllocFromApp`.
///   3. Populate       : ELF segments copied, relocations applied, static CPU
///                       patches and red-zone rewrites written in place.
///   4. Seal           : `FlushInstructionCache`, then `VirtualProtectFromApp`
///                       to PAGE_EXECUTE_READ. There is no writable+executable
///                       state, and a sealed page is never writable again
///                       until explicitly opened for patching.
///   5. JIT patching   : `TryPatchJit` faults on the sealed page; the fault
///                       handler opens it with a *flush* protection
///                       (`PAGE_EXECUTE_READWRITE` on desktop; on Xbox UWP,
///                       where that is illegal, `PAGE_READWRITE`), rewrites
///                       the instruction and seals it again.
///
/// On desktop Windows the same code path runs, so the Xbox lifecycle is
/// exercised by the desktop D3D12 CI job. On POSIX the guard compiles to the
/// platform's ordinary `mprotect` behaviour.
///
/// The guard is deliberately a thin, allocation-free layer: it is called from
/// module loading, from the region mappers and from a first-chance exception
/// handler, where taking a lock or allocating would be unsafe.
namespace Core::Wx {

/// Whether the build enforces AppContainer-strict W^X (Xbox UWP builds).
[[nodiscard]] constexpr bool IsStrictWxRequired() noexcept {
#if defined(GR2_XBOX_UWP)
    return true;
#else
    return false;
#endif
}

/// Whether the host refuses `PAGE_EXECUTE_READWRITE`. Equal to
/// IsStrictWxRequired() on Xbox, and probeable on desktop for diagnostics.
[[nodiscard]] bool HostForbidsRwx() noexcept;

/// Commits `size` bytes at `addr` (inside a region previously reserved by
/// `ReserveCodeArena`) as writable, non-executable memory. Guest code and
/// trampolines are written here.
bool CommitWritable(void* addr, u64 size);

/// Makes a populated, writable code range executable and read-only. Flushes
/// the instruction cache for the host CPU before dropping the write right.
bool SealExecutable(void* addr, u64 size);

/// Reopens a sealed code range for in-place instruction rewriting. On a strict
/// W^X host the range becomes writable and non-executable; on desktop it
/// becomes `PAGE_EXECUTE_READWRITE`. Returns false if the protection request
/// was rejected, in which case the caller must leave the code alone.
bool OpenForPatch(void* addr, u64 size);

/// Closes a range opened by `OpenForPatch`: flushes the instruction cache and
/// restores `PAGE_EXECUTE_READ`.
bool CloseAfterPatch(void* addr, u64 size);

/// Flushes the host instruction cache over `[addr, addr + size)`.
void FlushInstructionCacheRange(void* addr, u64 size);

/// Translates a guest protection into the host protection for a *code* page.
///
/// Unlike the memory backend's generic translation, this never yields
/// `PAGE_EXECUTE_READWRITE` on a strict W^X host: an execute+writable request
/// is split into its writeable phase (`PAGE_READWRITE`), and the caller must
/// call `SealExecutable` once the range is populated.
[[nodiscard]] u32 ProtForCodeInitial(u32 guest_prot, bool is_exec) noexcept;

/// Diagnostics, logged once at startup and available to the launcher.
struct Stats {
    u64 seal_count;
    u64 open_for_patch_count;
    u64 bytes_sealed;
};
[[nodiscard]] Stats GetStats() noexcept;
void LogStartupReport();

} // namespace Core::Wx
