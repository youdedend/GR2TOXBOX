// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.ui.core.h"
#include <cstddef>
using ULONG = std::uint32_t;
using DWORD = std::uint32_t;
using DWORD64 = std::uint64_t;
using ULONGLONG = std::uint64_t;
using HANDLE = void*;
using PVOID = void*;
using LONG = std::int32_t;
using BOOL = int;
#define CALLBACK
#define INVALID_HANDLE_VALUE reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1))
constexpr DWORD FILE_APPEND_DATA=4, FILE_SHARE_READ=1, FILE_SHARE_WRITE=2,
 FILE_FLAG_WRITE_THROUGH=0x80000000, CREATE_ALWAYS=2,
 ERROR_FILE_NOT_FOUND=2, ERROR_ALREADY_EXISTS=183, FILE_ATTRIBUTE_DIRECTORY=16;
constexpr DWORD EXCEPTION_ACCESS_VIOLATION=0xc0000005, EXCEPTION_IN_PAGE_ERROR=0xc0000006,
 EXCEPTION_ILLEGAL_INSTRUCTION=0xc000001d, EXCEPTION_STACK_OVERFLOW=0xc00000fd,
 EXCEPTION_INT_DIVIDE_BY_ZERO=0xc0000094;
constexpr LONG EXCEPTION_CONTINUE_SEARCH=0;
constexpr DWORD UNW_FLAG_NHANDLER=0, CP_UTF8=65001;
struct IMAGE_DOS_HEADER { std::int32_t e_lfanew; };
struct IMAGE_NT_HEADERS64 { struct { DWORD SizeOfImage; } OptionalHeader; };
BOOL SetThreadStackGuarantee(ULONG*);
struct CREATEFILE2_EXTENDED_PARAMETERS {
 DWORD dwSize, dwFileAttributes, dwFileFlags, dwSecurityQosFlags;
 void* lpSecurityAttributes; HANDLE hTemplateFile;
};
struct CONTEXT {
 DWORD64 Rax,Rbx,Rcx,Rdx,Rsi,Rdi,Rbp,Rsp,R8,R9,R10,R11,R12,R13,R14,R15,Rip;
 DWORD EFlags; unsigned short SegCs,SegSs,SegDs,SegEs,SegFs,SegGs;
};
struct EXCEPTION_RECORD {
 DWORD ExceptionCode, ExceptionFlags; EXCEPTION_RECORD* ExceptionRecord;
 void* ExceptionAddress; DWORD NumberParameters; std::uintptr_t ExceptionInformation[15];
};
struct EXCEPTION_POINTERS { EXCEPTION_RECORD* ExceptionRecord; CONTEXT* ContextRecord; };
struct RUNTIME_FUNCTION {};
struct WIN32_FILE_ATTRIBUTE_DATA { DWORD dwFileAttributes; };
enum GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard };
BOOL WriteFile(HANDLE,const void*,DWORD,DWORD*,void*);
BOOL FlushFileBuffers(HANDLE);
DWORD GetLastError();
DWORD GetCurrentThreadId();
ULONGLONG GetTickCount64();
DWORD GetEnvironmentVariableW(const wchar_t*,wchar_t*,DWORD);
PVOID AddVectoredExceptionHandler(unsigned long, LONG(CALLBACK*)(EXCEPTION_POINTERS*));
RUNTIME_FUNCTION* RtlLookupFunctionEntry(DWORD64,DWORD64*,void*);
void* RtlVirtualUnwind(DWORD,DWORD64,DWORD64,RUNTIME_FUNCTION*,CONTEXT*,void**,DWORD64*,void*);
int WideCharToMultiByte(unsigned,DWORD,const wchar_t*,int,char*,int,const char*,BOOL*);
