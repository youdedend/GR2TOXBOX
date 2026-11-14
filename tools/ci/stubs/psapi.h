// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A <psapi.h> stub for the host compile-only check, paired with the empty
// <windows.h> next to it. The console branch of src/core/uwp_memory.cpp falls
// back to PROCESS_MEMORY_COUNTERS_EX when C++/WinRT is not available, and that
// branch still has to be parsed before a push: it is the one place in the file
// that reads private memory accounting, and a typo there is a 25-minute round
// trip. Only the members the file touches are declared.
#pragma once

#include <windows.h>

typedef struct _PROCESS_MEMORY_COUNTERS {
    unsigned long cb;
    unsigned long PageFaultCount;
    unsigned long long PeakWorkingSetSize;
    unsigned long long WorkingSetSize;
    unsigned long long QuotaPeakPagedPoolUsage;
    unsigned long long QuotaPagedPoolUsage;
    unsigned long long QuotaPeakNonPagedPoolUsage;
    unsigned long long QuotaNonPagedPoolUsage;
    unsigned long long PagefileUsage;
    unsigned long long PeakPagefileUsage;
} PROCESS_MEMORY_COUNTERS;

typedef struct _PROCESS_MEMORY_COUNTERS_EX {
    unsigned long cb;
    unsigned long PageFaultCount;
    unsigned long long PeakWorkingSetSize;
    unsigned long long WorkingSetSize;
    unsigned long long QuotaPeakPagedPoolUsage;
    unsigned long long QuotaPagedPoolUsage;
    unsigned long long QuotaPeakNonPagedPoolUsage;
    unsigned long long QuotaNonPagedPoolUsage;
    unsigned long long PagefileUsage;
    unsigned long long PeakPagefileUsage;
    unsigned long long PrivateUsage;
} PROCESS_MEMORY_COUNTERS_EX;

typedef struct _MEMORYSTATUSEX {
    unsigned long dwLength;
    unsigned long dwMemoryLoad;
    unsigned long long ullTotalPhys;
    unsigned long long ullAvailPhys;
    unsigned long long ullTotalPageFile;
    unsigned long long ullAvailPageFile;
    unsigned long long ullTotalVirtual;
    unsigned long long ullAvailVirtual;
    unsigned long long ullAvailExtendedVirtual;
} MEMORYSTATUSEX;

unsigned long GetCurrentProcess();
int GlobalMemoryStatusEx(MEMORYSTATUSEX* status);
int GetProcessMemoryInfo(unsigned long process, PROCESS_MEMORY_COUNTERS* counters,
                         unsigned long size);

// MEMORYSTATUSEX and GlobalMemoryStatusEx are really in <winbase.h>, pulled in by
// <windows.h>. The stub <windows.h> next to this file is empty, so the host check
// cannot see them there and they are declared here instead. This split exists
// only in the stub tree; on a real SDK neither line is needed.
