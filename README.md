<div align="center">

# GR2toXBOX

**A native Direct3D 12 and Xbox Series X|S (Dev Mode) port of *Gravity Rush 2*.**

No emulation. No CPU translation. The game's own x86-64 code running on the
console's AMD Zen 2 cores, drawn by a ground-up Direct3D 12 renderer.

[![Xbox UWP (MSIX) and Windows D3D12](https://github.com/youdedend/GR2TOXBOX/actions/workflows/build-xbox-uwp.yml/badge.svg?branch=main)](https://github.com/youdedend/GR2TOXBOX/actions/workflows/build-xbox-uwp.yml)
[![Latest release](https://img.shields.io/github/v/release/youdedend/GR2TOXBOX?include_prereleases&color=e0a020&label=download)](https://github.com/youdedend/GR2TOXBOX/releases)
[![License: GPL-2.0](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE)
[![Target](https://img.shields.io/badge/target-Xbox%20Series%20X%7CS%20Dev%20Mode-107C10?logo=xbox&logoColor=white)](#)
[![Renderer](https://img.shields.io/badge/renderer-Direct3D%2012%20%C2%B7%20SM%206.6-0078D4?logo=windows&logoColor=white)](#)

</div>

---

## Overview

GR2toXBOX takes [GR2port](https://github.com/junminlee2004/GR2port) — a native
PC port of *Gravity Rush 2* descended from [shadPS4](https://github.com/shadps4-emu/shadPS4)
— and makes it run on an **Xbox Series X|S in Retail Dev Mode**, packaged as a
signed UWP `.msix` that GitHub Actions builds for you.

Three things had to change to make that possible.

### 1. The CPU needs no help — the *memory* does

Both consoles use an **AMD Zen 2 x86-64** CPU, which natively executes the PS4's
instruction set, including the AMD extensions Intel parts lack (`SSE4a`,
`AVX2`, `BMI1/2`, `MOVBE`, `LZCNT`). There is no interpreter and no recompiler on
the hot path: the guest's `.text` is loaded, relocated, patched and executed.

What the console does **not** allow is easy memory. An AppContainer forbids
`PAGE_EXECUTE_READWRITE` outright. The port therefore implements **strict W^X**:

```
reserve PAGE_NOACCESS  →  commit PAGE_READWRITE  →  relocate + patch
     →  FlushInstructionCache  →  seal PAGE_EXECUTE_READ
```

Every guest code page is executable **only** after it has been written and
flushed, and a lazily patched instruction briefly drops execute permission while
it is rewritten. See [`src/core/uwp_wx.cpp`](src/core/uwp_wx.cpp).

### 2. Xbox has no Vulkan driver

Upstream's renderer is Vulkan, all the way down — including its *shaders*, which
pass raw 64-bit GPU pointers inside `buffer_reference` structs and index draws by
`gl_BaseInstance`. None of that exists in HLSL.

So GR2toXBOX grows a second, native **Direct3D 12** backend
([`src/graphics/gr2port_d3d12/`](src/graphics/gr2port_d3d12)) on Feature Level
12_1 with Shader Model 6.6, and solves the two Vulkan-isms properly:

| Upstream (Vulkan) | This port (D3D12) |
| --- | --- |
| `layout(buffer_reference)` + `uint64_t` GPU pointers | **Unified Bindless Arena**: one `ByteAddressBuffer`, pointers become `{ arena, uint32 offset }` |
| `gl_BaseInstance` (no HLSL equivalent) | `uint base_instance` **root constant**, and inline in the `ExecuteIndirect` signature |
| `vk::Pipeline` + dynamic state | 64-bit state-keyed **PSO cache** in `ID3D12PipelineLibrary1` |
| Per-draw descriptor sets | SM 6.6 `ResourceDescriptorHeap[]` **bindless** table |

Because the whole guest GPU budget is under 4 GiB, a 32-bit offset addresses
every byte losslessly — and is *faster* than the Vulkan path on GCN, which pays
for 64-bit address arithmetic.

### 3. The console has a hard 5,120 MiB pool

Upstream sizes its guest backing at the PS4 Pro **devkit** figure, ≈8.25 GiB.
Committing that inside an AppContainer fails immediately with
`ERROR_COMMITMENT_LIMIT`. GR2toXBOX enforces a console budget
([`src/core/uwp_memory.cpp`](src/core/uwp_memory.cpp)):

| Allocation | Budget |
| --- | --- |
| Guest direct + flexible memory | **3,200 MiB** |
| Direct3D 12 resource heap | **1,200 MiB**, LRU eviction at 80% / 90% |
| Total process commit target | **< 4,600 MiB** of the 5,120 MiB pool |

The arithmetic is asserted by a dependency-free self-test that runs in CI.


## Performance and preset matrix

| Platform | Preset | Resolution | Target | Notes |
| --- | --- | --- | --- | --- |
| **Xbox Series X** | Quality | 3840×2160 | **60 FPS** | Native 4K, 1,200 MiB heap |
| **Xbox Series X** | Performance | 2560×1440 | **120 FPS** | VRR recommended |
| **Xbox Series S** | Quality | 1920×1080 | **60 FPS** | Native 1080p |
| **Xbox Series S** | Upscaled | 2560×1440 | **60 FPS** | FSR 3.1 from 1080p; 10 GiB-class GPU |
| **Windows PC** | D3D12 | Any | Unbounded | Tests the same backend desktop CI builds |
| **Windows PC** | Vulkan | Any | Unbounded | Upstream path, unchanged |

Frame-pacing work is the same as upstream (its vblank and frame-rate patches);
the CPU cost of the port's own layers is a small fraction of a millisecond per
frame and is not the bottleneck at these targets.


## Quick start (Xbox)

```
1.  Dev Mode on  →  Xbox Device Portal at  https://<xbox-ip>:11443
2.  Certificates  →  install   GR2port-XboxDevMode.cer
3.  My games & apps  →  Add  →  GR2port-XboxSeriesXS-UWP_x64.msix
4.  Change the app's type from "App" to "GAME"       ← mandatory, see below
5.  Put your dump on a USB SSD at   E:\GR2\app0\eboot.bin
6.  Launch, pick Series X / Series S, resolution and frame rate
```

> **Step 4 is not optional.** An Xbox title registered as an *App* gets roughly
> 1 GB of memory; the game needs the ~5 GB *Game* pool. Left as App, the port
> exits during boot with `ERROR_COMMITMENT_LIMIT`.

Full walkthrough: **[docs/XBOX_DEV_MODE_GUIDE.md](docs/XBOX_DEV_MODE_GUIDE.md)** ·
Nothing working? **[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)**


## Controller mapping

*Gravity Rush 2* uses the DualShock 4 touchpad (flicks, Panther) and its motion
sensors. Xbox pads have neither, so the port turns them into chords:

| Chord | Produces | Game action |
| --- | --- | --- |
| **View + LB** | `gr2_lunar_style` | Switch to Lunar style |
| **View + RB** | `gr2_jupiter_style` | Switch to Jupiter style |
| **View + B** | `gr2_normal_style` | Switch to Normal style |
| **View + L3** *(hold)* | `touchpad_center` | Panther transform |
| **View + Y** *(hold)* | motion tilt from right stick | Gravity slide / photo tilt |
| **View + D-Pad Up/Down/Left/Right** | `touchpad_swipe_*` | Frisk flicks *(off by default)* |

Flick chords start **disabled** because the D-Pad is the game's own photo-mode,
map, gesture and first-person-view input. Enable them — and rebind any chord —
in the launcher. Chords that borrow a button swallow it, so a chord never also
opens a menu. Implementation: [`src/uwp/chords.cpp`](src/uwp/chords.cpp).


## Building

Everything the console package needs is built by GitHub Actions:
the [`Xbox UWP (MSIX) and Windows D3D12`](.github/workflows/build-xbox-uwp.yml)
workflow compiles the Direct3D 12 backend, compiles every HLSL shader with
`dxc -T *_6_6 -HV 2021 -WX`, packs and signs the `.msix`, and publishes
`GR2port-XboxSeriesXS-UWP.zip` (`.msix` + `.cer` + quick start).

Locally, see **[docs/BUILDING_FROM_SOURCE.md](docs/BUILDING_FROM_SOURCE.md)**:

```bash
# Windows PC, Direct3D 12
cmake -S . -B build -G Ninja -DGR2_RENDERER=D3D12 -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Xbox Series X|S UWP package
cmake -S . -B build-uwp -G Ninja \
      -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0.22621.0 \
      -DGR2_XBOX_UWP=ON -DGR2_RENDERER=D3D12
cmake --build build-uwp --parallel
```


## Repository layout

```
src/core/uwp_wx.*            strict W^X code generation for the guest
src/core/uwp_memory.*        5,120 MiB pool budgeting and clamping
src/core/uwp_win_compat.h    AppContainer-safe memory/file API shims
src/graphics/gr2port/        upstream's Vulkan renderer (still supported on PC)
src/graphics/gr2port_d3d12/  the Direct3D 12 backend
  ├── device.*               ID3D12Device8, flip-model swapchain (HWND + CoreWindow)
  ├── allocator.*            D3D12MA + the 1,200 MiB budget and LRU eviction
  ├── descriptors.*          SM 6.6 bindless heaps
  ├── arena.*                Unified Bindless Arena (replaces buffer_reference)
  └── shaders/               HLSL SM 6.6 sources + common arena/vertex/scene headers
src/uwp/                     Xbox shell: manifest, chords, launcher
cmake/GR2D3D12Shaders.cmake  offline dxc pipeline -> embedded DXIL headers
uwp/                         Package.appxmanifest and MSIX visual assets
docs/                        architecture, installation, build and troubleshooting
tools/shaders/               shader-port coverage checker
```

Docs: [Implementation plan](docs/IMPLEMENTATION_PLAN.md) ·
[D3D12/UWP architecture](docs/ARCHITECTURE_D3D12_UWP.md) ·
[Port status](docs/PORT_STATUS.md)


## Project status

This is a **work in progress**, tracked honestly in
**[docs/PORT_STATUS.md](docs/PORT_STATUS.md)**. The infrastructure (W^X loader,
memory budgeting, D3D12 device/allocator/arena layer, SM 6.6 shader pipeline,
UWP manifest, CI packaging, controller chords) is in place and tested. The
remaining work is the long tail this project is honest about: porting the
renderer's frame graph and the ~184 shaders from GLSL to HLSL SM 6.6. The
coverage checker, `python3 tools/shaders/check_shader_coverage.py`, prints the
current number at any time.

**No prebuilt binary in this repository can play the game yet, and the release
artifacts say so.** What CI proves today is that the console toolchain, the
Direct3D 12 device layer, the W^X loader and the memory budget all build and pass
their tests.


## Legal

GR2toXBOX is an **unofficial, non-commercial compatibility project**. It ships
**no game data** — no `eboot.bin`, no `.pkg`, no `.zar`, no `.prx`, no
copyrighted assets. A `.gitignore` guardrail and a CI check
([`tools/ci/check_no_game_data.sh`](tools/ci/check_no_game_data.sh)) make that
structural, not a promise.

You must own *Gravity Rush 2* and dump your own copy (v1.11) to use this. It is
not affiliated with, endorsed by, or supported by Sony Interactive
Entertainment, Japan Studio, or Microsoft. Xbox, Xbox Series X|S and DirectX are
trademarks of Microsoft; PlayStation and Gravity Rush are trademarks of Sony.
Respect your local law.

## Credits and license

GPL-2.0-or-later. See [LICENSE](LICENSE), [LICENSES/](LICENSES/) and
[REUSE.toml](REUSE.toml).

* **[GR2port](https://github.com/junminlee2004/GR2port)** — the port this
  project builds on. Its emulator core, Gravity Rush 2 hooks, scene capture and
  Vulkan renderer are the foundation of everything here.
* **[shadPS4](https://github.com/shadps4-emu/shadPS4)** — the PS4 emulator whose
  core GR2port descends from.
* **[D3D12MemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator)**
  — MIT, vendored at v3.0.1 (see `src/graphics/gr2port_d3d12/d3d12ma`).
* **[DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler)**
  — used at build time, not distributed.
