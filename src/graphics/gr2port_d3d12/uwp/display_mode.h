// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The arithmetic behind 4K presentation, kept free of WinRT so it can be tested
// on the build host.
//
// On a console the swapchain is a CoreWindow's, and the window's size is the
// display's size *at the shell's composition scale*. Presenting a 1920x1080
// backbuffer into a 3840x2160 window therefore means one of two things: the
// compositor upscales it (bilinear, soft, and on a 4K panel visibly so), or the
// title renders at the panel resolution and the shell's scale transform is
// cancelled so the image is not scaled twice.
//
// The second is what "true 4K" means here, and it has a hard constraint: the
// 5,120 MiB Game-mode pool has to hold the guest, the heap, and now four 4K
// backbuffers plus their depth. At 4K that is 8.3 MiB per R8G8B8A8 buffer, so a
// triple-buffered 4K swapchain costs ~25 MiB of pool -- affordable. Three 4K
// buffers in R10G10B10A2 for HDR cost the same, since the format is also 32-bit.
// What is not affordable is asking for a resolution the panel does not have: a
// 4K backbuffer on a 1440p output is pure waste, scaled back down by the
// compositor.
//
// So the rule this file encodes is: render at min(panel, cap), and cancel the
// composition scale so the backbuffer maps 1:1 onto display pixels.

#pragma once

#include <algorithm>
#include <cstdint>

namespace Gr2Port::Uwp::Display {

/// The cap. Xbox Series X can drive 8K output, but the port's guest and heap
/// budgets are sized for 4K and a Game-mode title that asks for more than the
/// pool can hold fails at commit time rather than degrading. 3840x2160 is the
/// ceiling the memory planner was verified against (see uwp_memory.cpp).
inline constexpr uint32_t kMaxWidth = 3840;
inline constexpr uint32_t kMaxHeight = 2160;

/// A display mode as the shell reports it, reduced to what the renderer needs.
struct Mode {
    uint32_t width = 0;
    uint32_t height = 0;
    /// NaN-free; the shell reports 1.0 on a console output at native resolution.
    float composition_scale = 1.0f;
};

/// What the swapchain should be created with, and which transform cancels the
/// shell's scaling.
struct Plan {
    /// Backbuffer size. Never above kMax*, never above the panel, never zero.
    uint32_t width = 0;
    uint32_t height = 0;
    /// The inverse of the shell's composition scale, ready for
    /// IDXGISwapChain2::SetMatrixTransform, in row-major order
    /// (m11, m12, m21, m22 -- only the diagonal is ever non-zero here).
    ///
    /// A CoreWindow swapchain is composited at the window's size, and the window
    /// is reported in DIPs. Presenting a backbuffer of `width x height` physical
    /// pixels into a window whose logical size is `width/scale x height/scale`
    /// would have the compositor scale it by `scale`; multiplying by `1/scale`
    /// on the swapchain cancels that, so one backbuffer pixel is one display
    /// pixel.
    float transform[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    /// True when the panel is bigger than kMax* and the request was clamped.
    bool clamped_by_cap = false;
    /// True when the reported mode was unusable and a fallback was chosen.
    bool used_fallback = false;
};

/// Plans the presentation for a reported display mode.
///
/// `requested_width`/`requested_height` are what the game or the user asked for;
/// pass 0 for either to mean "the panel's resolution". A request is honoured when
/// it fits under the cap and the panel, and is otherwise reduced to the smaller
/// of the two. Zero, absurd or negative sizes fall back to 1920x1080 rather than
/// producing a swapchain DXGI would reject.
inline Plan MakePlan(const Mode& mode, uint32_t requested_width = 0,
                     uint32_t requested_height = 0) {
    Plan plan;

    // --- what the panel can actually show --------------------------------
    uint32_t panel_w = mode.width;
    uint32_t panel_h = mode.height;
    if (panel_w == 0 || panel_h == 0) {
        // A mode the shell has not filled in yet (the first frame, or a display
        // that is mid mode-change). 1080p is what the guest renders at anyway, so
        // it is the safe thing to present while waiting for a real answer.
        panel_w = 1920;
        panel_h = 1080;
        plan.used_fallback = true;
    }

    // --- what was asked for ----------------------------------------------
    uint32_t want_w = requested_width != 0 ? requested_width : panel_w;
    uint32_t want_h = requested_height != 0 ? requested_height : panel_h;

    // --- the cap ----------------------------------------------------------
    // Applied last, and to both axes together, so the aspect ratio the game
    // renders at is never silently changed by clamping one axis only.
    if (panel_w > kMaxWidth || panel_h > kMaxHeight) {
        // The panel exceeds the cap: present at the cap, and let the console's
        // scaler handle the last step to 8K. Rendering the cap and letting the
        // compositor upscale is the cheap half of that trade.
        const float fit = std::min(static_cast<float>(kMaxWidth) / static_cast<float>(panel_w),
                                   static_cast<float>(kMaxHeight) / static_cast<float>(panel_h));
        want_w = std::min(want_w, static_cast<uint32_t>(panel_w * fit));
        want_h = std::min(want_h, static_cast<uint32_t>(panel_h * fit));
        plan.clamped_by_cap = true;
        // Recompute against the capped panel so the next clamp is consistent.
        panel_w = static_cast<uint32_t>(panel_w * fit);
        panel_h = static_cast<uint32_t>(panel_h * fit);
    }

    // Never upscale beyond the panel: that is the compositor's job done badly.
    want_w = std::min(want_w, panel_w);
    want_h = std::min(want_h, panel_h);

    // A zero from a malformed request falls back rather than reaching DXGI.
    if (want_w == 0 || want_h == 0) {
        want_w = std::min<uint32_t>(1920, panel_w);
        want_h = std::min<uint32_t>(1080, panel_h);
        plan.used_fallback = true;
    }

    plan.width = want_w;
    plan.height = want_h;

    // --- the inverse composition transform ---------------------------------
    // Guard the scale itself: a NaN, an infinity, zero or a negative value cannot
    // produce a usable matrix, and DXGI rejects the set rather than clamping.
    float scale = mode.composition_scale;
    if (!(scale > 0.0f) || scale > 8.0f) {
        // !(scale > 0) catches NaN and every non-positive value in one test.
        scale = 1.0f;
    }
    const float inverse = 1.0f / scale;
    plan.transform[0] = inverse;  // m11
    plan.transform[1] = 0.0f;     // m12
    plan.transform[2] = 0.0f;     // m21
    plan.transform[3] = inverse;  // m22

    return plan;
}

/// True when the plan is at the panel's native resolution with no scaling, which
/// is the case worth logging once because it is what "4K" was supposed to mean.
inline bool IsNative(const Plan& plan, const Mode& mode) {
    return mode.width != 0 && mode.height != 0 && plan.width == mode.width &&
           plan.height == mode.height && plan.transform[0] == 1.0f &&
           plan.transform[3] == 1.0f;
}

} // namespace Gr2Port::Uwp::Display
