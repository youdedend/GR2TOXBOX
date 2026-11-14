# Architecture: Direct3D 12 and the Xbox UWP host

<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

This document explains how the console port works, at the level of detail
someone would need to change it. It assumes familiarity with D3D12 and with the
PS4's memory model.

---

## 1. The shape of the problem

Upstream GR2port is three things stacked:

```
   ┌──────────────────────────────────────────────┐
   │ Gravity Rush 2 (PS4, x86-64, Orbis)          │  user supplies this
   ├──────────────────────────────────────────────┤
   │ shadPS4-derived core                          │  loader, kernel LLE,
   │  address_space / memory / module / linker /   │  threads, TLS, CPU
   │  cpu_patches / tls / libraries                │  patches
   ├──────────────────────────────────────────────┤
   │ GR2port renderer (Vulkan)                     │  scene capture + frame
   │  capture / scene_ring / frame_graph_draws ... │  graph, 184 GLSL shaders
   └──────────────────────────────────────────────┘
```

On Xbox the bottom layer cannot exist (no Vulkan driver) and the middle layer
breaks in three places (RWX memory, an 8 GiB backing file, subprocess relaunch).
So the port replaces the bottom layer and repairs the middle one. The top layer
is untouched: the game's own code runs.

```
   ┌──────────────────────────────────────────────┐
   │ Gravity Rush 2 (PS4, x86-64)                 │
   ├──────────────────────────────────────────────┤
   │ shadPS4 core + Xbox fixes                     │
   │   uwp_wx      strict W^X                      │
   │   uwp_memory  5,120 MiB budgeting             │
   │   uwp_win_compat  *FromApp shims              │
   ├──────────────────────────────────────────────┤
   │ GR2port capture layer (unchanged)             │
   ├──────────────────────────────────────────────┤
   │ Direct3D 12 backend (new)                     │
   │   device / allocator / descriptors / arena    │
   │   HLSL SM 6.6 shaders                         │
   ├──────────────────────────────────────────────┤
   │ Xbox UWP shell (new)                          │
   │   IFrameworkView / CoreWindow / Gamepad       │
   └──────────────────────────────────────────────┘
```

---

## 2. Strict W^X execution

### Why it is needed

An AppContainer's token is not granted `SeCreatePagefilePrivilege`-class memory
rights that would allow a page to be simultaneously writable and executable.
`PAGE_EXECUTE_READWRITE` is refused; `rescap:codeGeneration` unlocks *execute*
permission, not *execute+write*.

### The lifecycle

`Core::Wx` ([`src/core/uwp_wx.cpp`](../src/core/uwp_wx.cpp)) is the single place
that changes page protections for code:

```
VirtualAllocFromApp(MEM_COMMIT, PAGE_READWRITE)     ← commit writable only
        │
        ├─ ELF PT_LOAD copy
        ├─ relocations
        ├─ PatchRedZoneMemoryInstructions()          ← rewrites guest code
        ├─ PatchCpuInstructionsStatically()
        │
FlushInstructionCache(range)                         ← before execute
        │
VirtualProtectFromApp(PAGE_EXECUTE_READ)             ← seal
        │
        └─ guest fetches instructions here
```

A lazily patched instruction (the path where static patching could not decide,
and an illegal-instruction fault brings the guest back) reopens its page:

```
OpenForPatch(addr, 15)   → PAGE_READWRITE      (strict W^X)  / PAGE_EXECUTE_READWRITE (desktop)
TryPatch(...)            → rewrite in place
CloseAfterPatch(...)     → FlushInstructionCache + PAGE_EXECUTE_READ
```

Three properties matter:

1. **No page is ever writable and executable at once** on a console build.
2. **The instruction cache is flushed before every transition into execute.**
   Without it x86-64 in practice needs no flush, but the console's cores and the
   JIT patches make it a correctness requirement, not a formality.
3. **The window is one instruction wide and held under the module lock**, so two
   threads cannot rewrite the same page concurrently.

### Where it plugs in

| Site | Change |
| --- | --- |
| `AddressSpace::Protect` | Splits execute+write into a writable phase and an execute-read phase; flushes on execute |
| `ToWindowsProt` | Returns `PAGE_READWRITE` rather than RWX on a strict-W^X build |
| `Module::LoadModuleToMemory` | Seals every `PF_EXEC` segment and the trampoline arena after static patching |
| `TryPatchJit` | Wraps the in-place rewrite in `OpenForPatch` / `CloseAfterPatch` |
| Backing section | Created `PAGE_READWRITE`, never `PAGE_EXECUTE_READWRITE` |

### Desktop equivalence

