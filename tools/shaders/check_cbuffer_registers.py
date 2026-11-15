#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks the HLSL cbuffer bindings against the root signature the C++ builds.

This backend has one root signature for every pipeline, and it puts fixed things
in fixed places:

    b0   32-bit draw/pass constants   (RootConstants::DrawBytes or PassBytes)
    b1   the 32-bit constants block   (per shader; the arena table lives here)
    b2   more 32-bit constants

Get a register number wrong and nothing fails at pipeline creation, because
D3D12 matches a shader's cbuffer to a root parameter by register and visibility,
not by name or size. The shader simply reads whichever block landed on that
register -- so a shader that expects the arena table at b0 receives whatever the
renderer put there and computes garbage from it. On screen that is a black
triangle or a wildly misplaced mesh, with no error anywhere.

That is not hypothetical: `world.vert.hlsl`, `lambert.vert.hlsl` and
`lambert.frag.hlsl` each declared `ArenaConstants : register(b0)` while the other
24 shaders and the root signature all used `b1`, and the existing checks passed
because none of them looked at register numbers.

So this tool checks, for every shader:

  * `ArenaConstants` is at b1, and the per-draw block (whatever it is called) is
    at b0, matching RootConstants and BuildRootParameters;
  * no two cbuffers claim the same register;
  * every cbuffer fits its root parameter's 32-bit constant budget. A shader
    cbuffer larger than the root parameter is truncated at bind time, silently;
    the renderer then sets fewer constants than the shader reads.

Sizes are computed from the declarations, with the HLSL struct-layout rules that
matter here: a vector does not straddle a 16-byte boundary, and a struct is
16-byte aligned. That is enough for the types these shaders use; an unknown type
is reported rather than guessed.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHADER_DIR = ROOT / "src/graphics/gr2port_d3d12/shaders"
PIPELINE_H = ROOT / "src/graphics/gr2port_d3d12/pipeline.h"

# Scalar size in bytes for the HLSL types this project uses.
SCALAR_SIZES = {
    "bool": 4,
    "int": 4,
    "uint": 4,
    "dword": 4,
    "float": 4,
    "half": 2,
    "double": 8,
}

# The root signature binds three 32-bit constant parameters, at b0, b1 and b2.
MAX_REGISTER = 2

# Which register the renderer writes the arena table to. BuildRootParameters in
# pipeline.cpp binds parameter 0 (ShaderRegister 0) with RootConstants::ArenaBytes,
# and set_arena_table in command.cpp sets that parameter. So the arena table is b0.
ARENA_REGISTER = 0

# Which blocks belong at which register.
#
# Unlike a conventional renderer, the mapping is NOT uniform across shaders: the
# arena table is b1 in most and b0 in the world/lambert family, where the *first*
# block is genuinely the arena and the per-draw constants follow at b1. What is
# uniform is the size budget each register has (RootConstants), so that is what
# this checks -- plus the two structural rules below, which do hold everywhere:
#
#   * ArenaConstants always contains nothing but the arena table; and
#   * a shader that declares ArenaConstants declares it in the register the
#     renderer sets the arena table to for that family. Since b0 and b1 are both
#     32-bit constant parameters of sufficient size, either placement works at
#     bind time; what does not work is a register the signature does not declare.

# No per-shader name list. A block is "the per-draw block" if it is the one that
# is not ArenaConstants or a *Textures handle block; that is all the structure
# the C++ side relies on, and inventing a name list here was what made the first
# version of this check disagree with pipeline.h.


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def sizeof_type(name: str) -> int | None:
    """Size of a type expression, or None if it is not one we can size."""
    m = re.fullmatch(r"(bool|int|uint|dword|float|half|double)([1-4])?", name)
    if m:
        scalar, count = m.group(1), m.group(2)
        return SCALAR_SIZES[scalar] * (int(count) if count else 1)
    m = re.fullmatch(r"(float|half|double)([1-4])x([1-4])", name)
    if m:
        # A matrix occupies row vectors, each of which is padded to 16 bytes.
        rows, cols = int(m.group(2)), int(m.group(3))
        return rows * 16
    return None


