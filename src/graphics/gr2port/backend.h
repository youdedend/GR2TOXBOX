// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/// What the scene-capture half of the port expects of whichever renderer is
/// behind it.
///
/// The split between src/graphics/gr2port/ (the capture, which names no graphics
/// API) and a backend (which draws what the capture found) was already real: the
/// D3D12 backend builds the API-agnostic modules and drops the Vulkan ones. What
/// was missing is the *contract* between them. The capture modules called into
/// the Vulkan renderer's namespaces directly, so a build with no Vulkan backend
/// compiled every capture module cleanly and then failed at link on the symbols
/// those calls name:
///
///   Gr2Port::Renderer::Start, Gr2Port::DeviceLost::Arm / LastRunLost,
///   Gr2Port::Photo::Read, Gr2Port::PipelineBeingBuilt, and the five ImGuiRelay
///   entry points ImGui's own code calls.
///
/// The Vulkan versions of these live in files that legitimately name vk:: types
/// (device_lost.h carries vk::CommandBuffer, imgui_relay.h a vk::Format), so a
/// backend cannot include their declarations. This header declares the subset
/// that has no API in its signature -- the calls the capture and the frontend
/// make -- so each backend can define them against its own types.
///
/// It is the port's own interface, not an upstream one, and that is why it lives
/// beside the capture rather than in a backend directory.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"

struct ImDrawData;
union SDL_Event;

namespace ImGui {
struct Texture;
}

namespace Gr2Port {

namespace Backend {
enum class Result : u32;
}

// --- The renderer's lifetime ------------------------------------------------
namespace Renderer {

/// Starts the render thread, which creates the window and the device.
///
/// A backend without a present path still accepts this: it records that the
/// renderer was asked to start and says so in the log, which is the difference
/// between "the frontend never asked" and "the backend cannot draw" when a
/// screenshot shows nothing.
void Start();

} // namespace Renderer

// --- Pipeline builds --------------------------------------------------------
//
// The Vulkan backend compiles its pipelines on a worker pool and the frontend waits
// for them, which is why these are part of the frontend-facing surface: a startup
// message reports how long the wait took, and the fault report names the pipeline
// that was building when the device was lost.

/// The pipeline being built on the calling thread, or empty.
[[nodiscard]] std::string PipelineBeingBuilt();

// --- A lost device ----------------------------------------------------------
namespace DeviceLost {

/// Before the renderer starts: turns the trace on for this run, naming why.
void Arm(std::string_view reason);

/// Whether the log at path, the last run's, reports a lost device.
[[nodiscard]] bool LastRunLost(const std::filesystem::path& path);

} // namespace DeviceLost

// --- The game's photos ------------------------------------------------------
namespace Photo {

/// On the encoder's thread: waits for a photo newer than the last one taken and
/// gives its pixels, rows of pitch bytes in B8G8R8A8 order.
///
/// False when no photo comes, which is also what a backend that does not capture
/// them answers -- the JPEG encoder treats it as "not this frame" and asks again.
[[nodiscard]] bool Read(std::vector<u8>& pixels, u32& width, u32& height, u32& pitch);

} // namespace Photo

// --- ImGui's windows --------------------------------------------------------
//
// ImGui's draw lists are copied on the present thread and drawn last by whichever
// renderer is up. Only the entry points that take or return no graphics API type
// are here; the ones that take a command buffer or a format belong to a backend
// and stay in the Vulkan header.
namespace ImGuiRelay {

/// Present thread, after ImGui::Render: copies the frame and answers ImGui's
/// texture requests, as ImGui's renderer backend would.
void Capture(ImDrawData& draw_data);

/// Any thread: the RGBA8 pixels of a texture a layer decodes itself.
void KeepPixels(const ImGui::Texture* texture, const void* pixels, u32 width, u32 height);

/// Any thread: the layer's texture is gone.
void Forget(const ImGui::Texture* texture);

/// Main thread, for the ImGui backend: a mouse event in the canvas's coordinates,
/// in copy. Other events come back as they are.
[[nodiscard]] const SDL_Event* ForImGui(const SDL_Event* event, SDL_Event& copy);

/// ImGui's frame, as it hides the idle cursor: puts it back where it is there to
/// show the change, and returns true.
bool WarpBack();

} // namespace ImGuiRelay

} // namespace Gr2Port
