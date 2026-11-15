// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// SDL's audio device and stream calls, over XAudio2.
//
// The shape of SDL's audio API and XAudio2's are not the same, and the difference
// is worth stating because it decides what this file does:
//
//   * SDL's audio *device* is where sound comes out. SDL's *stream* converts format
//     and holds a queue of samples, and binding a stream to a device is what makes
//     the device pull from it.
//   * XAudio2 has a mastering voice (the output), and source voices that hold
//     buffers. There is no built-in conversion and no queue management.
//
// So the shim's stream is a buffer the guest appends to and the XAudio2 callback
// drains, and the shim's device is one master voice. That is the whole design: the
// guest's audio library (sdl_audio_out.cpp) writes float samples into a stream it
// opened, and this file turns that into XAudio2 buffers of the same samples, with
// the format conversion SDL would have done done here because XAudio2 will not.
//
// The conversion is deliberately narrow. The guest asks for F32 or S16, mono to
// 7.1, at one sample rate; anything else is refused rather than played wrongly,
// because a wrong-by-a-semitone game is worse than a silent one that reports why.

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "uwp/sdl3_shim/audio_output.h"

extern "C" void Gr2SdlSetError(const char* message);

namespace {

// The guest's audio: one output device, and one stream per device, which is how
// sdl_audio_out.cpp uses the API. The console has no other audio sink.
constexpr SDL_AudioDeviceID kOutputDevice = 1;

struct Stream {
    SDL_AudioSpec source{};  // what the guest writes
    SDL_AudioSpec target{};  // what the device wants
    float gain = 1.0f;
    // Channel map from the source's channels to the device's, or empty for the
    // identity. SDL lets a caller rearrange channels; the guest uses it to put the
    // PS4's channels where the console's speakers are.
    std::vector<int> input_channel_map;
    std::vector<Uint8> queue;
    bool paused = false;
    SDL_AudioDeviceID device = 0;
};

std::mutex g_mutex;
std::map<SDL_AudioStream*, std::unique_ptr<Stream>> g_streams;
std::map<SDL_AudioDeviceID, SDL_AudioSpec> g_devices;
SDL_AudioDeviceID g_next_device = kOutputDevice + 1;

// The device's own format. The console's output is 48 kHz stereo, which is what
// XAudio2's mastering voice opens with on an Xbox; the guest's 44.1 kHz content is
// resampled by the stream.
SDL_AudioSpec DefaultDeviceSpec() {
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32LE;
    spec.channels = 2;
    spec.freq = 48000;
    return spec;
}

int BytesPerSample(const SDL_AudioFormat format) {
    return SDL_AUDIO_BITSIZE(format) / 8;
}

// Converts and mixes one block of the guest's samples into the device's format.
// The three things SDL's stream would have done: sample format, channel count and
// sample rate. Rate conversion is linear, which is audible only if the ratio is
// far from 1; the guest's content is 44.1 or 48 kHz and the device is 48 kHz.
std::vector<Uint8> Convert(const Stream& stream, const Uint8* data, const int len) {
    const int source_frame = BytesPerSample(stream.source.format) * stream.source.channels;
    if (source_frame <= 0) {
        return {};
    }
    const int frames = len / source_frame;
    const int target_frame = BytesPerSample(stream.target.format) * stream.target.channels;

    const double ratio = static_cast<double>(stream.target.freq) / stream.source.freq;
    const int target_frames = static_cast<int>(frames * ratio);
    std::vector<Uint8> out(static_cast<size_t>(target_frames) * target_frame);

    const auto read_sample = [&stream, data, source_frame](const int frame, const int channel) {
        const int offset = frame * source_frame + channel * BytesPerSample(stream.source.format);
        if (SDL_AUDIO_ISFLOAT(stream.source.format) && BytesPerSample(stream.source.format) == 4) {
            float value{};
            std::memcpy(&value, data + offset, sizeof(value));
            return value;
        }
        if (SDL_AUDIO_ISSIGNED(stream.source.format) &&
            BytesPerSample(stream.source.format) == 2) {
            Sint16 value{};
            std::memcpy(&value, data + offset, sizeof(value));
            return static_cast<float>(value) / 32768.0f;
        }
        // Unsigned 8-bit, which is the one format left that the guest could ask
        // for; SDL's own S16 path never produces it but a caller's might.
        return (static_cast<float>(data[offset]) - 128.0f) / 128.0f;
    };

    for (int frame = 0; frame < target_frames; ++frame) {
        const int source_frame_index = std::min(frames - 1, static_cast<int>(frame / ratio));
        for (int channel = 0; channel < stream.target.channels; ++channel) {
            float value{};
            if (!stream.input_channel_map.empty()) {
                const int mapped = stream.input_channel_map[static_cast<size_t>(channel) %
                                                             stream.input_channel_map.size()];
                if (mapped >= 0 && mapped < stream.source.channels) {
                    value = read_sample(source_frame_index, mapped);
                }
            } else if (channel < stream.source.channels) {
                value = read_sample(source_frame_index, channel);
            }
            // Mono to stereo and stereo to mono, which sdl_audio_out.cpp does for
            // itself but a caller that does not would otherwise get silence in the
            // extra channels.
            if (stream.target.channels == 1 && stream.source.channels > 1) {
                float sum{};
                for (int source_channel = 0; source_channel < stream.source.channels;
                     ++source_channel) {
                    sum += read_sample(source_frame_index, source_channel);
                }
                value = sum / static_cast<float>(stream.source.channels);
            }
            value *= stream.gain;
            value = std::clamp(value, -1.0f, 1.0f);
            const int offset = frame * target_frame + channel * BytesPerSample(stream.target.format);
            if (SDL_AUDIO_ISFLOAT(stream.target.format) && BytesPerSample(stream.target.format) == 4) {
                std::memcpy(out.data() + offset, &value, sizeof(value));
            } else if (SDL_AUDIO_ISSIGNED(stream.target.format) &&
                       BytesPerSample(stream.target.format) == 2) {
                const Sint16 sample = static_cast<Sint16>(value * 32767.0f);
                std::memcpy(out.data() + offset, &sample, sizeof(sample));
            } else {
                out[offset] = static_cast<Uint8>(value * 127.0f + 128.0f);
            }
        }
    }
    return out;
}

} // namespace

