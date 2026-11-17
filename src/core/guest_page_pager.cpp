// SPDX-FileCopyrightText: Copyright 2026 GR2port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/guest_page_pager.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <iomanip>
#include <sstream>
#include <utility>

#if defined(GR2_XBOX_UWP)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fileapifromapp.h>
#endif

namespace Core::Xbox {
#if defined(GR2_XBOX_UWP)
namespace {
std::uint64_t Fnv1a(const unsigned char* bytes, std::uint64_t size) {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::uint64_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}
} // namespace
#endif

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
    std::uint64_t hash{};
    if (!backing_ || !backing_->WriteSlot(victim->slot, victim->address,
                                          victim->protection, GuestPagerPageSize, hash)) {
        return GuestPagePagerResult::Refused("sparse LocalState page write failed");
    }
    if (!backing_->UnmapPage(victim->address)) {
        return GuestPagePagerResult::Refused("guest page unmap failed after sparse write");
    }
    // Record the slot and exact guest protection before publishing it as out.
    victim->hash = hash;
    victim->hash_valid = true;
    victim->evicted = true;
    victim->age = ++age_clock_;
    return {true, {}};
}

GuestPagePagerResult GuestPagePagerEngine::PageIn(std::uint64_t address) {
    std::scoped_lock lock(mutex_);
    auto it = pages_.find(address);
    if (it == pages_.end() || !it->second.evicted) {
        std::ostringstream message;
        message << "page-in requested for a resident or unknown address 0x" << std::hex << address;
        return GuestPagePagerResult::Refused(message.str());
    }
    auto& page = it->second;
    if (!backing_ || !backing_->ReadSlot(page.slot, address, GuestPagerPageSize)) {
        std::ostringstream message;
        message << "could not restore guest page at address 0x" << std::hex << address;
        return GuestPagePagerResult::Refused(message.str());
    }
    std::uint64_t actual_hash{};
    if (!page.hash_valid || !backing_->HashPage(address, GuestPagerPageSize, actual_hash) ||
        actual_hash != page.hash) {
        std::ostringstream message;
        message << "guest page FNV-1a hash mismatch at address 0x" << std::hex << address;
        return GuestPagePagerResult::Refused(message.str());
    }
    backing_->MarkSlotIn(page.slot);
    page.evicted = false;
    page.hash_valid = false;
    page.age = ++age_clock_;
    return {true, {}};
}

GuestPagePagerResult GuestPagePagerEngine::ProtectPage(std::uint64_t address, std::uint32_t mode) {
    std::scoped_lock lock(mutex_);
    auto it = pages_.find(address);
    if (it == pages_.end()) {
        std::ostringstream message;
        message << "protection requested for an unknown guest page at 0x" << std::hex << address;
        return GuestPagePagerResult::Refused(message.str());
    }
    if (!backing_ || !backing_->ProtectPage(address, mode)) {
        std::ostringstream message;
        message << "could not change protection for guest page at 0x" << std::hex << address;
        return GuestPagePagerResult::Refused(message.str());
    }
    it->second.protection = mode;
    it->second.executable = (mode & GuestPageExecute) != 0;
    it->second.mid_write = (mode & GuestPageWrite) != 0;
    it->second.age = ++age_clock_;
    return {true, {}};
}

#if defined(GR2_XBOX_UWP)
namespace {
class UwpSparsePageBacking final : public GuestPagePagerBacking {
public:
    UwpSparsePageBacking(std::uintptr_t section, void* alias, std::uintptr_t file,
                         GuestPagePagerHostOps host_ops)
        : section_(reinterpret_cast<HANDLE>(section)), alias_(static_cast<unsigned char*>(alias)),
          file_(reinterpret_cast<HANDLE>(file)), host_ops_(host_ops) {}

    ~UwpSparsePageBacking() override {
        if (file_ && file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    }

    bool WriteSlot(std::uint64_t slot, std::uint64_t address, std::uint32_t protection,
                   std::uint64_t size, std::uint64_t& hash) override {
        DWORD written{};
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(slot);
        const auto* bytes = reinterpret_cast<const unsigned char*>(address);
        hash = Fnv1a(bytes, size);
        if (!file_ || file_ == INVALID_HANDLE_VALUE || !SetFilePointerEx(file_, offset, nullptr, FILE_BEGIN) ||
            !WriteFile(file_, bytes, static_cast<DWORD>(size), &written, nullptr) || written != size)
            return false;
        saved_protection_[address] = protection;
        return true;
    }

    bool ReadSlot(std::uint64_t slot, std::uint64_t address, std::uint64_t size) override {
        const auto it = saved_protection_.find(address);
        if (it == saved_protection_.end() || !ProtectPage(address, GuestPageRead | GuestPageWrite))
            return false;
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(slot);
        DWORD read{};
        const bool read_ok = file_ && file_ != INVALID_HANDLE_VALUE &&
                             SetFilePointerEx(file_, offset, nullptr, FILE_BEGIN) &&
                             ReadFile(file_, reinterpret_cast<void*>(address), static_cast<DWORD>(size),
                                      &read, nullptr) && read == size;
        const bool restored = ProtectPage(address, it->second);
        return read_ok && restored;
    }

    bool HashPage(std::uint64_t address, std::uint64_t size, std::uint64_t& hash) override {
        hash = Fnv1a(reinterpret_cast<const unsigned char*>(address), size);
        return true;
    }

    bool ProtectPage(std::uint64_t address, std::uint32_t mode) override {
        return host_ops_.protect_page && host_ops_.protect_page(host_ops_.user, address, mode);
    }

    void MarkSlotIn(std::uint64_t) override {}

    bool UnmapPage(std::uint64_t address) override {
        return host_ops_.unmap_page && host_ops_.unmap_page(host_ops_.user, address);
    }

private:
    HANDLE section_{};
    unsigned char* alias_{};
    HANDLE file_{};
    GuestPagePagerHostOps host_ops_{};
    std::map<std::uint64_t, std::uint32_t> saved_protection_;
};
} // namespace
#endif

std::unique_ptr<GuestPagePagerEngine> CreateUwpGuestPagePager(
    std::uintptr_t sparse_section, void* section_alias, std::uintptr_t swap_file,
    GuestPagePagerHostOps host_ops) {
#if defined(GR2_XBOX_UWP)
    if (sparse_section == 0 || section_alias == nullptr || swap_file == 0 ||
        host_ops.unmap_page == nullptr || host_ops.protect_page == nullptr)
        return {};
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
