<#
.SYNOPSIS
    Turns a build or configure log into GitHub Actions annotations.

.DESCRIPTION
    A failed job whose log cannot be opened is not a diagnosis. This script
    re-emits the interesting part of a log file as ::error:: and ::warning::
    workflow commands, which GitHub surfaces on the check run itself, where the
    reason survives even if the raw log is unavailable or truncated.

    Two passes:

      1. Targeted: each LINE matching a compiler/linker/CMake failure, emitted as
         its own annotation so that truncation can never eat the message.
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
    'error C\d+',            # cl.exe / clang-cl
    'error LNK\d+',          # link.exe
    'fatal error',
    'CMake Error',
    'ninja: build stopped',
    'error MSB\d+',
    'D8021', 'D8037',        # compiler invocation failures
    # Not just "error:": dxc and the other LLVM-style tools in the shader
    # pipeline report 'file(line,col): error: ...' and also plain
    # "file:line:col: error: ...", and a custom-command failure's only diagnostic
    # can be the second form.
    'error:',
    ': error ',
    # lld's /FAILIFMISMATCH diagnostic names each conflicting object on the
    # following lines, not on the error line. Keep each line separately so the
    # API's annotation truncation cannot hide which library changed /MT to /MD.
    '>>>',
    'RuntimeLibrary|MD_DynamicRelease|MT_StaticRelease',
    'FAILED:',
    # The packaging tools. makeappx and makepri prefix their diagnostics with
    # 'MakeAppx : error:' / 'MakePri : error:' and can also print a bare
    # 'Error: <reason>' line; signtool prints 'SignTool Error:' and a bare
    # 'Error information:'. The packaging step is where a failure has no compiler
    # to quote, so these are the only lines that carry a reason at all.
    'MakeAppx\s*:\s*error',
    'MakePri\s*:\s*error',
    'SignTool Error',
    'Error information',
    '^Error:',
    'failed with error',
    'The specified path|The system cannot find the file|does not exist',
    # A terminating PowerShell error, which is how package_msix.ps1 signals every
    # one of its own failures (it uses throw, not exit).
    'Exception:',
    'throw',
    # The signing certificate and the manifest are checked by hand-written throws
    # in package_msix.ps1; this catches the rest of the family.
    'signing certificate|Signing certificate',
    'resources\.pri|AppxManifest',
    # Every failure package_msix.ps1 raises for itself is spelled
    # "<tool> <verb> failed (<exit code>)", so this matches the whole family
    # without listing each one: makepri createconfig failed (1), makeappx pack
    # failed (1), signtool verify failed (1).
    'failed \(\d+\)'
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

    # One annotation per diagnostic LINE, not per block.
    #
    # It is tempting to attach the following lines as context, because MSVC puts
    # its note under the error. But the annotation API truncates a message at
    # about 650 characters, and a block whose first line is a 1200-character
    # FAILED: command line truncates away the compiler's actual message -- which
    # is the only line that matters. Line-at-a-time is less pretty and always
    # survives.
    $text = $lines[$i].TrimEnd()
    if ($text.Length -gt 600) {
        # Keep the diagnostic, drop the command line it is attached to. For a
        # 'FAILED: <command>' line the command has already been printed by ninja
        # in the raw log; the interesting part is the diagnostic that follows.
        if ($text -match '^FAILED:') {
            Write-Host "::error::FAILED (command line elided; see the diagnostic annotations)"
            $emitted++
            continue
        }
        $text = $text.Substring(0, 600) + '...'
    }
    if ($text.Trim() -eq '' -or $seen.ContainsKey($text)) { continue }
    $seen[$text] = $true
    Write-Host "::error::$(Escape-Command $text)"
    $emitted++
    # CMake often puts the actual reason on the next indented line. Emit those
    # independently rather than attaching them to a long command/header.
    if ($line -match 'CMake Error') {
        for ($j = $i + 1; $j -lt [Math]::Min($i + 6, $lines.Count); $j++) {
            $context = $lines[$j].Trim()
            if ($context -eq '') { break }
            Write-Host "::error::CMake detail: $(Escape-Command $context)"
            $emitted++
        }
    }
    # The annotation API tolerates a few dozen per job; beyond that the summary
    # stops being readable anyway.
    if ($emitted -ge 40) { break }
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
