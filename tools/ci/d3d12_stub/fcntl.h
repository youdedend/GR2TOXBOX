#pragma once
#include <cstdio>
inline int _wfopen_s(std::FILE** f, const wchar_t* name, const wchar_t* mode) { *f = nullptr; return 1; }
