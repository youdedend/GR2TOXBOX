#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reports which upstream GLSL shaders still need an HLSL counterpart.

The D3D12 backend re-implements every shader in
src/graphics/gr2port/gr2port.cmake as HLSL (plus the GCN detiling compute shader
in src/graphics/host_shaders). This tool keeps the two lists honest:

  python3 tools/shaders/check_shader_coverage.py           # summary
  python3 tools/shaders/check_shader_coverage.py --missing # list what is left
  python3 tools/shaders/check_shader_coverage.py --check   # exit 1 if incomplete

A shader counts as ported when a file with the matching stem exists under
src/graphics/gr2port_d3d12/shaders/ (GLSL .tesc -> .hull.hlsl and
.tese -> .dom.hlsl, the D3D names for the tessellation stages).
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CMAKE = ROOT / "src/graphics/gr2port/gr2port.cmake"
HLSL_DIR = ROOT / "src/graphics/gr2port_d3d12/shaders"
HOST_GLSL_DIR = ROOT / "src/graphics/host_shaders"

STAGE_SUFFIX = {
    ".vert": ".vert.hlsl",
    ".frag": ".frag.hlsl",
    ".comp": ".comp.hlsl",
    ".geom": ".geom.hlsl",
    ".tesc": ".hull.hlsl",
    ".tese": ".dom.hlsl",
    ".glsl": ".hlsli",
}


def upstream_shaders() -> list[str]:
    text = CMAKE.read_text(encoding="utf-8")
    return re.findall(r"gr2port_add_shader\(\s*([^\s,)]+)", text)


def host_shaders() -> list[Path]:
    if not HOST_GLSL_DIR.is_dir():
        return []
    return sorted(p for p in HOST_GLSL_DIR.rglob("*.comp"))


def expected_hlsl(glsl_name: str) -> str:
    suffix = Path(glsl_name).suffix
    stem = glsl_name[: -len(suffix)] if suffix else glsl_name
    return stem + STAGE_SUFFIX.get(suffix, ".hlsl")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--missing", action="store_true", help="list unported shaders")
    parser.add_argument("--check", action="store_true", help="exit 1 when any is unported")
    args = parser.parse_args()

    upstream = upstream_shaders()
    missing: list[str] = []
    ported: list[str] = []
    for name in upstream:
        expected = HLSL_DIR / expected_hlsl(name)
        (ported if expected.exists() else missing).append(name)

    host_missing = [p.name for p in host_shaders()
                    if not (HLSL_DIR / "host" / (p.stem + ".comp.hlsl")).exists()]

    total = len(upstream)
    print(f"upstream GLSL shaders: {total}")
    print(f"  HLSL counterparts present: {len(ported)}")
    print(f"  still to port:             {len(missing)}")
    if host_shaders():
        print(f"GCN host compute shaders: {len(host_shaders())} "
              f"({len(host_missing)} still to port)")

    detile = HLSL_DIR / "detile.comp.hlsl"
    print("GCN detile kernel: " + ("present (2D display/thin micro + P2/P8 macro; MSAA/thick/PRT rejected)"
                                 if detile.exists() else "missing"))

    if args.missing:
        for name in missing:
            print(f"  missing: {expected_hlsl(name)}  (from {name})")
        for name in host_missing:
            print(f"  missing: host/{Path(name).stem}.comp.hlsl  (from {name})")

    if args.check and (missing or host_missing):
        print("\nFAIL: the D3D12 backend is missing shaders.", file=sys.stderr)
        return 1
    if args.check:
        print("\nOK: every upstream shader has an HLSL counterpart.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
