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

DEPS_ROOT=${GR2_DEPS_ROOT:-externals}
FMT_INCLUDE="$DEPS_ROOT/fmt/include"
SPDLOG_INCLUDE="$DEPS_ROOT/spdlog/include"
if [ ! -d "$FMT_INCLUDE" ]; then
    echo "local_precheck: submodules are not checked out." >&2
    echo "  Set GR2_DEPS_ROOT to a directory containing pinned dependency headers." >&2
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
    "src/graphics/gr2port_d3d12/detile_layout.cpp|"
    "src/uwp/chords.cpp|"
    "src/tests/selftest_log.cpp|"
)

echo "local_precheck: $($CXX --version | head -1)"
if ! python3 tools/ci/test_port_tools.py; then
    failures=$((failures + 1))
fi
if ! python3 tools/ci/check_uwp_types.py; then
    failures=$((failures + 1))
fi
if ! python3 tools/shaders/check_hlsl_local.py; then
    failures=$((failures + 1))
fi
# New Windows-only logger: check both platform branches against strict SDK API
# declarations and real spdlog, without initializing any submodules locally.
for blackbox_uwp in 0 1; do
    blackbox_defs=()
    [ "$blackbox_uwp" -eq 1 ] && blackbox_defs=(-DGR2_XBOX_UWP=1)
    if ! "$CXX" -std=c++20 -fsyntax-only -Itools/ci/uwp_stubs -Isrc \
        -I"$FMT_INCLUDE" -I"$SPDLOG_INCLUDE" "${blackbox_defs[@]}" \
        src/uwp/blackbox_log.cpp; then
        failures=$((failures + 1))
    fi
done

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

# The UWP import checker itself. There is no dumpbin here and no UWP binary to
# run it on, so what is checked is the checker: a fixture that must pass and one
# that must fail, because a checker that matches no import format reports every
# binary clean. That exact bug shipped in the first draft of this file -- the
# pattern required the three-column form and dumpbin /imports prints two.
if command -v python3 >/dev/null 2>&1; then
    imports_tmp=$(mktemp -d)
    cat >"$imports_tmp/clean.txt" <<'IMPORTS_CLEAN'
      api-ms-win-core-synch-l1-2-0.dll
                      2C  InitializeCriticalSectionEx
      D3D12Core.dll
                      1A  D3D12CreateDevice
IMPORTS_CLEAN
    cat >"$imports_tmp/leaky.txt" <<'IMPORTS_LEAKY'
      KERNEL32.dll
                      15  TlsAlloc
                      9F  GetModuleHandleW
                      8B  LCMapStringW
      USER32.dll
                      72  MessageBoxW
IMPORTS_LEAKY
    imports_ok=1
    if ! python3 tools/ci/check_uwp_imports.py --exe t.exe --imports-file "$imports_tmp/clean.txt" >/dev/null 2>&1; then
        echo "  FAIL UWP import check: a clean fixture was rejected"
        imports_ok=0
    fi
    if python3 tools/ci/check_uwp_imports.py --exe t.exe --imports-file "$imports_tmp/leaky.txt" >/dev/null 2>&1; then
        echo "  FAIL UWP import check: a leaking fixture was accepted"
        imports_ok=0
    fi
    rm -rf "$imports_tmp"
    if [ "$imports_ok" -eq 1 ]; then
        echo "  ok   UWP import check (clean fixture passes, leaking fixture fails)"
    else
        failures=$((failures + 1))
    fi
fi

# The WinRT include checker, plus its own fixture test.
#
# This exists because of a Windows failure the host precheck could not see: a 4K
# change used winrt::Windows::ApplicationModel::Core::CoreApplication while only
# winrt/Windows.UI.Core.h was included, and ::ABI::Windows::UI::Core with no ABI
# header at all. C++/WinRT does not exist on Linux, so the precheck compiles these
# files with the macro undefined, exercises the desktop half, and skips the console
# half entirely -- the build went green here and failed on the runner.
#
# The mapping is mechanical (namespace -> projection header), so it can be checked
# without a compiler. The fixtures are checked too: a scan that sees no WinRT
# types at all also reports every file clean, which is the failure mode worth
# guarding.
if command -v python3 >/dev/null 2>&1; then
    winrt_ok=1
    if ! python3 tools/ci/check_winrt_includes.py >"$OUT/err" 2>&1; then
        echo "  FAIL WinRT includes"
        sed 's/^/    /' "$OUT/err" | head -20
        winrt_ok=0
    fi

    # Fixture: a UWP file that uses WinRT and includes the right headers must be
    # accepted; the same file with the header removed must be rejected.
    winrt_fixtures=$(mktemp -d)
    cat >"$winrt_fixtures/good.cpp" <<'WINRT_GOOD'
