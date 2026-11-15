// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The frontend-facing half of the D3D12 backend: the renderer's lifetime, the
// lost-device log, the game's photos and the pipeline-build log.
//
// The D3D12 backend today owns a device, a command list and a pipeline library. It
// has no swapchain and no pass recording, so it cannot draw a frame -- and these
// four namespaces are what the *frontend* asks of a renderer regardless of whether
// one can draw: start a render thread, decide from the last run's log whether the
// device was lost, hand the JPEG encoder a photo, report how long the pipeline
// build took. None of them is allowed to be missing at link time, and none of them
// should lie.
//
// So each answer here is the true one for a backend without a present path, and it
// says so in the log rather than pretending. That is deliberate: a stub that
// returns success would turn "the renderer does not present yet" into a black
// window with no explanation, which is the failure mode this whole console port
// keeps having to dig out of logs.

#include <SDL3/SDL_messagebox.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "common/logging/log.h"
#include "graphics/gr2port/backend.h"
#include "graphics/gr2port/frame_stats.h"

#if defined(GR2_XBOX_UWP)
#include "uwp/sdl3_shim/pad_state.h"
#endif

namespace Gr2Port {

namespace {

/// Whether Renderer::Start has been called. The render thread itself arrives with
/// the swapchain; until then this is what a caller's "did the renderer start" test
/// answers against. Guarded because Start is allowed to be called from the UWP
/// view's thread and checked from the game's.
std::mutex g_state_mutex;
bool g_renderer_started = false;

/// What DeviceLost::Arm was told, and whether the pass trace is on. Kept because
/// the frontend asks for the trace before anything else has run, and the report it
/// produces is written when the device is created -- which the backend can do, even
/// without a swapchain.
std::string g_trace_reason;
bool g_trace_armed = false;

} // namespace

// --- Renderer ---------------------------------------------------------------

void Renderer::Start() {
    const std::scoped_lock lock{g_state_mutex};
    if (g_renderer_started) {
        LOG_INFO(Graphics, "D3D12: the renderer was already started; ignored.");
        return;
    }
    g_renderer_started = true;
    LOG_INFO(Graphics,
             "D3D12: the renderer has no present path yet -- the device, command list and "
             "pipeline library are built, the swapchain and the passes are not. The game "
             "will run, its capture will fill, and nothing will be drawn.");
}

// --- Pipeline builds --------------------------------------------------------
//
// The Vulkan backend compiles pipelines on a worker pool and records which one is in
// flight so a lost-device report can name it. The D3D12 backend compiles its
// pipelines synchronously on first use, so there is never one "being built" across a
// thread boundary: the honest answer is that this thread is not building any.
std::string PipelineBeingBuilt() {
    return {};
}

// --- Device lost ------------------------------------------------------------

void DeviceLost::Arm(std::string_view reason) {
    const std::scoped_lock lock{g_state_mutex};
    g_trace_armed = true;
    g_trace_reason = std::string{reason};
    LOG_INFO(Graphics, "D3D12: the pass trace is on for this run ({})", g_trace_reason);
}

bool DeviceLost::LastRunLost(const std::filesystem::path& path) {
    // The Vulkan report is a text file the renderer appends to; a run that lost its
    // device leaves a line saying so. Reading the same marker keeps the frontend's
    // behaviour identical on both renderers -- it arms the trace for the next run --
    // and works before the D3D12 backend has a device at all, which is exactly when
    // the frontend asks.
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return false;
    }
    // Opened through the path's native string rather than a stream over the path:
    // this is read before the renderer has a device or a working directory it can
    // rely on, and the narrow form is the one that works on every build.
    std::FILE* const file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    std::string contents;
    char chunk[4096];
    size_t read = 0;
    while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        contents.append(chunk, read);
    }
    std::fclose(file);

    static constexpr std::string_view kMarker = "device lost";
    return std::search(contents.begin(), contents.end(), kMarker.begin(), kMarker.end(),
                       [](const char a, const char b) {
                           return std::tolower(static_cast<unsigned char>(a)) ==
                                  std::tolower(static_cast<unsigned char>(b));
                       }) != contents.end();
}

// --- The game's photos ------------------------------------------------------

bool Photo::Read(std::vector<u8>& pixels, u32& width, u32& height, u32& pitch) {
    // A photo is a copy of the frame's photo target, taken by the screen drawing.
    // There is no screen drawing yet, so no photo is ever taken -- and the encoder
    // treats a false return as "not this frame", which is what it does while the
    // game has not reached a photo screen. Nothing is fabricated: a blank image
    // returned as success would be saved as the player's photograph.
    pixels.clear();
    width = 0;
    height = 0;
    pitch = 0;
    return false;
}

} // namespace Gr2Port