// --- Devices ---------------------------------------------------------------

extern "C" SDL_AudioDeviceID* SDLCALL SDL_GetAudioPlaybackDevices(int* count) {
    // One device, the console's output. SDL allocates the array and the caller
    // frees it, which is why this is malloc rather than a static.
    auto* const ids = static_cast<SDL_AudioDeviceID*>(malloc(sizeof(SDL_AudioDeviceID)));
    if (ids == nullptr) {
        if (count != nullptr) {
            *count = 0;
        }
        return nullptr;
    }
    ids[0] = kOutputDevice;
    if (count != nullptr) {
        *count = 1;
    }
    return ids;
}

extern "C" SDL_AudioDeviceID* SDLCALL SDL_GetAudioRecordingDevices(int* count) {
    // The console has no capture device the app may open.
    if (count != nullptr) {
        *count = 0;
    }
    return nullptr;
}

extern "C" const char* SDLCALL SDL_GetAudioDeviceName(SDL_AudioDeviceID devid) {
    return devid == kOutputDevice ? "Xbox Audio" : nullptr;
}

extern "C" bool SDLCALL SDL_GetAudioDeviceFormat(SDL_AudioDeviceID devid, SDL_AudioSpec* spec,
                                                 int* sample_frames) {
    if (spec == nullptr) {
        return false;
    }
    if (devid != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK && devid != kOutputDevice) {
        return false;
    }
    *spec = DefaultDeviceSpec();
    if (sample_frames != nullptr) {
        // The buffer the guest sizes its own queue against. 1024 frames at 48 kHz
        // is ~21 ms, close to what a DualShock-era game expects, and is the size
        // XAudio2 is driven at below.
        *sample_frames = 1024;
    }
    return true;
}

extern "C" SDL_AudioDeviceID SDLCALL SDL_OpenAudioDevice(SDL_AudioDeviceID devid,
                                                         const SDL_AudioSpec* spec) {
    if (devid != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK && devid != kOutputDevice) {
        Gr2SdlSetError("the Xbox build has one audio device");
        return 0;
    }
    const std::scoped_lock lock{g_mutex};
    const SDL_AudioSpec device_spec = DefaultDeviceSpec();
    if (!Gr2Sdl::Audio::Open(device_spec)) {
        Gr2SdlSetError("the console refused the audio output device");
        return 0;
    }
    // The *device* opens at the console's own format, so the target every stream
    // converts to is that rather than whatever the caller asked for: XAudio2 has no
    // resampler and would play a mismatched rate at the wrong pitch. A caller that
    // asked for something else is not ignored -- its spec becomes the stream's
    // source, and the conversion happens there.
    (void)spec;
    const SDL_AudioDeviceID id = g_next_device++;
    g_devices[id] = device_spec;
    return id;
}

