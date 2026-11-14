<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

## What this changes

<!-- One paragraph. What and why, not a restatement of the diff. -->

## Which phase

<!-- Phase 0 (audit) / 1 (D3D12 foundation) / 2 (UWP shell) / 3 (CI) / 4 (docs) / shader port -->

## Console-build checklist

- [ ] No `buffer_reference`, `gl_BaseInstance` or other GLSL-ism survives in new shader code
- [ ] New HLSL compiles with `dxc -T <stage>_6_6 -HV 2021 -WX` (CI proves it)
- [ ] No Vulkan in code reachable under `GR2_XBOX_UWP`
- [ ] No desktop-only Win32 without an `#if !defined(GR2_XBOX_UWP)` guard **and** a real `*FromApp` path
- [ ] No `PAGE_EXECUTE_READWRITE`; new code follows the W^X commit → populate → flush → seal lifecycle
- [ ] New growth paths are clamped on console builds, with an assertion in `xbox_memory_budget_test.cpp`
- [ ] `ctest` passes, including `xbox_memory_budget` and `xbox_chords`

## Source guard

- [ ] No game data, no `eboot.bin`/`.pkg`/`.zar`/`.prx`, no extracted assets, no save files
- [ ] `tools/ci/check_no_game_data.sh --all` passes locally

## Shader coverage

<!-- If you ported shaders, paste the before/after from tools/shaders/check_shader_coverage.py -->

```
before:
after:
```
