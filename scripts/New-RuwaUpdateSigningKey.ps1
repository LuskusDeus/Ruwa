# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPfxPath,

    [string]$OutputCertificatePath,

    [string]$TrustConfigPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$pfxPath = [IO.Path]::GetFullPath($OutputPfxPath)
if ($pfxPath.StartsWith($repoRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The private signing key must be stored outside the Ruwa repository.'
}
if ([string]::IsNullOrWhiteSpace($OutputCertificatePath)) {
    $OutputCertificatePath = [IO.Path]::ChangeExtension($pfxPath, '.cer')
}
if ([string]::IsNullOrWhiteSpace($TrustConfigPath)) {
    $TrustConfigPath = Join-Path $repoRoot 'cmake\RuwaUpdateTrust.cmake'
}
$certificatePath = [IO.Path]::GetFullPath($OutputCertificatePath)
$trustPath = [IO.Path]::GetFullPath($TrustConfigPath)

foreach ($path in @($pfxPath, $certificatePath)) {
    if (Test-Path -LiteralPath $path) {
        throw ('Refusing to overwrite existing key material: ' + $path)
    }
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($path)) -Force | Out-Null
}
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($trustPath)) -Force | Out-Null

$password = Read-Host 'Choose a strong password for the Ruwa update signing key' -AsSecureString
$passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($password)
$plainPassword = $null
$rsa = $null
$certificate = $null
try {
    $plainPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
    $rsa = [Security.Cryptography.RSA]::Create(3072)
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
        'CN=Ruwa Update Signing', $rsa,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add(
        [Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
            [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature, $true))
    $oids = [Security.Cryptography.OidCollection]::new()
    [void]$oids.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
    $request.CertificateExtensions.Add(
        [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($oids, $true))
    $certificate = $request.CreateSelfSigned((Get-Date).AddMinutes(-5), (Get-Date).AddYears(10))

    [IO.File]::WriteAllBytes($pfxPath,
        $certificate.Export(
            [Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $plainPassword))
    $publicBytes = $certificate.Export(
        [Security.Cryptography.X509Certificates.X509ContentType]::Cert)
    [IO.File]::WriteAllBytes($certificatePath, $publicBytes)

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $fingerprint = ([BitConverter]::ToString($sha256.ComputeHash($publicBytes))).Replace(
            '-', '').ToLowerInvariant()
    } finally {
        $sha256.Dispose()
    }
    $trustContents = @"
# SPDX-License-Identifier: MPL-2.0
# Public update trust anchor. This file does not contain a private key.
set(RUWA_UPDATE_SIGNER_CERT_SHA256
    "$fingerprint"
    CACHE STRING "SHA-256 fingerprint of the trusted DER update-signing certificate" FORCE)
"@
    [IO.File]::WriteAllText($trustPath, $trustContents, [Text.UTF8Encoding]::new($false))
} finally {
    if ($certificate) { $certificate.Dispose() }
    if ($rsa) { $rsa.Dispose() }
    if ($passwordPointer -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
    }
    $plainPassword = $null
}

Write-Host ''
Write-Host 'Ruwa update signing key created.' -ForegroundColor Green
Write-Host ('Private key:       ' + $pfxPath)
Write-Host ('Public certificate: ' + $certificatePath)
Write-Host ('Public trust config: ' + $trustPath)
Write-Host ''
Write-Host 'Back up the PFX and its password. Never commit or upload the PFX.' -ForegroundColor Yellow
