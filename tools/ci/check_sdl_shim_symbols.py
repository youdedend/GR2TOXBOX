#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that every SDL3 function a UWP translation unit calls is in the shim.

The packaged build has no SDL library behind it: src/uwp/sdl3_shim/ *is* the
library. A function the port calls but the shim does not define is an undefined
symbol at lld-link, twelve minutes into a Windows run -- which is how SDL_free and
the clipboard family were found.

This walks the SDL3 headers for every declared function, collects what the shim
defines, and compares against what the given translation units reference. It is
textual and therefore approximate in one direction only: it cannot tell that a
call sits inside an #if branch the real build compiles out, so it may report a
symbol that the linker would never have asked for. That is the safe direction --
a false positive costs a look, a false negative costs a run -- and it is why
SDL_GetPointerProperty is defined even though __WINRT__ makes the only call to it
dead code on this target.
"""

import glob
import re
import sys

SHIM_DIR = "src/uwp/sdl3_shim"
SDL_INCLUDE = "externals/sdl3/include/SDL3"


def declared_sdl_functions():
    """Every SDL3 function the headers declare, by name."""
    names = set()
    for header in glob.glob(f"{SDL_INCLUDE}/*.h"):
        with open(header, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        for match in re.finditer(
            r"extern\s+SDL_DECLSPEC\s+[^;]*?\b(SDL_[A-Za-z0-9_]+)\s*\(", text
        ):
            names.add(match.group(1))
    return names


def defined_by_shim():
    """Every SDL3 function the shim defines. Either spelling counts."""
    names = set()
    for source in glob.glob(f"{SHIM_DIR}/*.cpp"):
        with open(source, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        # extern "C" <ret> SDLCALL SDL_Name(
        for match in re.finditer(r'extern\s+"C"\s+[^;{}]*?\b(SDL_[A-Za-z0-9_]+)\s*\(', text):
            names.add(match.group(1))
        # A plain definition: <ret> SDL_Name(...) {
        for match in re.finditer(
            r"^\s*(?:SDL_DECLSPEC\s+)?[A-Za-z_][\w\s*&:<>,\[\]]*?"
            r"\b(SDL_[A-Za-z0-9_]+)\s*\([^;]*?\)\s*\{",
            text,
            re.MULTILINE,
        ):
            names.add(match.group(1))
    return names


def referenced_by(paths):
    names = set()
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        names |= set(re.findall(r"\b(SDL_[A-Za-z0-9_]+)\s*\(", text))
    return names


def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <translation unit>...", file=sys.stderr)
        return 2

    declared = declared_sdl_functions()
    defined = defined_by_shim()
    referenced = referenced_by(sys.argv[1:])

    # Only names that are really SDL3 functions; a macro or an enum constant that
    # happens to look like a call is not the linker's business.
    missing = sorted((referenced & declared) - defined)

    if missing:
        print("these SDL3 functions are called but not defined by the shim:")
        for name in missing:
            print(f"  {name}")
        print()
        print("the packaged build links no SDL library, so each of these is an")
        print("undefined symbol at lld-link. Define it in src/uwp/sdl3_shim/.")
        return 1

    print(
        f"{len(referenced & declared)} SDL3 calls across {len(sys.argv) - 1} "
        f"translation unit(s), all defined by the shim "
        f"({len(defined)} of {len(declared)} declared functions)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
