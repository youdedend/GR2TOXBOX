// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "common/types.h"

#if defined(_WIN32)
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#else
#error "The Direct3D 12 backend is Windows/Xbox only (GR2_RENDERER=D3D12)."
#endif

struct SDL_Window;

/// The Direct3D 12 backend's device layer.
///
/// This replaces `graphics/gr2port/device.h`'s Vulkan instance/device for
/// `-DGR2_RENDERER=D3D12` builds. It is the only place in the backend that
/// touches DXGI and `ID3D12Device` creation; everything above it works with
/// the handles below.
///
/// Targets:
///   * Desktop Win64  — `CreateSwapChainForHwnd`, any FL 12_0+ adapter.
///   * Xbox Series X|S — `CreateSwapChainForCoreWindow`, FL 12_0, SM 6.4 (the
///     console driver's ceiling), a 1,200 MiB heap budget from `Core::Xbox`.
namespace Gr2Port::D3D12 {

using Microsoft::WRL::ComPtr;

/// One GPU adapters's resolved capabilities.
struct Caps {
    std::wstring description;
    /// The FL12_0 minimum used for device creation on both targets.
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_12_0;
    u64 dedicated_vram = 0;
    /// Raytracing tier 1.1, used only for optional debug visualisation; the
    /// renderer runs without it.
    bool raytracing = false;
    /// Resource binding tier 3.
    ///
    /// This is the only bindless-related capability the renderer depends on, and
    /// it is what makes a descriptor table large enough to address every
    /// descriptor the arena hands out. Xbox in UWP has it.
    ///
    /// There is deliberately no `shader_model_6_6` or `bindless` flag here any
    /// more. Every stage is compiled for `*_6_4`, so "the device would accept
    /// 6.6 DXIL" describes a property nothing uses, and a `bindless` flag named
    /// after SM 6.6's `ResourceDescriptorHeap[]` described a path this renderer
    /// does not take: shaders reach the heap through descriptor tables in
    /// `space100..space111` (see `descriptors.h` and `common/descriptors.hlsli`).
    /// Keeping both flags meant every reader had to know which of the two was the
    /// real gate -- and the startup check got it wrong once already.
    bool binding_tier3 = false;
    /// Native 16-bit shader operations. Absent on Xbox in UWP. Reported only,
    /// because the half-precision path is written to fall back.
    bool native_16bit_ops = false;
    /// The swapchain can present with tearing (VSync off, VRR).
    bool tearing = false;
    /// The device supports `ID3D12Device8` (needed for `CreateCommittedResource3`
    /// with explicit barrier layouts).
    bool device8 = false;
    /// DXGI reports a display-timing / VRR-capable output.
    bool vrr = false;
};

/// Frames in flight. Three matches upstream's triple-buffered Vulkan path and
/// keeps the CPU from stalling on a GPU that is one frame behind.
inline constexpr u32 FramesInFlight = 3;

/// Everything the rest of the backend needs from the device.
struct Device {
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter4> adapter;
    ComPtr<ID3D12Device8> device;
    ComPtr<ID3D12CommandQueue> graphics_queue;
    /// Copy/compute queue used by the texture detile and skinning computes.
    ComPtr<ID3D12CommandQueue> compute_queue;
    Caps caps;

    /// DXGI flip-model swapchain. Created for HWND on desktop and for a
    /// CoreWindow (`CreateSwapChainForCoreWindow`) on Xbox UWP.
    ComPtr<IDXGISwapChain4> swapchain;
    DXGI_FORMAT swapchain_format = DXGI_FORMAT_R10G10B10A2_UNORM;
    DXGI_COLOR_SPACE_TYPE swapchain_colorspace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    u32 swapchain_width = 0;
    u32 swapchain_height = 0;
    /// How many buffers the swapchain owns; FramesInFlight is the CPU-side cap.
    u32 buffer_count = 3;
    bool tearing_enabled = false;
    bool hdr_enabled = false;

    /// Set on Xbox UWP, where presentation goes through a CoreWindow.
    bool core_window = false;

    /// Graphics-queue timeline fence, the backend's single synchronisation point.
    ComPtr<ID3D12Fence> timeline_fence;
    u64 timeline_value = 0;
    HANDLE timeline_event = nullptr;

    /// The largest SRV/UAV descriptor handle increment the device reports.
    u32 srv_descriptor_size = 0;
    u32 rtv_descriptor_size = 0;
    u32 dsv_descriptor_size = 0;
    u32 sampler_descriptor_size = 0;
};

/// Creation parameters, filled from the launcher's preset.
struct DeviceDesc {
    /// Desktop: the SDL window's HWND. Ignored on Xbox, where `core_window` is
    /// used instead.
    void* hwnd = nullptr;
    /// Xbox UWP: the `ICoreWindow*` the swapchain presents to.
    void* core_window = nullptr;
    u32 width = 1920;
    u32 height = 1080;
    /// Console presets request 4K60 / 1440p120 / 1080p60 / 1440p60.
    bool prefer_hdr = false;
    bool vsync = true;
    bool allow_tearing = true;
    /// Strongly prefer the high-performance adapter on desktop (hybrid laptops).
    bool high_performance = true;
};

/// Picks an adapter and creates the device, both queues and the timeline fence.
/// On Xbox the adapter list contains exactly one device, so selection is a
/// validation rather than a choice.
bool CreateDevice(const DeviceDesc& desc, Device& out);

/// Creates (or recreates) the flip-model swapchain. `DXGI_SWAP_EFFECT_FLIP_DISCARD`
/// always; tearing is enabled when requested and supported.
bool CreateSwapchain(Device& device, const DeviceDesc& desc);

/// Resizes the swapchain's buffers, preserving the format and effect.
bool ResizeSwapchain(Device& device, u32 width, u32 height);

/// Presents one buffer. Returns false when the swapchain must be recreated
/// (`DXGI_ERROR_DEVICE_REMOVED` / `DXGI_ERROR_DEVICE_RESET`).
bool Present(Device& device, bool vsync);

/// CPU waits until the GPU has finished the timeline value it was handed.
void WaitForFence(Device& device, u64 value);

/// Signals the timeline fence and records the new value.
u64 SignalFence(Device& device);

bool IsDeviceLost(HRESULT hr);
std::string HresultToString(HRESULT hr);

} // namespace Gr2Port::D3D12
