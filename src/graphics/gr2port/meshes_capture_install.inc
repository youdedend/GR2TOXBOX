// SPDX-License-Identifier: GPL-2.0-or-later
// Shared CPU capture implementation; included inside the backend namespace.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    const Hooks::Result built =
        Hooks::ObserveReturn(module_base, Builder, c, capacity, &OnBuilt, 0);
    const Hooks::Result unload =
        Hooks::ObserveEntry(module_base, Unload, c, capacity, &OnUnload, 0);
    const auto ok = [](Hooks::Result r) {
        return r == Hooks::Result::Hooked || r == Hooks::Result::Chained;
    };
    if (!ok(built) || !ok(unload)) {
        LOG_ERROR(Render_Vulkan, "GR2port: the mesh builder or unload is not the expected code");
        return false;
    }
    return true;
}
