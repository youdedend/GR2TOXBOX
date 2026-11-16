# Port status

<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

This file is the single honest answer to "how far along is this?". It is updated
in the same commit as the work it describes. If it disagrees with the README, this
file is right.

**Last updated:** 2026-11-15

---

## Summary

| Area | State |
| --- | --- |
| Xbox toolchain, packaging and CI | **Done and passing** |
| Strict W^X guest execution | **Done, unit-tested** |
| 5,120 MiB memory budget | **Done, unit-tested** |
| AppContainer Win32 shims | **Done** |
| Direct3D 12 device / allocator / descriptors / arena | **Done** |
| HLSL SM 6.4 shader pipeline (`dxc`, fail-loud) | **Done** |
| D3D12 command ring, root signature, PSO cache | **Done** |
| Direct3D 12 frame graph (passes, submits, presenter) | **In progress** |
| HLSL shader port | **In progress — 71 of 184 engine stages + 1/1 detile; 72 DXC/Slang-verified** |
| UWP shell (`IFrameworkView`, launcher, gamepad) | **In progress** |
| **UWP package builds at all** | **Yes — SDL3 replaced by an in-tree shim** |
| Gamepad chord remapping | **Done, unit-tested** |
| Audio backend for UWP | **Done — XAudio2 behind the shim** |
| Third-party dependencies under UWP | **Done — every one builds or is gated with a reason** |
| Playable on a console | **No** |

---

## What works today

### Toolchain and CI

* `.github/workflows/build-xbox-uwp.yml` runs on every push, PR and `v*` tag:
  a source guard (no game data), a Windows desktop D3D12 build, the UWP MSIX
  build with `makeappx`/`signtool`, and a release job.
* The UWP build genuinely configures with `CMAKE_SYSTEM_NAME=WindowsStore`,
  `GR2_XBOX_UWP=ON`, `GR2_RENDERER=D3D12`.
* HLSL shaders compile with `dxc -T *_6_4 -HV 2021 -WX`. A shader warning is a
  build failure. SM 6.4 is the ceiling the Xbox Series X|S driver actually
  honours: SM 6.6's `ResourceDescriptorHeap[]`/`SamplerDescriptorHeap[]` map onto
  Resource Binding Tier 3 imperfectly and the driver intermittently faults when
  they are indexed dynamically.
* **ccache in both Windows jobs, saved even when the job fails**, and the two
  jobs run in parallel. A superseded run is left to finish rather than cancelled:
  cancelling a cold build at minute 18 discards every object it had compiled.
* **`tools/ci/local_precheck.sh` runs in `source-guard` and is meant to be run
  before every push.** It compiles the host-portable units, the console-only
  branches under `GR2_XBOX_UWP`, and the D3D12 backend against
  `tools/ci/d3d12_stub`, a header stub that lets `g++ -fsyntax-only` type-check
  D3D12 code with no Windows SDK. It also runs both self-tests. A missing include,
  a wrong method name or a field that does not exist is now caught in seconds.
* The shader list is generated (`tools/shaders/generate_d3d12_manifest.py`,
  checked by `source-guard`), so a shader cannot be ported but never compiled.

### Core (console correctness)

* **Strict W^X**: guest code is committed `PAGE_READWRITE`, populated, patched,
  flushed and sealed `PAGE_EXECUTE_READ`. `PAGE_EXECUTE_READWRITE` is never
  requested on a console build. Lazy patches reopen and reseal. Desktop runs the
  same lifecycle.
* **Memory budget**: guest commit clamped to 3,200 MiB, D3D12 heap to 1,200 MiB,
  process target under 4,600 MiB of the 5,120 MiB pool. The backing file is sized
  from the console ceiling, not the 8.25 GiB devkit figure.
* **No subprocesses**: `Relaunch` refuses and points at an in-process hand-off;
  `SetPriorityClass`, `SetErrorMode` and the `ntdll` version probe are
  desktop-only; memory APIs go through `*FromApp`.
* **Tests**: `xbox_memory_budget` and `xbox_chords` run under CTest.

### Third-party dependencies under UWP

Every vendored dependency either builds for the package or is left out with a
stated reason. The ones that needed work:

* **`_WIN32_WINNT` now precedes `add_subdirectory(externals)`.** It was defined
  after it, so only `gr2port` saw it and third-party targets built for the wrong
  platform. `miniupnpc` was the visible casualty: its `minissdpc.c` picks
  `GetBestInterfaceEx` over the desktop-only `GetBestInterface` on it, and has a
  UWP branch for `in6addr_any` that was equally unreachable. Nothing else from
  that block moved up — `WIN32_LEAN_AND_MEAN` there stopped `windows.h` including
  `winsock.h` and broke `libusb`'s `io.c`, which is why only the SDK version
  definition lives above the externals.
* **`openal-soft`** sets `ALSOFT_UWP` itself and already drops avrt/DSound/WinMM
  there; this project's own `ALSOFT_BACKEND_WASAPI` force was the breakage, and is
  now desktop-only.
