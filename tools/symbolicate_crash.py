#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline nearest-symbol resolver for the matching GR2port.map and crash log.

python symbolicate_crash.py GR2port.map gr2toxbox_crash.log
Uses the linker's preferred base to convert MAP absolute addresses to RVAs.
Only resolves frames from the main module; other DLL and guest/JIT frames remain
unchanged. For source/line/inlined frames use the matching PDB with WinDbg or
llvm-symbolizer --obj=GR2port.exe --relative-address <RVA> on a developer PC.
Never mix MAP/PDB files from different commits.
"""
import argparse
from bisect import bisect_right
from pathlib import Path
import re


def load_map(text):
    match = re.search(r'Preferred load address is\s+([0-9a-fA-F]+)', text)
    if not match:
        raise ValueError('MAP has no preferred load address')
    base = int(match[1], 16)
    symbols = []
    for line in text.splitlines():
        match = re.match(r'\s+[0-9a-fA-F]{4}:[0-9a-fA-F]{8,16}\s+(\S+)\s+([0-9a-fA-F]{8,16})\b', line)
        if match:
            address = int(match[2], 16) - base
            if address >= 0:
                symbols.append((address, match[1]))
    if not symbols:
        raise ValueError('MAP has no symbols')
    return sorted(set(symbols))


def resolve(map_text, crash_text):
    symbols = load_map(map_text)
    addresses = [a for a, _ in symbols]
    module = None
    for line in crash_text.splitlines():
        match = re.search(r'module_base=0x([0-9a-fA-F]+)', line)
        if match:
            module = int(match[1], 16)
        rva = re.search(r'rva=\+0x([0-9a-fA-F]+)', line)
        image = re.search(r'image=0x([0-9a-fA-F]+)', line)
        if rva and (not image or (module is not None and int(image[1], 16) == module)):
            offset = int(rva[1], 16)
            index = bisect_right(addresses, offset) - 1
            if index >= 0:
                address, name = symbols[index]
                # Nearest public/static symbol is not proof of function extent.
                line += f' => nearest {name}+0x{offset-address:x}'
        yield line


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('map', type=Path)
    parser.add_argument('crash', type=Path)
    args = parser.parse_args()
    try:
        print('\n'.join(resolve(args.map.read_text(errors='replace'), args.crash.read_text(errors='replace'))))
    except ValueError as exc:
        parser.exit(1, f'{exc}\n')


if __name__ == '__main__':
    main()
