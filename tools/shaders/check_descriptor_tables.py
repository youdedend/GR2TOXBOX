#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that the descriptor tables agree between the two places that spell them.

The tables are declared twice, on purpose:

  * src/graphics/gr2port_d3d12/shaders/common/descriptors.hlsli -- what the
    shaders index, with a register space and an element count per table.
  * src/graphics/gr2port_d3d12/descriptors.h -- the same spaces and counts as
    D3D12_DESCRIPTOR_RANGEs, for the root signature.

They have to match or the binding is silently wrong: the root signature binds one
range, the shader reads another, and the symptom is wrong pixels rather than an
error. Nothing else checks the relationship -- dxc sees only the shader side, and
MSVC sees only the C++ side -- so it is checked here.

Deliberately not generated from one another: the shader header must stay readable
as HLSL, and the C++ list must stay readable as what the root signature does. A
generator would hide the very thing a reader needs to compare.
"""

import re
import sys

# The one table whose count is not the SRV slot count. Both files name their
# counts with their own macros, so the comparison is by value after resolving.
COUNT_PATTERNS = {
    "GR2_TABLE_SRVS": None,      # resolved from the shader header
    "GR2_TABLE_SAMPLERS": None,
    "kSrvCount": None,
    "kSamplerCount": None,
}


def parse_shader_header(path):
    """Returns {space: (count_macro, declaration)} from descriptors.hlsli."""
    text = open(path, encoding="utf-8").read()

    macros = dict(re.findall(r"#define\s+(GR2_TABLE_\w+)\s+(\d+)", text))
    for name, value in macros.items():
        COUNT_PATTERNS[name] = int(value)

    tables = {}
    for match in re.finditer(
        r"^\s*([\w<>]+)\s+(\w+)\[(\w+)\]\s*:\s*register\(\s*[tus]\d+\s*,\s*space(\d+)\s*\)",
        text,
        re.MULTILINE,
    ):
        element_type, name, count_macro, space = match.groups()
        tables[int(space)] = (count_macro, element_type, name)
    return tables


def parse_cpp_header(path):
    """Returns {space: (count_macro, range_type)} from descriptors.h."""
    text = open(path, encoding="utf-8").read()

    for name, value in re.findall(r"inline constexpr u32 (k\w+Count)\s*=\s*(\d+)", text):
        COUNT_PATTERNS[name] = int(value)

    tables = {}
    for match in re.finditer(
        r"\{\s*(\d+)\s*,\s*D3D12_DESCRIPTOR_RANGE_TYPE_(\w+)\s*,\s*(k\w+Count)\s*\}",
        text,
    ):
        space, range_type, count_macro = match.groups()
        tables[int(space)] = (count_macro, range_type.upper())
    return tables


def resolve(macro):
    return COUNT_PATTERNS.get(macro)


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <descriptors.hlsli> <descriptors.h>", file=sys.stderr)
        return 2

    shader = parse_shader_header(sys.argv[1])
    cpp = parse_cpp_header(sys.argv[2])

    problems = []

    if not shader:
        problems.append(f"no tables found in {sys.argv[1]} -- has the pattern changed?")
    if not cpp:
        problems.append(f"no tables found in {sys.argv[2]} -- has the pattern changed?")

    for space in sorted(set(shader) - set(cpp)):
        problems.append(
            f"space {space} ({shader[space][2]}) is declared in the shader header but "
            f"has no range in descriptors.h -- the shader would read a table the root "
            f"signature does not bind"
        )

    for space in sorted(set(cpp) - set(shader)):
        problems.append(
            f"space {space} has a range in descriptors.h but no declaration in the "
            f"shader header -- a bound table nothing indexes"
        )

    for space in sorted(set(shader) & set(cpp)):
        shader_count_macro, element_type, name = shader[space]
        cpp_count_macro, range_type = cpp[space]

        shader_count = resolve(shader_count_macro)
        cpp_count = resolve(cpp_count_macro)
        if shader_count is None:
            problems.append(f"space {space}: unknown count macro {shader_count_macro}")
        elif cpp_count is None:
            problems.append(f"space {space}: unknown count macro {cpp_count_macro}")
        elif shader_count != cpp_count:
            problems.append(
                f"space {space} ({name}): the shader declares {shader_count} elements "
                f"({shader_count_macro}) but the root signature binds {cpp_count} "
                f"({cpp_count_macro}); a handle past {cpp_count} would clamp to the "
                f"null descriptor"
            )

        # A writable texture needs a UAV range and a readable one an SRV range;
        # samplers need SAMPLER. Getting this wrong fails at PSO creation rather
        # than silently, but it is cheap to catch here.
        expected = None
        if "Sampler" in element_type:
            expected = "SAMPLER"
        elif element_type.startswith("RW"):
            expected = "UAV"
        elif "Texture" in element_type or "Buffer" in element_type:
            expected = "SRV"
        if expected is not None and range_type != expected:
            problems.append(
                f"space {space} ({name}): {element_type} needs a {expected} range but "
                f"descriptors.h declares {range_type}"
            )

    if problems:
        for problem in problems:
            print(problem)
        return 1

    print(
        f"{len(shader)} descriptor tables agree: "
        + ", ".join(f"space {s}" for s in sorted(shader))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
