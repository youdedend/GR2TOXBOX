// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The scene-capture half of the D3D12 backend: the hook bodies the game's draw
// thread calls, and the mesh/texture/particle handles it reads.
//
// The Vulkan versions of these live in capture.cpp, particles.cpp, meshes.cpp,
// textures.cpp and pipeline.cpp, all of which name vk:: types throughout -- the
// mesh capture copies into vk::Buffer, the texture capture reads GNF data into a
// vk::Image, the pipeline capture builds vk::Pipeline. A D3D12 build therefore
// compiles every API-agnostic module cleanly and then fails at *link* on these
// names, which is exactly what it did.
//
// The declarations are not restated here. The modules that call these already
// include capture.h / meshes.h / textures.h / particles.h / pipeline.h, so what is
// missing is only a definition for each. That is what this file is.
//
// What is deliberately not done is pretending. These entry points are called from
// the game's own draw thread every frame, and a stub that claimed to have captured
// a scene would turn "not implemented yet" into a black screen with no line in the
// log to explain it -- the failure mode this port keeps having to dig back out of.
// So: the frame counter is real (it counts what actually reached the capture), the
// capture is genuinely toggleable, and the handle lookups answer "nothing", which
// is what makes the capture skip a draw rather than draw it wrong.

#include <atomic>

#include "common/logging/log.h"
#include "graphics/gr2port/capture.h"
#include "graphics/gr2port/meshes.h"
#include "graphics/gr2port/particles.h"
#include "graphics/gr2port/pipeline.h"
#include "graphics/gr2port/textures.h"

namespace Gr2Port {

namespace {

/// The frames the game has started. Counted rather than inferred: it is the one
/// number here that is a fact even when nothing is drawn, and the renderer reports
/// it as the game's frame number.
std::atomic<u64> g_frames{0};

/// Whether the capture is on. ToggleCapture flips it, and the hook bodies respect
/// it, so the game can run with the renderer's work stopped even here.
std::atomic<bool> g_capture_enabled{true};

/// Whether the one explanation of the empty capture has been written. These fire
/// every frame on the draw thread; a log line each would bury everything else.
std::atomic<bool> g_said_no_capture{false};

void NoteEmptyCaptureOnce() {
    if (g_said_no_capture.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    LOG_INFO(Graphics,
             "GR2port/D3D12: the scene capture records nothing yet, so the game's frames are "
             "drawn without a scene. The hooks are installed and firing; the scene ring and the "
             "passes that consume it arrive with the draw path.");
}

} // namespace

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

// ---------------------------------------------------------------------------
// The scene capture
// ---------------------------------------------------------------------------

namespace Capture {

void OnFrame(VAddr frame_graph, VAddr view) {
    (void)frame_graph;
    (void)view;
    g_frames.fetch_add(1, std::memory_order_relaxed);
    if (g_capture_enabled.load(std::memory_order_relaxed)) {
        NoteEmptyCaptureOnce();
    }
}

void OnParticleSetup(VAddr fx) {
    (void)fx;
}

void OnParticleList(VAddr fx) {
    (void)fx;
}

void OnFrameEnd() {}

u64 Frames() {
    return g_frames.load(std::memory_order_relaxed);
}

void ToggleCapture() {
    // atomic<bool> has no fetch_xor, so this is a load, a compare and a store. Only
    // the UI thread calls it, so the non-atomic window does not matter.
    const bool enabled = !g_capture_enabled.load(std::memory_order_relaxed);
    g_capture_enabled.store(enabled, std::memory_order_relaxed);
    LOG_INFO(Graphics, "GR2port/D3D12: the scene capture is now {}", enabled ? "on" : "off");
    if (enabled) {
        // Re-arm the note, so turning the capture back on says again that it is not
        // recording rather than leaving the previous run's line to stand.
        g_said_no_capture.store(false, std::memory_order_relaxed);
    }
}

} // namespace Capture

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
