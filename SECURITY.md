<!--
SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Security policy

## What this project considers a security issue

GR2toXBOX executes guest code natively, so the interesting attack surface is the
boundary between *game data* and the *host process*, plus the package itself:

* a crafted game file causing code execution **outside** the guest's own
  address space;
* a path that lets guest data escape into the host filesystem (`LocalState`,
  USB drives) beyond the paths the user selected;
* a W^X bypass: any path where a page is simultaneously writable and executable
  on a console build, or where execute permission is granted without
  `FlushInstructionCache` after a patch;
* a packaging or signing flaw in `scripts/ci/`, or any way the published `.cer`
  could be used to sign a package the console trusts;
* anything that leaks the CI signing key. **The `.pfx` must never be published**;
  only the public `.cer` is distributed.

Bugs where the game crashes, renders incorrectly or runs out of memory are
**ordinary bugs**, not security issues — use the issue templates.

## Reporting

Report privately through GitHub's **Security → Report a vulnerability** on
`youdedend/GR2TOXBOX`. Please do not open a public issue.

Include the version from the release page and, if relevant, `log.txt` with any
personal paths trimmed.

You will get an acknowledgement within about a week. This is a hobby project
maintained in spare time; there is no bounty and no formal SLA, but reports that
are specific get fixed.

## Scope notes

* **The guest is not sandboxed from itself.** Gravity Rush 2's own code runs with
  the privileges the process has, exactly as it would on any native port. A
  malicious game file is a *modified* game file; we do not attempt to defend
  against a user deliberately feeding the port hostile PS4 code, and we say so
  rather than implying a guarantee.
* **AppContainer is a real boundary.** The package runs as UWP in an AppContainer
  with `broadFileSystemAccess`; it can read files the user points it at, and
  nothing more.
* **The update channel is the repository.** Packages come from GitHub Actions and
  GitHub Releases. If you obtained a build somewhere else, its signature is the
  only thing vouching for it — check that the certificate matches the release.
