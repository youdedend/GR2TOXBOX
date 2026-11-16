// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

/// Xbox Series X|S unified-memory management.
///
/// A UWP title on Xbox Series X|S runs inside a fixed memory pool. Even in
/// **Game** mode (which the title requests in the manifest and which the user
/// must confirm in Xbox Device Portal) the pool is capped at **5,120 MiB**
/// shared between the CPU and the GPU. In **App** mode the pool is a quarter of
/// that, which is why a title left in App mode dies with
/// `ERROR_COMMITMENT_LIMIT` during boot.
///
/// Upstream GR2port sizes its guest physical backing at the PS4 Pro *devkit*
/// figure (`ORBIS_KERNEL_TOTAL_MEM_DEV_PRO` + flexible = 7,936 + 512 MiB ≈
/// 8.25 GiB) because it targets desktop PCs. Committing that on Xbox fails
/// immediately, before the game's first frame.
///
/// The rules this module enforces:
///
/// | Allocation | Desktop | Xbox UWP |
/// | --- | --- | --- |
/// | Guest direct memory + flexible memory | PS4 Pro devkit (8.25 GiB) | **3,200 MiB** |
/// | Direct3D 12 heap budget | whatever DXGI reports | **1,200 MiB** |
/// | Total process commit target | unconstrained | **< 4,600 MiB** |
///
/// 3,200 + 1,200 + host code and stacks (~200 MiB) leaves headroom under the
/// 5,120 MiB pool on both Series X (10 GiB usable in Game mode) and Series S
/// (8 GiB, 5,120 MiB pool), so a title that respects the budget boots on
/// either console with the same package.
namespace Core::Xbox {

// ---------------------------------------------------------------------------
// Budget constants
// ---------------------------------------------------------------------------

/// Total guest physical memory (direct + flexible) the emulator may commit.
/// UWP packages can lower this with -DGR2_XBOX_GUEST_MEMORY_CEILING_MIB=N;
/// CMake rejects values at or above the platform's 5,120 MiB hard pool.
#ifndef GR2_XBOX_GUEST_MEMORY_CEILING_MIB
#define GR2_XBOX_GUEST_MEMORY_CEILING_MIB 3200
#endif
inline constexpr u64 GuestMemoryCeilingBytes =
    static_cast<u64>(GR2_XBOX_GUEST_MEMORY_CEILING_MIB) * 1_MB;
/// Guest direct memory, derived from the ceiling minus the flexible pool that
/// SetupMemoryRegions() keeps for the GPU-side flexible mappings.
inline constexpr u64 GuestDirectCeilingBytes = GuestMemoryCeilingBytes - 512_MB;
/// Guest flexible memory at the ceiling.
inline constexpr u64 GuestFlexibleCeilingBytes = 512_MB;

/// Direct3D 12 resource heap budget (D3D12MA + residency management).
inline constexpr u64 D3d12HeapBudgetBytes = 1200_MB;

/// The platform's hard pool. A title never gets more than this in Game mode.
inline constexpr u64 PlatformPoolBytes = 5120_MB;
/// The process commit this port targets, leaving room for host allocations.
inline constexpr u64 ProcessCommitTargetBytes = 4600_MB;

/// Xbox Series S has 8 GiB of GDDR6 shared and a smaller pool; the same
/// package runs on both consoles, so the budget is the Series S figure.
inline constexpr u64 SeriesSCeilingBytes = 4600_MB;

// ---------------------------------------------------------------------------
// Platform probing
// ---------------------------------------------------------------------------

/// Whether this build is the Xbox UWP package.
[[nodiscard]] constexpr bool IsXboxUwpBuild() noexcept {
#if defined(GR2_XBOX_UWP)
    return true;
#else
    return false;
#endif
}

/// Whether the process is actually running on an Xbox console (as opposed to a
/// desktop build being tested on Windows). Detected through the platform's
/// device family at runtime under GR2_XBOX_UWP.
[[nodiscard]] bool IsRunningOnConsole() noexcept;

/// The process's current commit limit in bytes, from
/// `Windows::System::MemoryManager::AppMemoryUsageLimit` on Xbox and from the
/// job object / system limits on desktop. Zero when unknown.
[[nodiscard]] u64 AppMemoryUsageLimit() noexcept;

/// The process's current committed bytes, or zero when unknown.
[[nodiscard]] u64 AppMemoryUsage() noexcept;

/// Creates the UWP LocalState sparse-file section used as guest physical backing.
/// The section's maximum permits RW and RX views; callers must never map a view
/// writable and executable, and must close the returned section handle.
[[nodiscard]] std::uintptr_t CreateGuestBackingSection(u64 size) noexcept;

/// Effective guest memory ceiling: GuestMemoryCeilingBytes on a console, and
/// the upstream PS4 Pro devkit figure on desktop (so PC behaviour is
/// unchanged).
[[nodiscard]] u64 EffectiveGuestMemoryCeiling() noexcept;

/// Effective Direct3D 12 heap budget in bytes: D3d12HeapBudgetBytes on a
/// console, and a generous desktop default elsewhere.
[[nodiscard]] u64 EffectiveD3d12HeapBudget() noexcept;

/// Clamps the guest memory totals that `MemoryManager::SetupMemoryRegions()` is
/// about to install.
///
/// `total_size` is the guest's total physical memory (direct + flexible) as the
/// game and the launcher's presets asked for; `flexible_size` is the flexible
/// pool plus its base (`ORBIS_KERNEL_FLEXIBLE_MEMORY_BASE`). The guest commit is
/// `total_size - flexible_base`. On Xbox that is clamped to
/// `GuestMemoryCeilingBytes`, taking the excess first out of the flexible pool
/// (down to a 128 MiB floor that keeps the GPU flexible mappings alive) and
/// then out of direct memory. Every clamp is logged: a silent clamp looks like
/// a game bug later.
void ClampGuestMemoryRegions(u64& total_size, u64& flexible_size, u64 flexible_base);

/// A warning to surface in the launcher when the package is running in App
/// mode (and therefore with an eighth of the pool). Empty when the pool is
/// adequate.
[[nodiscard]] bool IsAppModeMemoryPool() noexcept;

void LogStartupReport(u64 guest_commit_bytes, u64 direct_bytes, u64 flexible_bytes);

} // namespace Core::Xbox