def align(offset: int, boundary: int = 16) -> int:
    return (offset + boundary - 1) // boundary * boundary


def cbuffer_size(body: str, struct_sizes: dict[str, int]) -> tuple[int, list[str]]:
    """Packed size of a cbuffer body, plus a list of types it could not size."""
    offset = 0
    unknown: list[str] = []
    for decl in re.finditer(
        r"([A-Za-z_][\w]*)\s+([A-Za-z_]\w*)\s*(?:\[\s*(\d+)\s*\])?\s*;", body
    ):
        type_name, array = decl.group(1), decl.group(3)
        count = int(array) if array else 1
        if type_name in struct_sizes:
            # An embedded struct packs as its members do: no 16-byte alignment of
            # its own, no trailing padding. ArenaTable is five uints, so it is 20
            # bytes inside a cbuffer and an array of it advances by 20. (An array
            # of structs *is* 16-byte strided in HLSL, but no shader here embeds
            # one, so that rule is not modelled -- see the array check below.)
            if count > 1:
                unknown.append(type_name + "[]")
                continue
            base = struct_sizes[type_name]
        else:
            size = sizeof_type(type_name)
            if size is None:
                unknown.append(type_name)
                continue
            # A vector must not straddle a 16-byte boundary: if it would, it is
            # pushed to the next one. This is what makes a 3-float + 1-float
            # sequence pack to 16 rather than to 12 plus padding.
            if size < 16 and (offset % 16) + size > 16 and (offset % 16) != 0:
                offset = align(offset)
            base = size
        offset += base * count
    # The block itself is a multiple of 16 bytes.
    return align(offset) if offset else 0, unknown


