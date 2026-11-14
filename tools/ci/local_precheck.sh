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

# g++ first, deliberately. CI's clang++ and the local g++ disagree about
# transitive includes and about how much of a newer libstdc++ they can parse, and
# that divergence cost three red runs being chased with a compiler that was not
# the one failing. Whichever compiler CI uses, it is now the same one this script
# uses -- gcc, which is installed everywhere and is what has been verified here.
# Set CXX explicitly to check with something else; the script says which it used.
CXX=${CXX:-g++}
command -v "$CXX" >/dev/null 2>&1 || CXX=clang++
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

# Everything this script prints also goes to local-precheck.log, which the
# workflow uploads even when the job fails. Console output in Actions is a stream
# with annotations layered over it; a file is what survives.
exec > >(tee local-precheck.log) 2>&1
failures=0
syntax_only=0

# Emits one annotation carrying a whole compiler log. GitHub truncates check-run
# annotations at 200 of them and the runner also synthesises its own from lines
# containing "error", so per-line annotations drown in noise -- the earlier
# failures here arrived as a single useless "1 warning and 12 errors generated."
# One annotation holding everything, newline-escaped, survives intact.
annotate_log() {
    [ -s "$2" ] || return 0
    local text
    text=$(head -c 60000 "$2" | sed -e 's/%/%25/g' -e 's/\r//g' | awk 'BEGIN{ORS="%0A"}{print}')
    printf '::error file=%s::%s\n' "$1" "$text"
}
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
        annotate_log "$file" "$OUT/err"
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
    annotate_log "src/core/uwp_memory.cpp" "$OUT/err"
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
            annotate_log "$file" "$OUT/ours"
            failures=$((failures + 1))
        else
            echo "  ok   $file (vendored-header diagnostics only)"
        fi
    else
        echo "  ok   $file"
    fi
done

# The ported HLSL, structurally. dxc only runs on Windows; this catches the
# mistakes it reports that do not need a compiler, e.g. two cbuffers both
# declaring g_pad, or a stage using ArenaRef without including the header that
# declares it. Both of those cost a Windows run.
if command -v python3 >/dev/null 2>&1; then
    if ! python3 tools/shaders/check_hlsl_structure.py >"$OUT/err" 2>&1; then
        echo "  FAIL HLSL structure"
        sed 's/^/    /' "$OUT/err" | head -30
        annotate_log "src/graphics/gr2port_d3d12/shaders" "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   HLSL structure ($(ls src/graphics/gr2port_d3d12/shaders/*.hlsl | wc -l) stages)"
    fi
fi

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
            annotate_log "$name" "$OUT/err"
            failures=$((failures + 1))
            continue
        fi
        if "$OUT/$name" >"$OUT/run" 2>&1; then
            echo "  ok   $name: $(tail -1 "$OUT/run")"
        else
            echo "  FAIL run $name"
            sed 's/^/    /' "$OUT/run" | tail -15
            annotate_log "$name" "$OUT/run"
            failures=$((failures + 1))
        fi
    done
fi

if [ "$failures" -ne 0 ]; then
    echo "local_precheck: $failures check(s) failed -- do not push yet." >&2
    exit 1
fi
echo "local_precheck: clean."
