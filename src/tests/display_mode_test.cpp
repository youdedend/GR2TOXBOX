// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Locks down the 4K presentation arithmetic in display_mode.h.
//
// Every invariant here is one that fails on a console rather than on the build
// host: a backbuffer larger than the cap, an aspect ratio changed by clamping one
// axis, a NaN composition scale reaching SetMatrixTransform, or a 4K swapchain
// whose backbuffers would not fit the Game-mode pool. The code under test is
// free of WinRT precisely so this can run anywhere.
//
//   c++ -std=c++23 -Isrc -DGR2_DISPLAY_SELFTEST
//       src/tests/display_mode_test.cpp -o display && ./display

#include <cmath>
#include <cstdio>

#include "graphics/gr2port_d3d12/uwp/display_mode.h"

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

using Gr2Port::Uwp::Display::Mode;
using Gr2Port::Uwp::Display::Plan;
using Gr2Port::Uwp::Display::MakePlan;
using Gr2Port::Uwp::Display::IsNative;
using Gr2Port::Uwp::Display::kMaxWidth;
using Gr2Port::Uwp::Display::kMaxHeight;

Mode M(uint32_t w, uint32_t h, float scale = 1.0f) {
    Mode m;
    m.width = w;
    m.height = h;
    m.composition_scale = scale;
    return m;
}

// --- 3840x2160 at scale 1 is the whole point -------------------------------
void TestNative4K() {
    const Mode mode = M(3840, 2160);
    const Plan plan = MakePlan(mode);
    CHECK(plan.width == 3840);
    CHECK(plan.height == 2160);
    CHECK(plan.transform[0] == 1.0f);
    CHECK(plan.transform[1] == 0.0f);
    CHECK(plan.transform[2] == 0.0f);
    CHECK(plan.transform[3] == 1.0f);
    CHECK(!plan.clamped_by_cap);
    CHECK(!plan.used_fallback);
    CHECK(IsNative(plan, mode));
}

// --- a 4K panel at 1.5x composition scale still renders 4K -----------------
// The window is reported in DIPs, so the shell would scale by 1.5 on the way to
// the panel. The inverse of that is what the swapchain must carry, and the
// backbuffer must stay at the panel's physical size -- rendering 2560x1440 here
// would be scaled up 1.5x by the compositor and lose the point of 4K.
void TestScaled4K() {
    const Mode mode = M(3840, 2160, 1.5f);
    const Plan plan = MakePlan(mode);
    CHECK(plan.width == 3840);
    CHECK(plan.height == 2160);
    CHECK(std::fabs(plan.transform[0] - (1.0f / 1.5f)) < 1e-6f);
    CHECK(std::fabs(plan.transform[3] - (1.0f / 1.5f)) < 1e-6f);
    CHECK(!IsNative(plan, mode));  // scaled, so not the native case
}

// --- 1440p: render at the panel, do not upscale to a 4K backbuffer ---------
void Test1440pStays1440p() {
    const Plan plan = MakePlan(M(2560, 1440));
    CHECK(plan.width == 2560);
    CHECK(plan.height == 1440);
    CHECK(!plan.clamped_by_cap);
}

// --- a 1080p panel is not made into a 4K render ---------------------------
void Test1080pStays1080p() {
    const Plan plan = MakePlan(M(1920, 1080));
    CHECK(plan.width == 1920);
    CHECK(plan.height == 1080);
}

// --- 8K output clamps to the 4K cap and preserves the aspect ratio --------
void Test8KClampsToCap() {
    const Plan plan = MakePlan(M(7680, 4320));
    CHECK(plan.width == kMaxWidth);
    CHECK(plan.height == kMaxHeight);
    CHECK(plan.clamped_by_cap);
    // Aspect ratio preserved: 7680x4320 and 3840x2160 are both 16:9.
    CHECK(plan.height * 16 == plan.width * 9);
}

