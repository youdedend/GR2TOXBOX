#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Audit the UWP package manifest and its assets before makeappx sees them.

makeappx validates XML structure and refuses to pack a manifest that references a
missing asset, but everything else it accepts and the console then rejects at
install or launch -- which on a console means a sideload failure a long way from
its cause. This checks the things that are cheap to check here:

  * every asset the manifest references exists (case-sensitively, which is what
    matters: CI stages on Windows, but a wrong case is a wrong case everywhere);
  * the root namespace prefixes used by any element or attribute are declared, and
    the restricted-capability namespace is both declared and listed in
    IgnorableNamespaces where it should be;
  * the capabilities a game on Xbox needs are present, and no desktop-only
    capability has crept in;
  * the Identity Version can be stamped (the packaging script anchors on it);
  * TargetDeviceFamily lists an Xbox-relevant family with a sane MinVersion.

Deliberately not a schema validator: that is makeappx's job, and reproducing it
here would be a second, worse implementation.
"""

import os
import re
import sys
import xml.etree.ElementTree as ET

MANIFEST = "uwp/Package.appxmanifest"

# Namespace prefixes as they appear in the document, mapped to their URIs. Kept
# explicit rather than discovered so an added prefix has to be declared here too.
KNOWN_PREFIXES = {
    "uap": "http://schemas.microsoft.com/appx/manifest/uap/windows10",
    "rescap": "http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities",
    "mp": "http://schemas.microsoft.com/appx/2014/phone/manifest",
}

# A game on Xbox needs these; each maps to a failure that is invisible until the
# title is on hardware.
REQUIRED_CAPABILITIES = {
    "rescap:codeGeneration": "without it VirtualProtectFromApp cannot grant execute, "
    "so the guest's native x86-64 code cannot run at all",
    "rescap:broadFileSystemAccess": "without it the title cannot read the player's "
    "dump from a USB drive",
    "internetClient": "trophies and the network discovery layer",
}

# Nothing here should ask for a desktop-only capability; a package that does is
# rejected at Store validation and may be rejected at sideload too.
FORBIDDEN_CAPABILITIES = {
    "runFullTrust": "a packaged app cannot take full trust on Xbox",
}


def main():
    problems = []
    notes = []

    if not os.path.exists(MANIFEST):
        print(f"{MANIFEST} is missing")
        return 1

    raw = open(MANIFEST, encoding="utf-8").read()

    # --- XML well-formedness -------------------------------------------------
    try:
        root = ET.fromstring(raw)
    except ET.ParseError as error:
        print(f"{MANIFEST} is not well-formed XML: {error}")
        return 1

    # --- namespace prefixes --------------------------------------------------
    declared = dict(re.findall(r'xmlns:([A-Za-z0-9]+)="([^"]+)"', raw))
    used = set(re.findall(r"</?([A-Za-z0-9]+):", raw)) | set(
        re.findall(r"\s([A-Za-z0-9]+):[A-Za-z0-9_]+=", raw)
    )
    used -= {"xmlns"}
    for prefix in sorted(used):
        if prefix not in declared:
            problems.append(f"prefix '{prefix}:' is used but never declared with xmlns:")
    for prefix, uri in KNOWN_PREFIXES.items():
        if prefix in used and declared.get(prefix) != uri:
            problems.append(
                f"prefix '{prefix}:' is declared as {declared.get(prefix)!r}, "
                f"expected {uri!r}"
            )

    ignorable = re.search(r'IgnorableNamespaces="([^"]*)"', raw)
    ignorable_set = set(ignorable.group(1).split()) if ignorable else set()
    for prefix in sorted(used):
        # mp: is the phone manifest, which is Ignorable; uap: and rescap: are not
        # (they carry behaviour the package depends on).
        if prefix == "mp" and prefix not in ignorable_set:
            problems.append(
                "'mp:' is used but not listed in IgnorableNamespaces; a package that "
                "declares it without ignoring it is rejected"
            )

    # --- assets --------------------------------------------------------------
    # Both the escaped form in the XML and the plain form, since the manifest uses
    # backslashes.
    # Both spellings: <Logo>Assets\StoreLogo.png</Logo> is an element body, while
    # Square150x150Logo="..." on VisualElements is an attribute. Matching only
    # attributes reported StoreLogo.png as staged-but-unreferenced.
    referenced = set(re.findall(r'(?:Logo|Image)="([^"]+\.png)"', raw))
    referenced |= set(re.findall(r"<(?:[A-Za-z0-9]+:)?(?:Logo|Image)>\s*([^<]+\.png)\s*<", raw))
    for reference in sorted(referenced):
        path = reference.replace("\\", "/")
        full = os.path.join(os.path.dirname(MANIFEST), path)
        if not os.path.exists(full):
            problems.append(f"the manifest references {reference}, which does not exist")
            continue
        if not os.access(full, os.R_OK):
            problems.append(f"{reference} exists but is not readable")
        # A case-insensitive filesystem would hide a mismatch; check the exact name.
        parent = os.path.dirname(full)
        wanted = os.path.basename(full)
        if os.path.isdir(parent) and wanted not in os.listdir(parent):
            problems.append(
                f"{reference}: the file exists only under different capitalisation "
                f"(found {[n for n in os.listdir(parent) if n.lower() == wanted.lower()]})"
            )
        # A zero-byte or non-PNG asset packs fine and shows as a blank tile.
        if os.path.getsize(full) < 100:
            problems.append(f"{reference} is {os.path.getsize(full)} bytes; that is not an image")

    # Assets present but never referenced are not an error, just worth knowing.
    asset_dir = os.path.join(os.path.dirname(MANIFEST), "Assets")
    if os.path.isdir(asset_dir):
        present = {f"Assets\\{name}" for name in os.listdir(asset_dir)}
        unused = present - referenced
        if unused:
            notes.append(f"staged but unreferenced: {', '.join(sorted(unused))}")

    # --- capabilities --------------------------------------------------------
    # The namespace prefix is on the *element* (<rescap:Capability Name="codeGeneration">),
    # not in the Name attribute -- so a pattern on Name="rescap:..." finds nothing and
    # reports every restricted capability as missing. Collect (prefix, name) pairs.
    caps = set()
    for prefix, name in re.findall(
        r"<(?:([A-Za-z0-9]+):)?Capability\s+Name=\"([^\"]+)\"", raw
    ):
        caps.add(f"{prefix}:{name}" if prefix else name)
    for name, why in REQUIRED_CAPABILITIES.items():
        if name not in caps:
            problems.append(f"capability '{name}' is missing: {why}")
    for name, why in FORBIDDEN_CAPABILITIES.items():
        if name in caps:
            problems.append(f"capability '{name}' is present: {why}")

    # --- identity and version -----------------------------------------------
    identity = re.search(r"<Identity\b[^>]*>", raw, re.S)
    if not identity:
        problems.append("no <Identity> element; the package has no name or version")
    else:
        block = identity.group(0)
        if not re.search(r'\bVersion="\d+\.\d+\.\d+\.\d+"', block):
            problems.append(
                "<Identity> has no quad-dotted Version; scripts/ci/package_msix.ps1 "
                "anchors on it to stamp the build version"
            )
        if not re.search(r'\bName="[^"]+"', block):
            problems.append("<Identity> has no Name")
        if not re.search(r'\bPublisher="[^"]+"', block):
            problems.append("<Identity> has no Publisher")
    if not re.search(r"<Applications>\s*<Application\b", raw):
        problems.append("no <Application> element; there would be nothing to launch")

    # --- languages -----------------------------------------------------------
    # The packaging script builds the resource index from this list, so a huge or
    # empty list is the thing to catch here; the PRI itself is built on Windows.
    languages = re.findall(r'<Resource\s+Language="([^"]+)"', raw)
    if not languages:
        problems.append(
            "no <Resource Language>; the package declares no languages and makepri has "
            "no qualifier set to build a resource index from"
        )
    for language in languages:
        # en-us, ja-jp, ko-kr: a bare language or a full BCP-47 tag, never empty.
        if not re.fullmatch(r"[a-zA-Z]{2,3}(-[a-zA-Z0-9]{2,8})*", language):
            problems.append(f"Resource Language={language!r} is not a BCP-47 tag")

    # --- target device family ------------------------------------------------
    family = re.search(r"<TargetDeviceFamily\b[^>]*>", raw)
    if not family:
        problems.append("no <TargetDeviceFamily>; the package declares no OS floor")
    else:
        block = family.group(0)
        minimum = re.search(r'MinVersion="(\d+)\.(\d+)\.(\d+)\.(\d+)"', block)
        if not minimum:
            problems.append("<TargetDeviceFamily> has no MinVersion")
        else:
            parts = [int(p) for p in minimum.groups()]
            # A MinVersion of 10.0.x.y is the only shape that has ever been valid,
            # and 19041 is the floor the Xbox Dev Mode documentation states.
            if parts[0] != 10 or parts[1] != 0:
                problems.append(
                    f'MinVersion="{minimum.group(0).split(chr(34))[1]}" is not a 10.0.x.y '
                    f"OS version"
                )
            elif parts[2] < 19041:
                problems.append(
                    f"MinVersion {parts[2]} is below 19041, the floor the Xbox Dev Mode "
                    f"documentation states"
                )

    # --- report --------------------------------------------------------------
    for note in notes:
        print(f"note: {note}")
    if problems:
        for problem in problems:
            print(problem)
        print()
        print(f"{len(problems)} problem(s) in {MANIFEST}")
        return 1

    print(
        f"{MANIFEST}: {len(referenced)} assets present, {len(caps)} capabilities, "
        f"{len(languages)} language(s), namespaces and Identity consistent"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
