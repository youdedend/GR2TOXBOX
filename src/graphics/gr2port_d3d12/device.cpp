// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstdio>
#include <string>

#include <dxgidebug.h>

#include "common/logging/log.h"
#include "core/uwp_memory.h"
#include "graphics/gr2port_d3d12/device.h"

#if defined(GR2_XBOX_UWP)
#include <windows.ui.core.h>
#endif

namespace Gr2Port::D3D12 {

namespace {

std::string Trimmed(const std::wstring& wide) {
    std::string out;
    out.reserve(wide.size());
    for (const wchar_t c : wide) {
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    }
    return out;
}

/// Fills in the capabilities the backend actually branches on.
void ResolveCaps(IDXGIAdapter4* adapter, ID3D12Device8* device, bool tearing, Caps& caps) {
    DXGI_ADAPTER_DESC3 desc{};
    if (SUCCEEDED(adapter->GetDesc3(&desc))) {
        caps.description = desc.Description;
        caps.dedicated_vram = desc.DedicatedVideoMemory;
        caps.vrr = (desc.Flags & DXGI_ADAPTER_FLAG3_SUPPORT_MONITORED_FLIPS) != 0;
    }

    caps.feature_level = device->GetFeatureLevel();
    caps.tearing = tearing;
    caps.device8 = true;

    // Shader model: D3D12_FEATURE_D3D12_OPTIONS4 reports the highest model the
    // device compiles, which is what the offline dxc target must match.
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &options4,
                                              sizeof(options4)))) {
        caps.shader_model_6_6 = options4.ShaderModel >= D3D_SHADER_MODEL_6_6;
    } else {
        caps.shader_model_6_6 = false;
    }

    // Bindless: the renderer indexes ResourceDescriptorHeap[] directly, which
    // D3D12 exposes for SM 6.6 resources on 12_1 hardware. There is no separate
    // feature bit; the capability is the shader model plus a resource binding
    // tier of 3.
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    bool tier3 = false;
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options,
                                              sizeof(options)))) {
        tier3 = options.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_3;
    }
    caps.bindless = tier3 && caps.shader_model_6_6;

    // Raytracing is optional (debug visualisation only). XBOX_SERIES_X_DEVICE
    // reports tier 1.1, Series S does too, but the renderer never requires it.
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5,
                                              sizeof(options5)))) {
        caps.raytracing = options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
    }
}

bool EnableTearing(IDXGIFactory6* factory, bool allow) {
    if (!allow) {
        return false;
    }
    BOOL supported = FALSE;
    if (SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supported,
                                               sizeof(supported)))) {
        return supported == TRUE;
    }
    return false;
}

} // namespace

bool IsDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG;
}

std::string HresultToString(HRESULT hr) {
    const char* known = nullptr;
    switch (hr) {
    case S_OK:
        return "S_OK";
    case E_OUTOFMEMORY:
        known = "E_OUTOFMEMORY";
        break;
    case E_INVALIDARG:
        known = "E_INVALIDARG";
        break;
    case DXGI_ERROR_DEVICE_REMOVED:
        known = "DXGI_ERROR_DEVICE_REMOVED";
        break;
    case DXGI_ERROR_DEVICE_RESET:
        known = "DXGI_ERROR_DEVICE_RESET";
        break;
    case DXGI_ERROR_DEVICE_HUNG:
        known = "DXGI_ERROR_DEVICE_HUNG";
        break;
    case DXGI_ERROR_UNSUPPORTED:
        known = "DXGI_ERROR_UNSUPPORTED";
        break;
    case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
        known = "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";
        break;
    default:
        break;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
    return known != nullptr ? std::string(known) + " (" + buffer + ")" : std::string(buffer);
}

namespace {

/// Creates the adapter list and picks the GPU the backend runs on.
///
/// On Xbox there is exactly one adapter and it is always picked. On desktop the
/// high-performance adapter wins when the launcher asks for it, which matters on
/// hybrid laptops where the integrated GPU cannot hold the 1,200 MiB budget.
ComPtr<IDXGIAdapter4> PickAdapter(IDXGIFactory6* factory, const DeviceDesc& desc) {
    ComPtr<IDXGIAdapter4> best;
    u64 best_score = 0;

    const DXGI_GPU_PREFERENCE preference =
        desc.high_performance ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                              : DXGI_GPU_PREFERENCE_UNSPECIFIED;

    for (u32 index = 0;; ++index) {
        ComPtr<IDXGIAdapter4> adapter;
        if (factory->EnumAdapterByGpuPreference(index, preference, IID_PPV_ARGS(&adapter)) ==
            DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (!adapter) {
            break;
        }
        DXGI_ADAPTER_DESC3 desc3{};
        if (FAILED(adapter->GetDesc3(&desc3))) {
            continue;
        }
        // A software adapter (WARP) can hold the budget but is an order of
        // magnitude too slow; it is a last resort, never a preference.
        if ((desc3.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0) {
            if (!best) {
                best = adapter;
            }
            continue;
        }
        const u64 score = desc3.DedicatedVideoMemory + desc3.SharedSystemMemory / 2;
        if (score >= best_score) {
            best_score = score;
            best = adapter;
        }
    }
    return best;
}

} // namespace

bool CreateDevice(const DeviceDesc& desc, Device& out) {
    out.core_window = desc.core_window != nullptr;

    // --- Debug layer -------------------------------------------------------
    // Only in Debug builds and only on desktop: the Xbox package cannot load
    // d3d12sdklayers.dll and asking for it would fail device creation.
#if defined(_DEBUG) && !defined(GR2_XBOX_UWP)
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            ComPtr<IDXGIInfoQueue> info_queue;
            if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&info_queue)))) {
                info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,
                                               TRUE);
            }
        }
    }
