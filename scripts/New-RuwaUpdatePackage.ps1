# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$SourceDirectory,

    [Parameter(Mandatory = $true)]
    [string]$SigningKeyPath,

    [string]$OutputDirectory,
    [string]$Description = '',
    [string[]]$DeletePath = @(),
    [string]$ReleaseBaseUrl = 'https://github.com/LuskusDeus/Ruwa-releases/releases/download',
    [long]$MaximumPackageBytes = 67108864
)

$ErrorActionPreference = 'Stop'
if ($MaximumPackageBytes -le 0) {
    throw 'MaximumPackageBytes must be greater than zero.'
}
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$') {
    throw 'Version must be a valid Semantic Version, for example 0.2.4 or 0.2.4-alpha.1.'
}
$releaseUri = $null
if (-not [Uri]::TryCreate($ReleaseBaseUrl, [UriKind]::Absolute, [ref]$releaseUri) -or
    $releaseUri.Scheme -ne 'https') {
    throw 'ReleaseBaseUrl must be an absolute HTTPS URL.'
}
$versionWithoutBuild = $Version.Split('+')[0]
if ($versionWithoutBuild.Contains('-')) {
    $prerelease = $versionWithoutBuild.Substring($versionWithoutBuild.IndexOf('-') + 1)
    foreach ($identifier in $prerelease.Split('.')) {
        if ($identifier -match '^0[0-9]+$') {
            throw 'Numeric Semantic Version prerelease identifiers cannot have leading zeroes.'
        }
    }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$sourcePath = [IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\')
$pfxPath = [IO.Path]::GetFullPath($SigningKeyPath)
if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
    throw ('Update source directory does not exist: ' + $sourcePath)
}
if (-not (Test-Path -LiteralPath $pfxPath -PathType Leaf)) {
    throw ('Signing key does not exist: ' + $pfxPath)
}
if ($pfxPath.StartsWith($repoRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The private signing key must be stored outside the Ruwa repository.'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path ([IO.Path]::GetDirectoryName($sourcePath)) ('release-' + $Version)
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
if ($outputPath.Equals($sourcePath, [StringComparison]::OrdinalIgnoreCase) -or
    $outputPath.StartsWith($sourcePath + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The output directory must be outside the update source directory.'
}
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$archiveName = 'Ruwa-' + $Version + '-win64.patch.zip'
$archivePath = Join-Path $outputPath $archiveName
$manifestPath = Join-Path $outputPath 'latest.json'
$signaturePath = Join-Path $outputPath 'latest.json.p7s'
foreach ($path in @($archivePath, $manifestPath, $signaturePath)) {
    if (Test-Path -LiteralPath $path) {
        throw ('Refusing to overwrite an existing release artifact: ' + $path)
    }
}

$files = @()
$totalFileBytes = [long]0
$targetPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($file in Get-ChildItem -LiteralPath $sourcePath -Recurse -File -Force | Sort-Object FullName) {
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw ('Update sources cannot contain reparse points: ' + $file.FullName)
    }
    $relative = $file.FullName.Substring($sourcePath.Length).TrimStart('\').Replace('\', '/')
    $target = $relative
    if ($relative.StartsWith('Main/', [StringComparison]::OrdinalIgnoreCase)) {
        $target = $relative.Substring(5)
    }
    if (-not $targetPaths.Add($target)) {
        throw ('Multiple update files map to the same installation path: ' + $target)
    }
    if ($file.Length -gt $MaximumPackageBytes -or
        $totalFileBytes -gt $MaximumPackageBytes - $file.Length) {
        throw ('Update contents exceed the configured package limit of ' +
            $MaximumPackageBytes + ' bytes.')
    }
    $totalFileBytes += $file.Length
    $files += [ordered]@{
        source = $relative
        target = $target
        size = [long]$file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
if (-not $targetPaths.Contains('Ruwa.exe')) {
    throw 'The update source must contain Main\Ruwa.exe.'
}

$normalizedDeletes = @()
foreach ($path in $DeletePath) {
    $normalized = $path.Replace('\', '/').Trim('/')
    $parts = $normalized.Split('/')
    if ([string]::IsNullOrWhiteSpace($normalized) -or $normalized.Contains(':') -or
        $parts -contains '' -or $parts -contains '.' -or $parts -contains '..' -or
        $targetPaths.Contains($normalized) -or $normalizedDeletes -contains $normalized) {
        throw ('Unsafe or conflicting deletion path: ' + $path)
    }
    $normalizedDeletes += $normalized
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory(
    $sourcePath, $archivePath, [IO.Compression.CompressionLevel]::Optimal, $false)
$archiveItem = Get-Item -LiteralPath $archivePath
if ($archiveItem.Length -gt $MaximumPackageBytes) {
    Remove-Item -LiteralPath $archivePath -Force
    throw ('Compressed update exceeds the configured package limit of ' +
        $MaximumPackageBytes + ' bytes.')
}
$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
$archiveUrl = $ReleaseBaseUrl.TrimEnd('/') + '/v' + $Version + '/' + $archiveName

$manifest = [ordered]@{
    format = 'ruwa-patch-v1'
    product = 'Ruwa'
    version = $Version
    description = $Description
    platform = 'windows'
    architecture = 'x86_64'
    archive = [ordered]@{
        url = $archiveUrl
        fileName = $archiveName
        size = [long]$archiveItem.Length
        sha256 = $archiveHash
    }
    files = $files
    delete = $normalizedDeletes
}
$manifestJson = $manifest | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($manifestPath, $manifestJson, [Text.UTF8Encoding]::new($false))
$manifestBytes = [IO.File]::ReadAllBytes($manifestPath)

$password = Read-Host 'Signing key password' -AsSecureString
$passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($password)
$plainPassword = $null
$certificate = $null
try {
    $plainPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $pfxPath, $plainPassword,
        [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet)
    if (-not $certificate.HasPrivateKey) {
        throw 'The selected PFX does not contain a private signing key.'
    }

    try {
        Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop
    } catch {
        Add-Type -AssemblyName System.Security
    }
    $contentInfo = [Security.Cryptography.Pkcs.ContentInfo]::new($manifestBytes)
    $signedCms = [Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
    $signer = [Security.Cryptography.Pkcs.CmsSigner]::new($certificate)
    $signer.IncludeOption = [Security.Cryptography.X509Certificates.X509IncludeOption]::EndCertOnly
    $signer.DigestAlgorithm = [Security.Cryptography.Oid]::new('2.16.840.1.101.3.4.2.1')
    $signedCms.ComputeSignature($signer, $true)
    [IO.File]::WriteAllBytes($signaturePath, $signedCms.Encode())

    $verification = [Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
    $verification.Decode([IO.File]::ReadAllBytes($signaturePath))
    $verification.CheckSignature($true)

    $publicBytes = $certificate.Export(
        [Security.Cryptography.X509Certificates.X509ContentType]::Cert)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $certificateFingerprint = ([BitConverter]::ToString(
            $sha256.ComputeHash($publicBytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha256.Dispose()
    }
    $trustPath = Join-Path $repoRoot 'cmake\RuwaUpdateTrust.cmake'
    if (-not (Test-Path -LiteralPath $trustPath -PathType Leaf) -or
        -not (Select-String -LiteralPath $trustPath -SimpleMatch $certificateFingerprint -Quiet)) {
        throw 'This signing certificate is not the public key configured in cmake/RuwaUpdateTrust.cmake.'
    }
} catch {
    Remove-Item -LiteralPath $archivePath, $manifestPath, $signaturePath `
        -Force -ErrorAction SilentlyContinue
    throw
} finally {
    if ($certificate) { $certificate.Dispose() }
    if ($passwordPointer -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
    }
    $plainPassword = $null
}

Write-Host ''
Write-Host 'Signed Ruwa update created.' -ForegroundColor Green
Write-Host ('Archive:   ' + $archivePath + ' (' + $archiveItem.Length + ' bytes)')
Write-Host ('Manifest:  ' + $manifestPath)
Write-Host ('Signature: ' + $signaturePath)
Write-Host 'Upload all three files to the matching GitHub release.' -ForegroundColor Yellow
