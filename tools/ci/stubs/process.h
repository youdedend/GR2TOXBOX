// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// <process.h>, the MSVC C runtime's header for process and thread primitives.
//
// Only here because a header this host does compile against takes its _WIN32
// branch and includes it: externals/sdl3/include/SDL3/SDL_thread.h wants _beginthreadex
// and friends when _WIN32 is defined. Declaring those is not worth doing here --
// nothing in the checked translation units calls them, and a wrong declaration
// would be worse than a missing one -- so the include is an empty file. The real
// Windows toolchain supplies the real one.
//
// Kept as a file rather than an entry in a shared stub because it is a C runtime
// header, not a Windows API one, and mixing the two would make it harder to see
// which layer a missing name belongs to.
