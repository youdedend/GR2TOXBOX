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
        head -10 "$OUT/err" | while IFS= read -r line; do echo "::error file=$file::$line"; done
        failures=$((failures + 1))
    else
        echo "  ok   $file"
    fi
done

# The console-only branches. Same translation unit, GR2_XBOX_UWP defined, with an
# empty <windows.h> stub on the include path so the Win32 includes resolve. This
# is the check that would have caught the UWP job's host build needing the fmt
# include path and C++/WinRT, before a Windows runner was spent on it.
if ! "$CXX" "${CXXFLAGS[@]}" -Itools/ci/stubs -DGR2_XBOX_UWP=1 -DGR2_HOST_CHECK_WIN32=1 \
        -DFMT_HEADER_ONLY \
        -fsyntax-only src/core/uwp_memory.cpp 2>"$OUT/err"; then
    echo "  FAIL src/core/uwp_memory.cpp (GR2_XBOX_UWP)"
    sed 's/^/    /' "$OUT/err" | head -30
    head -10 "$OUT/err" | while IFS= read -r line; do echo "::error file=src/core/uwp_memory.cpp::$line"; done
    failures=$((failures + 1))
else
    echo "  ok   src/core/uwp_memory.cpp (GR2_XBOX_UWP)"
fi

# The Direct3D 12 backend, against the header stub. There is no Windows SDK here,
# so this is the only way to type-check a backend file before a Windows runner
# does it. Errors from the vendored D3D12MA header are filtered: the stub does
# not declare the whole of d3d12.h, and that header is not ours to change.
D3D12_UNITS=(
    src/graphics/gr2port_d3d12/pipeline.cpp
    src/graphics/gr2port_d3d12/command.cpp
)
for file in "${D3D12_UNITS[@]}"; do
    [ -f "$file" ] || continue
    if ! "$CXX" "${CXXFLAGS[@]}" -Itools/ci/d3d12_stub -D_WIN32 \
            -fsyntax-only "$file" 2>"$OUT/err"; then
        # Only the diagnostics that are not inside the vendored header count.
        grep -E 'error' "$OUT/err" | grep -v 'd3d12ma/' >"$OUT/ours" || true
        if [ -s "$OUT/ours" ]; then
            echo "  FAIL $file"
            sed 's/^/    /' "$OUT/ours" | head -20
            # Emit the workflow-command form as well: the job log is not always
            # reachable, and an unreadable failure costs a whole run. Harmless
            # outside Actions, where it is just a line of text.
            head -10 "$OUT/ours" | while IFS= read -r line; do
                echo "::error file=$(printf '%s' "$line" | cut -d: -f1)::$line"
            done
            failures=$((failures + 1))
        else
            echo "  ok   $file (vendored-header diagnostics only)"
        fi
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
            head -10 "$OUT/err" | while IFS= read -r line; do echo "::error::$line"; done
            failures=$((failures + 1))
            continue
        fi
        if "$OUT/$name" >"$OUT/run" 2>&1; then
            echo "  ok   $name: $(tail -1 "$OUT/run")"
        else
            echo "  FAIL run $name"
            sed 's/^/    /' "$OUT/run" | tail -15
            tail -10 "$OUT/run" | while IFS= read -r line; do echo "::error::$line"; done
            failures=$((failures + 1))
        fi
    done
fi

if [ "$failures" -ne 0 ]; then
    echo "local_precheck: $failures check(s) failed -- do not push yet." >&2
    exit 1
fi
echo "local_precheck: clean."
