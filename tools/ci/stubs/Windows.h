// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// `#include <Windows.h>` with a capital W.
//
// The Windows SDK accepts either spelling, so third-party headers differ:
// src/core/debug_state.h includes <Windows.h>, while everything else here uses
// <windows.h>. This host has a case-sensitive filesystem, where the two are
// different files; a symlink would be the obvious answer and is wrong, because
// #pragma once treats a symlink and its target as two headers -- the stub would
// then be parsed twice and every typedef in it would be a redefinition. So this
// is a real file whose only job is to include the other one, which the include
// guard dedupes properly.

#include <windows.h>