#endif

    // --- Factory -----------------------------------------------------------
    // The debug factory is only available on desktop; the Xbox package cannot
    // load dxgidebug.dll.
    UINT factory_flags = 0;
#if defined(_DEBUG) && !defined(GR2_XBOX_UWP)
    factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    HRESULT hr = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&out.factory));
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12: CreateDXGIFactory2 failed: {}", HresultToString(hr));
        return false;
    }

    out.adapter = PickAdapter(out.factory.Get(), desc);
    if (!out.adapter) {
        LOG_CRITICAL(Graphics, "D3D12: no DXGI adapter is available");
        return false;
    }

    const bool tearing = EnableTearing(out.factory.Get(), desc.allow_tearing && !out.core_window);

    // --- Device ------------------------------------------------------------
    // Feature level 12_1 is the floor: both Xbox Series X and Series S expose
    // 12_1, and every PSO in this backend is compiled for SM 6.6 against it.
    // 12_2 is offered first where the driver allows it; 12_1 is the contract.

    hr = D3D12CreateDevice(out.adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&out.device));
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12: D3D12CreateDevice failed: {} (FL 12_1 is required)",
                     HresultToString(hr));
        return false;
    }
    const D3D_FEATURE_LEVEL chosen_level = out.device->GetFeatureLevel();
    if (chosen_level < D3D_FEATURE_LEVEL_12_1) {
        LOG_CRITICAL(Graphics, "D3D12: device reports feature level {:#x}, 12_1 is required",
                     static_cast<u32>(chosen_level));
        return false;
    }

    ResolveCaps(out.adapter.Get(), out.device.Get(), tearing, out.caps);

    // --- Queues ------------------------------------------------------------
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = out.device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&out.graphics_queue));
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12: CreateCommandQueue(DIRECT) failed: {}",
                     HresultToString(hr));
        return false;
    }

    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    hr = out.device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&out.compute_queue));
    if (FAILED(hr)) {
        // A compute queue is an optimisation (detiling and skinning overlap the
        // graphics queue); fall back to running those on the graphics queue.
        LOG_WARNING(Graphics, "D3D12: CreateCommandQueue(COMPUTE) failed: {}; "
                              "compute work will run on the graphics queue",
                    HresultToString(hr));
        out.compute_queue = out.graphics_queue;
    }

    // --- Timeline fence ----------------------------------------------------
    hr = out.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&out.timeline_fence));
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12: CreateFence failed: {}", HresultToString(hr));
        return false;
    }
    out.timeline_value = 0;
    out.timeline_event = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (out.timeline_event == nullptr) {
        LOG_CRITICAL(Graphics, "D3D12: CreateEventExW failed");
        return false;
    }

    // --- Descriptor increments --------------------------------------------
    out.srv_descriptor_size =
        out.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    out.rtv_descriptor_size =
        out.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    out.dsv_descriptor_size =
        out.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    out.sampler_descriptor_size =
        out.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    LOG_INFO(Graphics,
             "D3D12 device: '{}', FL {:#x}, VRAM {} MiB, bindless {}, tearing {}, "
             "CoreWindow present {}",
             Trimmed(out.caps.description), static_cast<u32>(out.caps.feature_level),
             out.caps.dedicated_vram / 1_MB, out.caps.bindless, out.caps.tearing,
             out.core_window);

    if (!out.caps.shader_model_6_6) {
        LOG_CRITICAL(Graphics,
                     "D3D12: the device does not accept Shader Model 6.6, which every "
                     "shader in this backend is compiled for");
        return false;
    }

    const u64 budget = Core::Xbox::EffectiveD3d12HeapBudget();
    LOG_INFO(Graphics, "D3D12: resource heap budget {} MiB", budget / 1_MB);
    return true;
}

