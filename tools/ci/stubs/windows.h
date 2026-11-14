// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// An empty <windows.h> for the host compile-only check of the Xbox paths.
//
// tools/ci/local_precheck.sh has no Windows SDK, but the console-only branches of
// src/core/uwp_memory.cpp still need to be *parsed* before a push: a typo behind
// `#if defined(GR2_XBOX_UWP)` is invisible to a desktop compile and costs a
// 25-minute CI round trip. This stub satisfies the include so the branches are
// seen; the declarations they use are all hidden behind __has_include and the
// Windows-only #elif, so nothing here has to actually work.
#pragma once

// The memory file needs MEMORYSTATUSEX and GlobalMemoryStatusEx, which the real
// <windows.h> provides via <winbase.h>. It includes <psapi.h> right after this
// header, so the declarations live there in the stub tree. Keep the empty-header
// property otherwise: anything declared here would be invisible to a real build
// and could hide a missing include.
