# GR2toXBOX Implementation Plan

<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

This document is the engineering map for turning upstream
[GR2port](https://github.com/junminlee2004/GR2port) into a production-grade
**Xbox Series X|S Retail Dev Mode (UWP) + Windows Direct3D 12** port.

It records what upstream actually contains (the audit), the non-negotiable Xbox
UWP constraints that every change must satisfy, and the file-by-file plan for
each phase. It is deliberately concrete: every path below exists or is created
by a specific commit.

---

## 1. Baseline audit

**Imported upstream commit:** `49f763a72f0079a4502c2aa76d6147fee353b065`
(`GR2port: keep Korean and Chinese map titles centred when Cross confirms`),
imported with full history: 5,600 commits, tags `v1.00`–`v1.05`,
`LICENSE` (GPL-2.0), `LICENSES/`, `REUSE.toml`, and 45 gitlinked submodules in
`.gitmodules`.

**Tree:** 2,938 tracked files, ~40 MiB of source. It is a shadPS4-derived
emulator core with a bespoke, hand-written Vulkan renderer for Gravity Rush 2.

### 1.1 Files audited

| File | Finding |
| --- | --- |
| `CMakeLists.txt` (1,331 lines) | C++23, CMake ≥ 3.24. Windows path forces `-march` only on GNU; MSVC path calls `clang_rt.builtins-x86_64.lib`; **line 1241 `target_link_options(gr2port PRIVATE /DYNAMICBASE:NO)`**, **line 1255 `/BASE:0x700000000000`**, plus `/STACK:0x200000,0x200000` and `/GS-`. These three options are illegal for a Store/UWP package. |
| `.github/workflows/build.yml` (231 lines) | Jobs `clang-format`, `get-info`, `windows-sdl` (windows-2025-vs2026, clang-cl + ccache + Ninja), `linux-sdl`, `pre-release`. **Line 166 is a hardcoded `github.repository == 'junminlee2004/GR2port'` gate** that suppresses releases on forks. |
| `src/GR2port.cpp` (813 lines) | `Emulator::Emulator()` calls `SetPriorityClass` and `SetErrorMode` (lines 76–77) unguarded; `Emulator::Relaunch` (line 681+) uses **`CreateProcessW`** (line 704) to restart the process for settings reloads. |
| `src/core/address_space.cpp` (937 lines) | `ToWindowsProt()` (line 70) can return **`PAGE_EXECUTE_READWRITE`**; `static u64 BackingSize = ORBIS_KERNEL_TOTAL_MEM_DEV_PRO + ORBIS_KERNEL_FLEXIBLE_MEMORY_SIZE` = **7936 + 512 MiB ≈ 8.25 GiB**, far past the 5,120 MiB Xbox cap; `Impl` uses `VirtualAlloc2`/`MapViewOfFile3`/`CreateFileMapping2` and `GetModuleHandleW(L"ntdll.dll")` + `RtlGetVersion` (line 120). |
| `src/core/memory.h` (447 lines) | `MemoryProt` enum (`CpuExec` bit), `VMAType`, `VirtualMemoryArea`, and the whole `MemoryManager` surface that a clamp must go through. |
| `src/core/memory.cpp` | `SetupMemoryRegions()` picks `ORBIS_KERNEL_TOTAL_MEM_DEV_PRO` for devkit mode; `CreateArea()`/`MapMemory()` map with the guest's raw protection, including execute. |
| `src/core/module.cpp` (≈ 470 lines read) | ELF segments are mapped with the *guest's* `PF_EXEC` protection **before** relocation and static patching run — the exact W^X hazard the brief calls out. |
| `src/core/linker.cpp` (530 lines) | Loads/prepares modules; no protection transitions of its own (it delegates to `memory->MapMemory`). |
| `src/core/cpu_patches.cpp` (3,113 lines) | `PatchRedZoneMemoryInstructions()` (line 3017) and `PatchCpuInstructionsStatically()` (line 3022) rewrite guest code in place while it is mapped writable, and `TryPatchJit` (line 3040) patches lazily from an access-violation handler. |
| `src/core/tls.cpp` (221 lines) | Guest TLS via `FS`/`GS` segment bases — no UWP-specific problem, but requires the code page to be executable after init. |
| `src/core/game_check/game_patches.h` (105 lines) | Resolution patch catalogue includes `RenderSize{"5K", 2880}` and `RenderSize{"8K", 4320}`; `ApplySettings()` "raises the extra direct memory to what the chosen resolution's patch needs" — must be clamped on Xbox. |
| `src/graphics/gr2port/gr2port.cmake` (349 lines) | 203 `gr2port_add_shader()` calls: GLSL → `glslang-standalone --target-env vulkan1.3` → `spirv-opt -O` → embedded `.h` via `shader_header.cmake`. |
| `src/graphics/gr2port/device.h` (47 lines) | Vulkan-only `Device` (instance, surface, physical device, `VK_EXT_memory_budget`, `VK_EXT_device_fault`, `VK_EXT_extended_dynamic_state3`, pipeline cache). |
| `src/graphics/gr2port/pipeline.h` (128 lines) | `GraphicsPipelineInfo` with `vk::Format`, `vk::CompareOp::eGreater` (reverse-Z), dynamic rendering, dynamic topology/blend; `Pipeline` = `vk::Pipeline` + `vk::PipelineLayout`. |
| `src/graphics/gr2port/shaders/world.vert` (81 lines) | Canonical shader: `uint64_t vertices/skin/palette` GPU pointers inside a `Draw` struct, `buffer_reference` for the draw table, `gl_BaseInstance` as the draw index, and `clip.z = 0.5 * (clip.w - clip.z)` (GL → reverse-Z conversion). |
| `src/graphics/gr2port/shaders/*` | **124 shader files use `buffer_reference`; 107 sites use `gl_BaseInstance`**; 200 GLSL sources total, including `contour.geom`, `ui_stroke.geom`, `ui_text.geom`, `leaf.tesc/.tese`, `leaf_shadow.tese/.geom`. |
| `src/graphics/host_shaders/tiling.comp` | AMD GCN texture detiling compute shader (Vulkan GLSL). |

### 1.2 Consequences for the Xbox target

1. The renderer is **Vulkan end-to-end**; Xbox has no Vulkan driver, so a D3D12
   backend is not a port of a few call sites — it is a second backend with the
   same game-facing contract.
2. The GPU contract itself is Vulkan-shaped in the *shaders*: 64-bit
   `buffer_reference` pointers and `gl_BaseInstance`. Both have no HLSL
   equivalent and must be designed out, not "translated".
3. Memory sizing assumes an 8 GiB PS4 Pro devkit and will fail on Xbox with
   `ERROR_COMMITMENT_LIMIT`.
4. Module loading maps guest code **writable and executable at once**; Xbox
   AppContainer forbids RWX, so the load path must become strictly W^X.
5. `CreateProcessW`, `SetPriorityClass`, `SetErrorMode`, `ntdll` version probes
   and the desktop relaunch path do not exist under `WINAPI_PARTITION_APP`.

---

## 2. Non-negotiable constraints (definition of done)

| # | Rule | Enforcement point |
| --- | --- | --- |
| C1 | Direct3D 12 only on Xbox; Feature Level 12_1/12_2; SM 6.6 DXIL compiled offline by `dxc` | `cmake/GR2D3D12Shaders.cmake`, `src/graphics/gr2port_d3d12/` |
| C2 | Native x86-64 guest execution with **strict W^X**: map RW → relocate/patch → `FlushInstructionCache` → `PAGE_EXECUTE_READ` | `src/core/uwp_wx.*`, `src/core/memory.cpp`, `src/core/module.cpp` |
| C3 | Total guest commit clamped to 3,200 MiB, D3D12 heap budget 1,200 MiB, process peak < 4,600 MiB | `src/core/uwp_memory.*`, `src/graphics/gr2port_d3d12/allocator.cpp` |
| C4 | `/DYNAMICBASE /HIGHENTROPYVA /APPCONTAINER` on UWP; no fixed `/BASE` | `CMakeLists.txt` UWP branch |
| C5 | No subprocesses, no desktop-only Win32 under `WINAPI_PARTITION_APP` | `src/GR2port.cpp` guarded paths |
| C6 | No Sony binaries in the tree, ever | `.gitignore`, `tools/ci/check_no_game_data.sh` |
| C7 | Every shader compiles or CI fails loudly | `cmake/GR2D3D12Shaders.cmake` + `xbox-uwp-msix` job |

---

## 3. Phase 0 — bootstrap (this commit series)

| Item | Path | State |
| --- | --- | --- |
| Upstream tree + history | repository root | done (`76c4977a`) |
| Copyright/build guardrails | `.gitignore` | done |
| Audit + plan | `docs/IMPLEMENTATION_PLAN.md` | this file |
| CI guard | `tools/ci/check_no_game_data.sh` | planned |

---

## 4. Phase 1 — Direct3D 12 renderer

### 4.1 New backend tree

```
src/graphics/gr2port_d3d12/
├── device.h / device.cpp            # ID3D12Device2+, queues, DXGI factory, flip-model swapchain
├── allocator.h / allocator.cpp      # D3D12MA wrapper + 1,200 MiB budget + LRU eviction
├── descriptors.h / descriptors.cpp  # SM 6.6 bindless heaps (ResourceDescriptorHeap[])
├── arena.h / arena.cpp              # Unified Bindless Arena: ByteAddressBuffer, 32-bit offsets
├── pipeline.h / pipeline.cpp        # 64-bit state-key PSO cache + ID3D12PipelineLibrary1
├── command.h / command.cpp          # ring-buffered allocators, timeline fences, ExecuteIndirect
├── renderer.h / renderer.cpp        # frame graph reproduction of the Vulkan pass order
├── presenter.h / presenter.cpp      # CoreWindow / HWND presentation, VRR + tearing
├── upscaler.h / upscaler.cpp        # FidelityFX FSR 3.1 D3D12 path
├── d3d12ma/                         # vendored allocator (source pair, not a submodule)
└── shaders/
    ├── common/scene.hlsli           # Draw/Draws/Materials/Decals mirror of scene.glsl
    ├── common/arena.hlsli           # ByteAddressBuffer accessors replacing buffer_reference
    ├── common/vertex.hlsli          # LoadPosition/LoadNormal/... over 32-bit offsets
    ├── common/skin.hlsli            # SkinPosition/SkinDirection over arena offsets
    ├── <stage files>.hlsl           # ~155 shaders, 1:1 with gr2port.cmake entries
    └── host/tiling.hlsl             # GCN detiling port of host_shaders/tiling.comp
```

### 4.2 The two Vulkan-isms

**64-bit `buffer_reference` → Unified Bindless Arena.**

Upstream passes raw device addresses (`uint64_t vertices/skin/palette`, and a
`buffer_reference` draw table). D3D12 cannot follow arbitrary pointers, so:

* one large `ByteAddressBuffer` arena (one SRV) holds scene, geometry, skin,
  palette and draw data;
* every pointer becomes a **32-bit byte offset** (`ArenaOffset`), which addresses
  the whole < 5 GiB arena losslessly;
* the arena index is passed as a root constant, and
  `ResourceDescriptorHeap[NonUniformResourceIndex(arena_idx)]` selects the heap
  slot, so multiple arenas work without rebinding;
* `common/arena.hlsli` provides `ArenaLoad<uint>(offset)` helpers that mirror
  the GLSL `LoadPosition`/`LoadTexcoord`/`LoadNormal`/`SkinPosition` functions
  one-for-one, so the shader bodies stay recognisably upstream.

**`gl_BaseInstance` → root constant.**

HLSL `SV_InstanceID` restarts at 0 for every draw. The draw index that GLSL took
from `gl_BaseInstance` is passed as `uint base_instance : register(b0, space0)`
on direct draws, and as an inline root constant inside the
`ID3D12CommandSignature` for `ExecuteIndirect` multi-draws.

### 4.3 Shader work

1. `cmake/GR2D3D12Shaders.cmake` adds a `gr2port_add_hlsl_shader()` function that
   runs `dxc.exe -T <stage>_6_6 -HV 2021 -O3 -Qstrip_reflect -Fo` and converts the
   DXIL container into an embedded header (same pattern as `shader_header.cmake`).
2. Every entry in `gr2port.cmake` gets an HLSL counterpart; the list is generated
   by `tools/shaders/glsl_to_hlsl_manifest.py` so nothing is forgotten and CI can
   diff GLSL vs HLSL coverage.
3. Geometry → HLSL geometry shaders; `leaf.tesc`/`leaf.tese` → hull/domain with
   `[domain("tri")]` + `SV_DomainLocation`; `ui_text.geom` emits triangle strips
   with `TriangleStream<...>`.
4. Reverse-Z is preserved everywhere: DXGI depth is already `[1, 0]` with
   `D3D12_COMPARISON_FUNC_GREATER`, and the `0.5 * (w - z)` GL→reverse-Z
   conversion in the vertex shaders is replaced by a direct D3D clip-space
   transform.

### 4.4 CMake integration

`CMakeLists.txt` gains:

```cmake
set(GR2_RENDERER "VULKAN" CACHE STRING "Renderer backend: VULKAN or D3D12")
option(GR2_XBOX_UWP "Build the Xbox Series X|S UWP target" OFF)
```

and `src/graphics/gr2port_d3d12/gr2port_d3d12.cmake` is included only when
`GR2_RENDERER STREQUAL "D3D12"`. The Vulkan backend's files are excluded from a
D3D12 build so the same game hooks (`capture.cpp`, `particle_capture.cpp`,
`ui_capture.cpp`, `scene_ring.cpp`) compile against the D3D12 types.

---

## 5. Phase 2 — Xbox UWP shell

```
src/uwp/
├── uwp_main.cpp          # IFrameworkView, CoreApplication, CoreWindow, lifecycle
├── app.h / app.cpp       # Application singleton, in-process boot handoff
├── launcher.h / launcher.cpp  # 10-foot controller-driven ImGui launcher (D3D12)
├── input_xbox.h/.cpp     # Windows.Gaming.Input::Gamepad + XInput fallback + rumble
├── chords.h/.cpp        # View+DPad/RStick touchpad & gyro remap chords
├── audio_uwp.h/.cpp      # XAudio2 / WASAPI WinRT backend selection
└── Package.appxmanifest  # TargetDeviceFamily, capabilities, visual assets
```

* `Emulator::Relaunch` on UWP becomes `Uwp::App::RebootInProcess(settings)` — a
  clean teardown/rebuild of the emulator core without spawning a process.
* Launcher lets the player pick `E:\GR2`, `D:\GR2`, `LocalState\GR2` or a `.zar`
  archive, choose Series X / Series S presets (4K60, 1440p120, 1080p60,
  1440p60+FSR) and launch in-process.
* Controller chords map touchpad flicks/taps and gyro to the Xbox pad; native
  Xbox prompts come from upstream's existing prompt pack.

---

## 6. Phase 3 — GitHub Actions

`.github/workflows/build-xbox-uwp.yml`:

| Job | Runner | Purpose |
| --- | --- | --- |
| `windows-d3d12` | `windows-2022` | Desktop Win64 D3D12 build; proves every HLSL SM 6.6 shader compiles → `GR2port-Win64-D3D12.zip` |
| `xbox-uwp-msix` | `windows-2022` | `CMAKE_SYSTEM_NAME=WindowsStore`, `GR2_XBOX_UWP=ON`, `GR2_RENDERER=D3D12`; stage manifest + assets, `makepri`, `makeappx`, self-signed cert, `signtool`, zip `.msix` + `.cer` + `QUICKSTART-XBOX.txt` |
| `release` | `ubuntu-latest` | Attach artifacts to a Pre-Release on `main`, full Release on `v*` |

Upstream traps fixed: the hardcoded `github.repository` gate, and the fact that
upstream CI builds only Vulkan.

---

## 7. Phase 4 — documentation and governance

`README.md` (badges, overview, preset matrix, quick start, chord table, legal),
`docs/XBOX_DEV_MODE_GUIDE.md`, `docs/ARCHITECTURE_D3D12_UWP.md`,
`docs/BUILDING_FROM_SOURCE.md`, `docs/TROUBLESHOOTING.md`, `CHANGELOG.md`,
`CONTRIBUTING.md`, `SECURITY.md`, `.github/PULL_REQUEST_TEMPLATE.md`,
`.github/ISSUE_TEMPLATE/xbox_bug_report.yml`,
`.github/ISSUE_TEMPLATE/shader_visual_issue.yml`, plus repository description and
topics.

---

## 8. Honest status reporting

This plan is executed in reviewable commits. Because the porting surface is
large (≈ 155 shaders, a full renderer, a UWP host), each commit states exactly
what is complete and what is still outstanding; `docs/PORT_STATUS.md` is the
single source of truth for the remaining work, and it is updated in the same
commit as the code it describes.
