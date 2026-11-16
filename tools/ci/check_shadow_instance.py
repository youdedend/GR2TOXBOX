#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep D3D12 shadow draws' base-instance root and SV_InstanceID indexing paired."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
for name in ("shadow.vert.hlsl", "shadow_alpha.vert.hlsl"):
    source = (ROOT / "src/graphics/gr2port_d3d12/shaders" / name).read_text()
    if not re.search(r"cbuffer\s+ShadowConstants\s*:\s*register\(b1\).*?\buint\s+g_base_instance\s*;", source, re.S):
        sys.exit(f"{name}: ShadowConstants b1 must carry g_base_instance")
    if not re.search(r"LoadCasterAt\s*\(\s*ArenaAdd\s*\(\s*g_casters\s*,\s*\(\s*g_base_instance\s*\+\s*instance\s*\)", source):
        sys.exit(f"{name}: caster lookup must add g_base_instance to SV_InstanceID")
print("D3D12 shadow base-instance roots and caster indexing passed")