#if defined(GR2_XBOX_UWP)
#include <winrt/Windows.ApplicationModel.Core.h>
#include <windows.ui.core.h>
void f() {
    const auto view = winrt::Windows::ApplicationModel::Core::CoreApplication::MainView();
    auto* w = static_cast<::ABI::Windows::UI::Core::ICoreWindow*>(nullptr);
    (void)view; (void)w;
}
#endif
WINRT_GOOD
    # The bad fixture drops only the ApplicationModel.Core include.
    grep -v 'Windows.ApplicationModel.Core.h' "$winrt_fixtures/good.cpp" >"$winrt_fixtures/bad.cpp"
    if ! python3 tools/ci/check_winrt_includes.py --file "$winrt_fixtures/good.cpp" >/dev/null 2>&1; then
        echo "  FAIL WinRT includes: a correct fixture was rejected"
        winrt_ok=0
    fi
    if python3 tools/ci/check_winrt_includes.py --file "$winrt_fixtures/bad.cpp" >/dev/null 2>&1; then
        echo "  FAIL WinRT includes: a missing include was accepted"
        winrt_ok=0
    fi
    rm -rf "$winrt_fixtures"

    if [ "$winrt_ok" -eq 1 ]; then
        echo "  ok   WinRT includes (namespaces resolved, fixtures both ways)"
    else
        failures=$((failures + 1))
    fi
fi

# Namespace qualification.
#
# Same blind spot as the WinRT check above: `CreateSwapchain` is in Gr2Port::D3D12
# and used bare `Display::Mode`, but Display is declared in Gr2Port::Uwp::Display.
# Unqualified lookup searches D3D12, then Gr2Port, then global -- never a sibling
# namespace -- so it did not resolve, and only the UWP build ever compiled that
# line. The fixture test pins the behaviour: the correct form must pass and the
# sibling-qualified form must fail.
if command -v python3 >/dev/null 2>&1; then
    ns_ok=1
    if ! python3 tools/ci/check_namespace_qualification.py >"$OUT/err" 2>&1; then
        echo "  FAIL namespace qualification"
        sed 's/^/    /' "$OUT/err" | head -20
        ns_ok=0
    fi

    ns_tmp=$(mktemp -d)
    mkdir -p "$ns_tmp/a"
    # The use has to be a `::`-qualified one: the check is about the first
    # qualifier of an expression, and `Thing{}` is a constructor call that starts
    # no namespace lookup at all.
    cat >"$ns_tmp/a/decl.h" <<'NS_DECL'
namespace Outer::Sibling {
struct Thing {
    static constexpr int kValue = 7;
};
}
namespace Outer::Other {
inline int Use();
}
NS_DECL
    cat >"$ns_tmp/good.cpp" <<'NS_GOOD'
#include "decl.h"
namespace Outer::Other {
int Use() { return Sibling::Thing::kValue; }
}
NS_GOOD
    # The broken form: `Thing` is reachable as Sibling::Thing, not bare.
    cat >"$ns_tmp/bad.cpp" <<'NS_BAD'
#include "decl.h"
namespace Outer::Other {
int Use() { return Thing::kValue; }
}
NS_BAD
    if ! python3 tools/ci/check_namespace_qualification.py --file "$ns_tmp/good.cpp" >/dev/null 2>&1; then
        echo "  FAIL namespace qualification: a qualified fixture was rejected"
        ns_ok=0
    fi
    if python3 tools/ci/check_namespace_qualification.py --file "$ns_tmp/bad.cpp" >/dev/null 2>&1; then
        echo "  FAIL namespace qualification: a sibling-scope reference was accepted"
        ns_ok=0
    fi
    rm -rf "$ns_tmp"

    if [ "$ns_ok" -eq 1 ]; then
        echo "  ok   namespace qualification (sibling scopes, fixtures both ways)"
    else
        failures=$((failures + 1))
    fi
fi

# The HLSL cbuffer bindings against the root signature.
#
# Now a hard failure, not a warning. The register layout is uniform and enforced:
# b0 is the arena table (20 bytes, matching ArenaSet::RootArenaTable exactly), b1
# is the per-draw block, b2 is the texture-handle block. A shader that puts any of
# them elsewhere reads a root parameter the renderer never writes, and D3D12
# reports nothing when it does.
if command -v python3 >/dev/null 2>&1; then
    if ! python3 tools/shaders/check_cbuffer_registers.py >"$OUT/err" 2>&1; then
        echo "  FAIL HLSL cbuffer registers"
        sed 's/^/    /' "$OUT/err" | head -20
        annotate_log "src/graphics/gr2port_d3d12/shaders" "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   HLSL cbuffer registers"
    fi
fi

# uwp_app.cpp is compiled into BOTH builds: it owns RunGame and Shutdown for the
# console and the desktop. Its WinRT half therefore has to sit inside
# GR2_XBOX_UWP guards, and a guard that covers the wrong part of the function --
# which is exactly what shipped first here -- breaks the desktop build while the
# console one stays green. Compiling it with no GR2_XBOX_UWP is the cheap way to
# keep that honest.
if ! "$CXX" "${CXXFLAGS[@]}" -DFMT_HEADER_ONLY \
        -fsyntax-only src/graphics/gr2port_d3d12/uwp/uwp_app.cpp 2>"$OUT/err"; then
    echo "  FAIL src/graphics/gr2port_d3d12/uwp/uwp_app.cpp (desktop)"
    sed 's/^/    /' "$OUT/err" | head -30
    annotate_log "src/graphics/gr2port_d3d12/uwp/uwp_app.cpp" "$OUT/err"
    failures=$((failures + 1))
