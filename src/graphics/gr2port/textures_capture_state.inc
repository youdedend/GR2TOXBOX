// SPDX-License-Identifier: GPL-2.0-or-later
// Shared CPU capture implementation; included inside the backend namespace.
template <typename T>
T Load(VAddr address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

// What a capture reads of a texture at each draw that names it. It is apart from the rest of the
// texture so that four textures share a cache line.
struct Sampled {
    std::atomic<VAddr> object{0};
    /// The streaming tick of the last capture that read it.
    std::atomic<u32> used{0};
    /// The descriptor shaders sample it by: its handle or the handle's twin.
    std::atomic<u16> slot{0};
    std::atomic<bool> ready{false};
    std::atomic<bool> cube{false};
};
static_assert(sizeof(Sampled) == 16 && Descriptors - 1 <= 0xffff);

struct Texture {
    /// The address of its data, and for a texture of a T# alone, that T#'s key and whether it is
    /// linear.
    std::atomic<VAddr> address{0};
    std::atomic<u64> key{0};
    std::atomic<bool> sharp_only{false};
    std::atomic<bool> linear{false};
};

// The handles by the address of their data: open addressing over AddressProbes slots from a home
// slot, written under g_mutex and read without it.
constexpr u32 AddressSlots = Capacity * 2;
constexpr u32 AddressProbes = 16;
// Textures of a T# alone take handles below the ImGui textures' and the targets', which Start
// keeps out of use.
constexpr u32 FirstReserved = Scene::ImGuiHandles;
static_assert(Scene::UiTargetHandles + Scene::MaxUiTargets == Capacity);
static_assert(Scene::ImGuiHandles + Scene::MaxImGuiTextures == Scene::UiTargetHandles);

// What a texture object holds when it is built: its T# and the end of its GNF data.
struct Source {
    AmdGpu::Image sharp;
    VAddr end;
};

std::array<Texture, Capacity> g_textures;
std::array<Sampled, Capacity> g_sampled;
std::array<Source, Capacity> g_sources;
std::array<std::atomic<u32>, AddressSlots + AddressProbes> g_by_address{};
std::atomic<u32> g_tick{1u << 16};

// Guards the handle lists, the sources and the reads of texture data that destroys free.
std::mutex g_mutex;
std::vector<u32> g_free;
u32 g_unused = 1;
std::deque<u32> g_pending;
std::vector<u32> g_destroyed;
// Each drop raises its handle's generation, which abandons the copies of its data under way.
std::array<u32, Capacity> g_generations{};
u32 g_not_2d = 0;
u32 g_unsupported = 0;
u32 g_missed_destroys = 0;
u32 g_logged_skips = 0;
