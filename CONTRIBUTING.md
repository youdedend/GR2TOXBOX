<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Contributing to GR2toXBOX

Thanks for helping. This document is short on ceremony and long on the two rules
that actually matter here.

## The two rules

### 1. Never commit game data

No `eboot.bin`, `.pkg`, `.zar`, `.prx`, `.sprx`, `.self`, no extracted assets, no
screenshots of copyrighted artwork, no save files. This is checked twice:

* `.gitignore` blocks the extensions;
* `tools/ci/check_no_game_data.sh --all` runs as the first CI job and fails the
  build if anything slipped in.

If a test needs a game file, it must generate a synthetic one. There is no
exception, and a PR that violates this is closed rather than fixed.

### 2. Never break the console build

`GR2_XBOX_UWP=ON -DGR2_RENDERER=D3D12` must configure, compile and pass CTest on
Windows before review. Not "it will probably work on the console" — the CI job
`xbox-uwp-msix` is the gate.

That implies:

* **No Vulkan in code reachable on Xbox.** No `#include <vulkan/...>`, no
  `-DGR2_RENDERER=VULKAN` fallback. Xbox has no Vulkan driver.
* **No desktop-only Win32 without a guard.** If you call something an
  AppContainer forbids (`CreateProcessW`, `SetPriorityClass`, `VirtualProtect`
  on executable+writable, `GetModuleHandleW("ntdll.dll")`), put it behind
  `#if !defined(GR2_XBOX_UWP)` **and** provide the `*FromApp` path — empty
  substitutes are not acceptable.
* **No RWX.** Any new page of code follows the
  commit-readwrite → populate → flush → seal-execute-read lifecycle in
  `src/core/uwp_wx.cpp`.
* **No new memory without a budget.** Anything that can grow must be clamped by
  `Core::Xbox` on a console build; add an assertion to
  `src/tests/xbox_memory_budget_test.cpp`.
* **Shaders fail loudly.** HLSL is compiled with `-WX`. Do not add a pragma to
  silence a warning; fix it.

## The most useful contribution: a shader port

Each shader is a self-contained, verifiable unit of work, which is why it is the
best way to contribute right now.

```bash
python3 tools/shaders/check_shader_coverage.py --missing
```

Pick one, then:

1. **Keep the structure.** `world.vert.hlsl` deliberately mirrors the GLSL line
   for line. Diff them side by side; that is the point.
2. **Apply exactly two mechanical transformations** and comment them:
   * `buffer_reference` pointers → `ArenaRef { arena, offset }` resolved through
     `common/arena.hlsli`;
   * `gl_BaseInstance` → `g_scene.base_instance` from root constant `b0`.
3. **Do not "improve" the math.** Reverse-Z, the clip-space convention and the
   wrapping/packing rules must match upstream exactly or the scene will disagree
   with the PC Vulkan build.
4. **Verify coverage moved** — the script's count should include your stage.
5. **Open the PR.** CI compiles it with `dxc -T <stage>_6_6 -HV 2021 -WX`.

Naming is fixed: `.vert → .vert.hlsl`, `.frag → .frag.hlsl`, `.comp →
.comp.hlsl`, `.geom → .geom.hlsl`, `.tesc → .hull.hlsl`, `.tese → .dom.hlsl`.
An unknown extension is a configure-time error by design, so a stage cannot be
silently omitted.

## Commit messages

A commit should be atomic and explain **why**, not restate the diff. The body is
where the reasoning lives — which API refused what, which alternative you tried,
what would break if the change were reverted. Look at the history for the voice.

```
Phase N: <what changed, imperatively>

<Why. What constraint forced it. What the failure mode was before.>
```

Do not commit code that does not build, and do not split a change so the build is
broken between two commits.

## Style

* C++23, 100-column limit, 4-space indent, `snake_case` types, `PascalCase`
  methods, `m_` members. Match the file you are in; upstream's style wins over any
  personal preference.
* `//` comments explain *why*. The code already says *what*.
* Every new file carries the SPDX header
  (`SPDX-FileCopyrightText:` + `SPDX-License-Identifier: GPL-2.0-or-later`), and
  any new third-party code goes in `REUSE.toml`.

## Tests

| Suite | Add an assertion when you… |
| --- | --- |
| `src/tests/xbox_memory_budget_test.cpp` | touch any memory limit or clamp |
| `src/tests/xbox_chords_test.cpp` | change chord behaviour, defaults, or edge triggering |
| `tools/shaders/check_shader_coverage.py` | add a shader |

Both C++ suites are dependency-free (no test framework); copy the local `CHECK`
macro pattern.

## Legal

By contributing you agree your work is licensed GPL-2.0-or-later, and that you
did not derive it from decompiled Sony source, leaked SDKs, or anyone else's
copyrighted material. Descriptions of game *behaviour* are fine; code copied out
of the game is not. See [`SECURITY.md`](SECURITY.md) for how to report problems
privately.