* **`wepoll`** needs one guard (`SetHandleInformation` on the `EPOLL_CLOEXEC`
  path, which Windows never takes). It travels as
  `tools/ci/patches/wepoll-uwp.patch`, applied at configure time and only for
  UWP, because a dirty submodule would not reach CI and patching a dependency on
  every platform would be quietly forking it.
* **`libusb`** has no UWP answer — its Windows port is WinUSB plus
  SetupAPI/CfgMgr32 — so it is not built there. `src/core/libraries/usbd/usbd_uwp.cpp`
  answers `libSceUsbd` with an empty device list instead, which is what a console
  with nothing in its USB ports reports; all 65 NIDs match the desktop table
  exactly. The figure-portal emulation (Skylanders, Infinity, Dimensions) is
  consequently absent on the console, since a portal is a USB peripheral.
* **`hwinfo`** is headers-only (WMI is not in the onecore surface), and the
  desktop-only consumers are guarded.

### The UWP platform layer

SDL 3 removed WinRT/UWP support during its development and has never had it back
— its own `SDL_platform_defines.h` says so in an `#error`. The emulator's frontend
is SDL3 end to end, and every one of those files compiles into the package, so the
package could not get past vendored SDL.

`src/uwp/sdl3_shim/` is the answer: a header-and-source stand-in for exactly the
SDL calls GR2port makes, put on the include path **only** when `GR2_XBOX_UWP=ON`.

* The window, event loop and timers are `WinRT CoreWindow`; the gamepad is
  `Windows.Gaming.Input`; the audio device is XAudio2 (`xaudio2_9`); the iostreams
  are the WinRT file APIs. ImGui's platform layer rides on the same calls.
* It replaces one real SDL header (`SDL_platform_defines.h`, the one carrying the
  `#error`) and supplies its own declarations for the rest, so the 36
  `#include <SDL3/...>` sites across the tree are untouched.
* Desktop keeps upstream SDL3 unmodified, and the same source files still compile
  against it — `local_precheck.sh` builds the shim against SDL's *own* headers, so
  a wrong return type or a swapped parameter order fails locally in a second.
* Known blind spot: `sdl_gamepad_uwp.cpp` and `sdl_audio_uwp.cpp` include WinRT
  headers, which no Linux host has, so those two are compile-checked only by the
  Windows runner.

### Renderer foundation

* `ID3D12Device8` on FL 12_0, DIRECT + COMPUTE queues, timeline fence,
  flip-model swapchain for both HWND and CoreWindow, tearing/HDR/color-space.
* Bindless heaps (65,536 SRV/UAV, 256 samplers) with null descriptors.
* Unified Bindless Arena (5 arenas, 1,088 MiB) with `{arena, uint32 offset}`
  pointers replacing `buffer_reference`.
* D3D12MA (vendored v3.0.1) behind a 1,200 MiB budget with 80%/90% eviction.
* Shared HLSL headers: `arena.hlsli`, `vertex.hlsli`, `scene.hlsli`,
  `texel.hlsli`, `surface.hlsli`, `grade.hlsli`, `light.hlsli`, `fog.hlsli`.
* **Root signature and pipelines** (`pipeline.*`): one root signature for the
  whole renderer -- three root-constant blocks plus twelve descriptor-table
  parameters in distinct `register(..., spaceN)` spaces, which is what replaces
  upstream's descriptor arrays. Shaders reach them through `Texture2D`/
  `SamplerState`/`ByteAddressBuffer` arrays indexed with
  `NonUniformResourceIndex(...)`, the Resource Binding Tier 3 pattern. `GraphicsPipelineDesc` in the port's own terms, and a
  `PipelineCache` keyed by a hash of the state and the shader containers. On
  desktop that cache is backed by `ID3D12PipelineLibrary` when the device offers
  one; on UWP it is **never** created -- `CreatePipelineLibrary` is unsupported
  there, so the UWP path caches compiled DXIL blobs on disk instead.
* **Command ring** (`command.*`): one allocator, list and fence value per frame
  in flight, rotated; explicit transitions with named helpers; a render-pass
  begin/end pair that cannot inherit a target from the previous pass; full-screen
  draws, dispatches and the frame's submit.

### Presenter and console session (implemented, not hardware-verified)

* Actual HWND/CoreWindow clear/ImGui presentation, GPU-fenced uploads and resize
  handling are wired to the existing device, root and command ring.
* RunGame blocks in the controller launcher; A chooses a folder with the system
  picker, X reselects, A/Menu boots a GameCheck-validated dump through the normal
  in-process emulator path. FutureAccessList plus LocalState persist the choice.
* UI-thread events/gamepads/presentation keep pumping before and after handoff;
  the existing early 5 GiB arena is adopted. Blackbox mirrors to game/gr2_logs.
* Local precheck compiles presenter/ImGui GPU code and WinRT launcher contracts,
  and executes owned-snapshot lifetime/concurrency tests. This is not an Xbox
  boot, device-removal, resize, suspend/resume, or game-rendering test.