else
    echo "  ok   src/graphics/gr2port_d3d12/uwp/uwp_app.cpp (desktop guards)"
fi

# Compile the entire UI-thread picker/persistence/event loop against narrow WinRT
# signatures and the real SDL/ImGui headers; never skip the console-only source.
if ! "$CXX" "${CXXFLAGS[@]}" -Itools/ci/uwp_stubs -I"$DEPS_ROOT/sdl3/include" \
        -I"$DEPS_ROOT/imgui" '-DIMGUI_USER_CONFIG="imgui/imgui_config.h"' -DGR2_XBOX_UWP=1 \
        -fsyntax-only src/graphics/gr2port_d3d12/uwp/uwp_launcher.cpp 2>"$OUT/err"; then
    echo "  FAIL UWP launcher WinRT contracts"
    cat "$OUT/err"
    failures=$((failures + 1))
else
    echo "  ok   UWP launcher WinRT contracts (picker, access cache, dispatcher)"
fi

# The AppContainer Win32 wrappers, which had never been host-checked -- and that is
# why two of them shipped with the wrong number of arguments for the FromApp call
# they forward to. VirtualAlloc2FromApp takes seven parameters and
# MapViewOfFile3FromApp nine, the same lists as their desktop counterparts;
# nothing here declared them, so a four-argument call compiled cleanly on this host
# and failed only on the Windows runner. tools/ci/stubs/windows.h now declares the
# memory API with its real arities, which turns this into a real check.
#
# Both configurations are compiled: the UWP branch is the one that broke, and the
# desktop branch is what CI's desktop job exercises, so a mistake in either is
# caught here.
cat >"$OUT/win_compat.cpp" <<'COMPAT_EOF'
#include "core/uwp_win_compat.h"
int main() {
    return 0;
}
COMPAT_EOF
for compat_uwp in 1 0; do
    compat_defs=""
    compat_label="desktop"
    if [ "$compat_uwp" = "1" ]; then
        compat_defs="-DGR2_XBOX_UWP=1"
        compat_label="GR2_XBOX_UWP"
    fi
    # shellcheck disable=SC2086
    if ! "$CXX" "${CXXFLAGS[@]}" -Itools/ci/stubs -D_WIN32=1 $compat_defs \
            -fsyntax-only "$OUT/win_compat.cpp" 2>"$OUT/err"; then
        echo "  FAIL src/core/uwp_win_compat.h ($compat_label)"
        sed 's/^/    /' "$OUT/err" | head -20
        annotate_log "src/core/uwp_win_compat.h" "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   src/core/uwp_win_compat.h ($compat_label)"
    fi
done

# minimp3 under the console build. MINIMP3_NO_STDIO is what CMakeLists.txt defines
# for GR2_XBOX_UWP, because minimp3's file loader opens with CreateFileA and sizes
# the file with GetFileSize -- both desktop-only, and both rejected by the packaged
# build. Its in-memory decoder is what music_player_service.cpp calls there instead.
# This compiles the header both ways and checks the console build really has no
# reference to the desktop file calls, so the define cannot silently stop working.
cat >"$OUT/m3.cpp" <<'M3_EOF'
#define MINIMP3_IMPLEMENTATION
#include <minimp3_ex.h>
int main() {
    return 0;
}
M3_EOF
if ! "$CXX" "${CXXFLAGS[@]}" -I"$DEPS_ROOT/minimp3" -fsyntax-only "$OUT/m3.cpp" 2>"$OUT/err"; then
    echo "  FAIL externals/minimp3 (desktop)"
    sed 's/^/    /' "$OUT/err" | head -20
    failures=$((failures + 1))
else
    echo "  ok   externals/minimp3 (desktop)"
fi
if ! "$CXX" "${CXXFLAGS[@]}" -I"$DEPS_ROOT/minimp3" -DMINIMP3_NO_STDIO=1 -c "$OUT/m3.cpp" \
        -o "$OUT/m3.o" 2>"$OUT/err"; then
    echo "  FAIL externals/minimp3 (MINIMP3_NO_STDIO)"
    sed 's/^/    /' "$OUT/err" | head -20
    failures=$((failures + 1))
else
    if nm -C "$OUT/m3.o" 2>/dev/null | grep -qi 'CreateFile\|GetFileSize'; then
        echo "  FAIL externals/minimp3 (MINIMP3_NO_STDIO still reaches the desktop file calls)"
        failures=$((failures + 1))
    else
        echo "  ok   externals/minimp3 (MINIMP3_NO_STDIO)"
    fi
