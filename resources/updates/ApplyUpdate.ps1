# SPDX-FileCopyrightText: 2026 Ruwa contributors
# SPDX-License-Identifier: MPL-2.0

$ErrorActionPreference = 'Stop'
$configJson = [Text.Encoding]::UTF8.GetString(
    [Convert]::FromBase64String('__RUWA_CONFIG_BASE64__'))
$config = $configJson | ConvertFrom-Json
$stageDirectory = $null
$backupDirectory = $null
$failedDirectory = $null
$extractDirectory = $null
$oldInstallMoved = $false
$newInstallActivated = $false
$updateSucceeded = $false

function Write-UpdateLog([string]$Message) {
    $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
    Add-Content -LiteralPath $config.logPath -Value ('[' + $timestamp + '] ' + $Message)
}

function Invoke-WithRetry([scriptblock]$Action, [string]$Label) {
    for ($attempt = 1; $attempt -le 20; $attempt++) {
        try {
            & $Action
            return
        } catch {
            if ($attempt -eq 20) {
                throw ($Label + ': ' + $_.Exception.Message)
            }
            Start-Sleep -Milliseconds 500
        }
    }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-SafeRelativePath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or [IO.Path]::IsPathRooted($Path) -or
        $Path.Contains(':') -or $Path.Contains('\') -or $Path.StartsWith('/') -or
        $Path.EndsWith('/')) {
        throw ('Unsafe relative path: ' + $Path)
    }
    foreach ($part in $Path.Split('/')) {
        if ([string]::IsNullOrEmpty($part) -or $part -eq '.' -or $part -eq '..') {
            throw ('Unsafe relative path: ' + $Path)
        }
    }
}

