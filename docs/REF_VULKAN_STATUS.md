# Temporary UWP Vulkan reference path — status

This document records an implementation blocker, not a successful reference path.
The shipping configuration remains `GR2_XBOX_UWP=ON` with `GR2_RENDERER=D3D12`.

- Vulkan built: no. The UWP CMake configuration currently rejects `GR2_RENDERER=VULKAN`.
- Dozen DLL loaded: no. `vulkan_dzn.dll` is not built or staged in this repository's MSIX workflow.
- Vulkan instance and device created: no. There is no packaged-ICD dispatch or UWP Vulkan presentation bridge in this build.
- First frame presented: no. The only UWP presenter is the native D3D12 CoreWindow swapchain.
- Stopping HRESULT/VkResult: none; no Vulkan call was reached.

The recipe in `youdedend/Test-project` builds Mesa Dozen at
`Cosmo/mesa_ai_slop_version@a76cd3caa23f14839f15259cd3abbfa400b1b217` with MSVC 14.44,
loads it with `LoadPackagedLibrary`, and uses a custom D3D12 CoreWindow shared-image
presentation bridge. Importing only the DLL is insufficient: the current GR2port
Vulkan backend obtains its loader/surface through SDL and has no such UWP bridge.
A real diagnostic build must implement that bridge behind a default-off CMake
option, add a separately labelled launcher entry, and prefix all its output with
`REF:`. It must not change the native D3D12 shipping startup or release policy.

This reference-path status does not validate hand-ported HLSL. No hardware test
was performed, and no Vulkan runtime success is claimed.
