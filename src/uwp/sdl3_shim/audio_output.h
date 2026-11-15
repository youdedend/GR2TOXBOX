// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <SDL3/SDL_audio.h>

/// The console's audio output, as the shim needs it.
///
/// Same split as the pad: XAudio2 is a Windows SDK header, so the code that owns a
/// mastering voice lives in sdl_audio_uwp.cpp and everything that decides *what* to
/// play -- format conversion, the stream queue, SDL's own API shape -- lives in
/// sdl_audio.cpp, which the precheck compiles against the real SDL headers.
///
/// The contract is small on purpose. The device is opened once at the console's
/// native format, the guest's samples are converted to that format before they are
/// handed over, and the submit call copies them into XAudio2 buffers.
namespace Gr2Sdl::Audio {

/// Opens the mastering voice. Returns false when the console refused it, which the
/// caller reports through SDL_GetError rather than treating as fatal: a game that
/// runs silently is still playable, and sdl_audio_out.cpp already logs and carries
/// on when a device fails to open.
bool Open(const SDL_AudioSpec& spec);

/// Releases the mastering voice and everything still queued against it.
void Close();

/// Plays one block of samples, already in the format the device was opened with.
///
/// `bytes` is a multiple of the frame size. Returns false only when the voice is
/// gone; a full queue is not a failure, because dropping the newest block is what
/// keeps audio in step with a frame that took slightly too long.
bool Submit(const void* data, int bytes);

} // namespace Gr2Sdl::Audio
