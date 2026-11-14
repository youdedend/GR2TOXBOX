<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Changelog

All notable changes to GR2toXBOX are documented here. This project follows
[Semantic Versioning](https://semver.org/); the version tracks the **port**, not
the game, which stays at its own v1.11.

Because this is a console port, the entry that matters most is the one marked
**Xbox**: that is the first state in which a `.msix` built here can run on a
Series X|S.

## [Unreleased]

### Added

- **Strict W^X guest execution** (`src/core/uwp_wx.*`). Guest code is committed
  `PAGE_READWRITE`, relocated and patched there, flushed, then sealed
  `PAGE_EXECUTE_READ`. `PAGE_EXECUTE_READWRITE` is never requested on a console
  build. Lazily patched instructions reopen one page and reseal it under the
  module lock. The desktop build runs the same lifecycle, so CI exercises it.
- **5,120 MiB console memory budget** (`src/core/uwp_memory.*`): guest commit
  clamped to 3,200 MiB (direct 2,688 + flexible 512, excess taken from flexible
  first down to a 128 MiB floor), Direct3D 12 heap capped at 1,200 MiB with LRU
  eviction at 80%/90%, process target under 4,600 MiB. The backing section is
  sized from the console ceiling instead of the ~8.25 GiB PS4 Pro devkit total,
  which is what makes the first `SEC_COMMIT` succeed.
- **AppContainer Win32 shims** (`src/core/uwp_win_compat.h`): one typed surface
  over `VirtualAlloc2FromApp`, `MapViewOfFile3FromApp`, `CreateFileMappingFromApp`
  and `VirtualProtectFromApp`, used by the address space on both platforms.
- **No subprocesses on Xbox**: `Emulator::Relaunch` refuses under
  `GR2_XBOX_UWP` and points at the in-process hand-off, replacing `CreateProcessW`
  which an AppContainer cannot call. `SetPriorityClass`, `SetErrorMode` and the
  `ntdll` version probe are desktop-only; the address space uses a fixed,
  conservative user area on a console, where the Windows-11-22H2 workaround it
  probes for does not apply.
- **Direct3D 12 backend foundation** (`src/graphics/gr2port_d3d12/`):
  `ID3D12Device8` on Feature Level 12_1, DIRECT and COMPUTE queues, a timeline
  fence, a flip-model swapchain for both HWND and CoreWindow, and color-space
  handling including HDR.
- **Unified Bindless Arena** resolving the port's largest shader obstacle: GLSL
  `buffer_reference` 64-bit GPU pointers have no HLSL equivalent. They become
  `{ arena, uint32 offset }` pairs into one `ByteAddressBuffer`, addressed through
  the SM 6.6 `ResourceDescriptorHeap[]`. The 32-bit offset is exact, not a
  compromise: the console budget keeps all GPU memory under 4 GiB, and the
  narrower address arithmetic is faster on GCN.
- **`gl_BaseInstance` replacement**: the draw index arrives as a root constant
  (`b0, space0`) and inline in the `ExecuteIndirect` command signature.
- **Bindless descriptor heaps** (65,536 SRV/UAV, 256 samplers) with null
  descriptors so a missing binding cannot fault.
- **D3D12MA integration** (vendored v3.0.1, MIT) behind the console heap budget,
  refusing an allocation before D3D12 would fail to commit it — a failed commit
  inside an AppContainer leaves the device unable to commit the next one.
- **Offline HLSL SM 6.6 pipeline** (`cmake/GR2D3D12Shaders.cmake`): `dxc -T
  <stage>_6_6 -HV 2021 -O3 -WX -Qstrip_reflect` into embedded DXIL headers.
  Warnings are errors, stage inference is validated at configure time, and a
  missing compiler is a configure-time failure rather than a shaderless build.
- **Shared HLSL headers** replacing upstream's shared GLSL: `arena.hlsli`
  (buffer_reference replacement, half/unorm unpacking), `vertex.hlsli` (vertex
  pulling and skinning), `scene.hlsli` (Draw/Material/Decal packets,
  `base_instance`). First ported stage: `world.vert`.
- **Shader coverage checker** (`tools/shaders/check_shader_coverage.py`), so the
  port's progress is measured rather than claimed.
- **Xbox UWP package** (`uwp/Package.appxmanifest`, `uwp/Assets/`): x64-only
  `Windows.Universal` manifest declaring `Game`, with `codeGeneration` (execute
  permission), `broadFileSystemAccess` (USB dumps), `internetClient(Server)`,
  `privateNetworkClientServer`, `documentsLibrary`, `picturesLibrary` and
  `removableStorage`. Tile, splash and store logos generated from the project
  icon.
- **Controller chord layer** (`src/uwp/chords.*`): Xbox buttons stand in for the
  DualShock 4's touchpad and motion sensors, which the pad lacks. View+LB/RB/B
  select Lunar/Jupiter/Normal style, View+L3 holds Panther, View+Y drives motion
  tilt from the right stick, and View+D-Pad produces touchpad flicks — disabled by
  default, because the D-Pad is the game's own photo-mode, map, gesture and
  first-person input. Chords swallow the buttons they borrow.
- **MSIX CI/CD** (`.github/workflows/build-xbox-uwp.yml`): five jobs —
  source guard, info, Windows D3D12 build, UWP MSIX build
  (`makepri` → `makeappx` → `signtool verify`) and release. Publishes
  `GR2port-XboxSeriesXS-UWP.zip` (`.msix` + `.cer` + quick start) and writes a
  job summary whose one instruction is *change App type to Game*.
- **Signing tooling** (`scripts/ci/`): ephemeral self-signed certificate or
  secrets-supplied one, with an assertion that the subject equals the manifest's
  `Identity Publisher` — checked before `signtool`, which otherwise reports the
  mismatch as an opaque `0x8007000B`. Only the public `.cer` is published.
- **Documentation**: `README.md` (perf matrix, chord table, clean-room
  disclaimer), `docs/XBOX_DEV_MODE_GUIDE.md`, `docs/ARCHITECTURE_D3D12_UWP.md`,
  `docs/BUILDING_FROM_SOURCE.md`, `docs/TROUBLESHOOTING.md`, `docs/PORT_STATUS.md`
  and `CONTRIBUTING.md`/`SECURITY.md` plus issue and PR templates.
- **Tests**: `xbox_memory_budget` (16 assertions desktop, 20 console) and
  `xbox_chords` (25 assertions), both dependency-free and run by CTest.

### Changed

- **`/DYNAMICBASE /HIGHENTROPYVA /APPCONTAINER`** replace upstream's
  `/DYNAMICBASE:NO` + `/BASE:0x700000000000` on the Xbox target. A UWP package is
  rejected without dynamic base and high-entropy VA, and an explicit image base is
  illegal — so this target deliberately diverges from upstream.
- `GR2_RENDERER` (`VULKAN`/`D3D12`) and `GR2_XBOX_UWP` CMake options, with hard
  errors for impossible combinations (`GR2_XBOX_UWP` without D3D12, or without
  `CMAKE_SYSTEM_NAME=WindowsStore`).
- Reverse-Z is uniform everywhere: `[1,0]` depth with
  `D3D12_COMPARISON_FUNC_GREATER`, matching upstream's `VK_COMPARE_OP_GREATER`.

### Removed

- The hardcoded `github.repository == 'junminlee2004/GR2port'` guard in
  `build.yml`, which silently disabled releases on every fork.

### Known limitations

- **Not yet playable on a console.** Direct3D 12 frame graph, the UWP
  `IFrameworkView` shell and audio are not written, and 183 of 184 shader stages
  remain to port. See [docs/PORT_STATUS.md](docs/PORT_STATUS.md) for the live
  number and the order of work.

## [0.0.1] — Phase 0 audit

### Added

- Initial import of [GR2port](https://github.com/junminlee2004/GR2port) at
  `49f763a72f0079a4502c2aa76d6147fee353b065` (2026-11-14), preserving its
  GPL-2.0 license, `LICENSES/` and `REUSE.toml`.
- `docs/IMPLEMENTATION_PLAN.md`: the portability audit and the seven hard
  constraints (C1–C7) that shape every later commit.
- `tools/ci/check_no_game_data.sh`, the source guard that makes "no copyrighted
  binaries in the repository" a structural guarantee rather than a promise.