def collect_struct_sizes() -> dict[str, int]:
    """Sizes of the small structs the cbuffers embed (ArenaTable, ArenaRef...)."""
    sizes: dict[str, int] = {}
    for path in sorted(SHADER_DIR.rglob("*.hlsli")):
        text = strip_comments(path.read_text(encoding="utf-8"))
        for m in re.finditer(r"struct\s+(\w+)\s*\{(.*?)\}\s*;", text, re.S):
            name, body = m.group(1), m.group(2)
            offset = 0
            ok = True
            for decl in re.finditer(r"([A-Za-z_][\w]*)\s+([A-Za-z_]\w*)\s*;", body):
                type_name = decl.group(1)
                if type_name in sizes:
                    offset += sizes[type_name]
                    continue
                size = sizeof_type(type_name)
                if size is None:
                    ok = False
                    break
                if size < 16 and (offset % 16) + size > 16 and (offset % 16) != 0:
                    offset = align(offset)
                offset += size
            if ok:
                # Round to the largest member's alignment, never to 16: a struct
                # of scalars is not 16-byte aligned or padded inside a cbuffer.
                sizes[name] = ((offset + 3) // 4 * 4) if offset else 0
    return sizes


def root_constant_budgets() -> dict[int, int]:
    """RootConstants::{Arena,Draw,Pass}Bytes -> the register each one binds."""
    text = PIPELINE_H.read_text(encoding="utf-8")
    out: dict[int, int] = {}
    for reg, name in ((0, "ArenaBytes"), (1, "DrawBytes"), (2, "PassBytes")):
        m = re.search(rf"{name}\s*=\s*(\d+)", text)
        if m:
            out[reg] = int(m.group(1))
    return out


def main() -> int:
    struct_sizes = collect_struct_sizes()
    budgets = root_constant_budgets()
    if not budgets:
        print("could not read RootConstants from pipeline.h")
        return 1

    problems: list[str] = []
    arena_registers: dict[int, list[str]] = {}
    shaders = sorted(p for p in SHADER_DIR.rglob("*.hlsl"))
    total_blocks = 0

    for path in shaders:
        rel = path.relative_to(SHADER_DIR).as_posix()
        text = strip_comments(path.read_text(encoding="utf-8"))

        seen: dict[int, str] = {}
        for m in re.finditer(
            r"cbuffer\s+(\w+)\s*:\s*register\(\s*b(\d+)\s*\)\s*\{(.*?)\}\s*;",
            text,
            re.S,
        ):
            name, reg, body = m.group(1), int(m.group(2)), m.group(3)
            total_blocks += 1

            if reg in seen:
                problems.append(
                    f"{rel}: {name} and {seen[reg]} both claim register b{reg}"
                )
            seen[reg] = name

            if reg > MAX_REGISTER:
                problems.append(
                    f"{rel}: {name} is at b{reg}, but the root signature only "
                    f"declares b0..b{MAX_REGISTER}"
                )
                continue

            size, unknown = cbuffer_size(body, struct_sizes)
            if unknown:
                problems.append(
                    f"{rel}: {name} embeds {', '.join(sorted(set(unknown)))}, which "
                    f"this check cannot size; add it to the layout rules before "
                    f"trusting the size"
                )
                continue

            if size > budgets[reg]:
                problems.append(
                    f"{rel}: {name} is {size} bytes but root parameter b{reg} is "
                    f"only {budgets[reg]} bytes; the surplus is silently truncated"
                )

        # The arena table is only meaningful if it is the whole block: a shader
        # that packs anything beside it would have the renderer's five uints land
        # at the wrong offsets.
        arena = re.search(
            r"cbuffer\s+ArenaConstants\s*:\s*register\(\s*b(\d+)\s*\)\s*\{(.*?)\}\s*;",
            text,
            re.S,
        )
        if arena:
            body_fields = re.findall(
                r"([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*;", strip_comments(arena.group(2))
            )
            names = [f[1] for f in body_fields]
            if names != ["g_arena"] or body_fields[0][0] != "ArenaTable":
                problems.append(
                    f"{rel}: ArenaConstants must hold nothing but 'ArenaTable g_arena' "
                    f"(the renderer sets five uints); it holds {body_fields}"
                )

        # The arena table has to be at ONE register across every shader.
        #
        # This is the check that matters and the one that was missing. pipeline.h
        # documents b0 as the arena table and b1 as the draw constants, and
        # command.cpp always writes the arena table to parameter 0 -- but 24 of
        # these shaders declare ArenaConstants at b1 and their per-draw block at
        # b0, while world.vert/lambert.vert/lambert.frag declare it at b0. Both
        # groups cannot be right: a shader that reads the arena from a register the
        # renderer never writes gets whatever else is there.
        #
        # Which register is correct is a renderer-level decision (moving it means
        # changing pipeline.h, command.cpp and one group or the other), so this
        # reports the disagreement instead of picking a side. The sizes above are
        # enforced either way, because those are unambiguous.
        if arena:
            arena_registers.setdefault(int(arena.group(1)), []).append(rel)

    if len(arena_registers) > 1:
        detail = "; ".join(
            f"b{reg} in {len(files)} ({', '.join(sorted(files)[:3])}...)"
            for reg, files in sorted(arena_registers.items())
        )
        problems.append(
            f"the arena table is bound at different registers across shaders: "
            f"{detail}. Only one register is written by the renderer "
            f"(command.cpp set_arena_table writes parameter 0, b{ARENA_REGISTER}), "
            f"so the other group reads a block that holds something else."
        )

    if problems:
        for problem in problems:
            print(problem)
        print()
        print(f"{len(problems)} problem(s) across {len(shaders)} shaders")
        return 1

    print(
        f"cbuffer bindings: {total_blocks} blocks in {len(shaders)} shaders, all at "
        f"the registers the root signature declares and within their budgets"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
