// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// SDL's memory streams and WAV loader, for the Xbox UWP build.
//
// The only consumer in the UWP build is SDL_LoadWAV_IO, which reads a sound effect
// out of memory to make a trophy or notification noise. That is the reason this is
// a real implementation rather than a stub: the file it reads is a WAV the guest
// supplies, and a WAV parser is small, exact, and testable -- whereas a lie here
// would be a notification that never plays and nobody can diagnose.
//
// Memory streams only. There is no file stream, because the packaged app's file
// access goes through the guest's own file system emulation; nothing in the UWP
// build asks SDL to open a path.

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace {

struct MemoryStream {
    // A copy of the caller's bytes, because SDL's close of a memory stream does not
    // free the caller's buffer but the stream must outlive the caller's stack
    // frame in the one place this is used.
    std::vector<Uint8> bytes;
    size_t position = 0;
};

std::mutex g_mutex;
std::map<SDL_IOStream*, std::unique_ptr<MemoryStream>> g_streams;

MemoryStream* Lookup(SDL_IOStream* context) {
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(context);
    return it != g_streams.end() ? it->second.get() : nullptr;
}

// A WAV header is 44 bytes in its canonical form, but the format chunk can be
// longer and chunks can appear in either order, so this walks the chunks the way a
// real parser has to. Returns the offset and length of the sample data.
struct WavLayout {
    Uint16 format = 0;
    Uint16 channels = 0;
    Uint32 sample_rate = 0;
    Uint16 bits_per_sample = 0;
    size_t data_offset = 0;
    size_t data_length = 0;
};

Uint32 ReadLE32(const Uint8* p) {
    return static_cast<Uint32>(p[0]) | (static_cast<Uint32>(p[1]) << 8) |
           (static_cast<Uint32>(p[2]) << 16) | (static_cast<Uint32>(p[3]) << 24);
}

Uint16 ReadLE16(const Uint8* p) {
    return static_cast<Uint16>(static_cast<Uint16>(p[0]) | (static_cast<Uint16>(p[1]) << 8));
}

bool ParseWav(const std::vector<Uint8>& bytes, WavLayout& out) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return false;
    }
    size_t offset = 12;
    bool have_format = false;
    while (offset + 8 <= bytes.size()) {
        const Uint8* const chunk = bytes.data() + offset;
        const Uint32 length = ReadLE32(chunk + 4);
        const size_t body = offset + 8;
        if (body + length > bytes.size()) {
            // A truncated last chunk: take what is there rather than refuse the
            // whole file, which is what a streaming loader does.
            if (!have_format) {
                return false;
            }
            out.data_length = bytes.size() - body;
            return true;
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0 && length >= 16) {
            out.format = ReadLE16(bytes.data() + body);
            out.channels = ReadLE16(bytes.data() + body + 2);
            out.sample_rate = ReadLE32(bytes.data() + body + 4);
            out.bits_per_sample = ReadLE16(bytes.data() + body + 14);
            have_format = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            out.data_offset = body;
            out.data_length = length;
            return have_format;
        }
        // Chunks are word-aligned, so an odd length is padded.
        offset = body + length + (length & 1);
    }
    return false;
}

} // namespace

// --- Memory streams --------------------------------------------------------

extern "C" SDL_IOStream* SDLCALL SDL_IOFromConstMem(const void* mem, size_t size) {
    if (mem == nullptr || size == 0) {
        return nullptr;
    }
    auto stream = std::make_unique<MemoryStream>();
    const auto* const bytes = static_cast<const Uint8*>(mem);
    stream->bytes.assign(bytes, bytes + size);

    const std::scoped_lock lock{g_mutex};
    SDL_IOStream* const handle = reinterpret_cast<SDL_IOStream*>(stream.get());
    g_streams[handle] = std::move(stream);
    return handle;
}

extern "C" Sint64 SDLCALL SDL_GetIOSize(SDL_IOStream* context) {
    const MemoryStream* const stream = Lookup(context);
    return stream != nullptr ? static_cast<Sint64>(stream->bytes.size()) : -1;
}

extern "C" Sint64 SDLCALL SDL_TellIO(SDL_IOStream* context) {
    const MemoryStream* const stream = Lookup(context);
    return stream != nullptr ? static_cast<Sint64>(stream->position) : -1;
}

