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
| Direct3D 12 frame graph (passes, submits, presenter) | **5A + 5B core passes implemented; runtime unverified; 5C remains** |
| HLSL shader port | **In progress — 96 of 184 engine stages + 1/1 detile; 97 DXC/Slang-verified** |
| UWP shell (`IFrameworkView`, launcher, gamepad) | **In progress — first-frame launcher, controller focus/actions, explicit USB-root scan, picker-token restore, selected-root validation and in-process boot; hardware unverified** |
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

1. **88 of 184 engine shaders remain.** Coverage is 96 engine counterparts
   plus the separate 1/1 GCN detile kernel (97 compiled stages). Slice 4A adds
   all 25 `pfx/ui_*` stages: nine vertex, one stroke geometry, fifteen fragment.
   The shared `common/ui_pfx.hlsli` keeps the arena/draw roots, encoded-address
   decoding and non-uniform texture/sampler selection at SM 6.4. Microsoft DXC
   passes 97/97 with `-T *_6_4 -HV 2021 -WX`; the independent Slang front-end
   also passes all 97 locally. The global root remains 37 DWORDs: b0 = 20 bytes,
   b1 <= 64 bytes, b2 = 16 bytes, twelve tables in spaces 100–111. These are
   compilation/ABI checks, **not GPU/runtime parity proof**. Earlier shader work
   is described in [Batch 3](SHADER_BATCH3.md).

2. **The scene frame graph.** The real swapchain clear/ImGui presenter now exists
   in `backend_surface.cpp`: HWND/CoreWindow RTVs, barriers, ring submission,
   Present, resize and shutdown. Immutable owned ImGui snapshots and RGBA8 GPU
   uploads replace raw draw-data pointers. UI drawing and the Slice 5A core
   deferred/forward/post-process order are implemented below; the full frame graph
   is not complete. No hardware rendering or successful eboot progress has been
   demonstrated.
