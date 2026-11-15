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
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
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

# Manifest: stamp the build version onto the package Identity, and only there.
#
# A blanket -replace on Version="d.d.d.d" rewrites every Version attribute in the
# file, and there is a second one: TargetDeviceFamily's MinVersion. That left the
# generated manifest declaring MinVersion="0.1.0.0" -- not a valid OS version --
# which makeappx accepts and the package then refuses to install, so the failure
# would have looked like a signing problem on the console rather than a manifest
# bug here. Anchoring on the Identity element is what keeps the two apart.
# A package version is four 16-bit fields, and anything outside that makes
# makeappx fail with a message about the manifest rather than about the version.
if ($Version -notmatch '^\d{1,5}\.\d{1,5}\.\d{1,5}\.\d{1,5}$') {
    throw "Version '$Version' is not a quad-dotted package version (each field 0-65535)."
}
foreach ($field in $Version -split '\.') {
    if ([int]$field -gt 65535) {
        throw "Version '$Version' has a field above 65535, which a package version cannot express."
    }
    if ([int]$field -ne 0 -and $field -match '^0\d') {
        throw "Version '$Version' has a leading zero in a field, which makeappx rejects."
    }
}

$manifest = Get-Content "uwp/Package.appxmanifest" -Raw
$stamped = $manifest -replace '(?s)(<Identity\b[^>]*?\bVersion=")\d+\.\d+\.\d+\.\d+(")', "`${1}$Version`${2}"
if ($stamped -eq $manifest -and $manifest -notmatch '<Identity\b[^>]*?\bVersion=') {
    throw "Package.appxmanifest has no Identity Version to stamp; the manifest layout changed."
}
$manifest = $stamped
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
Write-Host "Packaged DLLs: $($dlls.Name -join ', ')"

# A packaged app cannot load loose DLLs from outside its package, so a DLL that
# turns up beside the executable and is not staged is a dependency that will fail
# at load on the console with nothing but a missing-import in the event log. The
# allow-list is what the build is expected to produce; anything else is either a
# new dependency that has to be staged deliberately or a build directory that has
# picked up a stray toolchain DLL.
$known = @("dxcompiler.dll", "dxil.dll", "openal32.dll", "avcodec-*.dll",
           "avformat-*.dll", "avutil-*.dll", "swresample-*.dll", "swscale-*.dll",
           "zlib*.dll", "libz*.dll")
