# SPDX-License-Identifier: MPL-2.0

<#
.SYNOPSIS
    Run clang-tidy over Ruwa's own sources using a CMake compilation database.

.DESCRIPTION
    Reads compile_commands.json from the build directory, keeps only Ruwa's own
    translation units under src/ (Qt, QWindowKit and vendored third-party code
    are skipped), and runs clang-tidy on each one in parallel. Checks, header
    filter and format style come from the repository .clang-tidy file.

    Report-only by default: findings are printed but the script exits 0. Pass
    -Strict to exit non-zero when any file produces a finding (used by CI so the
    check surfaces without blocking).

    When the compilation database was produced by a MinGW g++ toolchain, the
    matching GNU target and --gcc-toolchain are detected automatically so the
    clang-based analyzer can find the libstdc++ headers.

.PARAMETER BuildDir
    CMake build directory containing compile_commands.json. Default: build.

.PARAMETER Filter
    Optional regex; only files whose repo-relative path matches are analyzed.
    Example: -Filter 'features/brush'

.PARAMETER Jobs
    Number of files analyzed in parallel. Default: processor count.

.PARAMETER Fix
    Apply clang-tidy's suggested fixes in place (then reformat via .clang-format).
    Forces -Jobs 1 so concurrent runs never rewrite the same header.

.PARAMETER Strict
    Exit with code 1 if any file produced a warning or error.

.PARAMETER ClangTidy
    Path to clang-tidy. Auto-detected from PATH, then the bundled Qt Creator LLVM.

.PARAMETER GccToolchain
    MinGW/GCC install root. Auto-detected from the compilation database compiler.

.PARAMETER Target
    Compiler target triple. Auto-detected from the GCC toolchain layout.

.EXAMPLE
    pwsh scripts/run-clang-tidy.ps1 -BuildDir build

.EXAMPLE
    pwsh scripts/run-clang-tidy.ps1 -Filter 'features/liquify' -Fix
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$Filter,
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Fix,
    [switch]$Strict,
    [string]$ClangTidy,
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

$tidy = Resolve-AnalyzerTool -Explicit $ClangTidy -Name "clang-tidy"
Write-Host "clang-tidy : $tidy"
& $tidy --version | Select-Object -First 2 | ForEach-Object { Write-Host "             $_" }

$extraArgs = Get-MinGWExtraArgs -BuildDir $BuildDir -GccToolchain $GccToolchain -Target $Target
if ($extraArgs) { Write-Host "toolchain  : $($extraArgs -join ' ')" }

$files = Select-RuwaSources -DbPath $dbPath -RepoRoot $repoRoot -Filter $Filter
$total = @($files).Count
if ($total -eq 0) { Write-Warning "No matching source files to analyze."; exit 0 }

$tidyArgs = @("-p", $BuildDir, "--quiet") + $extraArgs
if ($Fix) { $tidyArgs += "--fix"; $Jobs = 1 }
if ($Jobs -lt 1) { $Jobs = 1 }
Write-Host "Analyzing $total file(s) from $dbPath with $Jobs job(s)`n"

$result = Invoke-AnalyzerParallel -Tool $tidy -ToolArgs $tidyArgs -Files $files -RepoRoot $repoRoot -Jobs $Jobs

Write-Host ("`n=== clang-tidy summary: {0} file(s), {1} with findings, {2} tool error(s) ===" -f $total, $result.WithFindings, $result.Failed)
if ($Strict -and ($result.WithFindings -gt 0 -or $result.Failed -gt 0)) { exit 1 }
exit 0