extern "C" Sint64 SDLCALL SDL_SeekIO(SDL_IOStream* context, Sint64 offset, SDL_IOWhence whence) {
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(context);
    if (it == g_streams.end()) {
        return -1;
    }
    MemoryStream& stream = *it->second;
    Sint64 base = 0;
    switch (whence) {
    case SDL_IO_SEEK_SET:
        base = 0;
        break;
    case SDL_IO_SEEK_CUR:
        base = static_cast<Sint64>(stream.position);
        break;
    case SDL_IO_SEEK_END:
        base = static_cast<Sint64>(stream.bytes.size());
        break;
    default:
        return -1;
    }
    const Sint64 target = base + offset;
    if (target < 0) {
        return -1;
    }
    stream.position = static_cast<size_t>(target);
    return target;
}

extern "C" size_t SDLCALL SDL_ReadIO(SDL_IOStream* context, void* ptr, size_t size) {
    const std::scoped_lock lock{g_mutex};
    const auto it = g_streams.find(context);
    if (it == g_streams.end() || ptr == nullptr) {
        return 0;
    }
    MemoryStream& stream = *it->second;
    if (stream.position >= stream.bytes.size()) {
        return 0;
    }
    const size_t count = std::min(size, stream.bytes.size() - stream.position);
    std::memcpy(ptr, stream.bytes.data() + stream.position, count);
    stream.position += count;
    return count;
}

extern "C" bool SDLCALL SDL_CloseIO(SDL_IOStream* context) {
    const std::scoped_lock lock{g_mutex};
    return g_streams.erase(context) != 0;
}

// --- WAV -------------------------------------------------------------------

extern "C" bool SDLCALL SDL_LoadWAV_IO(SDL_IOStream* src, bool closeio, SDL_AudioSpec* spec,
                                       Uint8** audio_buf, Uint32* audio_len) {
    if (spec == nullptr || audio_buf == nullptr || audio_len == nullptr) {
        return false;
    }
    *audio_buf = nullptr;
    *audio_len = 0;

    WavLayout layout;
    bool ok = false;
    {
        const std::scoped_lock lock{g_mutex};
        const auto it = g_streams.find(src);
        if (it != g_streams.end()) {
            ok = ParseWav(it->second->bytes, layout);
            if (ok) {
                // SDL hands the caller the samples and expects them freed with
                // SDL_free, so this is an owned copy rather than a view into the
                // stream -- the caller may close it before it plays.
                auto* const buffer = static_cast<Uint8*>(malloc(layout.data_length));
                if (buffer == nullptr) {
                    ok = false;
                } else {
                    std::memcpy(buffer, it->second->bytes.data() + layout.data_offset,
                                layout.data_length);
                    *audio_buf = buffer;
                    *audio_len = static_cast<Uint32>(layout.data_length);
                }
            }
        }
    }
    if (closeio) {
        SDL_CloseIO(src);
    }
    if (!ok) {
        return false;
    }

    // The WAV format code to SDL's. The guest's notification sounds are 16-bit
    // PCM; float and 8-bit PCM are what else a WAV can plausibly be.
    spec->channels = static_cast<int>(layout.channels);
    spec->freq = static_cast<int>(layout.sample_rate);
    switch (layout.format) {
    case 1: // PCM
        // 24-bit has no SDL format of its own; SDL's own loader widens it to
        // S32LE and the caller's stream converts from there, which is what this
        // does too rather than pretending the bytes are something they are not.
        spec->format = layout.bits_per_sample == 8   ? SDL_AUDIO_U8
                       : layout.bits_per_sample == 16 ? SDL_AUDIO_S16LE
                                                      : SDL_AUDIO_S32LE;
        break;
    case 3: // IEEE float
        if (layout.bits_per_sample != 32) {
            // SDL has no 64-bit float format, so a double-precision WAV is
            // refused rather than misreported.
            *audio_buf = nullptr;
            *audio_len = 0;
            return false;
        }
        spec->format = SDL_AUDIO_F32LE;
        break;
    default:
        *audio_buf = nullptr;
        *audio_len = 0;
        return false;
    }
    return true;
}

// SDL_free needs no definition: SDL_stdinc.h defines it as the C library's free,
// and every allocation in this shim is a C library allocation, so the macro is
// already correct. Writing one out would redeclare free() rather than add SDL's.
