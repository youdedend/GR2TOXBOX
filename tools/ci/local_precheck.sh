#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Local pre-check: compile the platform-portable sources before pushing.
#
# Why this exists. The Windows CI job costs 25 minutes cold, so a missing
# `#include` or a method that does not exist on the object it is called on used to
# cost half an hour per typo. Everything checked here is host-independent code --
# the memory budget, the W^X bookkeeping, the chord layer, the logging back end --
# and therefore compiles with the host's clang/gcc against the real fmt and spdlog
# headers. It cannot check the D3D12 backend (no Windows SDK here), but it removes
# the cheap mistakes from the expensive loop.
#
# Usage:
#   tools/ci/local_precheck.sh          # compile + run the self-tests
#   tools/ci/local_precheck.sh --syntax # compile only, no execution

set -uo pipefail

cd "$(dirname "$0")/../.." || exit 1

CXX=${CXX:-clang++}
command -v "$CXX" >/dev/null 2>&1 || CXX=g++
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "local_precheck: no C++ compiler found (tried clang++ and g++)" >&2
    exit 1
fi

FMT_INCLUDE=externals/fmt/include
SPDLOG_INCLUDE=externals/spdlog/include
if [ ! -d "$FMT_INCLUDE" ]; then
    echo "local_precheck: submodules are not checked out." >&2
    echo "  git submodule update --init --depth 1 externals/fmt externals/spdlog" >&2
    exit 1
fi

CXXFLAGS=(-std=c++23 -Wall -Wextra -Wno-unused-parameter
          -Isrc -I"$FMT_INCLUDE" -I"$SPDLOG_INCLUDE")

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
failures=0
syntax_only=0
[ "${1:-}" = "--syntax" ] && syntax_only=1

# Host-portable translation units. Each entry is "file|defines".
# Nothing here includes a Windows or winrt header.
UNITS=(
    "src/core/uwp_memory.cpp|"
    "src/uwp/chords.cpp|"
    "src/tests/selftest_log.cpp|"
)

echo "local_precheck: $($CXX --version | head -1)"

for entry in "${UNITS[@]}"; do
    file=${entry%%|*}
    defs=${entry#*|}
    # The selftest back end needs one of the selftest macros, otherwise its
    # accessors are not declared in log.h and -Wmissing-declarations fires.
    case "$file" in
        *selftest_log.cpp) defs="$defs -DGR2_CHORDS_SELFTEST" ;;
    esac
    # shellcheck disable=SC2086
    if ! "$CXX" "${CXXFLAGS[@]}" $defs -fsyntax-only "$file" 2>"$OUT/err"; then
        echo "  FAIL $file"
        sed 's/^/    /' "$OUT/err" | head -30
        failures=$((failures + 1))
    else
        echo "  ok   $file"
    fi
done

if [ "$syntax_only" -eq 0 ] && [ "$failures" -eq 0 ]; then
    echo "local_precheck: building and running the self-tests"
    for pair in "xbox_chords_test:chords:GR2_CHORDS_SELFTEST" \
                "xbox_memory_budget_test:memory:GR2_XBOX_BUDGET_SELFTEST"; do
        test=${pair%%:*}
        rest=${pair#*:}
        name=${rest%%:*}
        def=${rest#*:}
        case "$name" in
            chords) srcs=(src/tests/xbox_chords_test.cpp src/uwp/chords.cpp src/tests/selftest_log.cpp) ;;
            memory) srcs=(src/tests/xbox_memory_budget_test.cpp src/core/uwp_memory.cpp src/tests/selftest_log.cpp) ;;
        esac
        # fmt::vformat is a compiled symbol, so the test binaries link fmt itself.
        # spdlog links fmt as a dependency, but these tests use fmt directly.
        if ! "$CXX" "${CXXFLAGS[@]}" "-D$def" "${srcs[@]}" \
                -DFMT_HEADER_ONLY -o "$OUT/$name" 2>"$OUT/err"; then
            echo "  FAIL link $name"
            sed 's/^/    /' "$OUT/err" | head -20
            failures=$((failures + 1))
            continue
        fi
        if "$OUT/$name" >"$OUT/run" 2>&1; then
            echo "  ok   $name: $(tail -1 "$OUT/run")"
        else
            echo "  FAIL run $name"
            sed 's/^/    /' "$OUT/run" | tail -15
            failures=$((failures + 1))
        fi
    done
fi

if [ "$failures" -ne 0 ]; then
    echo "local_precheck: $failures check(s) failed -- do not push yet." >&2
    exit 1
fi
echo "local_precheck: clean."
