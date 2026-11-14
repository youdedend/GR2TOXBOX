# Troubleshooting

<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

Find the symptom, try the fix, and if it is not here, open an issue with
`log.txt` attached (see [Getting logs](#getting-logs)).

---

## Installation

### `0x800B0109` — certificate chain not trusted

The signing certificate was not installed on the console, or a different `.cer`
was installed.

1. Uninstall the package.
2. Xbox Device Portal → **Certificates** → remove any `GR2toXBOX Dev Mode` entry.
3. Install `GR2port-XboxDevMode.cer` from *the same release* as the `.msix`.
4. Reinstall the package.

### `0x80073CF0` / "package could not be opened"

Usually a truncated download. Re-download the bundle, and check the file size
matches the release page.

### `0x80073CFB` — package already installed

An older build with the same identity is present. Uninstall it first; Dev Mode
does not always replace a same-version package in place.

### `0x80073D02` — the package requires an update of Windows

The console's OS is older than `MinVersion` in the manifest (10.0.19041.0).
Update the console.

### The title installs but never appears in Dev Mode home

Sign out and back into the Dev Mode account, or reboot the console into Dev Mode
again. Dev Portal shows installed packages under **My games & apps**.

---

## Launch

### Exits during boot; log says `ERROR_COMMITMENT_LIMIT`

**The app type is still "App".** In Xbox Device Portal → app details, change
**App type / Application type** from `App` to `Game`. This is the most common
failure and it is not a port bug.

The port logs this explicitly before it happens:

```
Xbox memory: the package is running in App mode (limit 1280 MiB)...
```

### Exits during boot with no clear log line

Possible causes, in order of likelihood:

1. **Dump not found or wrong version.** The launcher reports the path it tried.
   Confirm `.../app0/eboot.bin` exists and the dump is v1.11.
2. **USB drive not readable.** `broadFileSystemAccess` covers it, but the drive
   must be mounted. Check the drive letter in the console's storage settings;
   it is usually `D:` or `E:` but not guaranteed.
3. **Out of memory from another app.** Close anything else running; the pool is
   shared.

### Stuck on the splash screen

The device may be building its PSO cache on first run. Give it 30–60 seconds
once. If it always hangs, the renderer could not create a device: look for the
`D3D12 device:` line — it is logged immediately after creation.

### "Game files not found"

The launcher could not see `app0/eboot.bin` under the path you chose.

* Try the other drive letter (`E:` vs `D:`).
* If your dump is compressed, point the launcher at the `.zar` file.
* Do not point at the `.pkg`; the port needs the extracted/installed layout.

---

## Graphics

### Black screen with audio

Shaders are compiling or a pipeline failed. Check the log for
`D3D12: refusing` or `arena ... is full`. If the log shows arena exhaustion at
4K, try the 1440p preset — the arena sizes are tuned for it on Series S.

### Colours look wrong / washed out / too dark

HDR: the swapchain requests HDR10 when the launcher's HDR option is on. If your
display or capture chain does not handle PQ, turn HDR off in the launcher.

### Geometry missing, flickering, or exploded

That is a shader port bug. Please use the **shader visual issue** template with a
screenshot and `log.txt`; the port is tracked shader by shader and
`tools/shaders/check_shader_coverage.py` lists what is still in progress.

### Stutter every few seconds

Heap eviction. At 4K the texture cache exceeds the 1,200 MiB budget and the
allocator trims at 80%/90%. The log records each trim:

```
D3D12: trimmed N MiB of evictable resources
```

Lower the resolution preset or the texture quality; this is the intended
behaviour of a hard budget, not a leak.

---

## Controller

### A chord does nothing

Only enabled chords fire. Open the launcher's chord editor; flick chords ship
**disabled** because the D-Pad is the game's own photo-mode/map/gesture input.

### Pressing a chord also opens a menu

A chord should swallow the button it borrowed. If it does not, the button is
bound twice (once as a chord trigger, once in the game's own bindings). Pick a
different trigger in the chord editor.

### No rumble

Confirm the pad is the active input (press a button). Xbox trigger rumble
requires the impulse-trigger API; if your pad reports none, that is expected.

---

## Getting logs

1. Xbox Device Portal → **File Explorer**.
2. Navigate to the app's `LocalState` folder.
3. Download `log.txt`.

Attach it to your issue. The first ~50 lines (device, memory pool, W^X mode,
arena sizes) usually identify the problem on their own.

---

## Reporting

| Template | Use it for |
| --- | --- |
| **Xbox bug report** | crashes, install failures, performance, input |
| **Shader visual issue** | anything that looks wrong on screen |
| **Feature request** | presets, bindings, quality of life |

Include: console model (Series X / Series S), preset, storage (USB SSD /
LocalState), package version, and `log.txt`.