fi

# src/uwp/uwp_shell_compat.h, in both configurations. It is the stand-in for two
# desktop shell calls that the packaged build's object files reference but cannot
# take from shell32/user32: CommandLineToArgvW (CLI11 calls it) and
# GetAsyncKeyState (the screenshot path). Written blind to the Windows toolchain,
# so this is the only check it gets before a runner compiles it -- and on desktop
# it must be completely inert, which is the second half of the check.
cat >"$OUT/shell_compat.cpp" <<'SHELL_EOF'
#include "uwp/uwp_shell_compat.h"
int main() {
    return 0;
}
SHELL_EOF
for sc_uwp in 1 0; do
    sc_defs=""
    sc_label="desktop (must be inert)"
    if [ "$sc_uwp" = "1" ]; then
        sc_defs="-DGR2_XBOX_UWP=1"
        sc_label="GR2_XBOX_UWP"
    fi
    # shellcheck disable=SC2086
    if ! "$CXX" "${CXXFLAGS[@]}" -Itools/ci/stubs -D_WIN32=1 $sc_defs \
            -fsyntax-only "$OUT/shell_compat.cpp" 2>"$OUT/err"; then
        echo "  FAIL src/uwp/uwp_shell_compat.h ($sc_label)"
        sed 's/^/    /' "$OUT/err" | head -20
        annotate_log "src/uwp/uwp_shell_compat.h" "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   src/uwp/uwp_shell_compat.h ($sc_label)"
    fi
done

# ImGui's SDL3 platform backend under the packaged build. It is what defines
# ImGui::Sdl::{Init,Shutdown,NewFrame,ProcessEvent}, which imgui_core.cpp calls on
# every build -- so if it does not compile here, gr2port.exe does not link, which
# is exactly what happened. Compiled in both configurations because both now build
# it: desktop against real SDL3, the console against the shim.
for iu_uwp in 1 0; do
    iu_defs=""
    iu_label="desktop"
    if [ "$iu_uwp" = "1" ]; then
        iu_defs="-DGR2_XBOX_UWP=1"
        iu_label="GR2_XBOX_UWP"
    fi
    # shellcheck disable=SC2086
    if ! "$CXX" "${CXXFLAGS[@]}" -I"$DEPS_ROOT/sdl3/include" -Isrc/uwp/sdl3_shim \
            -I"$DEPS_ROOT/imgui" -I"$DEPS_ROOT/json/include" -I"$DEPS_ROOT/ext-boost" \
            -I"$DEPS_ROOT/vulkan-headers/include" -Itools/ci/stubs -D_WIN32 $iu_defs \
            -include tools/ci/stubs/windows.h -fsyntax-only \
            src/imgui/renderer/imgui_impl_sdl3.cpp 2>"$OUT/err"; then
        echo "  FAIL src/imgui/renderer/imgui_impl_sdl3.cpp ($iu_label)"
        sed 's/^/    /' "$OUT/err" | head -20
        annotate_log "src/imgui/renderer/imgui_impl_sdl3.cpp" "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   src/imgui/renderer/imgui_impl_sdl3.cpp ($iu_label)"
    fi
done

# Every SDL3 function those translation units call is defined by the shim. The
# packaged build links no SDL library, so a missing one is an undefined symbol
# twelve minutes into a Windows run -- which is how SDL_free and the clipboard
# family were found. This is the check that would have caught both.
if command -v python3 >/dev/null 2>&1; then
    if ! python3 tools/ci/check_sdl_shim_symbols.py \
            src/imgui/renderer/imgui_impl_sdl3.cpp >"$OUT/err" 2>&1; then
        echo "  FAIL SDL shim (a called SDL3 function is not defined)"
        sed 's/^/    /' "$OUT/err" | head -20
        annotate_log "src/uwp/sdl3_shim" "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   SDL shim ($(sed 's/ .*//' "$OUT/err") SDL3 calls defined)"
    fi
fi

# The packaging scripts only run on Windows, so a structural mistake in them -- a
# typo'd variable, an unterminated here-string, an unbalanced brace -- costs a
# full Windows run to find, and it is the step that decides whether an artifact
# exists at all. These are the checks that do not need PowerShell.
if command -v python3 >/dev/null 2>&1; then
    if ! python3 tools/ci/check_powershell.py >"$OUT/err" 2>&1; then
        echo "  FAIL packaging scripts"
        sed 's/^/    /' "$OUT/err" | head -20
        annotate_log "scripts/ci" "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   packaging scripts"
    fi
fi

# The UWP package manifest and its assets, before makeappx sees them. makeappx
# validates XML and refuses to pack a missing asset, but everything else it accepts
# and the console rejects at install -- which is a sideload failure a long way from
# its cause. This checks asset existence (case-sensitively), the namespace prefixes,
# the capabilities a game on Xbox needs, and the shape of Identity and
# TargetDeviceFamily.
if command -v python3 >/dev/null 2>&1; then
    if ! python3 tools/ci/check_uwp_manifest.py >"$OUT/err" 2>&1; then
        echo "  FAIL UWP manifest"
        sed 's/^/    /' "$OUT/err" | head -20
        annotate_log "uwp/Package.appxmanifest" "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   UWP manifest ($(grep -o '[0-9]* assets' "$OUT/err" | head -1), $(grep -o '[0-9]* capabilities' "$OUT/err" | head -1))"
    fi
