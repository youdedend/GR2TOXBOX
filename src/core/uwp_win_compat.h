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
/// build. Only `CreateFileMapping2Compat` narrows its caller's signature, because
/// `CreateFileMappingFromApp` genuinely cannot express `CreateFileMapping2`'s
/// section flags; every other wrapper keeps the desktop parameter list, and says
/// so where it does.
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
/// The `FromApp` variants take the same parameter lists as their desktop
/// counterparts -- they are the desktop signatures with `FromApp` appended, not
/// narrowed forms of them. What differs is the access the container's token is
/// granted, not the call's shape.
inline void* VirtualAlloc2Compat(void* process, void* base, SIZE_T size, ULONG type,
                                 ULONG protect, void* extended_parameters,
                                 ULONG parameter_count) {
#if defined(GR2_XBOX_UWP)
    // The AppContainer variant takes the same seven parameters as VirtualAlloc2,
    // including the process handle and the extended-parameter list -- it is the
    // desktop signature with FromApp on the end, not a narrowed one. An earlier
    // version of this file assumed otherwise and passed four arguments, which is
    // why the packaged build reported "no matching function for call to
    // VirtualAlloc2FromApp".
    //
    // `process` is still ignored in effect: a UWP process may only name itself,
    // so the caller's own handle is the only value that can succeed.
    return VirtualAlloc2FromApp(static_cast<HANDLE>(process),
                                base, size, type, protect,
                                static_cast<MEM_EXTENDED_PARAMETER*>(extended_parameters),
                                parameter_count);
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
    // Nine parameters, the same nine as MapViewOfFile3: the extended-parameter
    // list is part of the AppContainer variant's signature, and an earlier version
    // of this file passed seven, which the packaged build rejected with "no
    // matching function for call to MapViewOfFile3FromApp". `base` is honoured as
    // the desktop call honours it -- a desired address, rounded down to 64k --
    // rather than being ignored, so the caller's placement still works and the
    // returned address remains authoritative either way.
    return MapViewOfFile3FromApp(mapping, static_cast<HANDLE>(process), base, offset, view_size,
                                 allocation_type, page_protection,
                                 static_cast<MEM_EXTENDED_PARAMETER*>(extended_parameters),
                                 parameter_count);
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

/// Queries the pages of a range.
///
/// `VirtualQueryEx` is the desktop call and takes a process handle; an
/// AppContainer may query only itself, and the onecore surface offers no Ex form.
/// The emulator never queries another process -- the handle it passes is its own
/// -- so `VirtualQuery` is the same answer with the handle dropped, and it also
/// avoids the `LPCVOID` -> `LPVOID` mismatch that `VirtualQueryEx`'s prototype
/// produces for a const-cast address.
inline SIZE_T VirtualQueryCompat(void* process, const void* address,
                                 MEMORY_BASIC_INFORMATION* info, SIZE_T length) {
    (void)process; // Only ever this process; see above.
#if defined(GR2_XBOX_UWP)
    // The onecore surface's query takes no process handle and a writable address.
    return VirtualQuery(const_cast<void*>(address), info, length);
#else
    return VirtualQueryEx(static_cast<HANDLE>(process), address, info, length);
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
