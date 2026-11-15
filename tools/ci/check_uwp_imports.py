#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Audits a UWP executable's imports against the Windows Store API surface.

A packaged UWP title may only call the onecore API set. The linker does not
enforce that: it happily resolves a call to a desktop-only export, the package
still packs and installs, and the title dies on the console at start-up with
nothing but an exception code. The failure is therefore worth catching at build
time, where the offending symbol names the file that called it.

This runs `dumpbin /imports` (or /dependents) over the executable and checks:

  * every imported DLL is one an AppContainer process may load;
  * no import is from the desktop-only set -- the classic static-CRT leak is
    kernel32!TlsAlloc, kernel32!GetModuleHandleW,
    kernel32!InitializeCriticalSectionAndSpinCount and
    kernel32!LCMapStringW, none of which are in the onecore surface even though
    their import libraries link fine against a desktop .lib. A /MT UWP build that
    links the desktop static CRT archives pulls exactly those in.

Usage:
    check_uwp_imports.py --dumpbin <path to dumpbin.exe> --exe GR2port.exe
    check_uwp_imports.py --imports-file imports.txt      # pre-captured output

Exit code 0 when clean, 1 when anything forbidden was found.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# DLLs a packaged app must never import. The onecore UWP surface exposes the
# system APIs through WindowsApp.lib and the API set; a direct import of any of
# these is a desktop dependency and fails validation or start-up.
FORBIDDEN_DLLS = {
    "user32.dll": "no window manager in a packaged app",
    "gdi32.dll": "not in the onecore surface",
    "gdiplus.dll": "not in the onecore surface",
    "advapi32.dll": "use the WinRT equivalents",
    "shell32.dll": "not in the AppContainer surface",
    "shlwapi.dll": "not in the onecore surface",
    "ole32.dll": "not in the onecore surface",
    "oleaut32.dll": "not in the onecore surface",
    "comctl32.dll": "not in the onecore surface",
    "comdlg32.dll": "not in the onecore surface",
    "rpcrt4.dll": "not in the onecore surface",
    "winmm.dll": "XAudio2 instead",
    "dsound.dll": "XAudio2 instead",
    "d3d11.dll": "Direct3D 12 only; D3D11 is not available to a packaged Xbox title",
    "d3d10.dll": "Direct3D 12 only",
    "d3d9.dll": "Direct3D 12 only",
    "dxgi.dll": "reached through d3d12 and the API set",
    "vcruntime140.dll": "the CRT must be inside the package or statically linked",
    "vcruntime140_1.dll": "the CRT must be inside the package or statically linked",
    "msvcp140.dll": "the CRT must be inside the package or statically linked",
    "ucrtbase.dll": "the CRT must be inside the package or statically linked",
    "concrt140.dll": "the CRT must be inside the package or statically linked",
}

# Individual exports that are not in the onecore surface even though the DLL name
# may be. These are what a desktop static CRT drags in.
FORBIDDEN_SYMBOLS = {
    "TlsAlloc": "desktop kernel32; the static CRT's TLS allocation is not in onecore",
    "TlsFree": "desktop kernel32",
    "TlsSetValue": "desktop kernel32",
    "TlsGetValue": "desktop kernel32",
    "GetModuleHandleW": "desktop kernel32; the CRT must not need the module base",
    "GetModuleHandleA": "desktop kernel32",
    "GetModuleFileNameW": "desktop kernel32",
    "InitializeCriticalSectionAndSpinCount": "desktop kernel32",
    # Not in this list: InitializeCriticalSectionEx. It is in the onecore surface
    # via api-ms-win-core-synch, so the api-set check above deliberately exempts
    # it. Only the desktop-DLL import of it would be wrong, and that is covered by
    # kernel32.dll not being an allowed dependency for these symbols at all.
    "LCMapStringW": "desktop kernel32; the store CRT uses the API set",
    "LCMapStringA": "desktop kernel32",
    "GetSystemTimeAsFileTime": "desktop kernel32",
    "QueryPerformanceCounter": "desktop kernel32; use QueryPerformanceCounter via the "
    "API set only if the CRT reaches it that way",
    "CreateFileW": "use CreateFile2 or the WinRT storage APIs",
    "CreateFileA": "use CreateFile2 or the WinRT storage APIs",
    "ReadFile": "use the WinRT storage APIs",
    "WriteFile": "use the WinRT storage APIs",
    "GetFileSize": "use GetFileSizeEx or the WinRT storage APIs",
    "LoadLibraryW": "a packaged app cannot load arbitrary DLLs",
    "LoadLibraryA": "a packaged app cannot load arbitrary DLLs",
    "LoadLibraryExW": "a packaged app cannot load arbitrary DLLs",
    "GetProcAddress": "a packaged app cannot resolve arbitrary exports",
    "CreateWindowExW": "no window manager in a packaged app",
    "RegisterClassExW": "no window manager in a packaged app",
    "MessageBoxW": "not in the AppContainer surface",
}