fi

# The console build's in-memory MP3 path. music_player_service.cpp reads the file
# with IOFile and hands the bytes to mp3dec_load_buf; the first version of that
# called IOFile by the wrong name, used operator bool that it does not have, and
# wrote an unscoped FileAccessMode -- three errors the Windows runner found and
# this host did not. The snippet below is that code, compiled as itself.
cat >"$OUT/mp3_load.cpp" <<'MP3_EOF'
#include "common/io_file.h"
#include <minimp3_ex.h>
#include <vector>
int main(int, char** argv) {
    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_file_info_t file_info{};
    int res = -1;
    Common::FS::IOFile mp3{std::filesystem::path(argv[1]), Common::FS::FileAccessMode::Read};
    if (mp3.IsOpen()) {
        std::vector<u8> bytes(mp3.GetSize());
        if (!bytes.empty() && mp3.Read(bytes) == bytes.size()) {
            res = mp3dec_load_buf(&dec, bytes.data(), bytes.size(), &file_info, nullptr, nullptr);
        }
    }
    return res;
}
MP3_EOF
if ! "$CXX" "${CXXFLAGS[@]}" -I"$DEPS_ROOT/minimp3" -DMINIMP3_NO_STDIO=1 \
        -fsyntax-only "$OUT/mp3_load.cpp" 2>"$OUT/err"; then
    echo "  FAIL music_player_service MP3 path (GR2_XBOX_UWP)"
    sed 's/^/    /' "$OUT/err" | head -20
    annotate_log "src/core/libraries/music/music_player_service.cpp" "$OUT/err"
    failures=$((failures + 1))
else
    echo "  ok   music_player_service MP3 path (GR2_XBOX_UWP)"
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
    if ! "$CXX" "${CXXFLAGS[@]}" -I"$DEPS_ROOT/sdl3/include" -Isrc/uwp/sdl3_shim \
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
# Only generated shader bytecode is stubbed, never the host dispatch module.
cat >"$OUT/detile_comp.h" <<'DETILE_HEADER'
#pragma once
#include <cstdint>
#include <span>
namespace Gr2Port::D3D12::Shaders {
inline constexpr std::uint8_t detile_comp[] = {0};
inline constexpr std::span<const std::uint8_t> detile_comp_span = detile_comp;
}
DETILE_HEADER
for shader in imgui_relay_vert imgui_relay_frag; do
    sed "s/detile_comp/$shader/g" "$OUT/detile_comp.h" >"$OUT/$shader.h"