The same code runs on a Windows desktop D3D12 build (where `IsStrictWxRequired()`
is false and the reopen uses `PAGE_EXECUTE_READWRITE`), so CI exercises the
lifecycle on every push rather than discovering it on a console.

---

## 3. The 5,120 MiB pool

### The budget

```
  5,120 MiB  platform pool (Game mode)
  ├─ 3,200 MiB  guest direct + flexible memory
  ├─ 1,200 MiB  Direct3D 12 resource heap
  └─  ~720 MiB  host code, stacks, CRT, driver overhead
  ─────────────
   4,400 MiB   committed by the port, 4,600 target, 5,120 hard limit
```

### Where it is enforced

* `Core::Xbox::ClampGuestMemoryRegions()` runs inside
  `MemoryManager::SetupMemoryRegions`, **before** the physical maps are sized, so
  the direct map, the flexible map and the backing file all agree. The clamp
  takes the excess out of the flexible pool first (down to a 128 MiB floor that
  keeps the GPU flexible mappings alive) and then out of direct memory.
* `AddressSpace`'s `BackingSize` is derived from the console ceiling instead of
  the 8.25 GiB PS4 Pro devkit total, so the first `SEC_COMMIT` cannot fail.
* `D3D12::Allocator` owns the heap budget: it refuses an allocation that would
  exceed 1,200 MiB *before* asking D3D12, because a failed commit inside an
  AppContainer leaves the device unable to commit the next one. It trims through
  an eviction callback at 80% and 90%.
* The 5K/8K resolution patches are not offered on a console build, since the
  guest memory they request does not exist.

The arithmetic is asserted by
[`src/tests/xbox_memory_budget_test.cpp`](../src/tests/xbox_memory_budget_test.cpp)
(16 checks desktop / 20 console) and run by CTest in CI.

---

## 4. The renderer: Vulkan → Direct3D 12

### What is shared and what is replaced

| Upstream | This port |
| --- | --- |
| `capture.cpp`, `particle_capture.cpp`, `ui_capture.cpp`, `scene_ring.cpp` | **Unchanged** — they accumulate scene data, not graphics API calls |
| `device.{h,cpp}` (Vulkan instance/device) | replaced by `gr2port_d3d12/device.*` |
| `pipeline.{h,cpp}` (`vk::Pipeline` + dynamic state) | replaced by a 64-bit state-keyed PSO cache |
| `gr2port.cmake` SPIR-V compilation | replaced by `cmake/GR2D3D12Shaders.cmake` + dxc |
| `shaders/*.glsl` | ported to `gr2port_d3d12/shaders/*.hlsl` |

### The two Vulkan-isms

#### 4.1 `buffer_reference` → Unified Bindless Arena

Upstream shaders hold raw GPU addresses:

```glsl
// world.vert (upstream)
Draws draws = scene_draws;                                  // buffer_reference
const uint64_t vertices = draws.draws[gl_BaseInstance].vertices;
const vec3 p = LoadPosition(vertices, stride, offset);      // dereferences uint64
```

HLSL has no equivalent: SM 6.6 DXIL has no generic pointer, and
`GL_EXT_buffer_reference` has no D3D analogue.

The arena turns a pointer into a pair:

```hlsl
// world.vert.hlsl (this port)
struct ArenaRef { uint arena; uint offset; };               // 8 bytes, same size

Draw draw = LoadDraw(g_scene, g_scene.base_instance, g_arena);
float3 p = LoadPosition(draw.vertices, draw.stride, draw.position, vertex_index, g_arena);
```

and the accessor resolves it through the bindless heap:

```hlsl
ByteAddressBuffer ArenaBuffer(ArenaRef ref, ArenaTable table) {
    return ResourceDescriptorHeap[NonUniformResourceIndex(ArenaIndex(ref, table))];
}
uint4 ArenaLoad4(ArenaRef ref, ArenaTable table) {
    return ArenaBuffer(ref, table).Load4(ref.offset);
}
```

**Why 32-bit offsets are correct, not a compromise:** the guest's committed GPU
memory is bounded by the budget above (3,200 MiB guest + 1,200 MiB heap < 4 GiB),
so every byte any shader can address fits in a `uint`. There is no aliasing and
no windowing to get wrong. It is also faster than the Vulkan path on GCN, where
64-bit address arithmetic costs additional VALU instructions.

Arenas ([`arena.h`](../src/graphics/gr2port_d3d12/arena.h)):

| Arena | Contents | Size |
| --- | --- | --- |
| Geometry | Vertex/index streams from `capture.cpp` | 512 MiB |
| Scene | Draw table, materials, decals, lights | 64 MiB |
| Skinning | Matrices and palettes (`skin.comp`) | 128 MiB |
| Particles | GPU particle state (`gfx/gpu_*.comp`) | 256 MiB |
| Scratch | Per-frame uploads, transient compute I/O | 128 MiB |

