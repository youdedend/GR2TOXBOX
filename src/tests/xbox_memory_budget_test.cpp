// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Locks down the Xbox Series X|S memory budget arithmetic. These invariants
// cannot regress: a guest commit above the ceiling, or a heap budget that
// pushes the process past the 5,120 MiB Game-mode pool, fails on a console with
// ERROR_COMMITMENT_LIMIT long after the code that caused it was written.
//
// The check is deliberately dependency-free: the upstream tree has no test
// target, and this must run in CI on any platform without a test framework.
// It builds as a standalone executable when GR2_XBOX_UWP is not defined and
// exercises the *console* arithmetic by calling the same functions with the
// numbers the console passes. Build:
//
//   c++ -std=c++23 -Isrc -DGR2_XBOX_BUDGET_SELFTEST \
//       src/tests/xbox_memory_budget_test.cpp src/core/uwp_memory.cpp -o budget
//   ./budget
//
// On a GR2_XBOX_UWP build the clamp itself is active, so the same test asserts
// the real console behaviour.

#include <cstdio>
#include <cstdlib>

#include "common/types.h"
#include "core/uwp_memory.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* expression, int line) {
    ++g_checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++g_failures;
    }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

constexpr u64 M = 1_MB;
/// ORBIS_KERNEL_FLEXIBLE_MEMORY_BASE: address space, not committed memory.
constexpr u64 FlexibleBase = 64 * M;
/// The PS4 Pro devkit total upstream sizes its backing file at.
constexpr u64 DevkitTotal = 7936 * M + 512 * M;

// ---------------------------------------------------------------------------
// Budget invariants
// ---------------------------------------------------------------------------

void GuestPlusHeapFitsTheGameModePool() {
    const u64 committed = Core::Xbox::GuestMemoryCeilingBytes + Core::Xbox::D3d12HeapBudgetBytes;
    CHECK(committed <= Core::Xbox::ProcessCommitTargetBytes);
    CHECK(Core::Xbox::ProcessCommitTargetBytes < Core::Xbox::PlatformPoolBytes);
}

void CeilingIsThreePointTwoGigabytes() {
    CHECK(Core::Xbox::GuestMemoryCeilingBytes == 3200 * M);
    CHECK(Core::Xbox::D3d12HeapBudgetBytes == 1200 * M);
    CHECK(Core::Xbox::GuestDirectCeilingBytes + Core::Xbox::GuestFlexibleCeilingBytes ==
          Core::Xbox::GuestMemoryCeilingBytes);
    CHECK(Core::Xbox::PlatformPoolBytes == 5120 * M);
}

// ---------------------------------------------------------------------------
// Clamp behaviour
// ---------------------------------------------------------------------------

/// Every clamp result must be self-consistent, whatever the input: the flexible
/// size includes its base, direct memory is never negative, and the committed
/// total never exceeds the ceiling.
void AssertSanePostClamp(u64 total, u64 flexible, u64 flexible_base) {
    CHECK(total >= flexible);
    CHECK(flexible >= flexible_base);
    // The ceiling only binds on the console build; a desktop PC keeps the
    // upstream PS4 Pro devkit totals, so the bound is not an invariant there.
    if (Core::Xbox::IsXboxUwpBuild()) {
        CHECK(total - flexible_base <= Core::Xbox::GuestMemoryCeilingBytes);
    }
}

void RequestsAtTheCeilingAreUntouched() {
    u64 total = FlexibleBase + Core::Xbox::GuestMemoryCeilingBytes;
    u64 flexible = FlexibleBase + Core::Xbox::GuestFlexibleCeilingBytes;
    Core::Xbox::ClampGuestMemoryRegions(total, flexible, FlexibleBase);
    AssertSanePostClamp(total, flexible, FlexibleBase);
    CHECK(total == FlexibleBase + Core::Xbox::GuestMemoryCeilingBytes);
    CHECK(flexible == FlexibleBase + Core::Xbox::GuestFlexibleCeilingBytes);
}

void OversizedDevkitRequestIsReducedToTheCeiling() {
    u64 total = DevkitTotal;
    u64 flexible = FlexibleBase + 512 * M;
    Core::Xbox::ClampGuestMemoryRegions(total, flexible, FlexibleBase);
    AssertSanePostClamp(total, flexible, FlexibleBase);
    if (Core::Xbox::IsXboxUwpBuild()) {
        // The console build must actually clamp, not merely stay consistent.
        CHECK(total - FlexibleBase == Core::Xbox::GuestMemoryCeilingBytes);
    }
}

void ResolutionPatchesCannotInflatePastTheCeiling() {
    // The 5K/8K resolution patches add gigabytes of extra direct memory.
    u64 total = DevkitTotal + 4096 * M;
    u64 flexible = FlexibleBase + 512 * M;
    Core::Xbox::ClampGuestMemoryRegions(total, flexible, FlexibleBase);
    AssertSanePostClamp(total, flexible, FlexibleBase);
}

void SmallGameIsNotInflated() {
    // A game that asks for less than the ceiling is left exactly as it asked.
    u64 total = FlexibleBase + 2048 * M;
    u64 flexible = FlexibleBase + 256 * M;
    Core::Xbox::ClampGuestMemoryRegions(total, flexible, FlexibleBase);
    CHECK(total == FlexibleBase + 2048 * M);
    CHECK(flexible == FlexibleBase + 256 * M);
}

} // namespace

int main() {
    std::printf("Xbox memory budget self-test (GR2_XBOX_UWP=%s)\n",
                Core::Xbox::IsXboxUwpBuild() ? "on" : "off");
    GuestPlusHeapFitsTheGameModePool();
    CeilingIsThreePointTwoGigabytes();
    RequestsAtTheCeilingAreUntouched();
    OversizedDevkitRequestIsReducedToTheCeiling();
    ResolutionPatchesCannotInflatePastTheCeiling();
    SmallGameIsNotInflated();
    std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
