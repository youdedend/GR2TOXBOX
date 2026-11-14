// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/// AppContainer-safe replacements for the desktop Win32 memory and file APIs the
/// emulator's address space uses.
///
/// A UWP process runs in an AppContainer. Several of the APIs upstream's
/// `AddressSpace::Impl` uses are not callable from one — not because they are
/// missing from the import library, but because the container's token is not
/// granted the access they require. The platform provides `*FromApp` variants
/// that work within the container and are what the package must call:
///
/// | Desktop call | AppContainer-safe call |
/// | --- | --- |
/// | `VirtualAlloc2` | `VirtualAlloc2FromApp` |
/// | `MapViewOfFile3` | `MapViewOfFile3FromApp` |
/// | `CreateFileMapping2` | `CreateFileMappingFromApp` |
/// | `VirtualProtect` | `VirtualProtectFromApp` |
///
/// Rather than sprinkle `#ifdef GR2_XBOX_UWP` through the address-space code,
/// this header exposes one set of names that resolve to the right call for the
/// build. The signatures are narrowed where the `FromApp` variant cannot express
/// something (notably `CreateFileMapping2`'s placeholder/parameter list), and a
/// comment records what is lost.
///
/// The desktop build gets thin inlines that forward to the desktop APIs, so the
/// address-space code compiles unchanged on both and the desktop CI job
/// exercises the same source.

#include "common/types.h"

#ifdef _WIN32
#include <windows.h>
#include <memoryapi.h>

namespace Core::WinCompat {

/// Reserves and/or commits pages at a requested address.
///
/// The `FromApp` variant takes no extended parameters. That is fine for this
/// emulator: the address-space code never passes any, and the one feature it
/// would want (large pages) is not available to an AppContainer anyway.
inline void* VirtualAlloc2Compat(void* process, void* base, SIZE_T size, ULONG type,
                                 ULONG protect, void* extended_parameters,
                                 ULONG parameter_count) {
#if defined(GR2_XBOX_UWP)
    (void)process; // A UWP process can only allocate in itself.
    (void)extended_parameters;
    (void)parameter_count;
    return VirtualAlloc2FromApp(base, size, type, protect);
#else
    return VirtualAlloc2(process, base, size, type, protect,
                         static_cast<MEM_EXTENDED_PARAMETER*>(extended_parameters),
                         parameter_count);
#endif
}

/// Maps a view of a section (or a placeholder) at a requested address.
inline void* MapViewOfFile3Compat(HANDLE mapping, void* process, void* base, ULONG64 offset,
                                  SIZE_T view_size, ULONG allocation_type, ULONG page_protection,
                                  void* extended_parameters, ULONG parameter_count) {
#if defined(GR2_XBOX_UWP)
    (void)process;
    (void)base;
    (void)extended_parameters;
    (void)parameter_count;
    // MapViewOfFile3FromApp cannot place a view at a caller-chosen address, so
    // the caller's `base` is a hint and the returned address is authoritative.
    // AddressSpace::Impl treats the return value as the mapping's address, which
    // is why its MapRegion() asserts against it rather than assuming success.
    return MapViewOfFile3FromApp(mapping, process, base, offset, view_size, allocation_type,
                                 page_protection);
#else
    return MapViewOfFile3(mapping, process, base, offset, view_size, allocation_type,
                          page_protection, static_cast<MEM_EXTENDED_PARAMETER*>(extended_parameters),
                          parameter_count);
#endif
}

/// Creates a page-file-backed section of `size` bytes.
///
/// `CreateFileMapping2` (with its SEC_COMMIT flag and protection argument) has no
/// AppContainer equivalent; `CreateFileMappingFromApp` always creates a
/// read-write section, which is what this emulator needs (the guest's backing
/// file is never executable — see Core::Wx).
inline HANDLE CreateFileMapping2Compat(HANDLE file, void* attributes, ULONG desired_access,
                                       ULONG page_protection, ULONG section_flags,
                                       ULONG64 maximum_size) {
#if defined(GR2_XBOX_UWP)
    (void)attributes;
    (void)desired_access;
    (void)page_protection;
    (void)section_flags;
    return CreateFileMappingFromApp(file, nullptr, PAGE_READWRITE, maximum_size, nullptr);
#else
    return CreateFileMapping2(file, static_cast<SECURITY_ATTRIBUTES*>(attributes), desired_access,
                              page_protection, section_flags, maximum_size, nullptr, nullptr, 0);
#endif
}

/// Changes a mapping's protection.
inline bool VirtualProtectCompat(void* address, SIZE_T size, ULONG new_protect,
                                 ULONG* old_protect) {
#if defined(GR2_XBOX_UWP)
    // VirtualProtectFromApp takes a DWORD* for the previous protection; the two
    // types are the same width and the value is a PAGE_* constant in both.
    return VirtualProtectFromApp(address, size, new_protect,
                                 reinterpret_cast<DWORD*>(old_protect)) != FALSE;
#else
    return VirtualProtect(address, size, new_protect, reinterpret_cast<DWORD*>(old_protect)) !=
           FALSE;
#endif
}

/// Releases a reserved/committed range.
inline bool VirtualFreeCompat(void* process, void* address, SIZE_T size, ULONG free_type) {
#if defined(GR2_XBOX_UWP)
    (void)process;
    // VirtualFreeFromApp exists but is not in every UWP SDK; VirtualFree is
    // permitted inside an AppContainer for memory the process itself allocated,
    // which is the only case here.
    return VirtualFree(address, size, free_type) != FALSE;
#else
    return VirtualFreeEx(process, address, size, free_type) != FALSE;
#endif
}

} // namespace Core::WinCompat

#endif // _WIN32