#### 4.2 `gl_BaseInstance` → root constant

HLSL's `SV_InstanceID` restarts at 0 for each draw, so the draw index that GLSL
took from `gl_BaseInstance` arrives as a root constant:

```hlsl
cbuffer SceneRoot : register(b1) { SceneConstants g_scene; };  // .base_instance
```

For `ExecuteIndirect` multi-draws the same value is supplied inline in the
command signature's root-constant arguments, so one indirect dispatch handles
many draws without a per-draw root signature write.

### 4.3 Reverse-Z

Upstream uses a reverse-Z depth range, `[1, 0]`, with
`VK_COMPARE_OP_GREATER`, and its vertex shaders convert the game's OpenGL clip
space with `clip.z = 0.5 * (clip.w - clip.z)`.

D3D12 already uses `[1, 0]` with `D3D12_COMPARISON_FUNC_GREATER`, so the port
applies the equivalent affine transform to the *projection matrix* on the host
side and the shaders keep `clip.z` as computed. This keeps every depth
comparison, bias and shadow-projection path identical to upstream while removing
a per-vertex operation.

### 4.4 PSO cache and console presets

`ID3D12PipelineLibrary1` serialises PSOs to disk, keyed by a 64-bit hash of the
full state (shaders, formats, blend, depth, topology, root signature). On boot
the library is loaded and pipeline creation is spread across worker threads, so
first-frame hitches match upstream's Vulkan pipeline cache behaviour.

Console presets are pure policy on top of that: the Series X and Series S
presets differ only in render-extent scale, the FSR 3.1 toggle and the frame
rate cap.

---

## 5. Shader port pipeline

```
src/graphics/gr2port_d3d12/shaders/*.hlsl
        │
        ├─ dxc -T vs_6_6 -HV 2021 -O3 -WX -Qstrip_reflect
        │
        ▼
   DXIL container (.dxil)
        │
        ├─ cmake/GR2D3D12ShaderHeader.cmake
        ▼
   embedded C++ header (std::span<const uint8_t>)
        │
        ▼
   D3D12CreatePipelineState
```

Properties:

* **A shader warning fails the build** (`-WX`). A misspelt semantic or an
  implicit truncation that would silently corrupt a picture on a console is a
  CI failure instead.
* **Stage inference is checked at configure time**: `.tesc` maps to `hs_6_6`,
  `.tese` to `ds_6_6`, an unknown extension is a `FATAL_ERROR`, so no stage can
  quietly be left out.
* **Coverage is measured, not claimed**:
  `python3 tools/shaders/check_shader_coverage.py` compares the HLSL tree with
  the 184 GLSL shaders `gr2port.cmake` registers and prints what is missing.

Shared headers replace upstream's shared GLSL:

| Header | Replaces |
| --- | --- |
| `common/arena.hlsli` | `GL_EXT_buffer_reference`, the `Words`/`Rows` views, half/unorm unpacking |
| `common/vertex.hlsli` | `vertex.glsl` — vertex pulling and skinning |
| `common/scene.hlsli` | `scene.glsl` — Draw/Material/Decal packets, `gl_BaseInstance` |

---

## 6. The UWP shell

```
CoreApplication
  → IFrameworkView::SetWindow(CoreWindow)
  → IFrameworkView::Run()
       ├─ create D3D12 device + swapchain for the CoreWindow
       ├─ Suspending / Resuming / VisibilityChanged handlers
       └─ launcher loop  →  in-process game boot
```

Design points:

* **One process.** `Emulator::Relaunch` cannot spawn a process in an
  AppContainer, so a settings change tears down the emulator core and reboots it
  in place while keeping the device and swapchain alive.
* **Presentation** goes through `CreateSwapChainForCoreWindow` with
  `DXGI_SWAP_EFFECT_FLIP_DISCARD` and `DXGI_SCALING_STRETCH` (Xbox rejects
  ratio-preserving scaling on a CoreWindow).
* **Input** comes from `Windows.Gaming.Input::Gamepad`, with the chord layer
  ([`src/uwp/chords.cpp`](../src/uwp/chords.cpp)) converting Xbox buttons into
  the DualShock 4 touchpad and motion outputs the game binds.
* **Suspend/resume**: the emulator must not be killed mid-boot, so the shell
  requests a suspension deferral while a save or a load is in flight.

---

## 7. Diagnostics

* Every subsystem logs through the existing `Common::Log` classes; the D3D12
  backend adds `Graphics`.
* W^X reports seals and patch reopens; the allocator reports budget
  utilisation, evictions and refusals; the memory layer reports every clamp.
* `log.txt` in `LocalState` is the artefact a bug report should attach.