3. **The D3D12 capture consumer (Slice 4A).** All of the shared `capture.cpp`
   now builds on D3D12 as well as Vulkan; its six real Capture entry points
   replace the old backend stubs. `Upscaler::Extent` / `RenderSize(width,height)`
   are API-neutral; Vulkan forwards to the unchanged `RenderExtent` API, D3D12
   uses 1:1 dimensions pending FSR. A 120 MiB (five 24 MiB slots) CPU-mapped,
   GPU-readable, read-only Scene Arena is attached during Surface creation with
   an encoded `{arena,offset}` address, **not** a raw GPU VA. The presenter polls
   `TryTake(seen,packet)` without waiting for a game frame. Slice 4A originally
   released packets without GPU reads; since 4B-2 all consumed drawing packets
   remain pinned through their graphics submission fence, including in Slice 5A.

   `local_precheck.sh` compiles shared capture on both API paths, checks twenty
   actual Capture/backend symbol definitions with a relocatable link (no duplicate
   Capture definitions), and runs the production SceneRing in normal and
   ASan/UBSan/leak-detection builds. Tests cover empty non-blocking polls, encoded
   addresses/payload roundtrips, newest and kept-frame ordering, seen watermarks,
   slot reservation/release, reattach and 20,000 concurrent kept packets. Attach
   and teardown require capture producer/GPU quiescence, as documented by the ring.
   Linux D3D12 syntax checks use SDK stubs; Windows/UWP compilation and packaging
   are checked by Actions, not by those stubs.

   **Slice 4B-1: mesh/texture residency is now implemented.** Shared `.inc`
   units keep the original Vulkan .gfx shape walk, bounds/weights/chunks, GNF
   parsing, address lookup, prefetch and hook installation. Both installs now
   report the actual `Hooked`/`Chained` results instead of unconditional false,
   allowing startup to proceed to Movies, FrameGraphDraws and Renderer. This
   control-flow change is compiled and link-audited; game execution is unverified.

   D3D12 adds a 256 MiB GPU-only Geometry Arena, 16 MiB Scratch Arena (GPU-only
   and UAV-backed since 5A),
   and the existing 120 MiB Scene Arena. A D3D12MA R16 index buffer has 48M
   indices on the 1,200 MiB console budget (64M on larger desktop budgets).
   Free-range allocation reuses geometry/index regions only after the graphics
   timeline fence and capture grace period have passed. Mesh uploads are capped
   at 64 MiB per frame; index/skin bounds are checked before publishing readiness.
   Geometry and index copies are transitioned back to GPU-readable states first.

   Texture handles below 32480 use descriptors `handle` / `handle + 32768`;
   reserved external UI/ImGui handles keep their existing low slots. The entire
   range `[1,65248)` is reserved before arenas/ImGui allocate descriptors, leaving
   288 ancillary slots; no table grows and the root remains 37 DWORDs. Descriptor
   zero is a real white RGBA8 image uploaded before draws. Raw SRV/UAV arena and
   detile views use R32_TYPELESS + RAW, not an invalid UNKNOWN/zero-stride view.
   Transient detile descriptors are recycled only after their submission fence.

   The format/layout planner supports BC1–BC7 (sRGB where defined), normalized
   R/RG/RGBA and float formats, BGRA swizzles, 2D/cube resources and padded cube
   source slices. Four display/thin tile modes dispatch the existing SM6.4
   detile kernel per mip/slice. Row copies repack its output into SDK-provided
   `GetCopyableFootprints` layouts (256-byte pitch, 512-byte placement) before
   `CopyTextureRegion`. Unsupported layouts/formats are rejected and counted.
   Uploads are bounded; larger chains or budget pressure select smaller initial
   mip chains. Full Vulkan-style quality convergence is not yet implemented.
   Static replacements retain both twins until their fence grace ends; dynamic
   linear/movie refresh keeps a stable descriptor and queue-orders copy barriers
   after prior reads. Movie refresh uses owned AvPlayer/kept-frame copies.

   New host and ASan/UBSan tests exercise the production range/encoded-address,
   index-bound, fence-gate, twin-rotation, format/tiling and footprint helpers,
   including padded cube mip chains. `local_precheck.sh` also compiles both original
   Vulkan mesh/texture TUs (only generated SPIR-V headers are dummy bytecode), the
   D3D12 implementation TUs against SDK stubs, and all twenty actual capture/link
   contract definitions. GCC's vendored D3D12MA member/type spelling needs
   `-fpermissive` only for the native symbol audit; real Windows/UWP compilers and
   the shader `-WX` gate do not use that exception.

   **Slice 4B-2 — UI recording/presentation implemented (device output unverified).**
   `ui_d3d12.{cpp,h}` records captured screen draws into up to 32 RGBA8
   targets, publishes their external SRVs, lazily builds compatible UI PSOs
   (arbitrary GCN blend control, list/strip topology, stroke/text geometry), and
   aspect-fits the latest valid picture before ImGui on **every** display tick.
   Game samplers occupy 16..19; sampler 3 is linear clamp for present; dynamic
   sampler allocation starts at 20. Targets rest in pixel/non-pixel SRV state;
   new/replaced targets clear to black with alpha 1; existing target contents
   survive kept/accumulation draws. Resource/descriptor replacement
   waits for prior graphics submissions, preserving shared reserved handles.
   The existing UI range is **above** `FirstReserved`, below `Capacity` (not a
   range available to the texture cache).

   b0 remains the five-DWORD arena table. b1 is the 20-byte `UiRoot`, with arena
   **before** offset, text scale and `base_draw`. Bounded CPU command reads set
   `base_draw=first_instance`, then call `DrawInstanced` with start-instance zero;
   vertex shaders include `SV_InstanceID`. Signed Vulkan viewport heights are
   normalized into positive D3D12 heights with equivalent clip Y supplied through
   the existing b2 block. Flat text retains the full physical shader interface,
   initializing unused attr1 to zero. The root signature stays **37 DWORDs**.

   Surface retires completed packet slots **before** `TryTake`, pins every
   recorded packet through its submission fence (including partial failure), and
   remembers the latest valid picture independently of slot ownership. Shutdown
   waits for GPU idle, releases all retained slots before detaching SceneRing,
   and stops UI before textures/arenas. Resize rebuilds the present PSO if the
   swapchain format changes, after GPU idle.

   Local production-helper tests cover blend factors/operations (including
   constant colour/alpha and legal alpha factors), Pairs/geometry selection,
   text scale, present fitting, viewport equivalence, root order, command/header
   bounds and fence-slot retirement, both normally and under ASan/UBSan. Full
   precheck also retains **97/97 Microsoft DXC** (HLSL 2021, SM6.4, `-WX`) and
   **97/97 Slang**, residency and SceneRing tests, Vulkan syntax checks and the
   capture link audit. Windows/UWP CI validates compilation/packaging, **not**
   GPU output. Boot logos, movies, title/menu/comic/HUD now have a recording and
   presentation path; none has been observed on a device in this workspace.
   `Photo::Read` remains false. No eboot progress, physical Xbox execution or
   visual/GPU parity is claimed. Slice 5A now supplies the core 3D path below;
   remaining modules and full texture-quality convergence are still separate.

   **Slice 5A — core 3D backbone implemented, device execution unverified.**
   `world_d3d12.{h,cpp}` and `look_d3d12.{h,cpp}` create bounded scene targets
   at the valid UI world target's dimensions (shared `Header` has no width/height
   fields). Resize/resource/descriptor replacement waits for prior graphics work.
   Depth is R32 typeless / D32 DSV / R32 SRV, cleared to zero; scene pipelines use
   GREATER_EQUAL reverse depth, positive viewports and counter-clockwise fronts.
   G-buffers match Vulkan's sRGB albedo, RG16F normal and RGBA8 material; the HDR
   scene is RGBA16F. Four world surface lists, world decals, Lambert, Vision,
   Character and Interior lists feed deferred lighting and depth-tested sky.
   Slice 5B extends readiness to in-bounds skinned streams and deformer-arena
   vertices; invalid streams and index ranges remain skipped and counted.

   Cascaded shadows now use forward-Z R16 typeless depth resources (two layers
   for cascades zero and one, one 2D map for cascade two), D16 DSVs, a
   LESS_EQUAL comparison sampler, positive viewports and depth-one clears. A
   bounded PSO cache bakes each list's integer/slope depth bias for FL 12_0; the
   shadow VS roots carry and apply each draw's base instance. The persistent
   point-light grid dispatches only when the packet has point lights and feeds
   deferred and character roots. Captured Light/Scene packets are not rewritten
   in place. AO/line/self-translucency/bloom and traced-shadow inputs still use
   neutral fallbacks where their separate passes are not implemented.

   Luminance -> exposure -> tonemap -> antialias-off -> composite now execute.
   Persistent 16-byte meter state starts at `{0,1,1,0}` in GPU-only Scratch,
   survives target resize, and uses UAV barriers and explicit UAV/SRV transitions.
   Writable arena views are contiguous SRV/UAV pairs: b0 still carries only five
   SRV indices; the shared UAV accessor selects the adjacent view. Meter compute
   reads through the UAV, not an SRV in UAV state. The existing antialias shader
   writes a row-pitched raw UAV buffer, copied into a typeless RGBA8 picture with
   an sRGB SRV (sRGB UAVs are not supported). Composite writes directly into
   `UiRenderer::BeginWorld`'s preserved RGBA8 target, then `EndWorld` restores SRV
   state before HUD/menu recording. New packets refresh `Header::dynamic` textures
   and call `Movies::Collect` in addition to UI texture refresh.

   The clip-depth audit checks all **17** non-pfx VS/DS stages against GLSL,
   including final GS conversions: missing world/character/Lambert/decal/glass/
   self-translucent/contour reverse-Z fixups are restored. Intermediate contour
   and leaf VS/DS outputs remain raw to avoid double conversion; leaf GS already
   converts. Shadow/leaf-shadow shaders retain **forward depth as in GLSL**, not
   the scene's reverse depth. Draw packed-pointer high words, 656-byte Lambert
   Character stride, 304-byte world-decal offsets and 304-byte Sky offsets were
   corrected to match shared capture. Newly executed sky/post stages account for
   D3D12's fullscreen Y orientation without changing the existing UI present path.

   Production-helper root sizes/offsets/word order, rigid readiness and index
   bounds are checked normally and under ASan/UBSan; a source parity/ABI guard
   accompanies **97/97 Microsoft DXC -WX** and **97/97 Slang**. The root remains
   **37 DWORDs**, FontPack is untouched, and packaging keeps PDB/MAP external.
   Passing compilation/tests is **not** proof of observed geometry, boot progress,
   visual parity, or physical Xbox execution.

   **Slice 5B — deformation, point lights and cascaded shadow recording
   implemented, device execution unverified.** The capture-facing `Deformers::Get`
   now shares a 16 MiB GPU-writable allocation with 256-byte-aligned instances;
   copies are initialized before the frame's compute work and retired after their
   graphics fence. The D3D12 update/record path runs effectors, skinning,
   soft-body simulation and soft-body vertex writes in the game order, with UAV
   barriers and explicit arena transitions. Vertex-skinned draws require an
   in-bounds geometry skin stream and scene palette; valid deformer-arena vertex
   addresses are also accepted. Point lights build the 60 x 36 x 8 grid into a
   persistent writable scratch range and pass its address through the deferred
   and character roots. Shadows render the packet's three cascades, validate
   command/index/caster bounds, and use a 48-entry fence-aware PSO cache keyed by
   list and baked depth bias. The two shadow vertex shaders add the per-draw base
   instance before indexing casters.

   `local_precheck.sh` checks the new units against the SDK stub, the capture
   symbol contract, SM 6.4 HLSL with Microsoft DXC `-WX` and Slang, host bounds
   tests and ASan/UBSan. These checks and Windows/UWP CI prove compilation and
   packaging only—not D3D12 execution, visual parity, or hardware behavior.
   **Slice 5C remains separate:** fog, bloom, world lines, AO, glass, water,
   particles and other advanced transparent passes are not part of this slice.
   Leaf/grass/branch draw and shadow passes remain incomplete as well. Full
   texture-quality convergence, `Photo::Read`, FSR and physical Xbox runtime
   verification remain outstanding. This build is still **not claimed playable**.

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

