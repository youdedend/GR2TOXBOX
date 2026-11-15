<#
.SYNOPSIS
    Creates the ephemeral self-signed certificate that signs the Xbox Dev Mode
    package, or reads one from repository secrets.

.DESCRIPTION
    Xbox Dev Mode accepts a package signed by a certificate the console trusts.
    For a sideloaded build that certificate is self-signed: the user installs the
    .cer on the console (Xbox Device Portal -> Certificates) and then the .msix.

    Either:
      * GR2_CERT_PFX_BASE64 / GR2_CERT_PASSWORD secrets are set -> export them; or
      * no secrets -> generate a throwaway certificate valid for two years.

    The generated certificate's subject MUST match the manifest's Identity
    Publisher exactly, which is why the default is the manifest's own
    "CN=GR2toXBOX Dev Mode".

    The .pfx never leaves the runner: only the .cer (public key) is published,
    because a private key in a public release would let anyone sign a package the
    user's console trusts.
#>
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [string]$Subject = "CN=GR2toXBOX Dev Mode",
    [string]$OutDir = "xbox-package/signing",
    [string]$CerName = "GR2port-XboxDevMode.cer",
    [string]$PfxName = "GR2port-XboxDevMode.pfx"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$cerPath = Join-Path $OutDir $CerName
$pfxPath = Join-Path $OutDir $PfxName

if ($env:GR2_CERT_PFX_BASE64 -and $env:GR2_CERT_PASSWORD) {
    Write-Host "Using the certificate from repository secrets."
    [IO.File]::WriteAllBytes($pfxPath, [Convert]::FromBase64String($env:GR2_CERT_PFX_BASE64))
    $secure = ConvertTo-SecureString -String $env:GR2_CERT_PASSWORD -AsPlainText -Force
    # X509Certificate2 with the password. The Get-PfxCertificate call that used to
    # sit here was both dead (overwritten on the next line) and a hang: a protected
    # .pfx makes it prompt on stdin, which nothing answers in CI.
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2(
        $pfxPath, $secure)
    $cerBytes = $cert.Export(
        [System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
    [IO.File]::WriteAllBytes($cerPath, $cerBytes)
} else {
    Write-Host "No signing secrets configured; generating an ephemeral Dev Mode certificate."
    $cert = New-SelfSignedCertificate `
        -Type Custom `
        -Subject $Subject `
        -KeyUsage DigitalSignature `
        -FriendlyName "GR2toXBOX Xbox Dev Mode" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @(
            "2.5.29.37={text}1.3.6.1.5.5.7.3.3",  # Extended Key Usage: code signing
            "2.5.29.19={text}"                        # Basic Constraints: not a CA
        ) `
        -NotAfter (Get-Date).AddYears(2)

    $password = -join ((48..57) + (65..90) + (97..122) |
        Get-Random -Count 32 | ForEach-Object { [char]$_ })
    $secure = ConvertTo-SecureString -String $password -AsPlainText -Force

    Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $secure | Out-Null
    Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null

    # The signing step needs the password in the same job only.
    "GR2_PFX_PASSWORD=$password" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
}

$cerCert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
    (Resolve-Path $cerPath).Path)
$thumbprint = $cerCert.Thumbprint
$cerCert.Dispose()
Write-Host "Certificate thumbprint: $thumbprint"
"GR2_CERT_THUMBPRINT=$thumbprint" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
"GR2_CERT_SUBJECT=$Subject" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8

# The manifest's Publisher must match the certificate subject; fail loudly here
# rather than at signtool time with a cryptic 0x8007000B.
$manifest = Get-Content "uwp/Package.appxmanifest" -Raw
if ($manifest -notmatch [regex]::Escape($Subject)) {
    Write-Error "The package manifest's Publisher does not match '$Subject'."
}
Write-Host "Certificate created and matched to the manifest Publisher."
