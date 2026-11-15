#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Structural checks on the packing scripts, which only ever run on Windows.

The packaging scripts cannot be executed here -- there is no PowerShell and no
Windows SDK -- so every mistake in them costs a full Windows run to discover, and
the packaging step is the last thing before an artifact exists. That makes the
cheap structural checks worth having on Linux:

  * here-strings (@' ... '@ and @" ... "@) are paired;
  * braces balance once comments and strings are removed;
  * every variable the script reads without defining is either a parameter, an
    automatic variable, a loop variable, or comes from the environment -- a typo'd
    variable name is silent in PowerShell (it expands to nothing), which turns a
    check into a no-op that then fails open;
  * every tool the script invokes through Find-SdkTool is a real SDK tool name.

Not a parser: a mistake this cannot see is still a mistake, but the ones it can see
have all cost a run at some point.
"""

import os
import re
import sys

SCRIPTS = [
    "scripts/ci/package_msix.ps1",
    "scripts/ci/make_certificate.ps1",
    "scripts/ci/annotate_failure.ps1",
]

# Variable names PowerShell provides, or that a pipeline supplies.
AUTOMATIC = {
    "_", "args", "env", "error", "false", "foreach", "home", "host", "input",
    "lastexitcode", "matches", "myinvocation", "null", "ofs", "pid", "profile",
    "psboundparameters", "pscmdlet", "psculture", "pshome", "pwd", "shellid",
    "stacktrace", "this", "true", "gitem", "?", "^", "$", "switch",
}

# Things that appear as $Name but are not variables: subexpression forms, scope
# qualifiers and the sigils PowerShell uses inside strings.
NOT_A_VARIABLE = re.compile(
    r"^(?:env|script|global|local|private|using|workflow|function|variable):"
)


# The qualifier dimensions makepri recognises in a /dq token.
QUALIFIER_DIMENSIONS = {
    "lang", "scale", "contrast", "homeRegion", "targetSize", "layoutDirection",
    "theme", "alternateForm", "dxFeatureLevel", "configuration", "deviceFamily",
    "custom",
}


def strip_for_balance(src):
    """Remove comments and string literals so brace counting means something."""
    out = []
    i = 0
    n = len(src)
    while i < n:
        # here-string
        m = re.match(r"@(['\"])\r?\n", src[i:])
        if m:
            close = m.group(1) + "@"
            end = src.find(close, i + m.end())
            i = n if end < 0 else end + len(close)
            continue
        c = src[i]
        # line comment
        if c == "#":
            end = src.find("\n", i)
            i = n if end < 0 else end
            continue
        # block comment
        if src.startswith("<#", i):
            end = src.find("#>", i + 2)
            i = n if end < 0 else end + 2
            continue
        # single- or double-quoted string, honouring the doubled-quote escape
        if c in "'\"":
            j = i + 1
            while j < n:
                if src[j] == c:
                    if j + 1 < n and src[j + 1] == c:
                        j += 2
                        continue
                    j += 1
                    break
                j += 1
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def check(path, problems):
    src = open(path, encoding="utf-8").read()

    # here-string pairing
    for tag in ('@"', "@'"):
        count = len(re.findall(r"(?m)^\s*" + re.escape(tag) + r"\s*$", src))
        closing = src.count(tag[::-1] + "\n") + len(re.findall(r"(?m)^" + re.escape(tag[::-1]) + r"\s*$", src))
        # Pragmatic: count openers at line start and closers at line start.
        openers = len(re.findall(r"(?m)^\s*" + re.escape(tag) + r"\s*$", src))
        closers = len(re.findall(r"(?m)^" + re.escape(tag[::-1]) + r"\s*$", src))
        if openers != closers:
            problems.append(
                f"{path}: {openers} '{tag}' here-string opener(s) but {closers} closer(s)"
            )

    # brace balance
    stripped = strip_for_balance(src)
    delta = stripped.count("{") - stripped.count("}")
    if delta != 0:
        problems.append(f"{path}: braces are unbalanced by {delta:+d}")

    # declared names: parameters, assignments, foreach/for variables
    declared = set()
    for m in re.finditer(r"\$([A-Za-z_][A-Za-z0-9_]*)", src):
        name = m.group(1)
        after = src[m.end():]
        # $x = ... is an assignment
        if re.match(r"\s*(?:[-+*/%]?=)(?!=)", after):
            declared.add(name)
    for m in re.finditer(r"param\s*\(([\s\S]*?)\)\s*\n", src):
        for p in re.finditer(r"\$([A-Za-z_][A-Za-z0-9_]*)", m.group(1)):
            declared.add(p.group(1))
    for m in re.finditer(r"foreach\s*\(\s*\$([A-Za-z_][A-Za-z0-9_]*)\s+in\b", src):
        declared.add(m.group(1))
    for m in re.finditer(r"\bfor\s*\(\s*\$([A-Za-z_][A-Za-z0-9_]*)", src):
        declared.add(m.group(1))
    # catch/try blocks bind nothing, but a -PipelineVariable or [ref] does not matter
    for m in re.finditer(r"\$([A-Za-z_][A-Za-z0-9_]*)\s*\+=", src):
        declared.add(m.group(1))

    reads = set()
    for m in re.finditer(r"\$([A-Za-z_][A-Za-z0-9_]*)", src):
        reads.add(m.group(1))

    # A variable that is read but never assigned, provided, or automatic. Compared
    # case-insensitively: PowerShell variables are case-insensitive.
    known = {n.lower() for n in declared} | {n.lower() for n in AUTOMATIC}
    for name in sorted(reads):
        low = name.lower()
        if low in known:
            continue
        if NOT_A_VARIABLE.match(name):
            continue
        # $env:FOO reads $env first; that is handled by the automatic list.
        problems.append(
            f"{path}: ${name} is read but never assigned -- PowerShell expands it to "
            f"nothing, so a check using it silently passes"
        )

    # A relative directory pushed onto the location stack and then used as the
    # base of a Join-Path: the relative path resolves against itself, so
    # Push-Location xbox-package/staging followed by
    # Join-Path xbox-package/staging AppxManifest.xml yields
    # xbox-package/staging/xbox-package/staging/AppxManifest.xml. That is a run
    # lost to a "cannot find path" from whichever line reads it first, and it has
    # happened here once already.
    for push in re.finditer(r"Push-Location\s+\$(\w+)", src):
        var = push.group(1)
        # Only a *relative* directory is a problem. A variable that was resolved
        # (Resolve-Path, or an absolute path) is the fix, not the bug.
        resolved = re.search(
            r"\$" + var + r"\s*=\s*(?:\(\s*Resolve-Path|['\"]?[A-Za-z]:|\$PWD|\$env:)",
            src[: push.start()],
        )
        if resolved:
            continue
        pop = src.find("Pop-Location", push.end())
        span = src[push.end(): pop if pop > 0 else len(src)]
        for join in re.finditer(r"Join-Path\s+\$" + var + r"\b", span):
            line = src[: push.end() + join.start()].count("\n") + 1
            problems.append(
                f"{path}:{line}: Join-Path uses ${var} inside a block that already "
                f"did Push-Location ${var} -- the relative path resolves twice. "
                f"Resolve it once before the Push-Location and use that."
            )

    # Get-PfxCertificate on a password-protected .pfx blocks on stdin.
    #
    # It has no -Password parameter, so on a protected file it prints
    # "Enter password for the private-key file:" and waits for input that a headless
    # CI step never provides. The step then hangs with no output until the job
    # timeout -- 80 minutes, once. X509Certificate2 takes the password as an
    # argument and never prompts; there is no reason to use the other one here.
    # Comments are stripped first so an explanatory comment is not a violation.
    code = strip_for_balance(src)
    for m in re.finditer(r"Get-PfxCertificate", code):
        line = code[: m.start()].count("\n") + 1
        problems.append(
            f"{path}:{line}: Get-PfxCertificate can block forever on a "
            f"password-protected .pfx (no -Password parameter, prompts on stdin). "
            f"Use [X509Certificate2]::new($path, $password) instead."
        )

    # makepri /dq takes one default qualifier *set*: tokens of the form
    # <dimension>-<value> joined by underscores. A bare language tag, or a
    # comma-separated list, is rejected at runtime -- and the rejection arrives as
    # "makepri createconfig failed (1)" on a 24-minute run.
    for m in re.finditer(r"createconfig[^\n]*/dq\s+(\S+)", src):
        value = m.group(1)
        if value.startswith('"$') or value.startswith("$"):
            continue  # built at runtime; checked below by shape where possible
        if "," in value:
            problems.append(
                f"{path}: makepri /dq {value!r} is comma-separated; qualifiers within "
                f"one set are joined by underscores"
            )
        for token in value.split("_"):
            dimension = token.split("-", 1)[0] if "-" in token else ""
            if dimension.lower() not in QUALIFIER_DIMENSIONS:
                problems.append(
                    f"{path}: makepri /dq token {token!r} does not start with a known "
                    f"qualifier dimension ({', '.join(sorted(QUALIFIER_DIMENSIONS))}); "
                    f"a language qualifier is 'lang-en-US', not 'en-US'"
                )

    # A qualifier string assembled from a list of languages is the same mistake in
    # its dynamic form: lang- takes one value.
    for m in re.finditer(r"\$\w*[Qq]ualifiers?\s*=\s*\([^)]*-join\s+','", src):
        problems.append(
            f"{path}: a makepri qualifier set is being built by joining with ','; "
            f"there is one default language and it is spelled lang-<tag>"
        )

    # Find-SdkTool must name tools that exist in the SDK
    for m in re.finditer(r'Find-SdkTool\s+"([^"]+)"', src):
        tool = m.group(1)
        if tool not in {
            "makepri.exe", "makeappx.exe", "signtool.exe", "makecert.exe",
            "certutil.exe", "mt.exe", "c1010.exe", "dumpbin.exe",
        }:
            problems.append(f"{path}: Find-SdkTool \"{tool}\" is not a tool this expects")


def main():
    problems = []
    for path in SCRIPTS:
        if not os.path.exists(path):
            problems.append(f"{path} is missing")
            continue
        check(path, problems)
    if problems:
        for p in problems:
            print(p)
        print()
        print(f"{len(problems)} problem(s) in the packaging scripts")
        return 1
    print(f"packaging scripts: {len(SCRIPTS)} files structurally consistent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