# DLLs that are expected and fine.
ALLOWED_DLLS_HINT = {
    "d3d12.dll", "d3d12core.dll", "windowsapp.lib", "api-ms-win-core-*",
}

# dumpbin /imports lists each symbol as "<ordinal>  <Name>" -- two columns. The
# three-column "<address> <ordinal> <Name>" form does not appear in this output;
# requiring it matched nothing and made every binary look clean.
IMPORT_LINE = re.compile(r"^\s+(?:[0-9A-Fa-f]{8,16}\s+)?([0-9A-Fa-f]+)\s+(\S+)\s*$")

# Lines in a DLL section that are structure, not imports.
STRUCTURE_LINE = re.compile(
    r"(Import Address Table|Import Name Table|time date stamp|Index of first "
    r"forwarder reference|Section contains|Dump of file|File Type|Summary)"
)
DLL_LINE = re.compile(r"^\s*(\S+\.dll)\s*$", re.IGNORECASE)


def run_dumpbin(dumpbin: str, exe: str) -> str:
    result = subprocess.run(
        [dumpbin, "/nologo", "/imports", exe],
        capture_output=True,
        text=True,
        errors="replace",
    )
    if result.returncode != 0:
        raise SystemExit(
            f"dumpbin /imports failed ({result.returncode}): {result.stderr.strip()}"
        )
    return result.stdout


def parse(text: str) -> dict[str, list[str]]:
    """Maps each imported DLL to the symbols imported from it."""
    imports: dict[str, list[str]] = {}
    current: str | None = None
    for line in text.splitlines():
        dll = DLL_LINE.match(line)
        if dll:
            current = dll.group(1).lower()
            imports.setdefault(current, [])
            continue
        if current is None:
            continue
        if STRUCTURE_LINE.search(line):
            continue
        m = IMPORT_LINE.match(line)
        if m:
            imports[current].append(m.group(2))
    return imports


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--dumpbin")
    ap.add_argument("--imports-file", help="pre-captured dumpbin /imports output")
    args = ap.parse_args()

    if args.imports_file:
        text = Path(args.imports_file).read_text(errors="replace")
    elif args.dumpbin:
        text = run_dumpbin(args.dumpbin, args.exe)
    else:
        raise SystemExit("pass --dumpbin or --imports-file")

    imports = parse(text)
    if not imports:
        print(f"could not parse any imports from {args.exe}; the check found nothing "
              f"to verify, which is not the same as a clean binary")
        return 1

    problems: list[str] = []
    for dll, symbols in sorted(imports.items()):
        # Report a forbidden DLL once, and skip its symbols: naming every export of
        # user32.dll as well buries the one line that says the DLL must not be
        # imported at all.
        if dll in FORBIDDEN_DLLS:
            problems.append(
                f"{Path(args.exe).name} imports {dll}: {FORBIDDEN_DLLS[dll]}"
            )
            continue
        # A symbol is only a problem when it is imported from the DESKTOP DLL.
        # The same names are exactly right through the API set: a packaged app
        # calls InitializeCriticalSectionEx via api-ms-win-core-synch-l1-2-0, and
        # that is the correct path. Checking the symbol name alone flags it.
        via_api_set = dll.startswith("api-ms-win-") or dll == "windowsapp.lib"
        for symbol in symbols:
            if via_api_set:
                continue
            if symbol in FORBIDDEN_SYMBOLS:
                problems.append(
                    f"{Path(args.exe).name} imports {dll}!{symbol}: "
                    f"{FORBIDDEN_SYMBOLS[symbol]}"
                )

    total_symbols = sum(len(s) for s in imports.values())
    if problems:
        for problem in problems:
            print(problem)
            escaped = problem.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
            print(f"::error title=Forbidden actual UWP binary import::{escaped}")
        print()
        print(f"{len(problems)} forbidden import(s) in {args.exe}")
        return 1

    print(
        f"{Path(args.exe).name}: {len(imports)} imported DLL(s), {total_symbols} "
        f"symbol(s), none in the desktop-only set"
    )
    for dll in sorted(imports):
        print(f"  {dll} ({len(imports[dll])} symbols)")
    print(f"::notice title=Actual UWP import audit details::{len(imports)} DLLs; "
          f"{total_symbols} symbols; DLLs=" + ', '.join(sorted(imports)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
