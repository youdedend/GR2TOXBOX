// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// <memoryapi.h> for the host compile-only check.
//
// uwp_win_compat.h includes this and <windows.h>; in the real SDK the two overlap,
// and here the declarations all live in windows.h so that there is one spelling of
// each name. This header exists so the include resolves, not so it can add
// anything -- add a name to windows.h instead.

#pragma once

#include <windows.h>