done
CXXFLAGS+=(-I"$OUT")
D3D12_UNITS=(
    "src/graphics/gr2port_d3d12/detile.cpp|-D_WIN32"
    "src/graphics/gr2port_d3d12/pipeline.cpp|-D_WIN32"
    "src/graphics/gr2port_d3d12/command.cpp|-D_WIN32"
    "src/graphics/gr2port_d3d12/backend_surface.cpp|-D_WIN32 -DGR2_RENDERER_D3D12=1"
    "src/graphics/gr2port_d3d12/imgui_gpu.cpp|-D_WIN32"
    "src/graphics/gr2port_d3d12/backend_capture.cpp|-DGR2_RENDERER_D3D12=1"
    "src/graphics/gr2port/capture.cpp|-DGR2_RENDERER_D3D12=1"
    "src/graphics/gr2port/scene_ring.cpp|"
    "src/graphics/gr2port_d3d12/arena.cpp|-D_WIN32"
    "src/graphics/gr2port_d3d12/meshes_d3d12.cpp|-D_WIN32 -DGR2_RENDERER_D3D12=1"
    "src/graphics/gr2port_d3d12/textures_d3d12.cpp|-D_WIN32 -DGR2_RENDERER_D3D12=1"
    "src/graphics/gr2port_d3d12/texture_layout.cpp|-D_WIN32"
    "src/graphics/gr2port_d3d12/descriptors.cpp|-D_WIN32"
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
    if ! "$CXX" "${CXXFLAGS[@]}" -Itools/ci/d3d12_stub -I"$DEPS_ROOT/sdl3/include" \
            -I"$DEPS_ROOT/imgui" -I"$DEPS_ROOT/ext-boost" -I"$DEPS_ROOT/vulkan-headers/include" '-DIMGUI_USER_CONFIG="imgui/imgui_config.h"' $extra \
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

# The D3D12 backend's definitions, by the names the linker will look for.
#
# -fsyntax-only above proves that backend_capture.cpp compiles; it says nothing
# about whether each definition landed in the namespace its declaration lives in,
# because a declaration is in scope either way. That is exactly how QueueCurve
# shipped once as Gr2Port::QueueCurve when particles.h declares
# Gr2Port::Particles::QueueCurve: the file compiled, the desktop link failed with
# one undefined symbol, and a 25-minute run was spent learning it. Compiling the
# object and reading its symbol table catches it in a second.
D3D12_SYMBOLS=(
    "Gr2Port::Capture::OnFrame(unsigned long, unsigned long)"
    "Gr2Port::Capture::OnParticleSetup(unsigned long)"
    "Gr2Port::Capture::OnParticleList(unsigned long)"
    "Gr2Port::Capture::OnFrameEnd()"
    "Gr2Port::Capture::Frames()"
    "Gr2Port::Capture::ToggleCapture()"
    "Gr2Port::Particles::QueueCurve(unsigned int, unsigned int, unsigned long, unsigned int)"
    "Gr2Port::Meshes::Get(unsigned long)"
    "Gr2Port::Meshes::Install(unsigned long, Xbyak::CodeGenerator&, unsigned long)"
    "Gr2Port::Textures::Get(unsigned long)"
    "Gr2Port::Textures::GetBySharp(unsigned int const*, bool&)"
    "Gr2Port::Textures::Install(unsigned long, Xbyak::CodeGenerator&, unsigned long)"
    "Gr2Port::Meshes::Prefetch(unsigned long)"
    "Gr2Port::Textures::GetCube(unsigned long)"
    "Gr2Port::Textures::Prefetch(unsigned long const*, unsigned int)"
    "Gr2Port::Textures::PrefetchRecords(unsigned long const*, unsigned int)"
    "Gr2Port::Upscaler::RenderSize(unsigned int, unsigned int)"
    "Gr2Port::Deformers::Get(unsigned long, unsigned long, Gr2Port::Meshes::Mesh const&, unsigned long, unsigned long)"
    "Gr2Port::WaitForPipelineBuilds()"
    "Gr2Port::PipelineBuildProgress()"
)
# Audit the real capture object plus its backend adapters, not removed stubs.
# -r also rejects duplicate definitions. A failed object compile MUST fail here.
if command -v nm >/dev/null 2>&1; then
    objects_ok=1
    for source in src/graphics/gr2port/capture.cpp src/graphics/gr2port_d3d12/backend_capture.cpp src/graphics/gr2port_d3d12/meshes_d3d12.cpp src/graphics/gr2port_d3d12/textures_d3d12.cpp; do
        object="$OUT/$(basename "$source" .cpp).o"
        resident_defs=()
        case "$source" in
            *meshes_d3d12.cpp|*textures_d3d12.cpp) resident_defs=(-D_WIN32) ;;
        esac
        if ! "$CXX" "${CXXFLAGS[@]}" -DGR2_RENDERER_D3D12=1 "${resident_defs[@]}" -fpermissive -c -o "$object" -Itools/ci/d3d12_stub \
                -I"$DEPS_ROOT/ext-boost" -I"$DEPS_ROOT/vulkan-headers/include" "$source" 2>"$OUT/err"; then
            echo "  FAIL capture link-contract object $source"; cat "$OUT/err"
            failures=$((failures + 1)); objects_ok=0
        fi
    done
    if [ "$objects_ok" -eq 1 ]; then
        if ! "$CXX" -r "$OUT/capture.o" "$OUT/backend_capture.o" "$OUT/meshes_d3d12.o" "$OUT/textures_d3d12.o" -o "$OUT/capture-contract.o" 2>"$OUT/err"; then
            echo "  FAIL duplicate capture definitions"; cat "$OUT/err"
            failures=$((failures + 1))
        else
            nm -C --defined-only "$OUT/capture-contract.o" >"$OUT/syms"
            missing_syms=0
            for sym in "${D3D12_SYMBOLS[@]}"; do
                if ! grep -qF " $sym" "$OUT/syms"; then
                    echo "  MISSING $sym"; missing_syms=$((missing_syms + 1))
                fi
            done
            if [ "$missing_syms" -ne 0 ]; then
                echo "  FAIL capture link contract ($missing_syms missing)"
                failures=$((failures + 1))
            else
                echo "  ok   shared capture/backend link contract (${#D3D12_SYMBOLS[@]} definitions; no duplicates)"
            fi
        fi
    fi
fi
# Preserve the Vulkan capture/header path as well (RenderExtent ABI unchanged).
if ! "$CXX" "${CXXFLAGS[@]}" -I"$DEPS_ROOT/ext-boost" -I"$DEPS_ROOT/vulkan-headers/include" \
        -fsyntax-only src/graphics/gr2port/capture.cpp 2>"$OUT/err"; then
    echo "  FAIL Vulkan shared capture syntax"; cat "$OUT/err"
    failures=$((failures + 1))
else
    echo "  ok   Vulkan shared capture syntax (unchanged RenderExtent API)"
fi

