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

# libSceUsbd on the console. The desktop file it replaces is libusb end to end,
# so this one stands in for the whole library and is the only definition of
# these 65 symbols the UWP link has -- a typo in a signature or a missing
# LIB_FUNCTION would otherwise only show up as an unresolved external on a
# Windows runner, which is exactly the failure mode this section exists to
# prevent. Compiled against the UWP build of usbd.h, so the declarations it is
# checked against are the ones the console actually sees.
if ! "$CXX" "${CXXFLAGS[@]}" -Itools/ci/stubs -DGR2_XBOX_UWP=1 -DGR2_HOST_CHECK_WIN32=1 \
        -DFMT_HEADER_ONLY \
        -fsyntax-only src/core/libraries/usbd/usbd_uwp.cpp 2>"$OUT/err"; then
    echo "  FAIL src/core/libraries/usbd/usbd_uwp.cpp (GR2_XBOX_UWP)"
    sed 's/^/    /' "$OUT/err" | head -30
    annotate_log "src/core/libraries/usbd/usbd_uwp.cpp" "$OUT/err"
    failures=$((failures + 1))
else
    echo "  ok   src/core/libraries/usbd/usbd_uwp.cpp (GR2_XBOX_UWP)"
fi

# The SDL3 shim, against SDL's own headers.
#
# This is the check that makes the shim trustworthy: the real SDL headers are on
# the include path and the shim's replacement SDL_platform_defines.h wins for the
# one header it replaces, so every definition in the shim is compiled against
# SDL's own declaration of it. A wrong return type or a swapped parameter order
# fails here in a second rather than on a Windows runner.
#
# The stub <windows.h> supplies the Win32 names the console branches use, and
# -include it is what makes them visible before the shim's own includes.
for file in src/uwp/sdl3_shim/*.cpp; do
    [ -f "$file" ] || continue
    if ! "$CXX" "${CXXFLAGS[@]}" -Iexternals/sdl3/include -Isrc/uwp/sdl3_shim \
            -Itools/ci/stubs -include tools/ci/stubs/windows.h \
            -fsyntax-only "$file" 2>"$OUT/err"; then
        echo "  FAIL $file (SDL shim)"
        sed 's/^/    /' "$OUT/err" | head -20
        annotate_log "$file" "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   $file (SDL shim)"
    fi
done

# The Direct3D 12 backend, against the header stub. There is no Windows SDK here,
# so this is the only way to type-check a backend file before a Windows runner
# does it. Errors from the vendored D3D12MA header are filtered: the stub does
# not declare the whole of d3d12.h, and that header is not ours to change.
# "file|defines". The backend surface and ImGui's relay name no Direct3D type --
# that is the point of them -- so they need the stub for the Windows names but not
# for DXGI, and they must NOT be given -D_WIN32: SDL's headers take that as a real
# Windows build and then want <process.h>, which this host does not have. The two
# files that do include d3d12.h need it.
D3D12_UNITS=(
    "src/graphics/gr2port_d3d12/pipeline.cpp|-D_WIN32"
    "src/graphics/gr2port_d3d12/command.cpp|-D_WIN32"
    "src/graphics/gr2port_d3d12/backend_surface.cpp|"
    "src/graphics/gr2port_d3d12/backend_capture.cpp|"
    "src/graphics/gr2port_d3d12/imgui_relay.cpp|"
)
for entry in "${D3D12_UNITS[@]}"; do
    file="${entry%%|*}"
    extra="${entry#*|}"
    [ -f "$file" ] || continue
    # The SDL headers are real here too: ImGui's relay and the backend surface take
    # SDL events and windows, and a stand-in for them would defeat the point.
    # shellcheck disable=SC2086
    # Vulkan's headers are real here, and have to be: backend_capture.cpp defines
    # the names the shared capture calls, so it includes the same headers those
    # modules do (meshes.h -> device.h -> vulkan.hpp). Vulkan::Headers is linked on
    # every build, D3D12 included, so this matches the real compile.
    if ! "$CXX" "${CXXFLAGS[@]}" -Itools/ci/d3d12_stub -Iexternals/sdl3/include \
            -Iexternals/imgui -Iexternals/vulkan-headers/include $extra \
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

# The ported HLSL, structurally. This catches the mistakes that do not need a
# compiler, e.g. two cbuffers both declaring g_pad, or a stage using ArenaRef
# without including the header that declares it. Both of those cost a Windows run.
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

# The ported HLSL, compiled. Microsoft ships a Linux x86_64 build of dxc as well as
# the Windows one, and it compiles every stage in well under a second. The CI
# source-guard job fetches it (see build-xbox-uwp.yml), so this step does real work
# on every push. Where it is present -- DXC=$PWD/dxc, or dxc on PATH -- every stage
# in the manifest is compiled for its real profile with warnings as errors, which
# is the only way a shader error is ever found without a Windows runner.
#
# Where it is absent the check is skipped with a line saying so, rather than
# silently passing: a shader that was never compiled must not look compiled. The
# tarball is linux_dxc_<date>.x86_x64.tar.gz from the DirectXShaderCompiler
# releases -- note x86_x64, which is not a typo in this comment and did cost one
# download to discover.
DXC_BIN="${DXC:-}"
if [ -z "$DXC_BIN" ] && command -v dxc >/dev/null 2>&1; then
    DXC_BIN=$(command -v dxc)
fi
if [ -n "$DXC_BIN" ] && [ -x "$DXC_BIN" ] && [ -f src/graphics/gr2port_d3d12/shaders_manifest.cmake ]; then
    echo "local_precheck: compiling HLSL with $("$DXC_BIN" --version | head -1)"
    hlsl_failed=0
    # The manifest names each stage file and its profile variable; the pairing is
    # the file's own stage suffix, which is what gr2port_add_hlsl_shader derives.
    for stage_file in src/graphics/gr2port_d3d12/shaders/*.hlsl src/graphics/gr2port_d3d12/shaders/pfx/*.hlsl; do
        [ -f "$stage_file" ] || continue
        case "$stage_file" in
            *.hlsli|*/common/*) continue ;;
        esac
        case "$stage_file" in
            *.vert.hlsl) profile="vs_6_6" ;;
            *.frag.hlsl) profile="ps_6_6" ;;
            *.comp.hlsl) profile="cs_6_6" ;;
            *.geom.hlsl) profile="gs_6_6" ;;
            *.hull.hlsl|*.tesc.hlsl) profile="hs_6_6" ;;
            *.dom.hlsl|*.tese.hlsl) profile="ds_6_6" ;;
            *) continue ;;
        esac
        # -HV 2021 matches the runner's dxc invocation; -WX makes a warning an
        # error, because a warning here is a line someone should look at.
        if ! "$DXC_BIN" -T "$profile" -E main -HV 2021 -WX -I src/graphics/gr2port_d3d12/shaders \
                -Fo /dev/null "$stage_file" >"$OUT/err" 2>&1; then
            echo "  FAIL $stage_file ($profile)"
            sed 's/^/    /' "$OUT/err" | head -20
            annotate_log "$stage_file" "$OUT/err"
            hlsl_failed=$((hlsl_failed + 1))
        fi
    done
    if [ "$hlsl_failed" -eq 0 ]; then
        echo "  ok   HLSL compile (all stages)"
    else
        failures=$((failures + hlsl_failed))
    fi
else
    echo "  skip HLSL compile (no dxc; set DXC=/path/to/dxc to enable)"
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
