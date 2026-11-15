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

// Deliberately not imgui.h. This file needs two things from ImGui -- that
// ImGui::Texture is a type, and the ImDrawData its Capture is handed -- and
// backend.h forward-declares the first. Pulling ImGui in would put its whole API
// and its ImTextureID definition in front of a file whose job is to copy plain
// pointers, and it would drag imgui_texture.h's RefCountedTexture (whose
// definition lives in the frontend's texture manager) in as a link dependency of
// the backend. The frame's own description is read through the members SDL and
// ImGui agree on, which Capture does below.
struct ImDrawData;

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
// Keyed by the texture's address as an opaque pointer. ImGui::Texture is an empty
// struct in this port -- its only purpose is to be an address a layer names -- and
// this file does not include imgui.h, so the address is what it keeps.
std::map<const void*, UploadedTexture> g_textures;

/// The draw data of the last captured frame. A copy, because ImGui reuses its
/// buffers on the next NewFrame and the drawing half runs one frame behind the
/// present thread.
struct CapturedFrame {
    ImDrawData* draw_data = nullptr;
    bool valid = false;
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

    // The frame's draw data is kept by address, not copied. Copying would need
    // ImGui's types -- which this file deliberately does not include, see above --
    // and ImGui guarantees the data stays valid until the next NewFrame, which is
    // the window the drawing half has.
    g_frame.draw_data = &draw_data;
    g_frame.valid = true;

    // The cursor is hidden while ImGui's frame draws its own, and Capture is the
    // only point that knows a frame was rendered at all. WarpBack reads this to
    // decide whether a warp is owed.
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
// The drawing half needs the captured frame and the canvas mapping. It does not
// exist yet -- no swapchain, no pass list -- so these two entry points are the seam
// it will join at: the swapchain, once it exists, calls SetWindow with the window
// it presents to and SetCanvasMapping with where ImGui's canvas sits in it.
//
// Nothing calls them today, and the defaults are correct without them: a warp with
// no window does nothing (WarpBack), and an identity mapping passes coordinates
// through unchanged (ForImGui). That is the property to keep in mind when wiring
// them up -- they may stay uncalled indefinitely without being wrong.

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
