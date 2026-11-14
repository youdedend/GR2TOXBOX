<#
.SYNOPSIS
    Turns a build or configure log into GitHub Actions annotations.

.DESCRIPTION
    A failed job whose log cannot be opened is not a diagnosis. This script
    re-emits the interesting part of a log file as ::error:: and ::warning::
    workflow commands, which GitHub surfaces on the check run itself, where the
    reason survives even if the raw log is unavailable or truncated.

    Two passes:

      1. Targeted: lines matching a compiler/linker/CMake failure, with the
         lines that follow them (MSVC puts the note under the error, not above).
      2. Fallback: if the targeted pass found nothing, the tail of the log is
         emitted verbatim. A failure that matches no pattern at all is precisely
         the case where the raw text is the only thing that helps.

    Messages are escaped for the workflow-command format: newlines become %0A
    and literal '::' is broken up, because an unexpected '::' inside a message
    would be parsed as a command of its own.
#>
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory = $true)][string]$LogFile,
    [int]$Context = 2,
    [int]$FallbackTailLines = 120
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $LogFile)) {
    Write-Host "::error::$LogFile does not exist; the build never produced a log."
    exit 0
}

# Patterns that mean "this is why the build stopped". Ordered loosely by how
# often they appear in this project's failures.
$patterns = @(
    'error C\d+',            # cl.exe
    'error LNK\d+',          # link.exe
    'fatal error',
    'FAILED:',
    'CMake Error',
    'ninja: build stopped',
    'error MSB\d+',
    'D8021', 'D8037',        # compiler invocation failures
    'dxc.*error'
)

function Escape-Command([string]$Text) {
    # Newlines cannot appear in a workflow command; '::' would start a new one.
    $t = $Text -replace "`r`n", ' ' -replace "`n", ' ' -replace "`r", ' '
    $t = $t -replace '::', ': :'
    # Percent signs are the escape character for these commands.
    return $t.Replace('%', '%25')
}

$lines = Get-Content -LiteralPath $LogFile -ErrorAction SilentlyContinue
if (-not $lines) {
    Write-Host "::error::$LogFile is empty."
    exit 0
}

$emitted = 0
$seen = @{}

for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    $hit = $false
    foreach ($p in $patterns) {
        if ($line -match $p) { $hit = $true; break }
    }
    if (-not $hit) { continue }

    # Context follows the match: MSVC prints "error C2065: ..." and then the
    # source line and a note underneath.
    $end = [Math]::Min($i + $Context, $lines.Count - 1)
    $block = ($lines[$i..$end] | ForEach-Object { $_.TrimEnd() }) -join ' | '
    if (-not $seen.ContainsKey($block)) {
        $seen[$block] = $true
        Write-Host "::error::$(Escape-Command $block)"
        $emitted++
        # The annotation API tolerates a few dozen per job; beyond that the
        # summary stops being readable anyway.
        if ($emitted -ge 25) { break }
    }
}

if ($emitted -eq 0) {
    # Nothing matched. A custom command, a script or a tool with its own
    # diagnostics failed; the tail is the only useful artefact.
    Write-Host "::error::No diagnostic matched a known pattern; the tail of the log follows."
    $start = [Math]::Max(0, $lines.Count - $FallbackTailLines)
    # Chunked so each message stays well under the annotation size limit.
    for ($i = $start; $i -lt $lines.Count; $i += 20) {
        $end = [Math]::Min($i + 19, $lines.Count - 1)
        $chunk = ($lines[$i..$end] | ForEach-Object { $_.TrimEnd() }) -join ' | '
        if ($chunk.Trim() -ne '') {
            Write-Host "::error::$(Escape-Command $chunk)"
        }
    }
}

Write-Host "Emitted $emitted diagnostic annotation(s) from $LogFile."
