// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"
HANDLE CreateFile2FromAppW(const wchar_t*,DWORD,DWORD,DWORD,CREATEFILE2_EXTENDED_PARAMETERS*);
BOOL DeleteFileFromAppW(const wchar_t*);
BOOL MoveFileFromAppW(const wchar_t*,const wchar_t*);
BOOL CreateDirectoryFromAppW(const wchar_t*,void*);
BOOL GetFileAttributesExFromAppW(const wchar_t*,GET_FILEEX_INFO_LEVELS,void*);
