#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks that every WinRT namespace a UWP file uses has its header included.

C++/WinRT is Windows-only, so the host precheck cannot compile the
`GR2_XBOX_UWP` branches at all -- it compiles those files with the macro
undefined, which exercises the desktop half and skips the console half entirely.
That is a real blind spot, and it has already cost a Windows run: a 4K change in
uwp_app.cpp used `winrt::Windows::ApplicationModel::Core::CoreApplication` with
only `winrt/Windows.UI.Core.h` included, and `::ABI::Windows::UI::Core` with no
ABI header at all. Both are errors that only the UWP build can see, and the host
build reported success either way.

The mapping is mechanical, which is what makes this checkable without a compiler:

    winrt::Windows::ApplicationModel::Core::CoreApplication
        needs  #include <winrt/Windows.ApplicationModel.Core.h>
    ::ABI::Windows::UI::Core::ICoreWindow
        needs  #include <windows.ui.core.h>

So: collect the namespace prefix of every `winrt::A::B::C::Thing` and every
`ABI::A::B::C::Thing` in the file, and require the corresponding include. The
prefix is the longest leading sequence of namespace components such that a
`<winrt/...>` (or `<....h>`) include of it is plausible -- from the longest
prefix down, so `Windows.Graphics.Display.Core` is preferred over
`Windows.Graphics.Display`.

Deliberately conservative: it reports only a namespace that appears with a
member after it and has no matching include anywhere in the file. It does not try
to decide whether an include is *sufficient*, which needs the real headers.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Files that contain a GR2_XBOX_UWP branch with WinRT in it.
CANDIDATES = [
    ROOT / "src/graphics/gr2port_d3d12/uwp/uwp_app.cpp",
    ROOT / "src/graphics/gr2port_d3d12/uwp/uwp_main.cpp",
]

WINRT_USE = re.compile(r"\bwinrt::((?:[A-Za-z_]\w*::)+)([A-Za-z_]\w*)")
ABI_USE = re.compile(r"::ABI::((?:[A-Za-z_]\w*::)+)([A-Za-z_]\w*)")


def includes_of(text: str) -> set[str]:
    return set(re.findall(r'#include\s*[<"]([^>"]+)[>"]', text))


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def prefixes(namespace: str) -> list[str]:
    """Namespace candidates from longest to shortest: A::B::C, A::B, A."""
    parts = [p for p in namespace.split("::") if p]
    return ["::".join(parts[:i]) for i in range(len(parts), 0, -1)]


def check(path: Path) -> list[str]:
    if not path.exists():
        return []
    raw = path.read_text(encoding="utf-8")
    if "GR2_XBOX_UWP" not in raw:
        return []
    text = strip_comments(raw)
    have = includes_of(text)
    problems: list[str] = []

    # Idents that live in <winrt/base.h> rather than a projection header.
    BASE_HEADER_TYPES = {"apartment_type"}

    def covered(kind: str, namespace: str) -> bool:
        for prefix in prefixes(namespace):
            if kind == "winrt":
                if f"winrt/{prefix.replace('::', '.')}.h" in have:
                    return True
            else:
                # The ABI headers are flat and lower-case: A::B::C -> a.b.c.h
                if f"{prefix.replace('::', '.').lower()}.h" in have:
                    return True
        return False

    # The whole file, not a single #if region.
    #
    # Taking the first `#if defined(GR2_XBOX_UWP)` block looks tidier and is
    # wrong: in these files that block is the include list itself, so the scan
    # only ever saw the includes and reported every file clean no matter what the
    # code below referenced. The files are UWP-conditional throughout, and an
    # unguarded WinRT reference in one is caught by the compiler, so scanning the
    # lot is both safer and simpler.
    region = text

    for kind, pattern in (("winrt", WINRT_USE), ("abi", ABI_USE)):
        reported: set[str] = set()
        for m in pattern.finditer(region):
            namespace = m.group(1).rstrip(":")
            if not namespace:
                continue
            # The regex is greedy, so for `winrt::A::B::C::Type::StaticMethod(`
            # the namespace comes back as `A::B::C::Type` -- one component too
            # long, with the type swallowed by the method name. A following "("
            # gives it away: the last component is the member, not the namespace.
            if region[m.end() : m.end() + 1] == "(" and "::" in namespace:
                namespace = namespace.rsplit("::", 1)[0]
            # winrt::apartment_type has no namespace component; it is declared in
            # base.h, which every C++/WinRT file includes.
            if kind == "winrt" and "::" not in namespace:
                if namespace in BASE_HEADER_TYPES and "winrt/base.h" in have:
                    continue
            if covered(kind, namespace):
                continue
            key = f"{kind}:{namespace}"
            if key in reported:
                continue
            reported.add(key)
            wanted = (
                f"<winrt/{namespace.replace('::', '.')}.h>"
                if kind == "winrt"
                else f"<{namespace.replace('::', '.').lower()}.h>"
            )
            try:
                shown = path.resolve().relative_to(ROOT).as_posix()
            except ValueError:
                shown = str(path)
            problems.append(
                f"{shown}: uses "
                f"{'winrt::' if kind == 'winrt' else '::ABI::'}{namespace} but no "
                f"header for it is included; add #include {wanted}"
            )
    return problems


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv

    # --file is for the fixture test in local_precheck.sh: run the same check over
    # a throwaway file instead of the real sources.
    if argv and argv[0] == "--file":
        if len(argv) != 2:
            print("usage: check_winrt_includes.py [--file PATH]")
            return 2
        problems = check(Path(argv[1]))
        if problems:
            for problem in problems:
                print(problem)
            return 1
        print("WinRT includes: 1 fixture, every namespace used has its header")
        return 0

    problems: list[str] = []
    checked = 0
    for path in CANDIDATES:
        if path.exists():
            checked += 1
            problems.extend(check(path))

    if problems:
        for problem in problems:
            print(problem)
        print()
        print(f"{len(problems)} missing WinRT include(s)")
        return 1

    print(f"WinRT includes: {checked} UWP file(s), every namespace used has its header")
    return 0


if __name__ == "__main__":
    sys.exit(main())
