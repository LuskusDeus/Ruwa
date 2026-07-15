# SPDX-License-Identifier: MPL-2.0

<#
    Shared helpers for the Ruwa static-analysis runner scripts
    (run-clang-tidy.ps1 / run-clazy.ps1). Dot-sourced, not run directly.
#>

# Locate an analyzer executable: an explicit path, then PATH, then the LLVM
# bundled with Qt Creator.
function Resolve-AnalyzerTool {
    param([string]$Explicit, [Parameter(Mandatory)][string]$Name)
    if ($Explicit) {
        if (Test-Path $Explicit) { return (Resolve-Path $Explicit).Path }
        throw "$Name not found at: $Explicit"
    }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $hints = @(
        "$env:ProgramFiles/Qt/Tools/QtCreator/bin/clang/bin/$Name.exe",
        "C:/Qt/Tools/QtCreator/bin/clang/bin/$Name.exe"
    )
    foreach ($h in $hints) { if (Test-Path $h) { return (Resolve-Path $h).Path } }
    throw "$Name was not found on PATH or in the bundled Qt Creator LLVM. Pass an explicit path."
}

# The clang-based analyzers default to an MSVC target on Windows and then fail to
# find the libstdc++ headers of a MinGW g++ compilation database. When the
# database was built by MinGW g++, return the extra clang arguments that point it
# at the matching GNU target and GCC toolchain. Returns an empty array for
# non-MinGW databases (plain Clang, or GCC on Linux), where nothing is needed.
function Get-MinGWExtraArgs {
    param([Parameter(Mandatory)][string]$BuildDir, [string]$GccToolchain, [string]$Target)

    if (-not $GccToolchain) {
        try {
            $entries = Get-Content (Join-Path $BuildDir "compile_commands.json") -Raw | ConvertFrom-Json
            $cmd = if ($entries[0].command) { $entries[0].command } else { ($entries[0].arguments -join ' ') }
            $first = ($cmd -split '\s+')[0].Trim('"')
            if ($first -match '(?i)(g\+\+|gcc|c\+\+)(\.exe)?$') {
                # <root>/bin/<compiler> -> <root>
                $root = Split-Path -Parent (Split-Path -Parent $first)
                if ($root -and (Test-Path (Join-Path $root "lib/gcc"))) { $GccToolchain = $root }
            }
        } catch { }
    }

    if (-not $GccToolchain -or -not (Test-Path (Join-Path $GccToolchain "lib/gcc"))) {
        return @()
    }
    if (-not $Target) {
        $triple = Get-ChildItem (Join-Path $GccToolchain "lib/gcc") -Directory -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($triple) { $Target = $triple.Name }
    }

    $extra = @()
    if ($Target) { $extra += "--extra-arg-before=--target=$Target" }
    $extra += "--extra-arg-before=--gcc-toolchain=$($GccToolchain -replace '\\','/')"
    return $extra
}

# Keep only Ruwa's own translation units under src/, dropping vendored and
# generated units, then optionally narrow by a repo-relative regex.
function Select-RuwaSources {
    param([Parameter(Mandatory)][string]$DbPath, [Parameter(Mandatory)][string]$RepoRoot, [string]$Filter)
    $srcPrefix = [IO.Path]::GetFullPath((Join-Path $RepoRoot "src")) + [IO.Path]::DirectorySeparatorChar
    $excludeRegex = 'third-party|_autogen|qwindowkit|[/\\]_deps[/\\]|\.moc$|[/\\]moc_|[/\\]qrc_|[/\\]ui_'
    # Assign first: in Windows PowerShell, piping ConvertFrom-Json straight into
    # ForEach-Object can pass the whole array as a single item.
    $entries = Get-Content $DbPath -Raw | ConvertFrom-Json
    $files = $entries |
        ForEach-Object { [IO.Path]::GetFullPath($_.file) } |
        Sort-Object -Unique |
        Where-Object { $_.StartsWith($srcPrefix, [StringComparison]::OrdinalIgnoreCase) } |
        Where-Object { $_ -notmatch $excludeRegex }
    if ($Filter) {
        $files = $files | Where-Object {
            ($_.Substring($RepoRoot.Length).TrimStart('\', '/') -replace '\\', '/') -match $Filter
        }
    }
    return $files
}

# Run $Tool $ToolArgs <file> across $Files using a runspace pool of size $Jobs.
# Diagnostics are printed as each file completes; returns the finding/error tally.
function Invoke-AnalyzerParallel {
    param(
        [Parameter(Mandatory)][string]$Tool,
        [Parameter(Mandatory)][string[]]$ToolArgs,
        [Parameter(Mandatory)][string[]]$Files,
        [Parameter(Mandatory)][string]$RepoRoot,
        [int]$Jobs = 1
    )

    # Runs in a child runspace: invoke the tool, flatten native-stderr error
    # records to text, and drop clang's per-TU cross-header noise.
    $worker = {
        param($tool, $toolArgs, $file)
        $raw = & $tool @toolArgs $file 2>&1 | ForEach-Object { $_.ToString() }
        $ec = $LASTEXITCODE
        $lines = $raw | Where-Object {
            $_ -notmatch '^\s*\d+\s+(warning|error)s?\s+generated' -and
            $_ -notmatch '^\s*Suppressed\s+\d+\s+warnings' -and
            $_ -notmatch 'Use -header-filter' -and
            $_ -notmatch 'argument unused during compilation'
        }
        $text = ($lines -join "`n")
        [pscustomobject]@{
            Text       = $text
            Exit       = $ec
            HasFinding = [bool]($text -match '(?m):\s*(warning|error):')
        }
    }

    $pool = [runspacefactory]::CreateRunspacePool(1, [Math]::Max(1, $Jobs))
    $pool.Open()
    try {
        $tasks = foreach ($file in $Files) {
            $ps = [powershell]::Create()
            $ps.RunspacePool = $pool
            [void]$ps.AddScript($worker).AddArgument($Tool).AddArgument($ToolArgs).AddArgument($file)
            [pscustomobject]@{ File = $file; PS = $ps; Handle = $ps.BeginInvoke() }
        }

        $total = @($tasks).Count
        $withFindings = 0
        $failed = 0
        $i = 0
        foreach ($t in $tasks) {
            $i++
            $res = $t.PS.EndInvoke($t.Handle)
            $t.PS.Dispose()
            $rel = $t.File.Substring($RepoRoot.Length).TrimStart('\', '/') -replace '\\', '/'
            Write-Host ("[{0,4}/{1}] {2}" -f $i, $total, $rel)
            if ($res) {
                if ($res.Exit -ne 0) { $failed++ }
                if ($res.HasFinding) { $withFindings++ }
                if ($res.Text) { Write-Host $res.Text }
            }
        }
        return [pscustomobject]@{ WithFindings = $withFindings; Failed = $failed }
    } finally {
        $pool.Close()
        $pool.Dispose()
    }
}