# Only generated SPIR-V headers are stood in for here; Vulkan's real C++ headers
# and both factored CPU implementations are compiled unchanged.
mkdir -p "$OUT/graphics/host_shaders"
for family in micro macro; do
    for bits in 8 16 32 64 128; do
        symbol="TILING_${family^^}_${bits}_COMP"
        printf '#pragma once\ninline constexpr unsigned int %s[] = {0};\n' "$symbol" \
            >"$OUT/graphics/host_shaders/tiling_${family}_${bits}_comp.h"
    done
done
for source in src/graphics/gr2port/meshes.cpp src/graphics/gr2port/textures.cpp; do
    if ! "$CXX" "${CXXFLAGS[@]}" -I"$DEPS_ROOT/ext-boost" -I"$DEPS_ROOT/vulkan-headers/include" \
            -I"$DEPS_ROOT/magic_enum/include" -fsyntax-only "$source" 2>"$OUT/err"; then
        echo "  FAIL shared Vulkan factoring $source";cat "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   shared Vulkan mesh/texture factoring: $source"
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
        echo "  ok   HLSL structure ($(find src/graphics/gr2port_d3d12/shaders -name '*.hlsl' | wc -l) stages)"
    fi
fi

# No Shader Model 6.5+ construct in any stage. The console's driver stops at 6.4
# and a 6.6 module does not load there at all, so a stray ResourceDescriptorHeap[]
# is not a slow path -- it is a shader that never runs, found only when someone
# tries it on hardware. -T *_6_4 would catch it, but dxc is absent from this host
# more often than not, so the check that can always run is a textual one.
#
# The only place the names may appear is descriptors.hlsli's own comments, which
# exist to explain what replaced them.
sm66_left=$(grep -rn 'ResourceDescriptorHeap\|SamplerDescriptorHeap' \
    src/graphics/gr2port_d3d12/shaders/ 2>/dev/null \
    | grep -v 'shaders/common/descriptors.hlsli' \
    | grep -v ':[0-9]*: *//' || true)
if [ -n "$sm66_left" ]; then
    echo "  FAIL HLSL is SM 6.4 (6.6-only constructs remain)"
    printf '%s\n' "$sm66_left" | sed 's/^/    /' | head -20
    printf '%s\n' "$sm66_left" >"$OUT/err"
    annotate_log "src/graphics/gr2port_d3d12/shaders" "$OUT/err"
    failures=$((failures + 1))
else
    echo "  ok   HLSL is SM 6.4 (no 6.6-only constructs)"
fi

# The descriptor tables agree between the shader header and the C++ list. These
# are two hand-written spellings of the same fact -- a space number and an element
# count -- and a table that disagrees is silent: the root signature binds one
# range, the shader reads another, and the result is wrong pixels rather than an
# error. dxc caught a related one (a Texture2DArray read through a Texture2D
# table); this catches the arithmetic that dxc cannot see.
if command -v python3 >/dev/null 2>&1; then
    if ! python3 tools/shaders/check_descriptor_tables.py \
            src/graphics/gr2port_d3d12/shaders/common/descriptors.hlsli \
            src/graphics/gr2port_d3d12/descriptors.h >"$OUT/err" 2>&1; then
        echo "  FAIL descriptor tables (shader header and C++ list disagree)"
        sed 's/^/    /' "$OUT/err" | head -20
        annotate_log "src/graphics/gr2port_d3d12/descriptors.h" "$OUT/err"
        failures=$((failures + 1))
    else
        echo "  ok   descriptor tables (shader header matches the C++ list)"
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
# tarball is linux_dxc_<date>.x86_64.tar.gz from DirectXShaderCompiler releases.
DXC_BIN="${DXC:-}"
if [ -z "$DXC_BIN" ] && command -v dxc >/dev/null 2>&1; then
    DXC_BIN=$(command -v dxc)
fi
if [ -n "$DXC_BIN" ] && [ -x "$DXC_BIN" ] && [ -f src/graphics/gr2port_d3d12/shaders_manifest.cmake ]; then
    echo "local_precheck: compiling HLSL with $("$DXC_BIN" --version | head -1)"
    if ! python3 tools/shaders/check_hlsl_local.py --dxc "$DXC_BIN" --require; then
        failures=$((failures + 1))
    fi
else
    echo "  skip HLSL compile (no dxc; set DXC=/path/to/dxc to enable)"
fi

# An independent front-end, not a substitute for the real DXC gate above.
# Explicit skip if not installed; local shader work must run this with --require.
if ! python3 tools/shaders/check_hlsl_local.py; then
    failures=$((failures + 1))
fi