$unexpected = @($dlls | Where-Object {
    $name = $_.Name
    -not ($known | Where-Object { $name -like $_ })
} | ForEach-Object { $_.Name })
if ($unexpected.Count -gt 0) {
    # Warn rather than fail: a legitimate new dependency must not need a script
    # edit to ship, but it must be visible in the log when a load fails later.
    Write-Warning "These DLLs are staged but are not in the expected set: $($unexpected -join ', ')."
    Write-Warning "If they are a real dependency, add them to the allow-list in package_msix.ps1."
}

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
# Resolve the staging directory before changing into it. Everything below runs
# with the staging directory as the working directory, so a relative $StageDir
# would resolve against itself -- Join-Path $StageDir 'AppxManifest.xml' from
# inside the staging directory produces
# staging\staging\AppxManifest.xml, and the only symptom is a "cannot find path"
# from whichever line reads it first.
$StageFull = (Resolve-Path $StageDir).Path
Push-Location $StageFull
try {
    # /dq is a single *default* qualifier set, not a list of languages.
    #
    # Two things make this easy to get wrong, and both were wrong here first:
    #
    #   * a qualifier is a token of the form <dimension>-<value>, so the language
    #     qualifier is 'lang-en-US', not 'en-US'. A bare language tag is not a
    #     qualifier token and makepri createconfig rejects it outright.
    #   * within one set, qualifiers are separated by underscores
    #     (lang-en-US_scale-100), and separators are for *different* dimensions.
    #     There is exactly one default language, so a comma-separated list of
    #     languages is never valid here.
    #
    # The manifest is what tells 'makepri new' the full set of languages the
    # package supports; /dq only says which one is the default. So the primary
    # language from the manifest is the right input, and the rest of the list does
    # not belong on this command line.
    $manifestXml = [xml](Get-Content (Join-Path $StageFull "AppxManifest.xml") -Raw)
    $languages = @($manifestXml.Package.Resources.Resource |
        ForEach-Object { $_.Language } | Where-Object { $_ })
    if ($languages.Count -eq 0) { $languages = @("en-US") }
    $primary = $languages[0]
    $parts = $primary -split '-'
    if ($parts.Count -eq 2) { $primary = "$($parts[0])-$($parts[1].ToUpper())" }
    $defaultQualifiers = "lang-$primary"
    Write-Host "Default qualifier set: $defaultQualifiers (manifest languages: $($languages -join ', '))"
    # Not piped to Out-Null: when makepri refuses a qualifier it says so, and that
    # sentence is the whole diagnosis. Discarding it left "makepri createconfig
    # failed (1)" and no reason.
    & $makepri createconfig /cf priconfig.xml /dq $defaultQualifiers /o
    if ($LASTEXITCODE -ne 0) {
        throw "makepri createconfig failed ($LASTEXITCODE) for qualifiers '$defaultQualifiers'"
    }
    & $makepri new /pr . /cf priconfig.xml /of resources.pri /in AppxManifest.xml /o
    if ($LASTEXITCODE -ne 0) { throw "makepri new failed ($LASTEXITCODE)" }
    Remove-Item -Force priconfig.xml
} finally {
    Pop-Location
}
if (-not (Test-Path (Join-Path $StageFull "resources.pri"))) {
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

# Sign without a timestamp server.
#
# A timestamp is not needed here and costs the job. The timestamp only says "this
# signature was made before the certificate expired", which matters for a
# long-lived signature on something a stranger will verify years later. This
# package is signed by a Dev Mode certificate that the user installs on their own
# console moments before sideloading it, so the RFC 3161 round trip buys nothing.
#
# It does cost something: /tr has no timeout of its own, so if the timestamp
# authority is unreachable from the runner signtool sits on the socket until the
# job's timeout expires. That is exactly what happened -- the packaging step ran
# for 87 minutes with no output before it was killed.
& $signtool sign /fd SHA256 /f $pfx /p $password $msixPath
if ($LASTEXITCODE -ne 0) { throw "signtool sign failed ($LASTEXITCODE)" }
# How to verify a package signed by a self-signed Dev Mode certificate.
#
# `/pa /v` alone always fails here, and not because the signature is wrong: the
# certificate chains to itself, so Authenticode reports "the certificate chain
# processed but terminated in a root certificate which is not trusted by the
# trust anchor" -- which is the correct answer to "is this signed by a publicly
# trusted authority" and the wrong question to ask of a Dev Mode package. The
# console does not consult a public anchor either: it trusts the certificate the
# user sideloaded alongside the package, which is exactly the .cer in this bundle.
#
# So verify what actually matters:
#
#   1. the file carries a signature at all (`/pa` without certificate-chain
#      revocation, which cannot succeed for a self-signed cert);
#   2. it was signed by *this* certificate -- comparing the signer thumbprint with
#      the exported .cer is what catches a stale PFX, which would install nowhere;
#   3. the bundle ships the .cer that matches, since that is what gets trusted.
#
# A failure in (2) is a real packaging bug. A failure in (1) means the package is
# unsigned. Neither is "the chain is untrusted".
# Run it once and keep the output: the reason it failed belongs in the message if
# the checks below reject the package, and running it a second time just prints the
# same failure twice.
$verifyOutput = (& $signtool verify /pa $msixPath 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    Write-Warning "signtool verify /pa reported a problem; checking the signer directly."
}

# The signer's thumbprint is what has to match the certificate the bundle ships.
#
# Load the .pfx through X509Certificate2 with the password, NEVER through
# Get-PfxCertificate. A .pfx with a password is a *protected* format, and
# Get-PfxCertificate has no -Password parameter: on a protected file it prompts on
# stdin ("Enter password for the private-key file:") and waits. In a headless CI
# step nothing ever types, so the step sits there until the job times out -- no
# output, no error, nothing in the log. That is what hung this job for 80 minutes.
# X509Certificate2 takes the password as an argument and never prompts.
$pfxCert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
    (Resolve-Path $pfx).Path, $password, 'EphemeralKeySet')
$expectedThumb = $pfxCert.Thumbprint
$pfxCert.Dispose()
$packageCert = Get-AuthenticodeSignature -FilePath $msixPath
if ($packageCert.Status -eq 'NotSigned' -or -not $packageCert.SignerCertificate) {
    throw "The package is not signed. signtool reported: $verifyOutput"
}
# 'UntrustedRoot' is expected and fine. Anything else is not: HashMismatch means
# the file changed after it was signed, NotTrusted/NotSignatureValid means the
# signature itself is bad. Rejecting those by name is the point of checking Status
# at all -- accepting "any status with a signer certificate attached" would let a
# tampered package through, which is the one thing a verify step exists to stop.
if ($packageCert.Status -notin @('Valid', 'UntrustedRoot')) {
    throw ("The package's signature is not sound: Status=$($packageCert.Status) " +
           "$($packageCert.StatusMessage)")
}
if ($packageCert.SignerCertificate.Thumbprint -ne $expectedThumb) {
    throw ("The package is signed by a different certificate than the one being " +
           "shipped: package $($packageCert.SignerCertificate.Thumbprint), " +
           "expected $expectedThumb $($packageCert.SignerCertificate.Subject)")
}
# The .cer next to the .msix is what the user installs into Trusted People; if it
# is not the signer's certificate the package will refuse to install with
# 0x800B0109 and look like a corrupt download.
$cerPath = Join-Path $SigningDir "GR2port-XboxDevMode.cer"
# Same reasoning: X509Certificate2, not Get-PfxCertificate. A .cer holds no private
# key so it is not password-protected in practice, but the rule is the rule -- and
# the one place it is violated is the one place that hangs.
$cerCert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
    (Resolve-Path $cerPath).Path)
$shipped = $cerCert.Thumbprint
$cerCert.Dispose()
if ($shipped -ne $expectedThumb) {
    throw "The .cer in the bundle ($shipped) is not the signing certificate ($expectedThumb)."
}
Write-Host "Signed by $($packageCert.SignerCertificate.Subject) ($expectedThumb), and the bundle ships the matching .cer."
Write-Host "Note: the chain terminates in an untrusted self-signed root by design; the console trusts the shipped .cer." 

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
