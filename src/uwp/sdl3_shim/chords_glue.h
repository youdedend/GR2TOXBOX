// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "uwp/sdl3_shim/pad_state.h"

/// The chord layer's link to the pad.
///
/// src/uwp/chords.h is pure logic with no platform dependency, so it can be tested
/// on a desktop; this is the one call that connects it to the shim's pad reading
/// and emits the SDL events the guest watches for.
namespace Gr2Sdl::Chords {

/// Runs one frame of chord processing against the pad's current state and pushes
/// the resulting synthetic buttons and tilt. Called by SDL_UpdateGamepads.
void Attach(const Gr2Sdl::Pad::State& state);

/// Releases anything the chord layer told the guest was held. Called when the pad
/// disconnects, so a chord cannot leave a key stuck down.
void Detach();

} // namespace Gr2Sdl::Chords
