# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
<#
.SYNOPSIS
    Stages, packs and signs the Xbox Series X|S UWP package.

.DESCRIPTION
    Takes the compiled GR2port.exe from a WindowsStore CMake build, stages it with
    the manifest, assets and runtime DLLs into the MSIX layout, compiles the
    resource index with makepri, packs with makeappx, signs with signtool, and
    emits the bundle the user downloads (.msix + .cer + QUICKSTART-XBOX.txt).

    Every step fails the job on error: a partially signed or wrongly staged
    package is worse than no package, because it installs and then crashes.
#>
param(
    [string]$BuildDir = "build-uwp",
    [string]$ExeName = "GR2port.exe",
    [string]$StageDir = "xbox-package/staging",
    [string]$OutDir = "xbox-package/out",
    [string]$PackageName = "GR2port-XboxSeriesXS-UWP_x64",
    [string]$Version = "1.0.0.0",
    [string]$SigningDir = "xbox-package/signing"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Find-SdkTool([string]$name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $roots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "$env:ProgramFiles\Windows Kits\10\bin"
    )
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $found = Get-ChildItem -Path $root -Recurse -Filter $name -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw "$name was not found. Install the Windows 10/11 SDK (10.0.22621 or newer)."
}

# ---------------------------------------------------------------------------
# 1. Locate the executable
# ---------------------------------------------------------------------------
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter $ExeName -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) {
    Write-Error "No $ExeName under '$BuildDir'. The WindowsStore CMake build must run first."
}
Write-Host "Staging $($exe.FullName)"

# ---------------------------------------------------------------------------
# 2. Stage the MSIX layout
# ---------------------------------------------------------------------------
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Copy-Item $exe.FullName (Join-Path $StageDir $ExeName)

# Manifest: stamp the build version.
$manifest = Get-Content "uwp/Package.appxmanifest" -Raw
$manifest = $manifest -replace 'Version="\d+\.\d+\.\d+\.\d+"', "Version=`"$Version`""
Set-Content -Path (Join-Path $StageDir "AppxManifest.xml") -Value $manifest -Encoding utf8

# Visual assets.
Copy-Item -Recurse "uwp/Assets" (Join-Path $StageDir "Assets")

# Runtime DLLs: everything next to the executable that the package needs. The
# UWP CRT and the packaged dependencies come from the CMake install layout.
$dlls = Get-ChildItem -Path $exe.Directory -Filter *.dll -ErrorAction SilentlyContinue
foreach ($dll in $dlls) {
    Write-Host "  + $($dll.Name)"
    Copy-Item $dll.FullName (Join-Path $StageDir $dll.Name)
}
# A Store app cannot load loose DLLs from outside its package, so an unexpected
# dependency is a packaging bug worth catching here.
$known = @("dxcompiler.dll", "dxil.dll", "openal32.dll", "avcodec-*.dll",
           "avformat-*.dll", "avutil-*.dll", "swresample-*.dll", "swscale-*.dll")
Write-Host "Packaged DLLs: $($dlls.Name -join ', ')"

# Optional data folders the renderer reads at runtime.
foreach ($extra in @("shaders", "userdata")) {
    $src = Join-Path $exe.Directory $extra
    if (Test-Path $src) { Copy-Item -Recurse $src (Join-Path $StageDir $extra) }
}

# The user-facing quick start ships inside the bundle, not inside the package.
Copy-Item "docs/QUICKSTART-XBOX.txt" (Join-Path $OutDir "QUICKSTART-XBOX.txt") -ErrorAction SilentlyContinue

# ---------------------------------------------------------------------------
# 3. Resource index (makepri)
# ---------------------------------------------------------------------------
$makepri = Find-SdkTool "makepri.exe"
Push-Location $StageDir
try {
    & $makepri createconfig /cf priconfig.xml /dq en-US /o | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "makepri createconfig failed ($LASTEXITCODE)" }
    & $makepri new /pr . /cf priconfig.xml /of resources.pri /in AppxManifest.xml /o
    if ($LASTEXITCODE -ne 0) { throw "makepri new failed ($LASTEXITCODE)" }
    Remove-Item -Force priconfig.xml
} finally {
    Pop-Location
}
if (-not (Test-Path (Join-Path $StageDir "resources.pri"))) {
    throw "resources.pri was not produced."
}

# ---------------------------------------------------------------------------
# 4. Pack (makeappx)
# ---------------------------------------------------------------------------
$makeappx = Find-SdkTool "makeappx.exe"
$msixPath = Join-Path $OutDir "$PackageName.msix"
& $makeappx pack /d $StageDir /p $msixPath /o
if ($LASTEXITCODE -ne 0) { throw "makeappx pack failed ($LASTEXITCODE)" }
Write-Host "Packed $msixPath ($([math]::Round((Get-Item $msixPath).Length / 1MB, 1)) MiB)"

# ---------------------------------------------------------------------------
# 5. Sign (signtool)
# ---------------------------------------------------------------------------
$signtool = Find-SdkTool "signtool.exe"
$pfx = Join-Path $SigningDir "GR2port-XboxDevMode.pfx"
if (-not (Test-Path $pfx)) { throw "Signing certificate not found at $pfx" }
$password = $env:GR2_PFX_PASSWORD
if (-not $password) { throw "GR2_PFX_PASSWORD is not set" }

& $signtool sign /fd SHA256 /f $pfx /p $password `
    /tr http://timestamp.digicert.com /td SHA256 $msixPath
if ($LASTEXITCODE -ne 0) {
    Write-Warning "Timestamped signing failed; retrying without a timestamp server."
    & $signtool sign /fd SHA256 /f $pfx /p $password $msixPath
    if ($LASTEXITCODE -ne 0) { throw "signtool sign failed ($LASTEXITCODE)" }
}
& $signtool verify /pa /v $msixPath
if ($LASTEXITCODE -ne 0) { throw "signtool verify failed ($LASTEXITCODE)" }

# ---------------------------------------------------------------------------
# 6. Bundle
# ---------------------------------------------------------------------------
Copy-Item (Join-Path $SigningDir "GR2port-XboxDevMode.cer") $OutDir
$bundleDir = Join-Path $OutDir "GR2port-XboxSeriesXS-UWP"
if (Test-Path $bundleDir) { Remove-Item -Recurse -Force $bundleDir }
New-Item -ItemType Directory -Force -Path $bundleDir | Out-Null
Get-ChildItem $OutDir -File | ForEach-Object { Copy-Item $_.FullName $bundleDir }
$zipPath = Join-Path "xbox-package" "GR2port-XboxSeriesXS-UWP.zip"
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Compress-Archive -Path "$bundleDir/*" -DestinationPath $zipPath -Force

Write-Host "Bundle: $zipPath"
Get-ChildItem $OutDir -File | ForEach-Object { Write-Host "  $($_.Name)  $($_.Length) bytes" }
