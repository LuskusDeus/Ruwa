# SPDX-License-Identifier: MPL-2.0

<#
.SYNOPSIS
    Check or add MPL-2.0 SPDX headers in Ruwa-owned source and configuration files.

.DESCRIPTION
    Covers C/C++, GLSL, CMake, public project scripts, Windows resources, Qt XML
    manifests/translations, GitHub YAML, and selected root configuration files.
    Assets, ignored local experiments, third-party, generated, and build files
    are excluded. Files carrying another SPDX identifier are reported as
    conflicts and are never rewritten.

    Check mode is the default and exits non-zero when headers are missing.
    Apply mode inserts the syntax-appropriate SPDX marker while preserving UTF
    encoding, BOM, line endings, shebang/encoding declarations, XML declarations,
    and GLSL #version placement.

.PARAMETER Check
    Check coverage without modifying files. This is the default mode.

.PARAMETER Apply
    Add the MPL-2.0 SPDX marker to every eligible file that lacks one.

.EXAMPLE
    pwsh scripts/manage-license-headers.ps1 -Check

.EXAMPLE
    pwsh scripts/manage-license-headers.ps1 -Apply
#>
[CmdletBinding(DefaultParameterSetName = "Check")]
param(
    [Parameter(ParameterSetName = "Check")]
    [switch]$Check,

    [Parameter(Mandatory, ParameterSetName = "Apply")]
    [switch]$Apply
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$spdxIdentifier = "MPL-2.0"
$cxxExtensions = @(".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl")
$shaderExtensions = @(".glsl", ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese")
$ownedCodeRoots = @("src", "sdk", "plugins", "tests", "tools")
$searchRoots = $ownedCodeRoots + @("cmake", "scripts", ".github", "translations")
$rootFiles = @(
    "CMakeLists.txt",
    "app.rc",
    "resources.qrc",
    ".clang-format",
    ".clang-tidy",
    ".editorconfig",
    ".gitignore"
)
$excludedDirectoryPattern = '(^|/)(third-party|generated|build|node_modules|__pycache__)(/|$)'
$excludedScriptPattern = '^scripts/(extract_shader\.py|generate_project\.js|kSmudgePickup[^/]*\.glsl|parse_dump\.py|resolve_stack\.py|sim_[^/]*\.(py|png)|package(-lock)?\.json)$'

function Get-RepoRelativePath {
    param([Parameter(Mandatory)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($repoRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the repository: $fullPath"
    }
    return $fullPath.Substring($repoRoot.Length).TrimStart('\', '/') -replace '\\', '/'
}

function New-LicenseProfile {
    param(
        [Parameter(Mandatory)][string]$Header,
        [ValidateSet("Top", "ScriptPreamble", "ShaderVersion", "XmlDeclaration")]
        [string]$Insertion = "Top"
    )

    return [pscustomobject]@{ Header = $Header; Insertion = $Insertion }
}

function Get-LicenseProfile {
    param([Parameter(Mandatory)][string]$RelativePath)

    $path = $RelativePath -replace '\\', '/'
    $lowerPath = $path.ToLowerInvariant()
    if ($lowerPath -match $excludedDirectoryPattern -or $lowerPath -match $excludedScriptPattern) {
        return $null
    }

    $extension = [IO.Path]::GetExtension($lowerPath)
    $fileName = [IO.Path]::GetFileName($lowerPath)
    $topLevel = ($lowerPath -split '/')[0]
    # REUSE-IgnoreStart
    # These strings generate headers; they are not licence declarations for this script.
    $slashHeader = "// SPDX-License-Identifier: $spdxIdentifier"
    $hashHeader = "# SPDX-License-Identifier: $spdxIdentifier"
    $xmlHeader = "<!-- SPDX-License-Identifier: $spdxIdentifier -->"
    $blockHeader = "/* SPDX-License-Identifier: $spdxIdentifier */"
    # REUSE-IgnoreEnd

    if ($ownedCodeRoots -contains $topLevel -and $cxxExtensions -contains $extension) {
        return New-LicenseProfile -Header $slashHeader
    }
    if ($lowerPath.StartsWith("src/shared/shaders/") -and $shaderExtensions -contains $extension) {
        return New-LicenseProfile -Header $slashHeader -Insertion "ShaderVersion"
    }
    if (($fileName -eq "cmakelists.txt" -and ($topLevel -eq "cmakelists.txt" -or $ownedCodeRoots -contains $topLevel)) `
        -or ($lowerPath.StartsWith("cmake/") -and $extension -eq ".cmake")) {
        return New-LicenseProfile -Header $hashHeader
    }
    if ($lowerPath.StartsWith("cmake/") -and $lowerPath.EndsWith(".h.in")) {
        return New-LicenseProfile -Header $slashHeader
    }
    if ($lowerPath.StartsWith("scripts/")) {
        if ($extension -in ".ps1", ".py") {
            return New-LicenseProfile -Header $hashHeader -Insertion "ScriptPreamble"
        }
        if ($extension -eq ".js") {
            return New-LicenseProfile -Header $slashHeader -Insertion "ScriptPreamble"
        }
        if ($shaderExtensions -contains $extension) {
            return New-LicenseProfile -Header $slashHeader -Insertion "ShaderVersion"
        }
    }
    if ($lowerPath.StartsWith(".github/") -and $extension -in ".yml", ".yaml") {
        return New-LicenseProfile -Header $hashHeader
    }
    if ($lowerPath.StartsWith("translations/") -and $extension -eq ".ts") {
        return New-LicenseProfile -Header $xmlHeader -Insertion "XmlDeclaration"
    }
    if ($lowerPath -eq "resources.qrc") {
        return New-LicenseProfile -Header $xmlHeader -Insertion "XmlDeclaration"
    }
    if ($lowerPath -eq "app.rc") {
        return New-LicenseProfile -Header $blockHeader
    }
    if ($lowerPath -in ".clang-format", ".clang-tidy", ".editorconfig", ".gitignore") {
        return New-LicenseProfile -Header $hashHeader
    }
    if ($ownedCodeRoots -contains $topLevel -and $extension -eq ".qss") {
        return New-LicenseProfile -Header $blockHeader
    }

    return $null
}

function Get-OwnedSourceFiles {
    $files = foreach ($rootName in $searchRoots) {
        $rootPath = Join-Path $repoRoot $rootName
        if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) {
            continue
        }

        # -Force so dot-directories (e.g. .github) and their contents are
        # enumerated on Unix, where a leading dot marks an item hidden.
        Get-ChildItem -LiteralPath $rootPath -File -Recurse -Force
    }
    $files = @($files)
    foreach ($fileName in $rootFiles) {
        $filePath = Join-Path $repoRoot $fileName
        if (Test-Path -LiteralPath $filePath -PathType Leaf) {
            # -Force so root dotfiles (.clang-format, .gitignore, ...) resolve on
            # Unix; without it Get-Item skips hidden items and throws.
            $files += Get-Item -LiteralPath $filePath -Force
        }
    }

    $eligible = foreach ($file in $files | Sort-Object FullName -Unique) {
        $relativePath = Get-RepoRelativePath -Path $file.FullName
        $profile = Get-LicenseProfile -RelativePath $relativePath
        if ($null -ne $profile) {
            [pscustomobject]@{ File = $file; RelativePath = $relativePath; Profile = $profile }
        }
    }
    return @($eligible)
}

function Read-EncodedTextFile {
    param([Parameter(Mandatory)][string]$Path)

    $bytes = [IO.File]::ReadAllBytes($Path)
    $preamble = [byte[]]@()
    $offset = 0

    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $encoding = New-Object Text.UTF8Encoding($false, $true)
        $preamble = [byte[]](0xEF, 0xBB, 0xBF)
        $offset = 3
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        $encoding = New-Object Text.UnicodeEncoding($false, $false, $true)
        $preamble = [byte[]](0xFF, 0xFE)
        $offset = 2
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
        $encoding = New-Object Text.UnicodeEncoding($true, $false, $true)
        $preamble = [byte[]](0xFE, 0xFF)
        $offset = 2
    } else {
        $encoding = New-Object Text.UTF8Encoding($false, $true)
    }

    try {
        $text = $encoding.GetString($bytes, $offset, $bytes.Length - $offset)
    } catch {
        throw "Source file is not valid UTF-8/UTF-16 and was not modified: $(Get-RepoRelativePath -Path $Path)"
    }

    return [pscustomobject]@{
        Text = $text
        Encoding = $encoding
        Preamble = $preamble
    }
}

function Write-EncodedTextFile {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][Text.Encoding]$Encoding,
        [Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$Preamble
    )

    $contentBytes = $Encoding.GetBytes($Text)
    $outputBytes = New-Object byte[] ($Preamble.Length + $contentBytes.Length)
    if ($Preamble.Length -gt 0) {
        [Array]::Copy($Preamble, 0, $outputBytes, 0, $Preamble.Length)
    }
    [Array]::Copy($contentBytes, 0, $outputBytes, $Preamble.Length, $contentBytes.Length)
    [IO.File]::WriteAllBytes($Path, $outputBytes)
}

function Add-LicenseHeader {
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Text,
        [Parameter(Mandatory)][object]$Profile
    )

    $lineEnding = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $prefixLength = 0

    switch ($Profile.Insertion) {
    "ShaderVersion" {
        $match = [regex]::Match($Text, '\A[ \t]*#version[^\r\n]*(?:\r\n|\n|$)')
        if ($match.Success) { $prefixLength = $match.Length }
    }
    "XmlDeclaration" {
        $match = [regex]::Match($Text, '\A[ \t]*<\?xml[^\r\n]*\?>[ \t]*(?:\r\n|\n|$)')
        if ($match.Success) { $prefixLength = $match.Length }
    }
    "ScriptPreamble" {
        $match = [regex]::Match($Text, '\A#![^\r\n]*(?:\r\n|\n|$)')
        if ($match.Success) { $prefixLength = $match.Length }

        $remaining = $Text.Substring($prefixLength)
        $encodingMatch = [regex]::Match(
            $remaining, '\A[ \t]*#.*coding[=:][ \t]*[-A-Za-z0-9_.]+[^\r\n]*(?:\r\n|\n|$)')
        if ($encodingMatch.Success) { $prefixLength += $encodingMatch.Length }
    }
    }

    $prefix = $Text.Substring(0, $prefixLength)
    $suffix = $Text.Substring($prefixLength)
    if ($prefixLength -gt 0 -and -not ($prefix.EndsWith("`n") -or $prefix.EndsWith("`r"))) {
        $prefix += $lineEnding
    }
    $headerSpacing = if ($prefixLength -gt 0 -and $suffix.StartsWith($lineEnding)) {
        $lineEnding
    } else {
        $lineEnding + $lineEnding
    }
    return $prefix + $Profile.Header + $headerSpacing + $suffix
}

$records = foreach ($candidate in Get-OwnedSourceFiles) {
    $encoded = Read-EncodedTextFile -Path $candidate.File.FullName
    $text = $encoded.Text
    $spdxMatch = [regex]::Match(
        $text, '(?im)^\s*(?://|/\*+|\*|#|<!--)\s*SPDX-License-Identifier\s*:\s*([^\r\n]+?)\s*(?:\*/|-->)?\s*$')
    $hasMplNotice = ($text -match '(?i)This Source Code Form') `
        -and ($text -match '(?is)Mozilla Public(?:\s|\*)+License')

    $status = if ($spdxMatch.Success) {
        $identifier = $spdxMatch.Groups[1].Value.Trim()
        if ($identifier -eq $spdxIdentifier) { "Covered" } else { "Conflict" }
    } elseif ($hasMplNotice) {
        "Covered"
    } else {
        "Missing"
    }

    [pscustomobject]@{
        File = $candidate.File
        RelativePath = $candidate.RelativePath
        Profile = $candidate.Profile
        Encoded = $encoded
        Status = $status
        ExistingIdentifier = if ($spdxMatch.Success) { $spdxMatch.Groups[1].Value.Trim() } else { $null }
    }
}

$records = @($records)
$conflicts = @($records | Where-Object Status -eq "Conflict")
$missing = @($records | Where-Object Status -eq "Missing")
$covered = @($records | Where-Object Status -eq "Covered")

if ($conflicts.Count -gt 0) {
    foreach ($record in $conflicts) {
        Write-Error "Conflicting SPDX identifier in $($record.RelativePath): $($record.ExistingIdentifier)" -ErrorAction Continue
    }
    throw "Aborted: $($conflicts.Count) file(s) use a different SPDX identifier. Review them manually."
}

if ($Apply) {
    foreach ($record in $missing) {
        $updatedText = Add-LicenseHeader -Text $record.Encoded.Text -Profile $record.Profile
        Write-EncodedTextFile -Path $record.File.FullName -Text $updatedText `
            -Encoding $record.Encoded.Encoding -Preamble $record.Encoded.Preamble
    }

    Write-Host "MPL-2.0 headers applied: $($missing.Count)"
    Write-Host "Already covered:        $($covered.Count)"
    Write-Host "Eligible source files:  $($records.Count)"
    exit 0
}

if ($missing.Count -gt 0) {
    foreach ($record in $missing) {
        Write-Output "Missing MPL-2.0 header: $($record.RelativePath)"
    }
    Write-Error "MPL-2.0 header check failed: $($missing.Count) of $($records.Count) eligible source files are missing a header."
    exit 1
}

Write-Host "MPL-2.0 header check passed: $($records.Count) eligible source files are covered."
exit 0
