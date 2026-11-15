// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// <windows.h> for the host compile-only check of the Xbox UWP paths.
//
// The console-only code is compiled twice: by MSVC on the Windows runner, and by
// the host's compiler here so a typo behind `#if defined(GR2_XBOX_UWP)` is found
// in seconds rather than after a 25-minute round trip. There is no Windows SDK
// in this container, so this declares the handful of Win32 and WinRT names that
// code actually uses.
//
// It is deliberately small and deliberately declarative: nothing here links, and
// nothing here should grow a behaviour. A name that is missing is a name to
// declare, not a reason to skip the check.
//
// <winapifamily.h> comes with it, because the SDL shim's replacement
// SDL_platform_defines.h includes the real one.

#pragma once

// --- winapifamily ---------------------------------------------------------
// A packaged app: WINAPI_PARTITION_APP is the one that holds, WINAPI_PARTITION_
// DESKTOP is the one that does not. WINAPI_FAMILY_WINRT is what SDL's own header
// turns into its #error, and the shim's replacement header is what stops that.

#ifndef WINAPI_FAMILY_PARTITION
#define WINAPI_PARTITION_DESKTOP 0x00000002
#define WINAPI_PARTITION_APP 0x00000001
#define WINAPI_FAMILY WINAPI_PARTITION_APP
#define WINAPI_PARTITION(p) ((WINAPI_FAMILY) & (p))
#define WINAPI_FAMILY_PARTITION(p) (WINAPI_PARTITION(p) != 0)
#define WINAPI_FAMILY_APP WINAPI_PARTITION_APP
#endif

// --- Basic types ---------------------------------------------------------
// The real header's spellings, because console code mixes them with the
// project's own u32/s64 aliases and the compiler has to see the same types.

#ifndef _WIN32_STUB_TYPES
#define _WIN32_STUB_TYPES
typedef int BOOL;
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned long long DWORD64;
typedef unsigned int UINT;
typedef int INT;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HWND;
typedef void* HINSTANCE;
typedef const char* LPCSTR;
typedef char* LPSTR;
typedef const wchar_t* LPCWSTR;
typedef wchar_t* LPWSTR;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long ULONG;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef int HRESULT;
typedef unsigned int UINT32;
typedef unsigned long long UINT64;
typedef long long INT64;
typedef unsigned char UCHAR;
typedef char CHAR;
typedef wchar_t WCHAR;
#ifdef _WIN64
typedef unsigned long long ULONG_PTR;
typedef long long LONG_PTR;
typedef unsigned long long UINT_PTR;
typedef long long INT_PTR;
#else
typedef unsigned long ULONG_PTR;
typedef long LONG_PTR;
typedef unsigned int UINT_PTR;
typedef int INT_PTR;
#endif
#endif

// --- Basic macros --------------------------------------------------------
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define WINAPI
#define CALLBACK
#define APIENTRY
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

// --- Structures the console code names ------------------------------------
typedef struct _LARGE_INTEGER {
    union {
        struct {
            DWORD LowPart;
            LONG HighPart;
        };
        LONGLONG QuadPart;
    };
} LARGE_INTEGER;

typedef struct _MEMORYSTATUSEX {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    unsigned long long ullTotalPhys;
    unsigned long long ullAvailPhys;
    unsigned long long ullTotalPageFile;
    unsigned long long ullAvailPageFile;
    unsigned long long ullTotalVirtual;
    unsigned long long ullAvailVirtual;
    unsigned long long ullAvailExtendedVirtual;
} MEMORYSTATUSEX;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

// --- psapi.h's surface, which uwp_memory.cpp uses -------------------------
// Declared here rather than in psapi.h so there is one spelling of each name;
// <psapi.h> in this tree includes this header and adds only what is genuinely
// psapi's.
typedef struct _PROCESS_MEMORY_COUNTERS {
    DWORD cb;
    DWORD PageFaultCount;
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
    PROCESS_MEMORY_COUNTERS counters;
    unsigned long long PrivateUsage;
} PROCESS_MEMORY_COUNTERS_EX;

// --- Functions the console code calls -------------------------------------
// Declared, never defined: this header exists so the host compiler can parse the
// console branches, not so they can run.

#ifdef __cplusplus
extern "C" {
#endif

void Sleep(DWORD milliseconds);
DWORD GetTickCount(void);
DWORD GetLastError(void);
BOOL QueryPerformanceCounter(LARGE_INTEGER* counter);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency);
HANDLE GetCurrentProcess(void);
HMODULE GetModuleHandleW(LPCWSTR name);
DWORD GetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size);
void OutputDebugStringW(LPCWSTR message);
void OutputDebugStringA(LPCSTR message);
BOOL GetProcessMemoryInfo(HANDLE process, PROCESS_MEMORY_COUNTERS* counters, DWORD size);
BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* status);

#ifdef __cplusplus
}
#endif
