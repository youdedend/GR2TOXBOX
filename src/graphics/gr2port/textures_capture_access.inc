// SPDX-License-Identifier: GPL-2.0-or-later
// Shared CPU capture implementation; included inside the backend namespace.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    const Hooks::Result init = Hooks::ObserveReturn(module_base, Init, c, capacity, &OnInit, 0);
    const Hooks::Result destroy =
        Hooks::ObserveEntry(module_base, Destroy, c, capacity, &OnDestroy, 0);
    const auto ok = [](Hooks::Result r) {
        return r == Hooks::Result::Hooked || r == Hooks::Result::Chained;
    };
    if (!ok(init) || !ok(destroy)) {
        LOG_ERROR(Render_Vulkan, "GR2port: the texture init or destroy is not the expected code");
        return false;
    }
    return true;
}

namespace {

u32 GetOfKind(VAddr texture, bool cube) {
    const u32 handle = Load<u32>(texture + TextureHandle);
    if (handle == 0 || handle >= Capacity) {
        return 0;
    }
    Sampled& s = g_sampled[handle];
    if (s.object.load(std::memory_order_relaxed) != texture ||
        s.cube.load(std::memory_order_relaxed) != cube) {
        return 0;
    }
    MarkUsed(s);
    return s.ready.load(std::memory_order_acquire) ? s.slot.load(std::memory_order_acquire) : 0;
}

} // namespace

u32 Get(VAddr texture) {
    return GetOfKind(texture, false);
}

void Prefetch(const VAddr* objects, u32 count) {
    for (u32 i = 0; i < count; i++) {
        __builtin_prefetch(reinterpret_cast<const void*>(objects[i] + TextureHandle));
    }
}

void PrefetchRecords(const VAddr* objects, u32 count) {
    for (u32 i = 0; i < count; i++) {
        const u32 handle = objects[i] != 0 ? Load<u32>(objects[i] + TextureHandle) : 0;
        if (handle < Capacity) {
            __builtin_prefetch(&g_sampled[handle]);
        }
    }
}

u32 GetCube(VAddr texture) {
    return GetOfKind(texture, true);
}

u32 GetBySharp(const u32* words, bool& dynamic) {
    dynamic = false;
    AmdGpu::Image sharp;
    std::memcpy(&sharp, words, sizeof(sharp));
    const VAddr address = sharp.Address();
    const u64 key = SharpKey(words);
    const u32 found = FindAddress(address);
    if (found != 0) {
        Texture& t = g_textures[found];
        const bool sharp_only = t.sharp_only.load(std::memory_order_relaxed);
        if (!sharp_only || t.key.load(std::memory_order_relaxed) == key) {
            dynamic = sharp_only && t.linear.load(std::memory_order_relaxed);
            Sampled& s = g_sampled[found];
            MarkUsed(s);
            return s.ready.load(std::memory_order_acquire) &&
                           !s.cube.load(std::memory_order_relaxed)
                       ? s.slot.load(std::memory_order_acquire)
                       : 0;
        }
    }
    // The first draw that reads it, or a T# of another texture at its address.
    std::unique_lock lock{g_mutex, std::try_to_lock};
    if (!lock.owns_lock() || sharp.GetType() != AmdGpu::ImageType::Color2D || !SingleImage(sharp) ||
        !Convertible(sharp)) {
        return 0;
    }
    if (found != 0 && g_textures[found].sharp_only.load(std::memory_order_relaxed)) {
        Drop(found);
    }
    u32 handle = 0;
    if (!g_free.empty()) {
        handle = g_free.back();
        g_free.pop_back();
    } else if (g_unused < FirstReserved) {
        handle = g_unused++;
    } else {
        return 0;
    }
    g_sources[handle] = {.sharp = sharp, .end = ~VAddr{0}};
    Texture& t = g_textures[handle];
    Sampled& s = g_sampled[handle];
    s.ready.store(false, std::memory_order_relaxed);
    s.cube.store(false, std::memory_order_relaxed);
    t.sharp_only.store(true, std::memory_order_relaxed);
    t.linear.store(!Tiled(sharp.GetTileMode()), std::memory_order_relaxed);
    t.key.store(key, std::memory_order_relaxed);
    s.object.store(address, std::memory_order_release);
    AddAddress(handle, address);
    g_pending.push_back(handle);
    return 0;
}
