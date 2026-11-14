# Building from source

<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

Two targets are supported:

| Target | Configure | Output |
| --- | --- | --- |
| Windows PC, Direct3D 12 | `-DGR2_RENDERER=D3D12` | `GR2port.exe` |
| Xbox Series X|S UWP | `-DGR2_XBOX_UWP=ON -DGR2_RENDERER=D3D12 -DCMAKE_SYSTEM_NAME=WindowsStore` | signed `.msix` |
| Windows PC, Vulkan (upstream) | default | `GR2port.exe` |

CI builds both Windows targets on every push; see
[`.github/workflows/build-xbox-uwp.yml`](../.github/workflows/build-xbox-uwp.yml).
Building locally is only necessary if you are changing the code.

---

## 1. Prerequisites

### All Windows targets

* **Visual Studio 2022** (17.8+) with *Desktop development with C++* and the
  *C++/WinRT* and *Windows 10 SDK* components.
* **Windows SDK 10.0.22621** or newer — provides `dxc.exe`, `makeappx.exe`,
  `makepri.exe`, `signtool.exe`.
* **CMake 3.24+** and **Ninja**.
* **DirectXShaderCompiler** (`dxc.exe`). Recent SDKs ship it; if yours does not,
  take the release from <https://github.com/microsoft/DirectXShaderCompiler/releases>
  and pass `-DGR2_DXC_EXECUTABLE=<path>`.

### Xbox UWP additionally

* The **Windows SDK UWP** components (the onecore import libraries —
  `WindowsApp.lib` — come with them).
* Optionally, the **Windows App Certification Kit**, to check the package
  against Store rules before sideloading.

The build deliberately **fails at configure time** if `dxc` cannot be found,
rather than producing a binary with no shaders in it.

---

## 2. Clone with submodules

GR2port's dependencies are git submodules:

```bash
git clone --recursive https://github.com/youdedend/GR2TOXBOX.git
cd GR2TOXBOX
# already cloned without --recursive:
git submodule update --init --recursive
```

---

## 3. Windows PC, Direct3D 12

```powershell
cmake -S . -B build-win64 -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DGR2_RENDERER=D3D12 `
      -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl

cmake --build build-win64 --parallel
ctest --test-dir build-win64 --output-on-failure
```

Notes:

* The desktop D3D12 build **keeps the upstream memory model** (PS4 Pro devkit
  totals, unbounded heap) so it can be compared against the Vulkan build on the
  same machine. The console budgets only engage on `GR2_XBOX_UWP`.
* It does **not** disable ASLR (`/DYNAMICBASE:NO`), because a dynamic-base image
  is what Store/Dev Mode requires and what the Xbox build ships. If you are
  debugging an address-space issue that upstream's fixed base hides, build the
  Vulkan target instead.

---

## 4. Xbox Series X|S UWP package

```powershell
cmake -S . -B build-uwp -G Ninja `
      -DCMAKE_SYSTEM_NAME=WindowsStore `
      -DCMAKE_SYSTEM_VERSION=10.0.22621.0 `
      -DCMAKE_BUILD_TYPE=Release `
      -DGR2_XBOX_UWP=ON `
      -DGR2_RENDERER=D3D12 `
      -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl

cmake --build build-uwp --parallel
```

Then stamp, pack and sign:

```powershell
# 1. Create a self-signed Dev Mode certificate. Its subject MUST match the
#    manifest's Identity Publisher (the script verifies this).
./scripts/ci/make_certificate.ps1 -OutDir xbox-package/signing

# 2. Stage, run makepri, pack with makeappx, sign and verify with signtool.
./scripts/ci/package_msix.ps1 -BuildDir build-uwp -SigningDir xbox-package/signing
```

Result:

```
xbox-package/out/GR2port-XboxSeriesXS-UWP_x64.msix
xbox-package/out/GR2port-XboxDevMode.cer
xbox-package/GR2port-XboxSeriesXS-UWP.zip
```

Install with [docs/XBOX_DEV_MODE_GUIDE.md](XBOX_DEV_MODE_GUIDE.md).

### Signing with your own certificate

The CI uses an ephemeral certificate unless the repository has:

| Secret | Contents |
| --- | --- |
| `GR2_CERT_PFX_BASE64` | your `.pfx`, base64-encoded |
| `GR2_CERT_PASSWORD` | its password |

If you use your own, edit `Identity Publisher` in `uwp/Package.appxmanifest` to
match the certificate's subject. The packaging script fails the build when they
disagree, which is cheaper than decoding `signtool`'s `0x8007000B`.

---

## 5. Shader work

Every HLSL shader compiles at build time. To compile one by hand while iterating:

```powershell
dxc -T vs_6_6 -E main -HV 2021 -O3 -WX `
    -I src/graphics/gr2port_d3d12/shaders `
    src/graphics/gr2port_d3d12/shaders/world.vert.hlsl -Fo world.dxil
```

To see how much of the shader set is ported:

```bash
python3 tools/shaders/check_shader_coverage.py --missing
```

---

## 6. Tests

```bash
ctest --test-dir build-win64 --output-on-failure
```

| Test | Covers |
| --- | --- |
| `xbox_memory_budget` | guest/heap/pool budget arithmetic and the clamp, in both build modes |
| `xbox_chords` | controller-chord defaults, edge triggering, swallow behaviour |

Both are dependency-free executables (no gtest), so they run anywhere.

---

## 7. Troubleshooting the build

| Symptom | Cause / fix |
| --- | --- |
| `GR2_RENDERER=D3D12 needs the DirectXShaderCompiler` | pass `-DGR2_DXC_EXECUTABLE=<path>` or install the SDK |
| `GR2_XBOX_UWP requires GR2_RENDERER=D3D12` | Xbox has no Vulkan driver; add `-DGR2_RENDERER=D3D12` |
| `GR2_XBOX_UWP requires -DCMAKE_SYSTEM_NAME=WindowsStore` | add the system name and version |
| a shader fails with a semantic or binding error | intended: `-WX` turns warnings into errors so a broken shader cannot ship |
| `makeappx` "package is unsigned" | run `package_msix.ps1`, not `makeappx` alone |
| `signtool` `0x8007000B` | certificate subject ≠ manifest `Publisher` |
| submodule directories are empty | `git submodule update --init --recursive` |