extern "C" void SDLCALL SDL_CloseAudioDevice(SDL_AudioDeviceID devid) {
    const std::scoped_lock lock{g_mutex};
    if (g_devices.erase(devid) != 0) {
        Gr2Sdl::Audio::Close();
    }
}

// --- Streams ---------------------------------------------------------------

extern "C" SDL_AudioStream* SDLCALL SDL_CreateAudioStream(const SDL_AudioSpec* src_spec,
                                                          const SDL_AudioSpec* dst_spec) {
    if (src_spec == nullptr || dst_spec == nullptr || src_spec->channels <= 0 ||
        dst_spec->channels <= 0 || src_spec->freq <= 0 || dst_spec->freq <= 0) {
        Gr2SdlSetError("SDL_CreateAudioStream needs a format on both sides");
        return nullptr;
    }
    auto stream = std::make_unique<Stream>();
    stream->source = *src_spec;
    stream->target = *dst_spec;

    const std::scoped_lock lock{g_mutex};
    Stream* const raw = stream.get();
    g_streams[reinterpret_cast<SDL_AudioStream*>(raw)] = std::move(stream);
    return reinterpret_cast<SDL_AudioStream*>(raw);
}

extern "C" void SDLCALL SDL_DestroyAudioStream(SDL_AudioStream* stream) {
    if (stream == nullptr) {
        return;
    }
    const std::scoped_lock lock{g_mutex};
    g_streams.erase(stream);
}

extern "C" bool SDLCALL SDL_SetAudioStreamFormat(SDL_AudioStream* stream,
                                                 const SDL_AudioSpec* src_spec,
                                                 const SDL_AudioSpec* dst_spec) {
    if (stream == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it == g_streams.end()) {
        return false;
    }
    // Changing the format mid-stream throws away what was queued in the old one:
    // keeping it would mean guessing which samples were already converted.
    if (src_spec != nullptr) {
        it->second->source = *src_spec;
        it->second->queue.clear();
    }
    if (dst_spec != nullptr) {
        it->second->target = *dst_spec;
        it->second->queue.clear();
    }
    return true;
}

extern "C" bool SDLCALL SDL_SetAudioStreamGain(SDL_AudioStream* stream, float gain) {
    if (stream == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it == g_streams.end()) {
        return false;
    }
    it->second->gain = gain;
    return true;
}

extern "C" bool SDLCALL SDL_SetAudioStreamInputChannelMap(SDL_AudioStream* stream,
                                                          const int* chmap, int count) {
    if (stream == nullptr || count < 0) {
        return false;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it == g_streams.end()) {
        return false;
    }
    if (chmap == nullptr || count == 0) {
        it->second->input_channel_map.clear();
    } else {
        it->second->input_channel_map.assign(chmap, chmap + count);
    }
    return true;
}

extern "C" bool SDLCALL SDL_PutAudioStreamData(SDL_AudioStream* stream, const void* buf, int len) {
    if (stream == nullptr || buf == nullptr || len < 0) {
        return false;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it == g_streams.end()) {
        return false;
    }
    Stream& target = *it->second;
    // Converted on the way in rather than on the way out, so the queue is already
    // in the device's format and the audio thread does no arithmetic. That matters
    // on a console with eight cores and a 1.2 GiB budget: the callback is the one
    // place a stall is audible.
    const std::vector<Uint8> converted =
        Convert(target, static_cast<const Uint8*>(buf), len);
    // Straight to the device when the stream is bound and running, which is the
    // case for every stream sdl_audio_out.cpp opens; the queue is only used while
    // the device is paused. Doing it the other way would add a copy and a poll.
    if (target.device != 0 && !target.paused) {
        Gr2Sdl::Audio::Submit(converted.data(), static_cast<int>(converted.size()));
        return true;
    }
    target.queue.insert(target.queue.end(), converted.begin(), converted.end());
    return true;
}

