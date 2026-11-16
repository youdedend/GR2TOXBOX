// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

#include "graphics/gr2port/backend.h"

#include "imgui/imgui_texture.h"
#include "graphics/gr2port_d3d12/imgui_snapshot.h"
#include <cstring>
#include <limits>

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
using Key = std::uint64_t;
std::map<Key,std::shared_ptr<const Pixels>> g_textures;
std::shared_ptr<const Snapshot> g_frame;
std::uint64_t g_revision = 0;
Key KeyOf(const ImGui::Texture* p) { return reinterpret_cast<std::uintptr_t>(p); }
Key KeyOf(const ImTextureData* p) { return (Key{1}<<63) | reinterpret_cast<std::uintptr_t>(p); }
ImGui::Texture g_own_texture;
std::shared_ptr<const Pixels> CopyPixels(const void* data, u32 width, u32 height) {
    if (!data || !width || !height || width>16384 || height>16384 || std::uint64_t(width)*height*4>64ull*1024*1024) return {};
    auto image=std::make_shared<Pixels>();
    image->revision=++g_revision; image->width=width; image->height=height;
    const auto* bytes=static_cast<const u8*>(data);
    image->rgba.assign(bytes,bytes+size_t(width)*height*4);
    return image;
}

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

std::shared_ptr<const Snapshot> Latest() {
    const std::scoped_lock lock{g_mutex}; return g_frame;
}
void ResetSnapshots() {
    const std::scoped_lock lock{g_mutex}; g_frame.reset(); g_textures.clear();
}
void Capture(ImDrawData& draw_data) {
    const std::scoped_lock lock{g_mutex};
    // ImGui 1.92 owns a mutable atlas. Copy before acknowledging its requests.
    if (draw_data.Textures) for (ImTextureData* texture : *draw_data.Textures) {
        if ((texture->Status==ImTextureStatus_WantCreate || texture->Status==ImTextureStatus_WantUpdates)
            && texture->Format==ImTextureFormat_RGBA32) {
            if(auto image=CopyPixels(texture->Pixels,u32(texture->Width),u32(texture->Height))) {
                g_textures[KeyOf(texture)]=std::move(image);
                texture->SetTexID(&g_own_texture); texture->SetStatus(ImTextureStatus_OK);
            }
        } else if(texture->Status==ImTextureStatus_WantDestroy && texture->UnusedFrames>0) {
            g_textures.erase(KeyOf(texture)); texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    auto frame=std::make_shared<Snapshot>();
    frame->position[0]=draw_data.DisplayPos.x; frame->position[1]=draw_data.DisplayPos.y;
    frame->size[0]=draw_data.DisplaySize.x; frame->size[1]=draw_data.DisplaySize.y;
    frame->framebuffer_scale[0]=draw_data.FramebufferScale.x;
    frame->framebuffer_scale[1]=draw_data.FramebufferScale.y;
    for(const ImDrawList* list : draw_data.CmdLists) {
        const auto vb=u32(frame->vertices.size()), ib=u32(frame->indices.size());
        for(const ImDrawVert& v : list->VtxBuffer)
            frame->vertices.push_back({{v.pos.x,v.pos.y},{v.uv.x,v.uv.y},v.col});
        for(ImDrawIdx i : list->IdxBuffer) frame->indices.push_back(u32(i));
        for(const ImDrawCmd& cmd : list->CmdBuffer) {
            // Callbacks contain application pointers. Never execute them later on
            // another thread; reset-state callbacks are unnecessary (we bind each draw).
            if(cmd.UserCallback || !cmd.ElemCount || cmd.IdxOffset>u32(list->IdxBuffer.Size) ||
               cmd.ElemCount>u32(list->IdxBuffer.Size)-cmd.IdxOffset) continue;
            const Key key=cmd.TexRef._TexData ? KeyOf(cmd.TexRef._TexData) : KeyOf(cmd.TexRef._TexID);
            auto image=g_textures.find(key); if(image==g_textures.end()) continue;
            frame->commands.push_back({cmd.ElemCount,ib+cmd.IdxOffset,vb+cmd.VtxOffset,
                {cmd.ClipRect.x,cmd.ClipRect.y,cmd.ClipRect.z,cmd.ClipRect.w},image->second});
        }
    }
    g_frame=std::move(frame);
    g_warp_pending=true;
}
void KeepPixels(const ImGui::Texture* texture,const void* pixels,u32 width,u32 height) {
    if(!texture) return;
    const std::scoped_lock lock{g_mutex};
    if(auto image=CopyPixels(pixels,width,height)) g_textures[KeyOf(texture)]=std::move(image);
}
void Forget(const ImGui::Texture* texture) {
    const std::scoped_lock lock{g_mutex}; g_textures.erase(KeyOf(texture));
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

// Input mapping is independent of the immutable render snapshots.
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
