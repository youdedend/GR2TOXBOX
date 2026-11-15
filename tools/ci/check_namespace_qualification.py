#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Flags a project name that is used without the namespace that encloses it.

This came out of a Windows-only failure. `CreateSwapchain` lives in
`Gr2Port::D3D12` and used `Display::Mode`, but `Display` is declared in
`Gr2Port::Uwp::Display`. Unqualified lookup searches the enclosing namespaces and
then the global one -- it never searches a *sibling* -- so the name did not
resolve. Everything after it cascaded:

    error: use of undeclared identifier 'Display'; did you mean 'modf'?
    error: use of undeclared identifier 'mode'

The host precheck cannot catch that with the compiler, because the UWP half of
these files is never compiled on Linux. It is catchable statically, though, and
the rule is narrow:

    a name declared in namespace N, used bare in a context whose enclosing
    namespaces are not a prefix of N (and which does not itself inherit N)

The check is deliberately conservative -- it only complains about names it has
actually seen declared in a project namespace, and only about the *first*
qualifier of an expression, which is where lookup starts.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src/graphics/gr2port_d3d12"

# Headers whose namespace contents are indexed.
HEADERS = sorted(SRC.rglob("*.h")) + sorted(SRC.rglob("*.hpp"))

# Files to check for bad usage.
TARGETS = sorted(SRC.rglob("*.cpp")) + sorted(SRC.rglob("*.h"))

# Name roots that are resolved elsewhere and never worth reporting.
EXTERNAL_ROOTS = {
    "std", "winrt", "ABI", "Microsoft", "Windows", "D3D12", "DXGI", "ID3D12",
    "DirectX", "DirectXMath", "dx", "absl", "fmt", "spdlog", "ImGui", "SDL",
    "gr2", "Core",
}

NS_OPEN = re.compile(r"^\s*namespace\s+([A-Za-z_][\w:]*)\s*\{?")
ANON_NS = re.compile(r"^\s*namespace\s*\{")
DECL = re.compile(
    r"^\s*(?:struct|class|union|enum(?:\s+class)?)\s+([A-Za-z_]\w*)"
    r"|^\s*namespace\s+([A-Za-z_][\w:]*)\s*\{"
    r"|^\s*(?:inline\s+)?(?:constexpr\s+)?[A-Za-z_][\w:<>,\s\*&]*?\b([A-Za-z_]\w*)\s*\("
)
USE = re.compile(r"\b([A-Za-z_]\w*)\s*::")


def brace_delta(line: str) -> int:
    """Net brace depth change, ignoring braces in comments and strings."""
    line = re.sub(r"//[^\n]*", "", line)
    line = re.sub(r'"[^"]*"', '""', line)
    line = re.sub(r"'[^']*'", "''", line)
    return line.count("{") - line.count("}")


class ScopeTracker:
    """Tracks namespace nesting with brace depth.

    A stack of names is not enough: a closing brace that ends a function body is
    indistinguishable from one that ends a namespace, and popping on any `}` pops
    the namespace the moment the first function closes. That reported 207 names as
    "used from (global)" when they were inside their own namespace. Each frame
    therefore remembers the depth it was opened at, and only a brace taking the
    depth back to that number closes it.
    """

    def __init__(self) -> None:
        self._frames: list[tuple[str, int]] = []
        self._depth = 0

    @property
    def current(self) -> str:
        # Anonymous namespaces are frames with an empty name: they occupy a brace
        # level but contribute nothing to the qualified name.
        return "::".join(name for name, _ in self._frames if name)

    def feed(self, line: str) -> None:
        m = NS_OPEN.match(line)
        anon = ANON_NS.match(line)
        before = self._depth
        self._depth += brace_delta(line)
        if m or anon:
            # Record the depth *after* the opening brace. Recording the depth
            # before it means a frame is only popped when the depth goes negative,
            # which never happens -- so every namespace stayed on the stack for the
            # rest of the file and everything after it was attributed to it.
            #
            # An anonymous `namespace {` gets a nameless frame. Skipping it entirely
            # leaves its closing brace to pop whatever named namespace encloses it,
            # which mis-attributed every following line to global scope.
            name = m.group(1) if m else ""
            self._frames.append((name, self._depth if self._depth > before else before + 1))
        while self._frames and self._depth < self._frames[-1][1]:
            self._frames.pop()


