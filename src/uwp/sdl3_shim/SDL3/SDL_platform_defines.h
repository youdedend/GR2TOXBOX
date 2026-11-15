// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// <SDL3/SDL_platform_defines.h> for the Xbox UWP build.
//
// This is the first header of the shim, and it exists for one reason: the real
// one contains
//
//     #if WINAPI_FAMILY_WINRT
//     #error Windows RT/UWP is no longer supported in SDL
//
// SDL 3 removed WinRT/UWP support during its development and never restored it.
// That #error is correct for SDL, and it is fatal for this project, whose entire
// frontend is SDL: window, events, gamepad, audio, ImGui's platform layer.
//
// The shim replaces the SDL *implementation* for UWP, not SDL's vocabulary. All
// the other headers -- SDL_events.h, SDL_gamepad.h, SDL_audio.h and the rest --
// are the real ones, taken from externals/sdl3/include, so every struct, enum
// value and function signature used here is SDL's own and cannot drift from it.
// What is replaced is the platform definition and the library behind the
// functions: src/uwp/sdl3_shim/*.cpp implements them on WinRT's CoreWindow,
// Windows.Gaming.Input and XAudio2.
//
// The include order in the UWP build puts this directory before externals/sdl3/
// include, so this file wins for the one header it replaces and everything else
// resolves to the real SDL. See the GR2_XBOX_UWP block in CMakeLists.txt.

#pragma once

// The platform is Windows, and deliberately not WinRT as far as these macros are
// concerned: every remaining SDL header reads them to decide what the *platform*
// offers, and the answer this build wants is "a packaged Windows app", which is
// what SDL_PLATFORM_WIN32 already means here.
#define SDL_PLATFORM_WINDOWS 1
#define SDL_PLATFORM_WIN32 1
#define SDL_PLATFORM "Windows"

// SDL's ABI macros. On a real Windows SDK build these come from the same
// platform-defines header; here they are stated because the shim is a static
// library, so nothing is imported from a DLL and nothing is exported.
#define SDL_DECLSPEC
#define SDLCALL
#define SDL_INLINE __inline
#define SDL_FORCE_INLINE __forceinline
#define SDL_NORETURN __declspec(noreturn)
#define SDL_RESTRICT __restrict
#define SDL_MALLOC
#define SDL_ALLOC_SIZE(x)
#define SDL_ALLOC_SIZE2(x, y)
#define SDL_DEPRECATED
#define SDL_UNUSED
#define SDL_ALIGNED(x) __declspec(align(x))
#define SDL_ANALYZER_NORETURN
#define SDL_NODISCARD
#define SDL_WARN_UNUSED_RESULT
#define SDL_PRINTF_VARARG_FUNC(fmtargnumber)
#define SDL_PRINTF_VARARG_FUNCV(fmtargnumber)
#define SDL_SCANF_VARARG_FUNC(fmtargnumber)
#define SDL_SCANF_VARARG_FUNCV(fmtargnumber)

// The rest of what the real header defines, stated minimally: this build is a
// 64-bit Windows app and none of the other platform branches apply.
#define SDL_BYTEORDER 1234 /* little endian */
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321

// Feature detection the remaining headers look for.
#define HAVE_WINAPIFAMILY_H 1
#include <winapifamily.h>

// SDL's own feature macros, which its headers branch on. The UWP build has no
// threads-through-SDL (the emulator uses std::thread), no filesystem through SDL
// (it uses std::filesystem), and the shim implements the rest.
#define SDL_HAS_64BIT_TYPE 1