bool CreateSwapchain(Device& device, const DeviceDesc& desc) {
    const u32 width = std::max<u32>(desc.width, 64);
    const u32 height = std::max<u32>(desc.height, 64);
    device.swapchain_width = width;
    device.swapchain_height = height;
    device.hdr_enabled = desc.prefer_hdr;
    device.tearing_enabled = device.caps.tearing && !desc.vsync;
    device.buffer_count = 3;

    // R10G10B10A2 (HDR10 output) when asked for; otherwise 8-bit sRGB, which
    // every display and the Series S both handle without a mode change.
    device.swapchain_format =
        desc.prefer_hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    device.swapchain_colorspace = desc.prefer_hdr
                                      ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                                      : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.Width = width;
    swap_desc.Height = height;
    swap_desc.Format = device.swapchain_format;
    swap_desc.Stereo = FALSE;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = device.buffer_count;
    // FLIP_DISCARD with a flip model is mandatory for a UWP CoreWindow swapchain
    // and is the fast path on desktop.
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swap_desc.Flags = device.tearing_enabled ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    // A CoreWindow swapchain must be created with a scaling of
    // DXGI_SCALING_STRETCH (ASPECT_RATIO_STRETCH is rejected on Xbox).
    swap_desc.Scaling = device.core_window ? DXGI_SCALING_STRETCH : DXGI_SCALING_NONE;

    ComPtr<IDXGISwapChain1> base;
    HRESULT hr = S_OK;

    if (device.core_window) {
#if defined(GR2_XBOX_UWP)
        // Xbox UWP: present straight to the CoreWindow the app created.
        auto* core_window = static_cast<::ABI::Windows::UI::Core::ICoreWindow*>(desc.core_window);
        hr = device.factory->CreateSwapChainForCoreWindow(
            device.graphics_queue.Get(), core_window, &swap_desc, nullptr, &base);
        if (FAILED(hr)) {
            LOG_CRITICAL(Graphics, "D3D12: CreateSwapChainForCoreWindow failed: {}",
                         HresultToString(hr));
            return false;
        }
#else
        LOG_CRITICAL(Graphics, "D3D12: CoreWindow presentation requested on a non-UWP build");
        return false;
#endif
    } else {
        // Desktop Win64.
        hr = device.factory->CreateSwapChainForHwnd(device.graphics_queue.Get(),
                                                    static_cast<HWND>(desc.hwnd), &swap_desc,
                                                    nullptr, nullptr, &base);
        if (FAILED(hr)) {
            LOG_CRITICAL(Graphics, "D3D12: CreateSwapChainForHwnd failed: {}",
                         HresultToString(hr));
            return false;
        }
        // Alt+Enter is handled by the launcher's own fullscreen toggle.
        device.factory->MakeWindowAssociation(static_cast<HWND>(desc.hwnd),
                                              DXGI_MWA_NO_ALT_ENTER);
    }

    hr = base.As(&device.swapchain);
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12: swapchain does not expose IDXGISwapChain4");
        return false;
    }

    // Opt into the format's colour space explicitly so an HDR display is driven
    // in PQ and an SDR one in sRGB, rather than relying on the default.
    const HRESULT cs_hr = device.swapchain->SetColorSpace1(device.swapchain_colorspace);
    if (FAILED(cs_hr)) {
        LOG_WARNING(Graphics, "D3D12: SetColorSpace1 failed: {}; using the default colour space",
                    HresultToString(cs_hr));
        device.hdr_enabled = false;
        device.swapchain_colorspace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }

    LOG_INFO(Graphics, "D3D12 swapchain: {}x{}, {} buffers, {}, {}{}",
             width, height, device.buffer_count,
             device.swapchain_format == DXGI_FORMAT_R10G10B10A2_UNORM ? "R10G10B10A2" : "R8G8B8A8",
             device.hdr_enabled ? "HDR10" : "SDR",
             device.tearing_enabled ? ", tearing" : "");
    return true;
}

bool ResizeSwapchain(Device& device, u32 width, u32 height) {
    if (!device.swapchain || width == 0 || height == 0) {
        return false;
    }
    if (width == device.swapchain_width && height == device.swapchain_height) {
        return true;
    }
    for (u32 i = 0; i < device.buffer_count; ++i) {
        // The frame graph must release its references before ResizeBuffers.
        ComPtr<ID3D12Resource> buffer;
        if (SUCCEEDED(device.swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer)))) {
            buffer.Reset();
        }
    }
    const UINT flags = device.tearing_enabled ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    const HRESULT hr = device.swapchain->ResizeBuffers(device.buffer_count, width, height,
                                                       device.swapchain_format, flags);
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12: ResizeBuffers({}x{}) failed: {}", width, height,
                     HresultToString(hr));
        return false;
    }
    device.swapchain_width = width;
    device.swapchain_height = height;
    return true;
}

bool Present(Device& device, bool vsync) {
    if (!device.swapchain) {
        return false;
    }
    const UINT flags =
        (!vsync && device.tearing_enabled) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT hr = device.swapchain->Present(vsync ? 1 : 0, flags);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
        return true;
    }
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12: Present failed: {}", HresultToString(hr));
        return false;
    }
    return true;
}

void WaitForFence(Device& device, u64 value) {
    if (!device.timeline_fence || !device.timeline_event) {
        return;
    }
    if (device.timeline_fence->GetCompletedValue() >= value) {
        return;
    }
    device.timeline_fence->SetEventOnCompletion(value, device.timeline_event);
    WaitForSingleObjectEx(device.timeline_event, INFINITE, FALSE);
}

u64 SignalFence(Device& device) {
    const u64 value = ++device.timeline_value;
    if (device.graphics_queue && device.timeline_fence) {
        device.graphics_queue->Signal(device.timeline_fence.Get(), value);
    }
    return value;
}

} // namespace Gr2Port::D3D12