function Resolve-ContainedPath([string]$Root, [string]$RelativePath) {
    Assert-SafeRelativePath $RelativePath
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $candidate = [IO.Path]::GetFullPath(
        [IO.Path]::Combine($Root, $RelativePath.Replace('/', '\')))
    if (-not $candidate.StartsWith($rootPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw ('Path escapes update root: ' + $RelativePath)
    }
    return $candidate
}

function Test-IsElevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Start-Ruwa([string]$ExecutablePath, [string]$WorkingDirectory, [string]$Argument) {
    if (-not (Test-IsElevated)) {
        if ([string]::IsNullOrEmpty($Argument)) {
            Start-Process -FilePath $ExecutablePath -WorkingDirectory $WorkingDirectory
        } else {
            Start-Process -FilePath $ExecutablePath -WorkingDirectory $WorkingDirectory `
                -ArgumentList $Argument
        }
        return
    }
    $shell = New-Object -ComObject Shell.Application
    try {
        $shell.ShellExecute($ExecutablePath, $Argument, $WorkingDirectory, 'open', 1)
    } finally {
        [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
    }
}

function Stop-HealthCheckProcess([string]$Token) {
    Get-CimInstance Win32_Process -Filter "Name = 'Ruwa.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.Contains($Token) } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 800
}

try {
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($config.logPath)) `
        -Force | Out-Null
    Set-Content -LiteralPath $config.logPath -Value (
        '[' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '] Starting signed update')

    if ($config.pid -gt 0) {
        Wait-Process -Id ([int]$config.pid) -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 500

    foreach ($requiredPath in @($config.archivePath, $config.manifestPath, $config.signaturePath)) {
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw ('Required update file is missing: ' + $requiredPath)
        }
    }

    try {
        Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop
    } catch {
        Add-Type -AssemblyName System.Security
    }
    $manifestBytes = [IO.File]::ReadAllBytes($config.manifestPath)
    $signatureBytes = [IO.File]::ReadAllBytes($config.signaturePath)
    $contentInfo = [Security.Cryptography.Pkcs.ContentInfo]::new($manifestBytes)
    $signedCms = [Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
    $signedCms.Decode($signatureBytes)
    $signedCms.CheckSignature($true)
    if ($signedCms.SignerInfos.Count -ne 1 -or -not $signedCms.SignerInfos[0].Certificate) {
        throw 'The update manifest must contain exactly one signer certificate'
    }
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $certificateHash = [BitConverter]::ToString(
            $sha256.ComputeHash($signedCms.SignerInfos[0].Certificate.RawData)).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha256.Dispose()
    }
    if ($certificateHash -ne ([string]$config.trustedCertificateSha256).ToLowerInvariant()) {
        throw 'The update was signed by an unknown publisher'
    }

    $manifest = [Text.Encoding]::UTF8.GetString($manifestBytes) | ConvertFrom-Json
    if ($manifest.format -ne 'ruwa-patch-v1' -or $manifest.product -ne 'Ruwa' -or
        $manifest.platform -ne 'windows' -or $manifest.architecture -ne 'x86_64' -or
        $manifest.version -ne $config.expectedVersion) {
        throw 'The signed update manifest does not match this installation'
    }
    if ([IO.Path]::GetFileName($config.archivePath) -ne $manifest.archive.fileName -or
        (Get-Item -LiteralPath $config.archivePath).Length -ne [long]$manifest.archive.size -or
        (Get-Sha256 $config.archivePath) -ne ([string]$manifest.archive.sha256).ToLowerInvariant()) {
        throw 'The update archive does not match the signed manifest'
    }

    $installDirectory = [IO.Path]::GetFullPath([string]$config.installDirectory).TrimEnd('\')
    $parentDirectory = [IO.Path]::GetDirectoryName($installDirectory)
    $installName = [IO.Path]::GetFileName($installDirectory)
    $suffix = ([string]$config.healthToken).ToLowerInvariant()
    $stageDirectory = Join-Path $parentDirectory ('.' + $installName + '.stage-' + $suffix)
    $backupDirectory = Join-Path $parentDirectory ('.' + $installName + '.backup-' + $suffix)
    $failedDirectory = Join-Path $parentDirectory ('.' + $installName + '.failed-' + $suffix)
    $extractDirectory = Join-Path ([IO.Path]::GetTempPath()) ('RuwaUpdate-' + $suffix)

    foreach ($path in @($stageDirectory, $backupDirectory, $failedDirectory, $extractDirectory)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
    New-Item -ItemType Directory -Path $extractDirectory -Force | Out-Null
    Expand-Archive -LiteralPath $config.archivePath -DestinationPath $extractDirectory -Force

    $expectedSources = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $manifest.files) {
        Assert-SafeRelativePath ([string]$file.source)
        Assert-SafeRelativePath ([string]$file.target)
        [void]$expectedSources.Add(([string]$file.source).Replace('\', '/'))
        $sourcePath = Resolve-ContainedPath $extractDirectory ([string]$file.source)
        $sourceItem = Get-Item -LiteralPath $sourcePath
        if ($sourceItem.PSIsContainer -or ($sourceItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            $sourceItem.Length -ne [long]$file.size -or
            (Get-Sha256 $sourcePath) -ne ([string]$file.sha256).ToLowerInvariant()) {
            throw ('Extracted update file failed validation: ' + $file.source)
        }
    }
    $actualFiles = @(Get-ChildItem -LiteralPath $extractDirectory -Recurse -File -Force)
    if ($actualFiles.Count -ne $expectedSources.Count) {
        throw 'The update archive contains unexpected files'
    }
    foreach ($actualFile in $actualFiles) {
        $relative = $actualFile.FullName.Substring($extractDirectory.Length).TrimStart('\').Replace('\', '/')
        if (-not $expectedSources.Contains($relative)) {
            throw ('Unexpected file in update archive: ' + $relative)
        }
    }

    New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $installDirectory -Force |
        Copy-Item -Destination $stageDirectory -Recurse -Force

    if ($manifest.delete) {
        foreach ($deletePath in $manifest.delete) {
            $destination = Resolve-ContainedPath $stageDirectory ([string]$deletePath)
            if (Test-Path -LiteralPath $destination) {
                Remove-Item -LiteralPath $destination -Recurse -Force
            }
        }
    }
    foreach ($file in $manifest.files) {
        $source = Resolve-ContainedPath $extractDirectory ([string]$file.source)
        $destination = Resolve-ContainedPath $stageDirectory ([string]$file.target)
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) `
            -Force | Out-Null
        if (Test-Path -LiteralPath $destination -PathType Container) {
            throw ('Update target is a directory: ' + $file.target)
        }
        Copy-Item -LiteralPath $source -Destination $destination -Force
        if ((Get-Item -LiteralPath $destination).Length -ne [long]$file.size -or
            (Get-Sha256 $destination) -ne ([string]$file.sha256).ToLowerInvariant()) {
            throw ('Staged update file failed validation: ' + $file.target)
        }
    }

    $stagedExecutable = Join-Path $stageDirectory 'Ruwa.exe'
    if (-not (Test-Path -LiteralPath $stagedExecutable -PathType Leaf)) {
        throw 'Ruwa.exe is missing from the staged installation'
    }

    Invoke-WithRetry { Move-Item -LiteralPath $installDirectory -Destination $backupDirectory } `
        'Failed to back up the current installation'
    $oldInstallMoved = $true
    Invoke-WithRetry { Move-Item -LiteralPath $stageDirectory -Destination $installDirectory } `
        'Failed to activate the staged installation'
    $newInstallActivated = $true

    $newExecutable = Join-Path $installDirectory 'Ruwa.exe'
    Start-Ruwa $newExecutable $installDirectory ('--ruwa-update-health=' + $config.healthToken)
    $deadline = (Get-Date).AddSeconds(60)
    while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath $config.healthMarkerPath)) {
        Start-Sleep -Milliseconds 250
    }
    if (-not (Test-Path -LiteralPath $config.healthMarkerPath) -or
        (Get-Content -LiteralPath $config.healthMarkerPath -Raw).Trim() -ne $config.expectedVersion) {
        throw 'The updated application did not complete its startup health check'
    }

    $updateSucceeded = $true
    Write-UpdateLog ('Update to ' + $config.expectedVersion + ' completed successfully')
    Remove-Item -LiteralPath $backupDirectory -Recurse -Force
    $oldInstallMoved = $false
    Remove-Item -LiteralPath $config.archivePath, $config.manifestPath, $config.signaturePath `
        -Force -ErrorAction SilentlyContinue
} catch {
    Write-UpdateLog ('Update failed: ' + $_.Exception.Message)
    if ($newInstallActivated) {
        Stop-HealthCheckProcess ([string]$config.healthToken)
        if (Test-Path -LiteralPath $config.installDirectory) {
            Invoke-WithRetry {
                Move-Item -LiteralPath $config.installDirectory -Destination $failedDirectory
            } 'Failed to remove the unsuccessful installation'
        }
        $newInstallActivated = $false
    }
    if ($oldInstallMoved -and (Test-Path -LiteralPath $backupDirectory) -and
        -not (Test-Path -LiteralPath $config.installDirectory)) {
        Invoke-WithRetry {
            Move-Item -LiteralPath $backupDirectory -Destination $config.installDirectory
        } 'Failed to restore the previous installation'
        $oldInstallMoved = $false
        Write-UpdateLog 'Previous installation restored'
        $oldExecutable = Join-Path $config.installDirectory 'Ruwa.exe'
        if (Test-Path -LiteralPath $oldExecutable -PathType Leaf) {
            Start-Ruwa $oldExecutable $config.installDirectory $null
        }
    }
} finally {
    Remove-Item -LiteralPath $config.healthMarkerPath -Force -ErrorAction SilentlyContinue
    foreach ($path in @($stageDirectory, $extractDirectory)) {
        if ($path -and (Test-Path -LiteralPath $path)) {
            Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    if ($updateSucceeded -and $failedDirectory -and (Test-Path -LiteralPath $failedDirectory)) {
        Remove-Item -LiteralPath $failedDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }
}
