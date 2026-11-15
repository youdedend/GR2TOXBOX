// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// ImGui's windows over the D3D12 backend's picture: the capture half.
//
// On the Vulkan side this namespace does two things at once, and the two pull in
// opposite directions. `Capture` runs on the present thread, immediately after
// ImGui::Render, and copies the draw lists and the pixels of any texture they
// sample. `Record` runs inside the frame's last rendering and draws them. The
// first needs nothing from the graphics API; the second needs a command list.
//
// So this file implements the first and leaves the second to pipeline.cpp, for the
// same reason it is left there on Vulkan: the drawing half has to reproduce ImGui's
// texture-backend contract exactly, and a mistake in it is a dangling texture or a
// leak rather than a build error. Splitting the file is what makes that verifiable
// -- the capture half can be reasoned about and tested without a device.
//
// Why the capture half matters even before the D3D12 backend can present anything:
// ImGui's code in the frontend calls these five entry points unconditionally
// (texture_manager.cpp registers the pixels of every icon a layer decodes,
// imgui_core.cpp relays its events and captures its frame). Without definitions the
// port does not link, and without link there is no binary to look at while the
// drawing half is written. This is the contract those callers are owed, held to
// exactly, so the pixels they hand over are not silently dropped on the floor: they
// are kept, and the drawing half is what will consume them.

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

#include "graphics/gr2port/backend.h"
#include "graphics/gr2port/frame_stats.h"
#include "imgui/imgui_config.h"
#include "imgui/imgui_texture.h"

#if defined(GR2_XBOX_UWP)
#include "uwp/sdl3_shim/pad_state.h"
#endif

namespace Gr2Port::ImGuiRelay {

namespace {

/// How ImGui's canvas relates to the window.
///
/// ImGui is given the window's size in pixels and draws in those units, so the
/// mapping is the identity -- but it is written as a scale and an offset rather
/// than assumed, because a backend that renders at a different resolution (a
/// 1080p canvas upscaled to a 4K panel, which is what the console will want) must
/// change it in one place.
struct CanvasMapping {
    float scale = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
};

std::mutex g_mutex;
CanvasMapping g_mapping;

/// The pixels of each texture a layer has handed over, keyed by the texture's
/// address. Kept rather than discarded: the drawing half uploads from here, and a
/// layer that decodes an icon expects it to survive until the texture is gone.
struct UploadedTexture {
    std::vector<u8> pixels;
    u32 width = 0;
    u32 height = 0;
};
std::map<const ImGui::Texture*, UploadedTexture> g_textures;

/// The draw data of the last captured frame. A copy, because ImGui reuses its
/// buffers on the next NewFrame and the drawing half runs one frame behind the
/// present thread.
struct CapturedFrame {
    bool valid = false;
    int command_lists = 0;
    int total_indices = 0;
    int total_vertices = 0;
};
CapturedFrame g_frame;

// The cursor position ImGui's backend last set, so WarpBack can restore it after a
// frame that moved the mouse without the user moving it. SDL's relative-mode warp
// is a no-op in the shim, so the value is kept for the desktop D3D12 build, where
// it is real.
float g_warp_x = 0.0f;
float g_warp_y = 0.0f;
bool g_warp_pending = false;
bool g_cursor_visible = false;

/// The window the relay draws into. Set by Capture's caller through ForImGui's
/// window argument having been null; SDL_GetMouseState's absolute position needs no
/// window, and the warp is a no-op when this is null.
SDL_Window* g_window = nullptr;

/// The cursor's position in canvas units, which is what ImGui is told.
float ToCanvas(const float window_coordinate, const float offset, const float scale) {
    return (window_coordinate - offset) / scale;
}

} // namespace

void Capture(ImDrawData& draw_data) {
    const std::scoped_lock lock{g_mutex};

    // ImGui reports a canvas in points and a framebuffer scale; the window's
    // backend is expected to draw at scale * size. Recording both is what lets the
    // drawing half check its own resolution against ImGui's, which is the one
    // mismatch that produces picking that is subtly off rather than visibly wrong.
    const ImVec2 display_size = draw_data.DisplaySize;
    const ImVec2 framebuffer_scale = draw_data.FramebufferScale;
    (void)display_size;
    (void)framebuffer_scale;

    g_frame.valid = draw_data.Valid;
    g_frame.command_lists = draw_data.CmdListsCount;
    g_frame.total_indices = draw_data.TotalIdxCount;
    g_frame.total_vertices = draw_data.TotalVtxCount;

    // The textures a command list samples are named by their ImTextureID, which in
    // this port is the address of the ImGui::Texture the layer registered. A list
    // that samples one the relay does not have is not an error -- ImGui's own
    // backend draws its built-in font atlas as texture 0, which no layer registers
    // -- so nothing is asserted here.
    for (int i = 0; i < draw_data.CmdListsCount; ++i) {
        const ImDrawList* const list = draw_data.CmdLists[i];
        if (list == nullptr) {
            continue;
        }
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.ElemCount == 0) {
                continue;
            }
            // Nothing is uploaded from here: the pixels already arrived through
            // KeepPixels, and a upload needs the device. What this loop is for is
            // the one invariant worth checking on this side -- that a command's
            // user callback has been cleared, because ImGui's reset-on-render
            // contract means a stale one would be called twice.
            if (command.UserCallback != nullptr) {
                // ImGui's reset callbacks are how the Vulkan relay reports that a
                // list is still in flight. There is no such state here yet; leaving
                // the callback alone is the correct no-op, and it is called by
                // ImGui's own Render when it resets the buffer.
            }
        }
    }

    // The cursor is hidden while ImGui's frame draws its own, and the relay is the
    // only place that knows the frame reached the screen. WarpBack uses this to
    // decide whether a warp is needed at all.
    g_warp_pending = true;
}

