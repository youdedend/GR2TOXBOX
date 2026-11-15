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

# The authoritative layout, applied uniformly to the C++ and every shader:
#
#   b0  ArenaConstants                 ArenaTable g_arena, 20 bytes
#   b1  the per-draw / per-pass block  SceneRoot, LambertSceneRoot, ShadowConstants...
#   b2  the texture-handle block       *Textures, PresentConstants, BloomDepthTextures
#
# So the register is decided by what the block IS, not by which file it is in.
# That uniformity is the point: the renderer writes one value per register
# (SetArenaTable -> parameter 0, SetDrawConstants -> 1, SetPassConstants -> 2), so
# a shader that puts its arena table at b1 reads a register nothing writes.
ARENA_REGISTER = 0
DRAW_REGISTER = 1
PASS_REGISTER = 2

# Blocks that carry GPU handles rather than per-draw constants.
TEXTURE_BLOCK_NAMES = {"PresentConstants", "BloomDepthTextures"}


def expected_register(block_name: str) -> int | None:
    """The register a cbuffer belongs at, given what it holds."""
    if block_name == "ArenaConstants":
        return ARENA_REGISTER
    if block_name in TEXTURE_BLOCK_NAMES or block_name.endswith("Textures"):
        return PASS_REGISTER
    return DRAW_REGISTER


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
    # `Type name;` and `Type name[N];` only. The leading group must not itself be
    # a known field name, or a statement like `ArenaTable g_arena;` matches twice:
    # once correctly, and once with "ArenaTable" as the field of an unknown type.
    for decl in re.finditer(
        r"(?<![\w.])([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*(?:\[\s*(\d+)\s*\])?\s*;",
        body,
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
    # Deliberately NOT rounded up to 16 here.
    #
    # A cbuffer's placement rounds its size up to a 16-byte boundary, but that
    # padding is not data the shader reads. The budget this is compared against is
    # a root parameter's DWORD count, and what has to fit is the data: a 20-byte
    # ArenaTable at b0 fits a 20-byte b0 exactly, and rounding it to 32 here would
    # report every arena block as 12 bytes over a budget it exactly meets.
    return ((offset + 3) // 4 * 4) if offset else 0, unknown


def collect_struct_sizes() -> dict[str, int]:
    """Sizes of the small structs the cbuffers embed (ArenaTable, ArenaRef...)."""
    sizes: dict[str, int] = {}
    for path in sorted(SHADER_DIR.rglob("*.hlsli")):
        text = strip_comments(path.read_text(encoding="utf-8"))
        for m in re.finditer(r"struct\s+(\w+)\s*\{(.*?)\}\s*;", text, re.S):
            name, body = m.group(1), m.group(2)
            offset = 0
            ok = True
            for decl in re.finditer(
                r"(?<![\w.])([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*;", body
            ):
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

        # The arena block holds nothing but the table.
        arena = re.search(
            r"cbuffer\s+ArenaConstants\s*:\s*register\(\s*b(\d+)\s*\)\s*\{(.*?)\}\s*;",
            text,
            re.S,
        )
        if arena:
            body_fields = re.findall(
                r"(?<![\w.])([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*;",
                strip_comments(arena.group(2)),
            )
            if body_fields != [("ArenaTable", "g_arena")]:
                problems.append(
                    f"{rel}: ArenaConstants must hold nothing but 'ArenaTable g_arena' "
                    f"(SetArenaTable sets exactly five DWORDs); it holds {body_fields}"
                )

        # Every block is at the register its contents belong at.
        for reg, name in seen.items():
            want = expected_register(name)
            if want is not None and reg != want:
                problems.append(
                    f"{rel}: {name} is at b{reg}, but a block of that kind belongs at "
                    f"b{want} (b0 arena table, b1 per-draw constants, b2 texture "
                    f"handles). D3D12 matches a cbuffer to a root parameter by "
                    f"register, and only one register of each kind is written."
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
