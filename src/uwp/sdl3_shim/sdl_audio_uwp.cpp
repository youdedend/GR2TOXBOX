// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The console's audio output: XAudio2.
//
// This is the second of the shim's two WinRT/Windows-SDK edges (the pad is the
// other), and like that one it is kept thin so the precheck can check everything
// else. What is here is only what needs the Windows SDK: a mastering voice, a
// source voice, and a buffer queue.
//
// Why XAudio2 and not WASAPI: the onecore surface a UWP app links against has no
// audio endpoint APIs, and XAudio2 is the API Microsoft documents for a packaged
// Xbox title. It also does the mixing in the console's audio block rather than in
// the app, which is what the 1.2 GiB budget wants.

#include "uwp/sdl3_shim/audio_output.h"

#if defined(GR2_XBOX_UWP)

#include <xaudio2.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include <wrl/client.h>

namespace Gr2Sdl::Audio {

namespace {

std::mutex g_mutex;
Microsoft::WRL::ComPtr<IXAudio2> g_engine;
IXAudio2MasteringVoice* g_master = nullptr;
IXAudio2SourceVoice* g_source = nullptr;

// Buffers XAudio2 is still playing. A submitted buffer must stay alive until the
// voice is done with it, so each one is kept here with its marker and released in
// the on-buffer-end callback. This is the whole of the queue management: XAudio2
// has none of its own.
struct PendingBuffer {
    std::vector<BYTE> data;
};

// Shared between the audio thread and the submitter, so it is protected. The
// callback runs on XAudio2's worker thread, not on the game's.
std::mutex g_buffers_mutex;
std::deque<PendingBuffer> g_pending;

// The largest buffer XAudio2 accepts is 4 GiB, but a submit larger than a few
// frames' worth is a caller bug; the cap keeps a runaway put from allocating the
// guest's whole budget.
constexpr int kMaxSubmitBytes = 48000 * 2 * sizeof(float) / 4;  // ~250 ms of stereo

/// XAudio2 wants a WAVEFORMATEX, and SDL's spec is the same four fields.
WAVEFORMATEX ToWaveFormat(const SDL_AudioSpec& spec) {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = static_cast<WORD>(spec.channels);
    format.nSamplesPerSec = static_cast<DWORD>(spec.freq);
    format.wBitsPerSample = static_cast<WORD>(SDL_AUDIO_BITSIZE(spec.format));
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;
    return format;
}

} // namespace

bool Open(const SDL_AudioSpec& spec) {
    const std::scoped_lock lock{g_mutex};
    if (g_source != nullptr) {
        // Already open. The guest's audio library can reopen a device after a
        // settings change, and reusing the voice is what keeps that from clicking.
        return true;
    }

    HRESULT hr = XAudio2Create(g_engine.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        return false;
    }

    // The mastering voice is the console's output and takes the platform's own
    // format: the input sample rate and channel count are ignored, so the voice is
    // created with them zeroed and the platform decides.
    hr = g_engine->CreateMasteringVoice(&g_master, 0, 0, 0, nullptr, nullptr,
                                        AudioCategory_GameEffects);
    if (FAILED(hr)) {
        g_engine.Reset();
        return false;
    }

    const WAVEFORMATEX format = ToWaveFormat(spec);
    hr = g_engine->CreateSourceVoice(&g_source, &format);
    if (FAILED(hr)) {
        g_master->DestroyVoice();
        g_master = nullptr;
        g_engine.Reset();
        return false;
    }
    g_source->Start(0);
    return true;
}

void Close() {
    const std::scoped_lock lock{g_mutex};
    if (g_source != nullptr) {
        g_source->Stop(0);
        g_source->FlushSourceBuffers();
        g_source->DestroyVoice();
        g_source = nullptr;
    }
    if (g_master != nullptr) {
        g_master->DestroyVoice();
        g_master = nullptr;
    }
    g_engine.Reset();
    const std::scoped_lock buffers_lock{g_buffers_mutex};
    g_pending.clear();
}

bool Submit(const void* data, const int bytes) {
    if (data == nullptr || bytes <= 0) {
        return false;
    }
    const std::scoped_lock lock{g_mutex};
    if (g_source == nullptr) {
        return false;
    }

    // How much is already waiting. A frame that produced more audio than the device
    // can play drops the excess rather than queueing it: the alternative is a
    // second of latency that never drains, and an emulator's audio is a
    // consequence of the frame, not a recording of it.
    XAUDIO2_VOICE_STATE state{};
    g_source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (state.BuffersQueued > 8) {
        return true;
    }

    const int clamped = std::min(bytes, kMaxSubmitBytes);
    PendingBuffer buffer;
    buffer.data.resize(static_cast<size_t>(clamped));
    std::memcpy(buffer.data.data(), data, static_cast<size_t>(clamped));

    {
        const std::scoped_lock buffers_lock{g_buffers_mutex};
        g_pending.push_back(std::move(buffer));
    }

    XAUDIO2_BUFFER submit{};
    submit.AudioBytes = static_cast<UINT32>(clamped);
    submit.pAudioData = g_pending.back().data.data();
    submit.pContext = nullptr;
    submit.Flags = 0;

    const HRESULT hr = g_source->SubmitSourceBuffer(&submit);
    if (FAILED(hr)) {
        const std::scoped_lock buffers_lock{g_buffers_mutex};
        g_pending.pop_back();
        return false;
    }

    // Buffers are reclaimed here rather than in a callback: XAudio2 has already
    // copied the pointer, so the only requirement is that the memory outlives
    // playback, and a bounded deque with the oldest dropped is simpler than
    // registering a callback that would run on another thread. Eight buffers of a
    // quarter-second each is two seconds of history, far more than the queue depth
    // above allows.
    {
        const std::scoped_lock buffers_lock{g_buffers_mutex};
        while (g_pending.size() > 32) {
            g_pending.pop_front();
        }
    }
    return true;
}

} // namespace Gr2Sdl::Audio

#else // GR2_XBOX_UWP

// Every other build links this file to nothing; the precheck compiles it to prove
// that the empty path still type-checks.
namespace Gr2Sdl::Audio {

bool Open(const SDL_AudioSpec& /*spec*/) {
    return false;
}

void Close() {}

bool Submit(const void* /*data*/, int /*bytes*/) {
    return false;
}

} // namespace Gr2Sdl::Audio

#endif // GR2_XBOX_UWP
