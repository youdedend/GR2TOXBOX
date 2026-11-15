// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <SDL3/SDL_events.h>

/// Calls the shim's own entry points from one shim file in another.
///
/// SDL's public headers are the interface between the emulator and the shim; this
/// is the interface *inside* the shim, for the few things that do not belong to any
/// one SDL module. It is not part of SDL and no emulator file includes it, which is
/// what keeps the shim's internals from leaking into the include path the rest of
/// the project sees.
extern "C" {

/// Runs every registered SDL_AddEventWatch filter against an event.
///
/// Called by SDL_PushEvent, because on this platform the shim *is* the event
/// source: the desktop's SDL calls its watches from the platform's event pump, and
/// there is no such pump here.
void Gr2SdlRunEventWatches(SDL_Event* event);

} // extern "C"
