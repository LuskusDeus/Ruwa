# SPDX-FileCopyrightText: 2026 Ruwa contributors
# SPDX-License-Identifier: MPL-2.0

$ErrorActionPreference = 'Stop'
# This process must not keep the installation directory as its working directory: Windows holds
# an open handle on it without FILE_SHARE_DELETE, which makes the backup rename fail. The host
# already launches us elsewhere; this is the second line of defence.
[Environment]::CurrentDirectory = [IO.Path]::GetTempPath()
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
$scriptStopwatch = [Diagnostics.Stopwatch]::StartNew()

# The log is append-only and shared with the application, so one file tells the whole story of an
# attempt: what the app did before the restart, every step here, and how it ended. Logging is a
# diagnostic aid only - a failure to write must never break or roll back an update.
function Write-UpdateLog([string]$Message) {
    try {
        $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
        $elapsed = '{0,6:0.0}s' -f $scriptStopwatch.Elapsed.TotalSeconds
        Add-Content -LiteralPath $config.logPath `
            -Value ('[' + $timestamp + '] [installer ' + $elapsed + '] ' + $Message)
    } catch {
    }
}

function Get-DirectorySummary([string]$Path) {
    try {
        $files = @(Get-ChildItem -LiteralPath $Path -Recurse -File -Force -ErrorAction Stop)
        $bytes = ($files | Measure-Object -Property Length -Sum).Sum
        if (-not $bytes) { $bytes = 0 }
        return ('{0} files, {1:N1} MB' -f $files.Count, ($bytes / 1MB))
    } catch {
        return ('unreadable: ' + $_.Exception.Message)
    }
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

function Get-InstallProcessIds([string]$ExecutablePath) {
    $expectedPath = [IO.Path]::GetFullPath($ExecutablePath)
    return @(
        Get-CimInstance Win32_Process -Filter "Name = 'Ruwa.exe'" -ErrorAction SilentlyContinue |
            ForEach-Object {
                if ($_.ExecutablePath) {
                    try {
                        $processPath = [IO.Path]::GetFullPath([string]$_.ExecutablePath)
                        if ($processPath.Equals(
                                $expectedPath, [StringComparison]::OrdinalIgnoreCase)) {
                            [int]$_.ProcessId
                        }
                    } catch {
                        # A process can exit while its path is being inspected.
                    }
                }
            }
    )
}

try {
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($config.logPath)) `
        -Force | Out-Null
    $currentVersion = 'unknown'
    if ($config.currentVersion) {
        $currentVersion = [string]$config.currentVersion
    }
    Write-UpdateLog ('Starting signed update: ' + $currentVersion + ' -> ' +
        [string]$config.expectedVersion)
    Write-UpdateLog ('Install directory: ' + [string]$config.installDirectory + ' (' +
        (Get-DirectorySummary ([string]$config.installDirectory)) + ')')
    Write-UpdateLog ('Installer host: PowerShell ' + $PSVersionTable.PSVersion.ToString() +
        ', elevated: ' + (Test-IsElevated) + ', user: ' + [Environment]::UserName)

    if ($config.pid -gt 0) {
        Write-UpdateLog ('Waiting for the application (PID ' + [int]$config.pid + ') to exit')
        Wait-Process -Id ([int]$config.pid) -ErrorAction SilentlyContinue
        Write-UpdateLog 'The application has exited'
    } else {
        Write-UpdateLog 'No application process to wait for'
    }
    Start-Sleep -Milliseconds 500

    foreach ($requiredPath in @($config.archivePath, $config.manifestPath, $config.signaturePath)) {
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw ('Required update file is missing: ' + $requiredPath)
        }
    }
    Write-UpdateLog 'Package, manifest and signature are present'

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
    Write-UpdateLog ('Manifest signature verified, signer ' +
        $signedCms.SignerInfos[0].Certificate.Subject)

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
    Write-UpdateLog ('Manifest accepted: ' + [string]$manifest.version + ', ' +
        @($manifest.files).Count + ' files to replace, ' +
        @($manifest.delete).Count + ' to delete; archive SHA-256 matches')

    $installDirectory = [IO.Path]::GetFullPath([string]$config.installDirectory).TrimEnd('\')
    $parentDirectory = [IO.Path]::GetDirectoryName($installDirectory)
    $installName = [IO.Path]::GetFileName($installDirectory)
    if ([string]::IsNullOrEmpty($parentDirectory) -or [string]::IsNullOrEmpty($installName)) {
        throw ('Ruwa cannot update itself when it is installed in a drive root: ' +
            $installDirectory + '. Move the installation into a subfolder.')
    }
    $suffix = ([string]$config.healthToken).ToLowerInvariant()
    $stageDirectory = Join-Path $parentDirectory ('.' + $installName + '.stage-' + $suffix)
    $backupDirectory = Join-Path $parentDirectory ('.' + $installName + '.backup-' + $suffix)
    $failedDirectory = Join-Path $parentDirectory ('.' + $installName + '.failed-' + $suffix)
    $extractDirectory = Join-Path ([IO.Path]::GetTempPath()) ('RuwaUpdate-' + $suffix)

    Write-UpdateLog ('Working directories: stage ' + $stageDirectory + ', backup ' +
        $backupDirectory + ', extract ' + $extractDirectory)
    foreach ($path in @($stageDirectory, $backupDirectory, $failedDirectory, $extractDirectory)) {
        if (Test-Path -LiteralPath $path) {
            Write-UpdateLog ('Removing a leftover directory from an earlier attempt: ' + $path)
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
    New-Item -ItemType Directory -Path $extractDirectory -Force | Out-Null
    Expand-Archive -LiteralPath $config.archivePath -DestinationPath $extractDirectory -Force
    Write-UpdateLog ('Archive extracted (' + (Get-DirectorySummary $extractDirectory) + ')')

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
    Write-UpdateLog ('All ' + $actualFiles.Count + ' extracted files match the signed manifest')

    New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $installDirectory -Force |
        Copy-Item -Destination $stageDirectory -Recurse -Force
    Write-UpdateLog ('Current installation copied to the stage directory (' +
        (Get-DirectorySummary $stageDirectory) + ')')

    if ($manifest.delete) {
        foreach ($deletePath in $manifest.delete) {
            $destination = Resolve-ContainedPath $stageDirectory ([string]$deletePath)
            if (Test-Path -LiteralPath $destination) {
                Write-UpdateLog ('Deleting from the stage: ' + $deletePath)
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
    Write-UpdateLog ('Stage is ready (' + (Get-DirectorySummary $stageDirectory) + ')')

    $installedExecutable = Join-Path $installDirectory 'Ruwa.exe'
    $runningInstallProcessIds = @(Get-InstallProcessIds $installedExecutable)
    if ($runningInstallProcessIds.Count -gt 0) {
        throw ('Other Ruwa instances are still using this installation (PID: ' +
            ($runningInstallProcessIds -join ', ') + '). Close them before applying the update.')
    }

    Invoke-WithRetry { Move-Item -LiteralPath $installDirectory -Destination $backupDirectory } `
        'Failed to back up the current installation'
    $oldInstallMoved = $true
    Write-UpdateLog 'The previous installation has been moved aside'
    Invoke-WithRetry { Move-Item -LiteralPath $stageDirectory -Destination $installDirectory } `
        'Failed to activate the staged installation'
    $newInstallActivated = $true
    Write-UpdateLog 'The staged installation is now live'

    $newExecutable = Join-Path $installDirectory 'Ruwa.exe'
    Write-UpdateLog ('Starting the updated application for the health check: ' + $newExecutable)
    Start-Ruwa $newExecutable $installDirectory ('--ruwa-update-health=' + $config.healthToken)
    $healthStopwatch = [Diagnostics.Stopwatch]::StartNew()
    $deadline = (Get-Date).AddSeconds(60)
    while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath $config.healthMarkerPath)) {
        Start-Sleep -Milliseconds 250
    }
    if (-not (Test-Path -LiteralPath $config.healthMarkerPath)) {
        throw ('The updated application wrote no health marker within ' +
            ('{0:0.0}' -f $healthStopwatch.Elapsed.TotalSeconds) + 's (it did not start, or it ' +
            'crashed before finishing startup)')
    }
    $reportedVersion = (Get-Content -LiteralPath $config.healthMarkerPath -Raw).Trim()
    if ($reportedVersion -ne $config.expectedVersion) {
        throw ('The updated application reports version "' + $reportedVersion + '" but the ' +
            'manifest promised "' + [string]$config.expectedVersion + '"')
    }
    Write-UpdateLog ('Health check passed after ' +
        ('{0:0.0}' -f $healthStopwatch.Elapsed.TotalSeconds) + 's, the running build reports ' +
        $reportedVersion)

    # The health check is the transaction commit point. Cleanup failures after
    # this point must not roll a healthy installation back.
    $updateSucceeded = $true
    $oldInstallMoved = $false
    Write-UpdateLog ('Update to ' + $config.expectedVersion + ' completed successfully (' +
        (Get-DirectorySummary $installDirectory) + ')')
    try {
        Invoke-WithRetry {
            Remove-Item -LiteralPath $backupDirectory -Recurse -Force
        } 'Failed to remove the previous installation backup'
        Write-UpdateLog 'The previous installation backup has been removed'
    } catch {
        Write-UpdateLog ('Update completed, but backup cleanup failed: ' + $_.Exception.Message)
    }
    Remove-Item -LiteralPath $config.archivePath, $config.manifestPath, $config.signaturePath `
        -Force -ErrorAction SilentlyContinue
} catch {
    $failureOrigin = 'unknown location'
    if ($_.InvocationInfo) {
        $failureOrigin = 'installer line ' + $_.InvocationInfo.ScriptLineNumber
    }
    if ($updateSucceeded) {
        Write-UpdateLog ('Post-commit update cleanup failed at ' + $failureOrigin + ': ' +
            $_.Exception.Message)
    } else {
        Write-UpdateLog ('Update failed at ' + $failureOrigin + ': ' + $_.Exception.Message)
        Write-UpdateLog ('State when it failed: previous installation moved aside = ' +
            $oldInstallMoved + ', new installation activated = ' + $newInstallActivated)
        if ($newInstallActivated) {
            Write-UpdateLog 'Rolling back: stopping the health-check instance'
            Stop-HealthCheckProcess ([string]$config.healthToken)
            if (Test-Path -LiteralPath $config.installDirectory) {
                Invoke-WithRetry {
                    Move-Item -LiteralPath $config.installDirectory -Destination $failedDirectory
                } 'Failed to remove the unsuccessful installation'
                Write-UpdateLog ('The unsuccessful installation was kept for inspection at ' +
                    $failedDirectory)
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
                # The rollback is already complete: a failed restart must not hide it.
                try {
                    Start-Ruwa $oldExecutable $config.installDirectory $null
                    Write-UpdateLog 'The previous version has been restarted'
                } catch {
                    Write-UpdateLog ('The previous version was restored but could not be ' +
                        'restarted: ' + $_.Exception.Message)
                }
            }
        } elseif (-not (Test-Path -LiteralPath $config.installDirectory)) {
            Write-UpdateLog ('WARNING: the installation directory is missing and no backup could ' +
                'be restored. The backup, if any, is at ' + $backupDirectory)
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
    Write-UpdateLog ('Installer finished after ' +
        ('{0:0.0}' -f $scriptStopwatch.Elapsed.TotalSeconds) + 's, update succeeded = ' +
        $updateSucceeded)
}
