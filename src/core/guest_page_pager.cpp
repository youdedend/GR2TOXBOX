// SPDX-FileCopyrightText: Copyright 2026 GR2port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/guest_page_pager.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <utility>

#if defined(GR2_XBOX_UWP)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fileapifromapp.h>
#endif

namespace Core::Xbox {

GuestPagePagerEngine::GuestPagePagerEngine(std::unique_ptr<GuestPagePagerBacking> backing)
    : backing_(std::move(backing)) {}

void GuestPagePagerEngine::TrackPage(std::uint64_t address, std::uint64_t slot,
                                     std::uint32_t protection, bool executable, bool locked) {
    std::scoped_lock lock(mutex_);
    auto& page = pages_[address];
    page.address = address;
    page.slot = slot;
    page.protection = protection;
    page.executable = executable || (protection & GuestPageExecute) != 0;
    page.locked = locked;
    page.mid_write = (protection & GuestPageWrite) != 0;
    page.evicted = false;
    page.age = ++age_clock_;
}

void GuestPagePagerEngine::UpdateProtection(std::uint64_t address, std::uint32_t protection) {
    std::scoped_lock lock(mutex_);
    auto it = pages_.find(address);
    if (it == pages_.end()) return;
    it->second.protection = protection;
    it->second.executable = it->second.executable || (protection & GuestPageExecute) != 0;
    it->second.mid_write = (protection & GuestPageWrite) != 0;
    it->second.age = ++age_clock_;
}

void GuestPagePagerEngine::SetLocked(std::uint64_t address, bool locked) {
    std::scoped_lock lock(mutex_);
    auto it = pages_.find(address);
    if (it != pages_.end()) it->second.locked = locked;
}

bool GuestPagePagerEngine::IsEvicted(std::uint64_t address) const {
    std::scoped_lock lock(mutex_);
    auto it = pages_.find(address);
    return it != pages_.end() && it->second.evicted;
}

GuestPagePagerResult GuestPagePagerEngine::EvictPage(std::uint64_t address) {
    std::scoped_lock lock(mutex_);
    Page* victim = nullptr;
    for (auto& [page_address, page] : pages_) {
        if (page_address == address || page.evicted || page.executable || page.locked ||
            page.mid_write || !(page.protection & GuestPageRead))
            continue;
        if (!victim || page.age < victim->age) victim = &page;
    }
    if (!victim) {
        return GuestPagePagerResult::Refused("no cold, readable, unlocked, non-executable page is eligible");
    }
    // Executable, guest-locked, and actively writable pages are hard exclusions.
    // Keep these assertions next to the transition so a later selection change
    // cannot silently make one of them evictable.
    assert(!victim->executable);
    assert(!victim->locked);
    assert(!victim->mid_write);
    if (!backing_ || !backing_->WriteSlot(victim->slot, victim->address,
                                          victim->protection, GuestPagerPageSize)) {
        return GuestPagePagerResult::Refused("sparse LocalState page write failed");
    }
    if (!backing_->UnmapPage(victim->address)) {
        return GuestPagePagerResult::Refused("guest page unmap failed after sparse write");
    }
    // Record the slot and exact guest protection before publishing it as out.
    victim->evicted = true;
    victim->age = ++age_clock_;
    return {true, {}};
}

GuestPagePagerResult GuestPagePagerEngine::PageIn(std::uint64_t) {
    return GuestPagePagerResult::Refused("page-in is not implemented yet");
}

GuestPagePagerResult GuestPagePagerEngine::ProtectPage(std::uint64_t address, std::uint32_t mode) {
    (void)address;
    (void)mode;
    return GuestPagePagerResult::Refused("pager protection is not implemented yet");
}

#if defined(GR2_XBOX_UWP)
namespace {
class UwpSparsePageBacking final : public GuestPagePagerBacking {
public:
    UwpSparsePageBacking(std::uintptr_t section, void* alias, std::uintptr_t file,
                         GuestPagePagerHostOps host_ops)
        : section_(reinterpret_cast<HANDLE>(section)), alias_(static_cast<unsigned char*>(alias)),
          file_(reinterpret_cast<HANDLE>(file)), host_ops_(host_ops) {}

    bool WriteSlot(std::uint64_t slot, std::uint64_t address, std::uint32_t,
                   std::uint64_t size) override {
        OVERLAPPED offset{};
        offset.Offset = static_cast<DWORD>(slot);
        offset.OffsetHigh = static_cast<DWORD>(slot >> 32);
        DWORD written{};
        return file_ && file_ != INVALID_HANDLE_VALUE &&
               WriteFile(file_, reinterpret_cast<const void*>(address), static_cast<DWORD>(size),
                         &written, &offset) && written == size;
    }

    bool UnmapPage(std::uint64_t address) override {
        return host_ops_.unmap_page && host_ops_.unmap_page(host_ops_.user, address);
    }

private:
    HANDLE section_{};
    unsigned char* alias_{};
    HANDLE file_{};
    GuestPagePagerHostOps host_ops_{};
};
} // namespace
#endif

std::unique_ptr<GuestPagePagerEngine> CreateUwpGuestPagePager(
    std::uintptr_t sparse_section, void* section_alias, std::uintptr_t swap_file,
    GuestPagePagerHostOps host_ops) {
#if defined(GR2_XBOX_UWP)
    return std::make_unique<GuestPagePagerEngine>(std::make_unique<UwpSparsePageBacking>(
        sparse_section, section_alias, swap_file, host_ops));
#else
    (void)sparse_section;
    (void)section_alias;
    (void)swap_file;
    (void)host_ops;
    return {};
#endif
}

} // namespace Core::Xbox
