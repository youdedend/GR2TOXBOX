// SPDX-License-Identifier: GPL-2.0-or-later
// Shared CPU capture implementation; included inside the backend namespace.
u32 AddressHome(VAddr address) {
    return static_cast<u32>(address >> 8) * 0x9e3779b1u >> (32 - std::countr_zero(AddressSlots));
}

// Under g_mutex.
void AddAddress(u32 handle, VAddr address) {
    g_textures[handle].address.store(address, std::memory_order_release);
    const u32 home = AddressHome(address);
    for (u32 i = 0; i < AddressProbes; i++) {
        if (g_by_address[home + i].load(std::memory_order_relaxed) == 0) {
            g_by_address[home + i].store(handle, std::memory_order_release);
            return;
        }
    }
    g_by_address[home].store(handle, std::memory_order_release);
}

// Under g_mutex.
void RemoveAddress(u32 handle) {
    const VAddr address = g_textures[handle].address.exchange(0, std::memory_order_relaxed);
    if (address == 0) {
        return;
    }
    const u32 home = AddressHome(address);
    for (u32 i = 0; i < AddressProbes; i++) {
        if (g_by_address[home + i].load(std::memory_order_relaxed) == handle) {
            g_by_address[home + i].store(0, std::memory_order_relaxed);
            return;
        }
    }
}

u32 FindAddress(VAddr address) {
    const u32 home = AddressHome(address);
    for (u32 i = 0; i < AddressProbes; i++) {
        const u32 handle = g_by_address[home + i].load(std::memory_order_acquire);
        if (handle != 0 && g_textures[handle].address.load(std::memory_order_relaxed) == address) {
            return handle;
        }
    }
    return 0;
}

// Under g_mutex: the texture is no longer the game's, and its handle is freed once no frame can
// sample it.
void Drop(u32 handle) {
    g_sampled[handle].ready.store(false, std::memory_order_relaxed);
    g_sampled[handle].object.store(0, std::memory_order_release);
    g_generations[handle]++;
    RemoveAddress(handle);
    g_destroyed.push_back(handle);
}

// Notes that a capture reads the texture in the current tick.
void MarkUsed(Sampled& s) {
    const u32 tick = g_tick.load(std::memory_order_relaxed);
    if (s.used.load(std::memory_order_relaxed) != tick) {
        s.used.store(tick, std::memory_order_relaxed);
    }
}

// What tells two T#s of one address apart: the format, size, levels, tiling and pitch, not the
// order of the components.
u64 SharpKey(const u32* sharp) {
    return (u64{sharp[2]} << 32 | sharp[1]) ^ (u64{sharp[4]} << 20) ^ (sharp[3] >> 12);
}

void LogSkip(VAddr texture, const AmdGpu::Image& sharp, const char* reason) {
    if (g_logged_skips >= LoggedTextures) {
        return;
    }
    g_logged_skips++;
    LOG_INFO(Render_Vulkan,
             "GR2port: texture {:#x} not converted ({}): type {}, {}x{}, levels {}, layers {}, "
             "tile mode {}, data format {}, number format {}",
             texture, reason, static_cast<u32>(sharp.GetType()), sharp.width + 1, sharp.height + 1,
             sharp.NumLevels(), sharp.NumLayers(), static_cast<u32>(sharp.GetTileMode()),
             static_cast<u32>(sharp.GetDataFmt()), static_cast<u32>(sharp.GetNumberFmt()));
}

void PS4_SYSV_ABI OnInit(u32, const Hooks::Arguments* arguments, u64) {
    const VAddr texture = arguments->rdi;
    if (texture == 0) {
        return;
    }
    AmdGpu::Image sharp;
    std::memcpy(&sharp, reinterpret_cast<const void*>(texture + TextureSharp), sizeof(sharp));
    const VAddr blob = Load<VAddr>(texture + TextureBlob);
    std::scoped_lock lock{g_mutex};
    // The handle field starts as heap contents, which can be the handle of an earlier texture
    // at this address.
    const u32 earlier = Load<u32>(texture + TextureHandle);
    if (earlier != 0 && earlier < Capacity &&
        g_sampled[earlier].object.load(std::memory_order_relaxed) == texture) {
        Drop(earlier);
        g_missed_destroys++;
    }
    // A texture of the T# alone at this address is this one's data from now on.
    const u32 sharp_only = FindAddress(sharp.Address());
    if (sharp_only != 0 && g_textures[sharp_only].sharp_only.load(std::memory_order_relaxed)) {
        Drop(sharp_only);
    }
    u32 handle = 0;
    if (!SingleImage(sharp)) {
        g_not_2d++;
        LogSkip(texture, sharp, "not a single 2D image or cube");
    } else if (blob == 0 || !Convertible(sharp)) {
        g_unsupported++;
        LogSkip(texture, sharp, "tiling or format");
    } else if (!g_free.empty()) {
        handle = g_free.back();
        g_free.pop_back();
    } else if (g_unused < FirstReserved) {
        handle = g_unused++;
    }
    std::memcpy(reinterpret_cast<void*>(texture + TextureHandle), &handle, sizeof(handle));
    if (handle == 0) {
        return;
    }
    const VAddr stream = blob + BlobContents + Load<u32>(blob + BlobContentsSize);
    g_sources[handle] = {.sharp = sharp, .end = stream + Load<u32>(blob + BlobStreamSize)};
    g_sampled[handle].ready.store(false, std::memory_order_relaxed);
    g_sampled[handle].cube.store(IsCube(sharp), std::memory_order_relaxed);
    g_textures[handle].sharp_only.store(false, std::memory_order_relaxed);
    g_sampled[handle].object.store(texture, std::memory_order_release);
    AddAddress(handle, sharp.Address());
    g_pending.push_back(handle);
}

void PS4_SYSV_ABI OnDestroy(u32, const Hooks::Registers* registers, u64) {
    const VAddr texture = registers->rdi;
    if (texture == 0) {
        return;
    }
    const u32 handle = Load<u32>(texture + TextureHandle);
    if (handle == 0 || handle >= Capacity ||
        g_sampled[handle].object.load(std::memory_order_relaxed) != texture) {
        return;
    }
    std::scoped_lock lock{g_mutex};
    if (g_sampled[handle].object.load(std::memory_order_relaxed) != texture) {
        return;
    }
    Drop(handle);
}

// The worker's loop: copies each job's data while its texture lives, a chunk per lock.
