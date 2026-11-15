// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"
constexpr DWORD MEM_RESERVE=0x2000, MEM_RESERVE_PLACEHOLDER=0x40000, PAGE_NOACCESS=1;
struct MEM_EXTENDED_PARAMETER;
HANDLE GetCurrentProcess();
void* VirtualAlloc2FromApp(HANDLE, void*, std::size_t, DWORD, DWORD, MEM_EXTENDED_PARAMETER*, unsigned long);
