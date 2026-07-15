# SPDX-License-Identifier: MPL-2.0

<#
.SYNOPSIS
    Run clazy-standalone (Qt-aware static analysis) over Ruwa's own sources.

.DESCRIPTION
    Reads compile_commands.json from the build directory, keeps only Ruwa's own
    translation units under src/ (Qt, QWindowKit and vendored third-party code
    are skipped), and runs clazy-standalone on each one in parallel with the
    selected check level. clazy catches Qt-specific issues clang-tidy does not:
    unnecessary QString detaches, wrong connect() usage, container
    inefficiencies, and so on.

    Report-only by default: findings are printed but the script exits 0. Pass
    -Strict to exit non-zero when any file produces a finding (used by CI).

    When the compilation database was produced by a MinGW g++ toolchain, the
    matching GNU target and --gcc-toolchain are detected automatically so
    clazy-standalone can find the libstdc++ headers.

.PARAMETER BuildDir
    CMake build directory containing compile_commands.json. Default: build.

.PARAMETER Checks
    clazy check specification. Default 'level1' (level0 + recommended, low false
    positives). Raise to 'level2' for more aggressive checks, or compose your own
    e.g. 'level1,detaching-temporary,-qstring-allocations'.

.PARAMETER Filter
    Optional regex; only files whose repo-relative path matches are analyzed.

.PARAMETER Jobs
    Number of files analyzed in parallel. Default: processor count.

.PARAMETER Strict
    Exit with code 1 if any file produced a warning or error.

.PARAMETER Clazy
    Path to clazy-standalone. Auto-detected from PATH, then Qt Creator's LLVM.

.PARAMETER GccToolchain
    MinGW/GCC install root. Auto-detected from the compilation database compiler.

.PARAMETER Target
    Compiler target triple. Auto-detected from the GCC toolchain layout.

.EXAMPLE
    pwsh scripts/run-clazy.ps1 -BuildDir build

.EXAMPLE
    pwsh scripts/run-clazy.ps1 -Checks level2 -Filter 'services'
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$Checks = "level1",
    [string]$Filter,
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Strict,
    [string]$Clazy,
    [string]$GccToolchain,
    [string]$Target
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/StaticAnalysisCommon.ps1"

$dbPath = Join-Path $BuildDir "compile_commands.json"
if (-not (Test-Path $dbPath)) {
    throw "compile_commands.json not found in '$BuildDir'. Configure the project first (CMAKE_EXPORT_COMPILE_COMMANDS is on by default)."
}

$clazy = Resolve-AnalyzerTool -Explicit $Clazy -Name "clazy-standalone"
Write-Host "clazy-standalone : $clazy"
& $clazy --version | Select-Object -First 3 | ForEach-Object { Write-Host "                   $_" }
Write-Host "checks           : $Checks"

$extraArgs = Get-MinGWExtraArgs -BuildDir $BuildDir -GccToolchain $GccToolchain -Target $Target
if ($extraArgs) { Write-Host "toolchain        : $($extraArgs -join ' ')" }

$files = Select-RuwaSources -DbPath $dbPath -RepoRoot $repoRoot -Filter $Filter
$total = @($files).Count
if ($total -eq 0) { Write-Warning "No matching source files to analyze."; exit 0 }
if ($Jobs -lt 1) { $Jobs = 1 }
Write-Host "Analyzing $total file(s) from $dbPath with $Jobs job(s)`n"

$clazyArgs = @("-p", $BuildDir, "--checks=$Checks") + $extraArgs
$result = Invoke-AnalyzerParallel -Tool $clazy -ToolArgs $clazyArgs -Files $files -RepoRoot $repoRoot -Jobs $Jobs

Write-Host ("`n=== clazy summary: {0} file(s), {1} with findings, {2} tool error(s) ===" -f $total, $result.WithFindings, $result.Failed)
if ($Strict -and ($result.WithFindings -gt 0 -or $result.Failed -gt 0)) { exit 1 }
exit 0
