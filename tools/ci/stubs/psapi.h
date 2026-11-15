// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// <psapi.h> for the host compile-only check. The declarations live in
// <windows.h> in this stub tree, so there is exactly one spelling of each name:
// an earlier version of this file declared GetCurrentProcess with a different
// return type and the two collided as soon as windows.h declared the real one.
#pragma once

#include <windows.h>
