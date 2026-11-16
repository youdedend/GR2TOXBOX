// SPDX-License-Identifier: GPL-2.0-or-later
// Shared CPU capture implementation; included inside the backend namespace.
const Mesh* Get(VAddr shape) {
    const u32 handle = Load<u32>(shape + ShapeHandle);
    if (handle >= Capacity) {
        return nullptr;
    }
    Mesh& mesh = g_meshes[handle];
    if (mesh.shape.load(std::memory_order_relaxed) != shape) {
        return nullptr;
    }
    const u32 seconds = g_seconds.load(std::memory_order_relaxed);
    if (mesh.used.load(std::memory_order_relaxed) != seconds) {
        mesh.used.store(seconds, std::memory_order_relaxed);
    }
    if (mesh.ready.load(std::memory_order_acquire)) {
        return &mesh;
    }
    // A draw wants an evicted mesh: keep one being evicted, and upload one evicted again.
    u8 state = Evicting;
    if (!mesh.eviction.compare_exchange_strong(state, Rescued, std::memory_order_acq_rel) &&
        state == Evicted &&
        mesh.eviction.compare_exchange_strong(state, Kept, std::memory_order_acq_rel)) {
        std::scoped_lock lock{g_mutex};
        g_pending.push_back(handle);
    }
    return nullptr;
}

void Prefetch(VAddr shape) {
    const u32 handle = Load<u32>(shape + ShapeHandle);
    if (handle < Capacity) {
        const auto* const mesh = reinterpret_cast<const u8*>(&g_meshes[handle]);
        __builtin_prefetch(mesh);
        __builtin_prefetch(mesh + 64);
    }
}
