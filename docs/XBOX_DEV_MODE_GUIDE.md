# Xbox Dev Mode Installation Guide

<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

This guide takes you from a stock Xbox Series X|S to *Gravity Rush 2* running
from this port, and explains *why* each step exists so you can debug it when
something differs on your console.

**Contents**

1. [What Dev Mode is, and its limits](#1-what-dev-mode-is-and-its-limits)
2. [Activating Dev Mode](#2-activating-dev-mode)
3. [Reaching Xbox Device Portal](#3-reaching-xbox-device-portal)
4. [Installing the signing certificate](#4-installing-the-signing-certificate)
5. [Installing the package](#5-installing-the-package)
6. [Changing App type to Game — mandatory](#6-changing-app-type-to-game--mandatory)
7. [Preparing your game dump](#7-preparing-your-game-dump)
8. [Launching and choosing a preset](#8-launching-and-choosing-a-preset)
9. [Where your data lives](#9-where-your-data-lives)
10. [Uninstalling and resetting](#10-uninstalling-and-resetting)

---

## 1. What Dev Mode is, and its limits

Xbox Retail Dev Mode (the Developer Mode "app", not the deprecated dev kit) gives
a retail console the ability to sideload signed UWP packages and run them with
some extra permissions.

It is **not** the same as a development kit:

| | Dev Mode (retail) | Dev kit |
| --- | --- | --- |
| Package format | `.msix` (UWP) | `.msixvc` / dev kit tooling |
| Memory pool (Game mode) | **5,120 MiB shared** | Larger, and configurable |
| `PAGE_EXECUTE_READWRITE` | **Forbidden** | Allowed |
| GPU API | Direct3D 12 only | Direct3D 12 only |
| Vulkan | **Not present** | Not present |

The two rows in bold are the reason this port's core is shaped the way it is:
strict W^X ([`src/core/uwp_wx.cpp`](../src/core/uwp_wx.cpp)) and a hard memory
budget ([`src/core/uwp_memory.cpp`](../src/core/uwp_memory.cpp)).

## 2. Activating Dev Mode

You need the **Xbox Dev Mode activation app** from the Microsoft Store on the
console, and a Microsoft account.

1. Search the Microsoft Store on the console for *"Xbox Dev Mode"* (or install
   it from `aka.ms/xboxdevmode`).
2. Run it and follow the activation flow. Activation currently has a one-time
   fee charged by Microsoft, not by this project.
3. Note the **developer account name** you set: you will sign into Xbox Device
   Portal with it.
4. The console reboots into Dev Mode. You can switch between Retail and Dev Mode
   from the *Dev Mode* app at any time.

## 3. Reaching Xbox Device Portal

Dev Mode exposes a web administration interface over your LAN.

1. On the Dev Mode home screen, note the console's **IP address**.
2. From a PC on the same network, browse to `https://<xbox-ip>:11443`.
3. Your browser will warn about the self-signed certificate. Proceed — this is
   the console's own management certificate, not the package certificate.
4. Sign in with the developer account from step 2.

> If the page does not load: confirm both devices are on the same subnet, that
> the PC is not on a "guest" Wi-Fi network that blocks device-to-device traffic,
> and that a firewall on the PC is not blocking outbound 11443.

## 4. Installing the signing certificate

The package is signed by a certificate that the console does not know yet.

1. In Xbox Device Portal, open **Home → My games & apps → Certificates** (the
   exact wording changes between Dev Portal versions; on some builds it is under
   *Settings → Certificates*).
2. Upload **`GR2port-XboxDevMode.cer`** from the release bundle.
3. Confirm the entry appears.

Notes:

* The certificate is **self-signed and generated per CI run** unless the
  repository has signing secrets configured. It is valid for two years from the
  build date. If your console rejects the package after a long time, install the
  certificate from a newer release.
* The **`.pfx` is never published** — only the public `.cer`. Publishing the
  private key would let anyone sign a package your console trusts.
* The certificate's subject must equal the manifest's `Identity Publisher`. The
  packaging script checks this and fails the CI job if they diverge, because
  `signtool` alone reports it as an opaque `0x8007000B`.

If you later see `0x800B0109` ("certificate chain not trusted"), this step was
skipped or the wrong `.cer` was installed.

## 5. Installing the package

1. In Xbox Device Portal: **Home → My games & apps → Add**.
2. Select `GR2port-XboxSeriesXS-UWP_x64.msix`.
3. Wait for **"Package installed"**. The first install can take a minute.
4. The title now appears in Dev Mode home as **Gravity Rush 2**.

## 6. Changing App type to Game — mandatory

**This is the step that most often gets skipped, and it looks like a port bug
when it is missed.**

An Xbox UWP title runs in one of two memory modes:

| App type | Guest memory available | What happens |
| --- | --- | --- |
| **App** | ≈1,280 MiB | The emulator cannot commit guest memory: boot ends with `ERROR_COMMITMENT_LIMIT` and the log reports the clamp refusing to grow |
| **Game** | ≈5,120 MiB | Works within the port's 3,200 MiB guest + 1,200 MiB heap budget |

To change it:

1. In Xbox Device Portal, open the installed app's details page.
2. Find **App type** (may be labelled *Application type*).
3. Change **App → Game**.
4. Save and launch the title.

**How the two halves fit together.** The manifest can ask for the game pool but
it cannot set the app type. The ask is the restricted capability
`rescap:expandedResources`, which the package declares; that is what makes the
title *eligible* for the 5 GiB pool. Whether it actually gets it is the
console-side app type above, which is a property of the installed application,
not of the package. There is no `ApplicationType` attribute in the manifest
schema, and there is no `windows.gameOsConfiguration` extension category either
— an earlier revision of this package declared one, and `makeappx` rejected the
whole package with a bare `0x80080204`.

If the capability is missing the title runs in the app pool no matter what the
app type says; if the capability is present but the app type is still `App`, it
also runs in the app pool. Both are checked at startup, and the port logs:

```
Xbox memory: the package is running in App mode (limit 1280 MiB). Gravity Rush 2
needs the ~5 GB Game-mode pool: in Xbox Device Portal open the app's settings and
change App type from 'App' to 'Game', then relaunch.
```

## 7. Preparing your game dump

**You must supply your own legally dumped copy of Gravity Rush 2 (PS4), version
1.11.** This project distributes no game data whatsoever.

The port accepts two layouts:

### Loose files (recommended)

```
E:\GR2\app0\eboot.bin
E:\GR2\app0\sce_sys\param.sfo
E:\GR2\app0\...
```

| Where | When to use it |
| --- | --- |
| `E:\GR2` or `D:\GR2` on a USB 3.0 SSD | **Recommended.** A USB SSD is fast enough to stream the game's assets and costs no internal space. Format the drive NTFS from a PC, or let the console format it as media storage. |
| `LocalState\GR2` | Works, but copies the whole dump into the app's isolated storage and counts against internal space. |
| A `.zar` archive | Supported if your dump is compressed; the port reads it directly. |

`rescap:broadFileSystemAccess` in the manifest is what allows reading the USB
drive; the launcher's path list is built from the drives the console exposes.

### Verifying your dump

* The dump must be **v1.11** of *Gravity Rush 2*. Other versions have different
  code and patches and are not supported.
* `app0/eboot.bin` must exist. If your dump is an `app0` folder plus a separate
  `patch` folder, the `app0` contents are what this port expects.
* Your dump is not committed anywhere and must never be. See
  `.gitignore` and `tools/ci/check_no_game_data.sh`.

## 8. Launching and choosing a preset

Start **Gravity Rush 2** from Dev Mode home. A controller-driven launcher appears
(10-foot UI, fully navigable with the pad):

1. **Game path** — `E:\GR2`, `D:\GR2`, `LocalState\GR2`, or *Browse for a
   `.zar`*.
2. **Preset**

   | Console | Preset | Resolution | Target |
   | --- | --- | --- | --- |
   | Series X | Quality | 4K | 60 FPS |
   | Series X | Performance | 1440p | 120 FPS |
   | Series S | Quality | 1080p | 60 FPS |
   | Series S | Upscaled | 1440p (FSR 3.1) | 60 FPS |

3. **Launch**.

The launcher also hosts the controller-chord editor (see the table in the
[README](../README.md#controller-mapping)).

## 9. Where your data lives

| Item | Location |
| --- | --- |
| Save data | The app's `LocalState` (per Dev Mode account) |
| `log.txt` | The app's `LocalState` — attach this to any bug report |
| Shader/PSO cache | Rebuilt automatically in `LocalState` |
| Your dump | Wherever you put it; never modified by the port |

To retrieve logs, use Xbox Device Portal's **File Explorer** to open the app's
`LocalState` folder and download `log.txt`.

## 10. Uninstalling and resetting

1. **Uninstall the package**: Dev Mode home → highlight the title → *Menu* →
   *Uninstall*. Xbox Device Portal's app list also has an uninstall action.
2. **Remove the certificate**: Dev Portal → Certificates → remove the
   `GR2toXBOX Dev Mode` entry.
3. **Leave Dev Mode**: run the *Dev Mode* app and choose to switch back to
   Retail Mode. Your retail games are untouched.

---

## Legal

Xbox, Xbox Series X|S and DirectX are trademarks of Microsoft Corporation.
PlayStation and *Gravity Rush* are trademarks of Sony Interactive Entertainment.
GR2toXBOX is an unofficial, non-commercial compatibility project, unaffiliated
with and unsupported by either company. You are responsible for owning and
dumping your own copy of the game, and for complying with the law where you live.
