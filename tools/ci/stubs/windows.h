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
// Declaring _WIN32 makes third-party headers (Vulkan's, notably) take their
// MSVC branch, whose calling-convention spelling this host's gcc does not know.
// The keyword is defined as nothing, which is what it means on x86-64 anyway.
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
typedef int BOOL;
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned long long DWORD64;
typedef unsigned int UINT;
typedef int INT;
typedef short SHORT;
typedef void* HLOCAL;
// The pointer-sized unsigned type the memory API is declared in terms of.
typedef unsigned long long SIZE_T;
typedef unsigned long long ULONG64;
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
// Read by the UWP branch of GetGlobalUserFolder, which takes the package's
// local state from this rather than from the desktop shell's folder lookup.
DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size);
void OutputDebugStringW(LPCWSTR message);
void OutputDebugStringA(LPCSTR message);
BOOL GetProcessMemoryInfo(HANDLE process, PROCESS_MEMORY_COUNTERS* counters, DWORD size);
BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* status);

// The local heap, which uwp_shell_compat.h's CommandLineToArgvW replacement uses.
// Both are documented as UWP-capable, so unlike most of this file they are not
// stand-ins for missing desktop calls but the real thing under another header.
#ifndef LMEM_FIXED
#define LMEM_FIXED 0x0000
#endif
HLOCAL LocalAlloc(UINT flags, SIZE_T bytes);
HLOCAL LocalFree(HLOCAL block);

// The dynamic loader, which vulkan.hpp's dynamic-dispatch branch takes on _WIN32.
// Declared for the same reason as the memory API above: without the declaration
// the third-party header does not compile, and the check it is part of is worth
// more than the names cost.
HMODULE LoadLibraryA(LPCSTR name);
HMODULE LoadLibraryW(LPCWSTR name);
BOOL FreeLibrary(HMODULE module);
void* GetProcAddress(HMODULE module, LPCSTR name);

// --- memoryapi.h's surface, as src/core/uwp_win_compat.h uses it ------------
//
// The arities here are the real ones, taken from Microsoft's documentation, and
// that is the whole point: an earlier version of uwp_win_compat.h called
// VirtualAlloc2FromApp with four arguments and MapViewOfFile3FromApp with seven,
// when they take seven and nine. Both compiled on this host only because nothing
// declared them here, so the call was never checked; the packaged build was the
// first thing to reject it. Declaring them with their true signatures means the
// host check catches the next such mistake in a second.

#ifndef MEM_COMMIT
#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_RELEASE 0x00008000
#define MEM_FREE 0x00010000
#endif
#ifndef PAGE_NOACCESS
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#endif
#ifndef MEM_EXTENDED_PARAMETER_TYPE
// The field is only ever read by the real SDK; the wrapper passes the pointer
// through, so an opaque struct of the right shape is enough here.
typedef struct MEM_EXTENDED_PARAMETER {
    DWORD64 Type;
    union {
        DWORD64 ULong64;
        void* Pointer;
        SIZE_T Size;
        HANDLE Handle;
        DWORD ULong;
    };
} MEM_EXTENDED_PARAMETER;
#endif

typedef struct SECURITY_ATTRIBUTES {
    DWORD nLength;
    void* lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct MEMORY_BASIC_INFORMATION {
    void* BaseAddress;
    void* AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION;
typedef MEMORY_BASIC_INFORMATION* PMEMORY_BASIC_INFORMATION;

void* VirtualAlloc2(HANDLE process, void* base, SIZE_T size, DWORD type, DWORD protect,
                    MEM_EXTENDED_PARAMETER* extended, DWORD count);
void* VirtualAlloc2FromApp(HANDLE process, void* base, SIZE_T size, DWORD type, DWORD protect,
                           MEM_EXTENDED_PARAMETER* extended, DWORD count);
void* MapViewOfFile3(HANDLE mapping, HANDLE process, void* base, unsigned long long offset,
                     SIZE_T view_size, DWORD allocation_type, DWORD page_protection,
                     MEM_EXTENDED_PARAMETER* extended, DWORD count);
void* MapViewOfFile3FromApp(HANDLE mapping, HANDLE process, void* base, unsigned long long offset,
                            SIZE_T view_size, DWORD allocation_type, DWORD page_protection,
                            MEM_EXTENDED_PARAMETER* extended, DWORD count);
BOOL VirtualProtect(void* address, SIZE_T size, DWORD new_protect, DWORD* old_protect);
BOOL VirtualProtectFromApp(void* address, SIZE_T size, DWORD new_protect, DWORD* old_protect);
BOOL VirtualFree(void* address, SIZE_T size, DWORD free_type);
BOOL VirtualFreeFromApp(void* address, SIZE_T size, DWORD free_type);
SIZE_T VirtualQuery(const void* address, MEMORY_BASIC_INFORMATION* info, SIZE_T length);
SIZE_T VirtualQueryEx(HANDLE process, const void* address, MEMORY_BASIC_INFORMATION* info,
                      SIZE_T length);
void* CreateFileMappingFromApp(HANDLE file, SECURITY_ATTRIBUTES* attributes, DWORD protect,
                               unsigned long long maximum_size, LPCWSTR name);
void* CreateFileMapping2(HANDLE file, SECURITY_ATTRIBUTES* attributes, DWORD desired_access,
                         DWORD page_protection, DWORD section_flags,
                         unsigned long long maximum_size, LPCWSTR name,
                         MEM_EXTENDED_PARAMETER* extended, DWORD count);
BOOL VirtualFreeEx(HANDLE process, void* address, SIZE_T size, DWORD free_type);
BOOL UnmapViewOfFile2(HANDLE process, void* base, DWORD unmap_flags);

#ifdef __cplusplus
}
#endif
