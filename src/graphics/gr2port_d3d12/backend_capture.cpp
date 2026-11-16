// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Remaining honest link adapters for shared capture.cpp. Mesh/texture hooks
// and residency live in their real backend implementation units.

#include <atomic>

#include "common/logging/log.h"
#include "graphics/gr2port/deformers.h"
#include "graphics/gr2port/upscaler.h"
#include "graphics/gr2port/meshes.h"
#include "graphics/gr2port/particles.h"
#include "graphics/gr2port/pipeline.h"
#include "graphics/gr2port/textures.h"

namespace Gr2Port {

// ---------------------------------------------------------------------------
// Pipeline builds
// ---------------------------------------------------------------------------
//
// D3D12 creates each pipeline state object on demand, at the first draw that needs
// it, so there is no build-ahead set. False is the documented answer for "nothing
// was waited for"; the caller then logs nothing, which is right.

bool WaitForPipelineBuilds() {
    return false;
}

BuildProgress PipelineBuildProgress() {
    return BuildProgress{};
}

namespace Upscaler {
Extent RenderSize(u32 width, u32 height) { return {width, height}; }
} // namespace Upscaler

namespace Deformers {
Copy Get(VAddr, VAddr, const Meshes::Mesh&, u64, u64) { return {}; }
} // namespace Deformers

// ---------------------------------------------------------------------------
// Particle curves
// ---------------------------------------------------------------------------

namespace Particles {

void QueueCurve(u32 destination, u32 columns, VAddr texels, u32 pitch) {
    (void)destination;
    (void)columns;
    (void)texels;
    (void)pitch;
    // The particle capture gathers these into a curve texture the particle shaders
    // sample. There is no such texture yet. Unlike the hook bodies above this is
    // called once per curve the game queues rather than once per frame, so the count
    // is worth keeping: it says how much the consuming pass owes once it exists.
    static std::atomic<u64> curves{0};
    const u64 seen = curves.fetch_add(1, std::memory_order_relaxed) + 1;
    if (seen == 1 || seen % 256 == 0) {
        LOG_DEBUG(Graphics, "GR2port/D3D12: {} particle curves queued and dropped", seen);
    }
}

} // namespace Particles

} // namespace Gr2Port