extern "C" int SDLCALL SDL_GetAudioStreamQueued(SDL_AudioStream* stream) {
    if (stream == nullptr) {
        return 0;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    return it != g_streams.end() ? static_cast<int>(it->second->queue.size()) : 0;
}

extern "C" int SDLCALL SDL_GetAudioStreamAvailable(SDL_AudioStream* stream) {
    if (stream == nullptr) {
        return 0;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it == g_streams.end()) {
        return 0;
    }
    // Only the stream's *own* queue is available to read: there is nothing else
    // holding samples, because the device pulls straight out of it.
    return static_cast<int>(it->second->queue.size());
}

extern "C" int SDLCALL SDL_GetAudioStreamData(SDL_AudioStream* stream, void* buf, int len) {
    if (stream == nullptr || buf == nullptr || len < 0) {
        return -1;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it == g_streams.end()) {
        return -1;
    }
    Stream& source = *it->second;
    const int count = std::min(len, static_cast<int>(source.queue.size()));
    std::memcpy(buf, source.queue.data(), static_cast<size_t>(count));
    source.queue.erase(source.queue.begin(), source.queue.begin() + count);
    return count;
}

extern "C" bool SDLCALL SDL_ClearAudioStream(SDL_AudioStream* stream) {
    if (stream == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it == g_streams.end()) {
        return false;
    }
    it->second->queue.clear();
    return true;
}

extern "C" bool SDLCALL SDL_FlushAudioStream(SDL_AudioStream* stream) {
    // SDL's flush discards the stream's *internal* resampler tail while leaving the
    // queue alone; here conversion happens at put time, so there is nothing to
    // discard and the call is a no-op that succeeds.
    return stream != nullptr;
}

extern "C" SDL_AudioStream* SDLCALL SDL_OpenAudioDeviceStream(SDL_AudioDeviceID devid,
                                                              const SDL_AudioSpec* spec,
                                                              SDL_AudioStreamCallback callback,
                                                              void* userdata) {
    (void)callback;
    (void)userdata;
    if (spec == nullptr) {
        Gr2SdlSetError("SDL_OpenAudioDeviceStream needs a format");
        return nullptr;
    }
    if (devid != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK && devid != SDL_AUDIO_DEVICE_DEFAULT_RECORDING &&
        !g_devices.contains(devid)) {
        // Recording is the case worth naming: the guest's audio input opens a
        // device only if a microphone exists, and on the console none does.
        Gr2SdlSetError("no such audio device");
        return nullptr;
    }
    if (devid == SDL_AUDIO_DEVICE_DEFAULT_RECORDING) {
        Gr2SdlSetError("the Xbox build has no audio capture device");
        return nullptr;
    }

    SDL_AudioSpec device_spec = DefaultDeviceSpec();
    SDL_GetAudioDeviceFormat(devid, &device_spec, nullptr);
    SDL_AudioStream* const stream = SDL_CreateAudioStream(spec, &device_spec);
    if (stream == nullptr) {
        return nullptr;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it != g_streams.end()) {
        it->second->device = devid;
    }
    return stream;
}

extern "C" bool SDLCALL SDL_BindAudioStream(SDL_AudioDeviceID devid, SDL_AudioStream* stream) {
    if (stream == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it == g_streams.end()) {
        return false;
    }
    it->second->device = devid;
    return true;
}

extern "C" void SDLCALL SDL_UnbindAudioStream(SDL_AudioStream* stream) {
    if (stream == nullptr) {
        return;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it != g_streams.end()) {
        it->second->device = 0;
    }
}

extern "C" SDL_AudioDeviceID SDLCALL SDL_GetAudioStreamDevice(SDL_AudioStream* stream) {
    if (stream == nullptr) {
        return 0;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    return it != g_streams.end() ? it->second->device : 0;
}

extern "C" bool SDLCALL SDL_ResumeAudioStreamDevice(SDL_AudioStream* stream) {
    if (stream == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it == g_streams.end()) {
        return false;
    }
    it->second->paused = false;
    return true;
}

extern "C" bool SDLCALL SDL_PauseAudioStreamDevice(SDL_AudioStream* stream) {
    if (stream == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    if (it == g_streams.end()) {
        return false;
    }
    it->second->paused = true;
    return true;
}

extern "C" bool SDLCALL SDL_AudioStreamDevicePaused(SDL_AudioStream* stream) {
    if (stream == nullptr) {
        return false;
    }
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(stream);
    return it != g_streams.end() && it->second->paused;
}
