#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Structural checks on the ported HLSL that do not need a compiler.

dxc only runs on Windows and reaching it costs a CI round trip, so the mistakes
it reports are worth catching here first. Every rule below comes from a failure
that actually happened:

  * cbuffers share one global namespace in HLSL, so two blocks both declaring
    `g_pad` is a redefinition -- and dxc blames whichever use site it reaches
    second, in a file that may not be the one at fault.
  * A stage that reads the arena has to declare the arena table, and a stage that
    declares it has to use it: `ArenaRef` without `ArenaConstants` is a type that
    does not exist.
  * Two cbuffers cannot be bound to the same register.
  * Every `#include` has to resolve relative to the shader directory.

Usage:
  tools/shaders/check_hlsl_structure.py            # check, exit 1 on a problem
  tools/shaders/check_hlsl_structure.py --verbose   # and say what was checked
"""

from __future__ import annotations

import collections
import pathlib
import re
import sys

SHADER_ROOT = pathlib.Path(__file__).resolve().parents[2] / "src" / "graphics" / "gr2port_d3d12" / "shaders"

CBUFFER_RE = re.compile(r"cbuffer\s+(\w+)\s*:\s*register\((b\d+)\)\s*\{(.*?)\}\s*;", re.S)
MEMBER_RE = re.compile(r"^\s*(?:[A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*(?:\[\d+\])?\s*;", re.M)
INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"', re.M)

ARENA_TYPES = ("ArenaRef", "ArenaTable", "ArenaBuffer")


def check_file(path: pathlib.Path) -> list[str]:
    text = path.read_text()
    problems: list[str] = []

    # Slang accepts GLSL scalar-splat constructors that DXC/HLSL2021 rejects.
    # Catch the literal case locally; vectors must spell out their components.
    if re.search(r"\b(?:float|uint|int)[234]\(\s*-?\d+(?:\.\d+)?[fu]?\s*\)", text):
        problems.append("GLSL scalar-splat constructor: use explicit HLSL vector components")

    blocks = CBUFFER_RE.findall(text)
    registers: dict[str, str] = {}
    members: dict[str, str] = {}
    for name, register, body in blocks:
        if register in registers:
            problems.append(
                f"cbuffer {name} and {registers[register]} both bind {register}"
            )
        registers[register] = name
        for member in MEMBER_RE.findall(body):
            if member in members:
                problems.append(
                    f"'{member}' is declared in both {members[member]} and {name}; "
                    "cbuffers share one namespace"
                )
            members[member] = name

    uses_arena = any(word in text for word in ARENA_TYPES)
    has_table = "cbuffer ArenaConstants" in text
    if uses_arena and not has_table:
        problems.append("reads the arena but declares no ArenaConstants block")
    if has_table and not uses_arena:
        problems.append("declares ArenaConstants but never reads the arena")

    for include in INCLUDE_RE.findall(text):
        if not (SHADER_ROOT / include).is_file():
            problems.append(f'#include "{include}" does not resolve')

    return problems


def check_shader_visibility() -> list[str]:
    """A helper a stage calls must be declared somewhere that stage includes.

    This is the imgui_relay case: the stage included texel.hlsli, which samples
    through handles, and used ArenaRef, which texel.hlsli does not declare.
    """
    problems: list[str] = []
    for path in sorted(SHADER_ROOT.rglob("*.hlsl")):
        text = path.read_text()
        included = set(INCLUDE_RE.findall(text))
        for include in tuple(included):
            target = SHADER_ROOT / include
            if not target.is_file():
                continue
            header = target.read_text()
            # Includes the header itself pulls in count too.
            for nested in INCLUDE_RE.findall(header):
                included.add(nested)
        available = ""
        for include in sorted(included):
            candidate = SHADER_ROOT / include
            if candidate.is_file():
                available += candidate.read_text() + "\n"
        for symbol in ("ArenaRef", "ArenaTable"):
            if f" {symbol} " in text and symbol not in available:
                problems.append(f"uses {symbol} but no included header declares it")
    return problems


def main() -> int:
    verbose = "--verbose" in sys.argv[1:]
    if not SHADER_ROOT.is_dir():
        print(f"check_hlsl_structure: {SHADER_ROOT} is missing", file=sys.stderr)
        return 1

    files = sorted(SHADER_ROOT.rglob("*.hlsl"))
    failures = 0
    for path in files:
        problems = check_file(path)
        for problem in problems:
            print(f"{path.relative_to(SHADER_ROOT)}: {problem}")
        failures += len(problems)
    for problem in check_shader_visibility():
        print(problem)
        failures += 1

    if verbose:
        kinds = collections.Counter(p.suffix for p in files)
        print(f"checked {len(files)} HLSL files: {dict(kinds)}")
    if failures:
        print(f"check_hlsl_structure: {failures} problem(s)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