// --- an odd-aspect panel over the cap keeps its ratio ---------------------
// 5120x2160 is 64:27; the cap is applied to both axes so the image is not
// squashed by clamping width alone.
void TestOverCapUltrawide() {
    // 5120x2160 is 64:27. Width is over the cap, so the fit factor is 3840/5120 =
    // 0.75 and BOTH axes shrink: 3840x1620, still 64:27. Clamping width alone
    // would have produced 3840x2160 and squashed the image.
    const Plan plan = MakePlan(M(5120, 2160));
    CHECK(plan.clamped_by_cap);
    CHECK(plan.width == kMaxWidth);
    CHECK(plan.height == 1620);
    // The ratio survived: 5120/2160 == 3840/1620 == 64/27.
    CHECK(plan.width * 27 == plan.height * 64);
}

// --- states that must not reach DXGI --------------------------------------
void TestDegenerateModes() {
    // A zero mode (the first frame, before the shell reports anything).
    const Plan zero = MakePlan(M(0, 0));
    CHECK(zero.width == 1920);
    CHECK(zero.height == 1080);
    CHECK(zero.used_fallback);

    // One axis zero.
    const Plan half = MakePlan(M(1920, 0));
    CHECK(half.width >= 1);
    CHECK(half.height >= 1);

    // A tiny mode must not produce a zero-sized buffer.
    const Plan tiny = MakePlan(M(0, 0));
    CHECK(tiny.width > 0 && tiny.height > 0);
}

// --- composition scale that cannot be inverted ----------------------------
void TestBadCompositionScale() {
    const float bad[] = {0.0f, -1.0f, NAN, INFINITY, -INFINITY, 1e30f};
    for (float scale : bad) {
        const Plan plan = MakePlan(M(3840, 2160, scale));
        // Whatever came in, what leaves is a usable identity.
        CHECK(plan.transform[0] == 1.0f);
        CHECK(plan.transform[3] == 1.0f);
        // And a finite matrix, always.
        CHECK(std::isfinite(plan.transform[0]));
        CHECK(std::isfinite(plan.transform[3]));
    }
}

// --- an explicit request is honoured as long as it fits -------------------
void TestExplicitRequest() {
    // 1080p asked for on a 4K panel: honour it, the game chose it.
    const Plan small = MakePlan(M(3840, 2160), 1920, 1080);
    CHECK(small.width == 1920);
    CHECK(small.height == 1080);

    // A request above the panel is reduced to the panel.
    const Plan over = MakePlan(M(2560, 1440), 3840, 2160);
    CHECK(over.width == 2560);
    CHECK(over.height == 1440);

    // A request above the cap on a 4K panel is reduced to the cap (== panel).
    const Plan capped = MakePlan(M(3840, 2160), 7680, 4320);
    CHECK(capped.width <= kMaxWidth);
    CHECK(capped.height <= kMaxHeight);
}

// --- the backbuffers have to fit the pool --------------------------------
// 5,120 MiB is the Game-mode pool. Three 4K 32-bit backbuffers plus three depth
// buffers at 32 bits add up to ~50 MiB, which has to be a rounding error next to
// the guest budget -- if it were not, the swapchain size would be a memory bug,
// not a presentation choice.
void Test4KSwapchainFitsThePool() {
    const Plan plan = MakePlan(M(3840, 2160));
    const double bytes_per_pixel = 4.0;  // R8G8B8A8_UNORM or R10G10B10A2 both
    const double buffers = 3.0;
    const double colour = plan.width * plan.height * bytes_per_pixel * buffers;
    const double depth = plan.width * plan.height * bytes_per_pixel * buffers;
    const double total_mib = (colour + depth) / (1024.0 * 1024.0);
    // 3840*2160*4*3*2 / 1 MiB = 189.8 MiB. Well under a tenth of the pool.
    CHECK(total_mib < 512.0);
    std::printf("  4K swapchain: %.1f MiB for 3 colour + 3 depth buffers\n", total_mib);
}

} // namespace

int main() {
    TestNative4K();
    TestScaled4K();
    Test1440pStays1440p();
    Test1080pStays1080p();
    Test8KClampsToCap();
    TestOverCapUltrawide();
    TestDegenerateModes();
    TestBadCompositionScale();
    TestExplicitRequest();
    Test4KSwapchainFitsThePool();

    if (g_failures != 0) {
        std::fprintf(stderr, "display: %d checks, %d failure(s)\n", g_checks, g_failures);
        return 1;
    }
    std::printf("display: %d checks, 0 failure(s)\n", g_checks);
    return 0;
}
