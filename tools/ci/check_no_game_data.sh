#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fails if a commit's tracked files contain Sony/PS4 game data or packaging
# output. Runs in CI on every push and can be run locally before committing.
#
# Usage: tools/ci/check_no_game_data.sh [--all | <git-range>]
#   --all      check every tracked file (default)
#   <range>    check only files changed in the range, e.g. origin/main...HEAD

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

mode="${1:---all}"
if [[ "$mode" == "--all" ]]; then
    mapfile -t files < <(git ls-files)
else
    mapfile -t files < <(git diff --name-only --diff-filter=ACMR "$mode")
fi

# Copyrighted game data, decryption material and package output. Any match is fatal.
patterns=(
    '\.pkg$' '\.PKG$' '\.zar$' '\.ZAR$' '\.ffpkg$' '\.pup$'
    '\.prx$' '\.sprx$' '\.self$' '\.sfo$' '\.pfs$' '\.gp4$'
    '(^|/)eboot\.bin$' '(^|/)EBOOT\.BIN$' '(^|/)eboot\.elf$'
    '(^|/)sce_sys/' '(^|/)sce_module/'
    '(^|/)app0/' '(^|/)GameData/'
    '\.msix$' '\.msixbundle$' '\.msixupload$' '\.appx$' '\.appxupload$'
    '\.cer$' '\.pfx$' '\.pvk$' '\.spc$'
)

violations=0
for f in "${files[@]:-}"; do
    [[ -z "$f" ]] && continue
    for p in "${patterns[@]}"; do
        if [[ "$f" =~ $p ]]; then
            echo "::error file=$f::Forbidden game data or package output: $f"
            violations=$((violations + 1))
            break
        fi
    done
done

if (( violations > 0 )); then
    echo
    echo "FAILED: $violations forbidden path(s) are tracked."
    echo "This repository is source-only and must never contain Sony/PS4 game"
    echo "data, signing material or package output. See .gitignore."
    exit 1
fi

echo "OK: no copyrighted game data or package output is tracked (${#files[@]} files checked)."