def index_headers(extra: list[Path] | None = None) -> dict[str, set[str]]:
    """Map a name -> the scopes it is *visible in*.

    The distinction matters and is where the first version of this file went
    wrong. If `namespace A::B::C { struct Mode; }` appears, then:
        Mode     is visible inside A::B::C
        C        is visible inside A::B
    Bare `C::Mode` therefore only resolves from a scope whose prefix chain
    contains A::B -- being inside `A::D` is not enough, because lookup searches
    A::D, then A, then global, and `C` is a member of neither A::D nor A.
    Storing "the namespace the name lives in" instead conflates those two and
    accepts exactly the bug this check exists for.
    """
    declared: dict[str, set[str]] = {}
    for path in HEADERS + list(extra or []):
        tracker = ScopeTracker()
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            d = DECL.match(line)
            if d:
                if d.group(2):
                    # `namespace A::B::C {` -- the name C is visible in A::B.
                    parts = d.group(2).split("::")
                    name = parts[-1]
                    visible_in = "::".join(parts[:-1])
                    declared.setdefault(name, set()).add(visible_in)
                else:
                    name = d.group(1) or d.group(3)
                    if name and name not in {"if", "for", "while", "switch", "return"}:
                        declared.setdefault(name, set()).add(tracker.current)
            tracker.feed(line)
    return declared


def enclosing_namespaces(path: Path) -> list[tuple[int, str]]:
    """(line number, fully-qualified namespace) at each line that uses a scope."""
    seen: list[tuple[int, str]] = []
    tracker = ScopeTracker()
    for number, line in enumerate(
        path.read_text(encoding="utf-8", errors="replace").splitlines(), 1
    ):
        seen.append((number, tracker.current))
        tracker.feed(line)
    return seen


def lookup_scopes(enclosing: str) -> list[str]:
    """The scopes unqualified lookup searches, innermost first, then global."""
    if not enclosing:
        return [""]
    parts = enclosing.split("::")
    return ["::".join(parts[:i]) for i in range(len(parts), 0, -1)] + [""]


def reachable(enclosing: str, visible_in: str) -> bool:
    """Would unqualified lookup from `enclosing` search the scope `visible_in`?

    Exactly: is `visible_in` one of the enclosing namespaces (or global)?
    A *sibling* of an enclosing namespace is never searched, which is the whole
    point -- `Gr2Port::D3D12` does not see a name that is visible in `Gr2Port::Uwp`.
    """
    return visible_in in lookup_scopes(enclosing)


def strip_comments_and_strings(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r'"[^"\n]*"', '""', text)
    return text


def check(path: Path, declared: dict[str, set[str]]) -> list[str]:
    scopes = dict(enclosing_namespaces(path))
    text = strip_comments_and_strings(
        path.read_text(encoding="utf-8", errors="replace")
    )
    problems: list[str] = []
    reported: set[tuple[int, str]] = set()

    for number, line in enumerate(text.splitlines(), 1):
        # `namespace Gr2Port::Uwp::Display {` would otherwise be read as a use of
        # `Uwp::` (and of `Gr2Port::`) from the enclosing scope.
        if NS_OPEN.match(line):
            continue
        for m in USE.finditer(line):
            tok = m.group(1)
            if tok in EXTERNAL_ROOTS or tok in {"this"}:
                continue
            # Only a *first* qualifier starts a lookup. In
            # `winrt::Windows::Graphics::Display::DisplayInformation` the token
            # `Display` is part of an already-qualified name, so nothing is looked
            # up by it -- it resolves inside `winrt`.
            prefix = line[: m.start()].rstrip()
            if prefix.endswith("::") or prefix.endswith(".") or prefix.endswith("->"):
                continue
            visible = declared.get(tok)
            if not visible:
                continue
            enclosing = scopes.get(number, "")
            if any(reachable(enclosing, scope) for scope in visible):
                continue
            # The fix depends on where the name is actually visible, so say so.
            good = sorted(visible)[0]
            suggestion = good.split("::")[-1] if good else tok
            key = (number, tok)
            if key in reported:
                continue
            reported.add(key)
            try:
                shown = path.resolve().relative_to(ROOT).as_posix()
            except ValueError:
                shown = str(path)
            where = f"`{good}`" if good else "the global namespace"
            problems.append(
                f"{shown}:{number}: `{tok}` is visible inside {where} but used "
                f"bare from `{enclosing or '(global)'}`; unqualified lookup does "
                f"not search a sibling namespace -- write `{suggestion}::{tok}`"
            )
    return problems


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    targets = TARGETS
    extra: list[Path] = []
    if argv and argv[0] == "--file":
        if len(argv) != 2:
            print("usage: check_namespace_qualification.py [--file PATH]")
            return 2
        target = Path(argv[1])
        targets = [target]
        # A fixture declares its namespaces in a header next to it, so index the
        # directory too -- otherwise the fixture's own declarations are invisible
        # and a deliberately broken fixture is reported clean.
        extra = sorted(target.parent.rglob("*.h"))

    declared = index_headers(extra)

    problems: list[str] = []
    for path in targets:
        if path.exists():
            problems.extend(check(path, declared))

    if problems:
        for problem in problems:
            print(problem)
        print()
        print(f"{len(problems)} unqualified reference(s) to a name outside the scope")
        return 1

    print(f"namespace qualification: {len(targets)} file(s), every qualifier resolves")
    return 0


if __name__ == "__main__":
    sys.exit(main())