# GCC alone rejects the vendored D3D12MA AllocHandle member/type spelling;
# -fpermissive is confined to its native object-symbol audit above. Real Windows
# builds use clang-cl/MSVC; shader warnings remain fatal, without exceptions.
if [ "$syntax_only" -eq 0 ] && [ "$failures" -eq 0 ]; then
    for mode in host asan-ubsan; do
        sanitize=()
        if [ "$mode" = asan-ubsan ]; then
            sanitize=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie)
        fi
        if "$CXX" "${CXXFLAGS[@]}" -O1 -g "${sanitize[@]}" -DFMT_HEADER_ONLY -DGR2_CHORDS_SELFTEST \
                -Itools/ci/d3d12_stub -I"$DEPS_ROOT/magic_enum/include" \
                src/tests/gpu_residency_test.cpp src/tests/selftest_log.cpp \
                src/graphics/gr2port_d3d12/texture_layout.cpp src/graphics/gr2port_d3d12/detile_layout.cpp \
                src/graphics/amdgpu/pixel_format.cpp src/graphics/amdgpu/tiling.cpp \
                -o "$OUT/gpu-residency-$mode" 2>"$OUT/err" && \
                ASAN_OPTIONS=detect_leaks=1 "$OUT/gpu-residency-$mode"; then
            echo "  ok   GPU residency $mode layout/range/fence/twin tests"
        else
            echo "  FAIL GPU residency $mode tests";cat "$OUT/err"
            failures=$((failures + 1))
        fi
    done
fi

if [ "$syntax_only" -eq 0 ] && [ "$failures" -eq 0 ]; then
    for mode in host asan-ubsan; do
        sanitize=()
        if [ "$mode" = asan-ubsan ]; then
            sanitize=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie)
        fi
        if "$CXX" "${CXXFLAGS[@]}" -O1 -g -pthread "${sanitize[@]}" \
                src/tests/scene_ring_test.cpp src/graphics/gr2port/scene_ring.cpp \
                -o "$OUT/scene-ring-$mode" 2>"$OUT/err" && \
                ASAN_OPTIONS=detect_leaks=1 "$OUT/scene-ring-$mode"; then
            echo "  ok   SceneRing $mode roundtrip/concurrent ordering test"
        else
            echo "  FAIL SceneRing $mode test"; cat "$OUT/err"
            failures=$((failures + 1))
        fi
    done
fi

if [ "$syntax_only" -eq 0 ] && [ "$failures" -eq 0 ]; then
    echo "local_precheck: building and running the self-tests"
    for pair in "xbox_chords_test:chords:GR2_CHORDS_SELFTEST" \
                "xbox_memory_budget_test:memory:GR2_XBOX_BUDGET_SELFTEST" \
                "display_mode_test:display:GR2_DISPLAY_SELFTEST"; do
        test=${pair%%:*}
        rest=${pair#*:}
        name=${rest%%:*}
        def=${rest#*:}
        case "$name" in
            chords) srcs=(src/tests/xbox_chords_test.cpp src/uwp/chords.cpp src/tests/selftest_log.cpp) ;;
            memory) srcs=(src/tests/xbox_memory_budget_test.cpp src/core/uwp_memory.cpp src/tests/selftest_log.cpp) ;;
            # Header-only: the plan arithmetic has no .cpp to link against.
            display) srcs=(src/tests/display_mode_test.cpp) ;;
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

if [ "$syntax_only" -eq 0 ] && [ "$failures" -eq 0 ]; then
    if "$CXX" -std=c++23 -O0 -pthread -ffunction-sections -fdata-sections -Wl,--gc-sections \
        '-DIMGUI_USER_CONFIG="imgui/imgui_config.h"' -Isrc -I"$DEPS_ROOT/imgui" -I"$DEPS_ROOT/sdl3/include" \
        src/tests/imgui_snapshot_test.cpp src/graphics/gr2port_d3d12/imgui_relay.cpp \
        "$DEPS_ROOT/imgui/imgui.cpp" "$DEPS_ROOT/imgui/imgui_draw.cpp" \
        "$DEPS_ROOT/imgui/imgui_tables.cpp" "$DEPS_ROOT/imgui/imgui_widgets.cpp" \
        -o "$OUT/snapshot" 2>"$OUT/err" && "$OUT/snapshot"; then
        echo "  ok   ImGui immutable snapshot lifetime/concurrent publication test"
    else
        echo "  FAIL ImGui snapshot test"; cat "$OUT/err"
        failures=$((failures + 1))
    fi
fi

if [ "$syntax_only" -eq 0 ] && [ "$failures" -eq 0 ]; then
    if "$CXX" -std=c++23 -pthread -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -Isrc -Itools/ci/stubs -I"$DEPS_ROOT/sdl3/include" \
        src/tests/sdl_event_wait_test.cpp src/uwp/sdl3_shim/sdl_core.cpp \
        -o "$OUT/event-wait" 2>"$OUT/err" && "$OUT/event-wait"; then
        echo "  ok   SDL timed event wait linked test (timeout, FIFO, peek, producer wake)"
    else
        echo "  FAIL SDL timed event wait test"; cat "$OUT/err"
        failures=$((failures + 1))
    fi
fi

if [ "$failures" -ne 0 ]; then
    echo "local_precheck: $failures check(s) failed -- do not push yet." >&2
    exit 1
fi
echo "local_precheck: clean."
