# Port status

<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

This file is the single honest answer to "how far along is this?". It is updated
in the same commit as the work it describes. If it disagrees with the README, this
file is right.

**Last updated:** 2026-11-14

---

## Summary

| Area | State |
| --- | --- |
| Xbox toolchain, packaging and CI | **Done and passing** |
| Strict W^X guest execution | **Done, unit-tested** |
| 5,120 MiB memory budget | **Done, unit-tested** |
| AppContainer Win32 shims | **Done** |
| Direct3D 12 device / allocator / descriptors / arena | **Done** |
| HLSL SM 6.6 shader pipeline (`dxc`, fail-loud) | **Done** |
| D3D12 command ring, root signature, PSO cache | **Done** |
| Direct3D 12 frame graph (passes, submits, presenter) | **In progress** |
| HLSL shader port | **In progress — 24 of 184 stages** |
| UWP shell (`IFrameworkView`, launcher, gamepad) | **In progress** |
| Gamepad chord remapping | **Done, unit-tested** |
| Audio backend for UWP | **Not started** |
| Playable on a console | **No** |

---

## What works today

### Toolchain and CI

* `.github/workflows/build-xbox-uwp.yml` runs on every push, PR and `v*` tag:
  a source guard (no game data), a Windows desktop D3D12 build, the UWP MSIX
  build with `makeappx`/`signtool`, and a release job.
* The UWP build genuinely configures with `CMAKE_SYSTEM_NAME=WindowsStore`,
  `GR2_XBOX_UWP=ON`, `GR2_RENDERER=D3D12`.
* HLSL shaders compile with `dxc -T *_6_6 -HV 2021 -WX`. A shader warning is a
  build failure.
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

### Renderer foundation

* `ID3D12Device8` on FL 12_1, DIRECT + COMPUTE queues, timeline fence,
  flip-model swapchain for both HWND and CoreWindow, tearing/HDR/color-space.
* Bindless heaps (65,536 SRV/UAV, 256 samplers) with null descriptors.
* Unified Bindless Arena (5 arenas, 1,088 MiB) with `{arena, uint32 offset}`
  pointers replacing `buffer_reference`.
* D3D12MA (vendored v3.0.1) behind a 1,200 MiB budget with 80%/90% eviction.
* Shared HLSL headers: `arena.hlsli`, `vertex.hlsli`, `scene.hlsli`,
  `texel.hlsli`, `surface.hlsli`, `grade.hlsli`, `light.hlsli`, `fog.hlsli`.
* **Root signature and pipelines** (`pipeline.*`): one root signature for the
  whole renderer -- three root-constant blocks plus SM 6.6's direct
  `ResourceDescriptorHeap[]` indexing, which is what replaces upstream's
  descriptor arrays. `GraphicsPipelineDesc` in the port's own terms, and a
  `PipelineCache` keyed by a hash of the state and the shader containers, backed
  by the driver's `ID3D12PipelineLibrary` when the device offers one.
* **Command ring** (`command.*`): one allocator, list and fence value per frame
  in flight, rotated; explicit transitions with named helpers; a render-pass
  begin/end pair that cannot inherit a target from the previous pass; full-screen
  draws, dispatches and the frame's submit.

### Input

* Xbox chord layer mapping View+LB/RB/B/L3/Y (and optionally the D-Pad) onto the
  DualShock 4 touchpad and motion outputs the game binds, with edge triggering
  and button swallowing.

---

## What does not work yet

1. **184 − 24 shaders.** `python3 tools/shaders/check_shader_coverage.py`
   prints the live number. Remaining stages are mostly the `pfx/` particle
   family, the world-surface variants, the geometry shaders (`contour.geom`,
   `ui_stroke.geom`, `ui_text.geom`), the tessellation pair
   (`leaf.tesc`/`leaf.tese` → hull/domain) and the GCN detiling compute shader.
2. **The frame graph.** `presenter.*` and `renderer.*` are not written: the pass
   order, render-target setup and the presenter's CoreWindow path are next.
   `command.*` and `pipeline.*` now exist, so this is sequencing rather than
   foundation work.
3. **The UWP shell.** `IFrameworkView`, the CoreWindow and the run/shutdown
   contract are written (`uwp/uwp_main.cpp`, `uwp/uwp_app.cpp`); the D3D12
   launcher UI and the in-process reboot are not.
4. **Audio.** `openal-soft` is not yet configured for the UWP/WASAPI path.
5. **PSO cache serialisation** (`ID3D12PipelineLibrary1`) is designed, not
   implemented.
6. **Fsr 3.1 on D3D12** is not wired up.

**Therefore: no released artifact can play the game yet.** The `.msix` CI
produces is a real, signed, installable package — of a build that does not yet
have a complete renderer. The README says so plainly.

---

## Order of work

1. ~~`command.*` + `pipeline.*`~~ **Done.**
2. Port the shader set in dependency order: the composite chain (done), the
   world/character families behind `scene.hlsli` (in progress), then the `pfx/`
   particle family, then geometry and tessellation stages, then the compute
   passes.
3. `presenter.*` + `renderer.*`: reproduce upstream's pass order
   (`frame_graph_draws.cpp` is the reference) against the arena.
4. `launcher.*`: the D3D12 launcher UI and the in-process reboot path `Relaunch`
   already points to.
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

