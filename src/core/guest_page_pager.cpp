// SPDX-FileCopyrightText: Copyright 2026 GR2port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/guest_page_pager.h"

#include <map>
#include <utility>

namespace Core::Xbox {
namespace {

// Adapted from Cosmo/gow3_xbox xbox/platform/guest_swap.inc and its KytyPS5
// page records. Keep virtual-page state separate from physical-page state so
// aliases share residency, protection, pinning, and integrity metadata.
struct GuestPageRecord {
    std::uint64_t address{};
    std::uint64_t physical_offset{};
    std::uint64_t last_touch{};
    std::uint32_t protection{};
    bool mapped{};
    bool executable{};
    bool locked{};
    bool evicted{};
};

struct PhysicalPageRecord {
    std::uint64_t owner_address{};
    std::uint64_t hash{};
    std::uint32_t references{};
    bool pinned{};
    bool hash_valid{};
    bool on_disk{};
};

class UwpGuestPagePager final : public GuestPagePager {
public:
    UwpGuestPagePager(std::uintptr_t section, void* alias) : section_(section), alias_(alias) {}

    GuestPagePagerResult EvictPage(std::uint64_t address) override {
        (void)address;
        return GuestPagePagerResult::Refused(
            "eviction not implemented; the sparse section is mapped whole and never paged out");
    }

    GuestPagePagerResult PageIn(std::uint64_t address) override {
        (void)address;
        return GuestPagePagerResult::Refused(
            "page-in not implemented; the sparse section remains mapped whole");
    }

    GuestPagePagerResult ProtectPage(std::uint64_t address, std::uint32_t mode) override {
        (void)address;
        (void)mode;
        return GuestPagePagerResult::Refused(
            "pager protection refused; use the existing AddressSpace protection path");
    }

private:
    // These are the existing LocalState-backed section and its whole-range
    // alias, created and mapped by AddressSpace exactly as before.
    std::uintptr_t section_{};
    void* alias_{};
    // The page and physical records mirror the upstream separation; later steps
    // connect their transitions to the existing AddressSpace mappings.
    std::map<std::uint64_t, GuestPageRecord> pages_;
    std::map<std::uint64_t, PhysicalPageRecord> physical_pages_;
    std::uint64_t age_clock_{};
};

} // namespace

std::unique_ptr<GuestPagePager> CreateUwpGuestPagePager(std::uintptr_t sparse_section,
                                                        void* section_alias) {
    return std::make_unique<UwpGuestPagePager>(sparse_section, section_alias);
}

} // namespace Core::Xbox