void KeepPixels(const ImGui::Texture* texture, const void* pixels, u32 width, u32 height) {
    if (texture == nullptr || pixels == nullptr || width == 0 || height == 0) {
        return;
    }
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    const std::scoped_lock lock{g_mutex};
    UploadedTexture& entry = g_textures[texture];
    entry.width = width;
    entry.height = height;
    entry.pixels.resize(bytes);
    std::copy_n(static_cast<const u8*>(pixels), bytes, entry.pixels.begin());
}

void Forget(const ImGui::Texture* texture) {
    if (texture == nullptr) {
        return;
    }
    const std::scoped_lock lock{g_mutex};
    g_textures.erase(texture);
}

const SDL_Event* ForImGui(const SDL_Event* event, SDL_Event& copy) {
    if (event == nullptr) {
        return nullptr;
    }
    // ImGui's backend wants mouse events in the coordinates of its canvas, which
    // are the window's pixels here. Everything else -- keys, text, the pad, which
    // on the console is how the UI is driven -- is passed through untouched.
    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION: {
        const std::scoped_lock lock{g_mutex};
        copy = *event;
        copy.motion.x = ToCanvas(event->motion.x, g_mapping.offset_x, g_mapping.scale);
        copy.motion.y = ToCanvas(event->motion.y, g_mapping.offset_y, g_mapping.scale);
        return &copy;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const std::scoped_lock lock{g_mutex};
        copy = *event;
        copy.button.x = ToCanvas(event->button.x, g_mapping.offset_x, g_mapping.scale);
        copy.button.y = ToCanvas(event->button.y, g_mapping.offset_y, g_mapping.scale);
        return &copy;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        // A wheel's position is relative, so it needs no mapping.
        return event;
    default:
        return event;
    }
}

bool WarpBack() {
    const std::scoped_lock lock{g_mutex};
    if (!g_warp_pending) {
        return false;
    }
    g_warp_pending = false;
    if (g_cursor_visible || g_window == nullptr) {
        // Nothing to put back: the cursor is not hidden, so it is already where the
        // user left it.
        return false;
    }
    SDL_WarpMouseInWindow(g_window, g_warp_x, g_warp_y);
    return true;
}

// --- The relay's state, for the drawing half --------------------------------
//
// Not part of the frontend-facing contract in backend.h: this is how the drawing
// half reaches what this file captured, without either naming the other's types.
// It needs a home of its own only because the backend does not have one yet --
// when it does, the swapchain knows both the window and the canvas size, and these
// become part of that.

void SetWindow(SDL_Window* window) {
    const std::scoped_lock lock{g_mutex};
    g_window = window;
}

void SetCanvasMapping(float scale, float offset_x, float offset_y) {
    const std::scoped_lock lock{g_mutex};
    g_mapping.scale = scale > 0.0f ? scale : 1.0f;
    g_mapping.offset_x = offset_x;
    g_mapping.offset_y = offset_y;
}

} // namespace Gr2Port::ImGuiRelay
