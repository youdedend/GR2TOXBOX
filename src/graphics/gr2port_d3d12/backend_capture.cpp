// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Honest link adapters for shared capture.cpp. Mesh/texture GPU storage and their
// hooks remain unavailable until Slice 4B; the shared scene capture itself is real.

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
// Meshes
// ---------------------------------------------------------------------------
//
// The mesh capture hooks the game's .gfx builder and copies each shape's vertices
// and indices into the renderer's memory. Both of those halves are D3D12 work that
// does not exist yet: there is no vertex buffer to copy into and no index buffer to
// share.
//
// Install answers false for that reason, and it is the honest answer rather than a
// shortcut. The hook installer itself is API-agnostic and would succeed; what would
// not work is what happens next. Installing hooks that fire on every shape the
// game loads and then record nothing is indistinguishable, from a log, from a game
// that loaded nothing -- so instead the startup sequence stops here, with a line
// that says which capture is missing. The caller (gr2port.cpp) treats false as
// fatal to the renderer's start and does not go on to claim it started.

namespace Meshes {

void Prefetch(VAddr) {}

const Mesh* Get(VAddr shape) {
    // Nothing is captured, so no shape has a mesh. Nullptr is what the caller tests
    // and skips the draw on, which is the right outcome for a draw whose geometry
    // nothing owns.
    (void)shape;
    return nullptr;
}

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    (void)module_base;
    (void)c;
    (void)capacity;
    // The caller's test is `Meshes::Install(...) || Textures::Install(...)`, which
    // short-circuits: returning false here means Textures::Install is never called,
    // so its message would never be printed and a reader would fix the meshes only
    // to discover the textures on the next run. Both are named here instead, so one
    // line reports the whole of what is missing.
    LOG_ERROR(Graphics,
              "GR2port/D3D12: the mesh and texture captures are not implemented, so the renderer "
              "cannot start. The hook installers, the shape walk and the GNF header parsing are "
              "all shared code; what is missing is the D3D12 side of each -- a vertex buffer to "
              "upload shapes into, and a texture plus descriptor for the maps.");
    return false;
}

} // namespace Meshes

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

namespace Textures {

u32 GetCube(VAddr) { return 0; }
void Prefetch(const VAddr*, u32) {}
void PrefetchRecords(const VAddr*, u32) {}

u32 Get(VAddr texture) {
    // 0 is the capture's "no texture" handle; a draw that names it is skipped
    // rather than drawn with the wrong map.
    (void)texture;
    return 0;
}

u32 GetBySharp(const u32* sharp, bool& dynamic) {
    (void)sharp;
    dynamic = false;
    return 0;
}

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    (void)module_base;
    (void)c;
    (void)capacity;
    // Not reached today: the caller's `||` short-circuits on Meshes::Install, which
    // also returns false and says both. Kept correct anyway, because the day the
    // mesh capture lands this becomes the line that reports what is left, and a
    // message that says the wrong thing is worse than none.
    LOG_ERROR(Graphics,
              "GR2port/D3D12: the texture capture is not implemented, so the renderer cannot "
              "start. The GNF header parsing is shared code; what is missing is the upload into a "
              "D3D12 texture and the descriptor that points at it.");
    return false;
}

} // namespace Textures

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