### Input

* Xbox chord layer mapping View+LB/RB/B/L3/Y (and optionally the D-Pad) onto the
  DualShock 4 touchpad and motion outputs the game binds, with edge triggering
  and button swallowing.

---

## What does not work yet

1. **113 of 184 engine shaders remain.** Coverage is 71 engine counterparts
   plus the separate 1/1 GCN detile kernel (72 stages). Batch 3 adds skinning,
   soft-body simulation/writeback, light-grid/world-lines and branch/grass/leaf
   simulation and shading, including leaf hull/domain/geometry and shadow
   domain/geometry. Both Microsoft DXC and Slang pass all 72 stages locally.
   Compilation is not GPU/runtime parity proof. See [Batch 3](SHADER_BATCH3.md).

2. **The scene frame graph.** The real swapchain clear/ImGui presenter now exists
   in `backend_surface.cpp`: HWND/CoreWindow RTVs, barriers, ring submission,
   Present, resize and shutdown. Immutable owned ImGui snapshots and RGBA8 GPU
   uploads replace raw draw-data pointers. The game's pass order, scene resources,
   uploads and draw execution remain unimplemented. No hardware rendering or
   successful eboot progress has been demonstrated.
3. **The D3D12 capture.** The scene capture's API-agnostic half is built and
   running in the D3D12 job; what is missing is the renderer side of it, and
   `src/graphics/gr2port_d3d12/backend_capture.cpp` now defines the twelve entry
   points that were failing at link so the gap is a stated one rather than an
   unresolved external: `Capture::{OnFrame,OnFrameEnd,OnParticleList,OnParticleSetup,Frames,ToggleCapture}`,
   `Particles::QueueCurve`, `Meshes::{Install,Get}`, `Textures::{Install,Get,GetBySharp}`,
   and the pipeline pair `WaitForPipelineBuilds` / `PipelineBuildProgress`.
   `Meshes::Install` and `Textures::Install` answer false, which stops the
   startup sequence before the renderer claims to have started, and prints one
   line naming both. The vertex upload and the texture/descriptor path are the
   work that turns those false into true.

4. **ImGuiRelay.** ~~Resolved~~ — `backend_surface.cpp` and `imgui_relay.cpp`
   supply the frontend-facing entry points without naming a graphics API, so the
   desktop D3D12 link no longer wants the five ImGuiRelay symbols.
5. **Audio.** ~~Resolved for the console~~ — the shim's audio device is XAudio2
   (`sdl_audio_uwp.cpp`, linked against `xaudio2_9`), and `openal-soft` no longer
   forces `ALSOFT_BACKEND_WASAPI` on UWP, so it falls through to its null backend
   there. The desktop build keeps WASAPI.
6. **PSO cache serialisation** (`ID3D12PipelineLibrary1`) is designed, not
   implemented.
7. **Fsr 3.1 on D3D12** is not wired up.

**Therefore: no released artifact can play the game yet.** The `.msix` CI
produces is a real, signed, installable package — of a build that does not yet
have a complete renderer. The README says so plainly.

---

## Order of work

0. ~~Decide what the UWP package is for~~ **Settled: an in-tree SDL3 shim.**
   `src/uwp/sdl3_shim/` supplies the SDL window, event, timer, gamepad, audio
   and iostream calls GR2port makes, over WinRT `CoreWindow`,
   `Windows.Gaming.Input` and XAudio2, on the include path only when
   `GR2_XBOX_UWP=ON`. Desktop keeps upstream SDL3 untouched, and the 36
   `#include <SDL3/...>` sites did not have to change.
1. ~~`command.*` + `pipeline.*`~~ **Done.**
2. Port the shader set in dependency order: the composite chain (done), the
   world/character families behind `scene.hlsli` (in progress), then the `pfx/`
   particle family, then geometry and tessellation stages, then the compute
   passes.
3. `presenter.*` + `renderer.*`: reproduce upstream's pass order
   (`frame_graph_draws.cpp` is the reference) against the arena.
4. Hardware-test the blocking controller launcher and in-process boot bridge.
   It now restores FutureAccessList/LocalState, opens a UI-thread FolderPicker,
   validates with GameCheck and hands off to Core::Emulator::Run while continuing
   CoreWindow pumping/presentation. Relaunch and actual game progress still need
   runtime validation; the scene renderer is not playable.
5. Audio, FSR 3.1, preset tuning.

---

## How to help

The highest-leverage contribution right now is a shader port, because each one is
self-contained and CI tells you immediately whether it compiles:

1. Pick a stage `tools/shaders/check_shader_coverage.py --missing` lists.
2. Port it following `world.vert.hlsl` (structure-preserving, comment the two
   mechanical changes: arena refs, `base_instance`).
3. `python3 tools/shaders/check_shader_coverage.py` should count it.
4. Open a PR — the CI job compiles it with `-WX`.

