// SPDX-FileCopyrightText: Copyright 2026 GR2port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/guest_page_pager.h"

#include <utility>

namespace Core::Xbox {
namespace {

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
};

} // namespace

std::unique_ptr<GuestPagePager> CreateUwpGuestPagePager(std::uintptr_t sparse_section,
                                                        void* section_alias) {
    return std::make_unique<UwpGuestPagePager>(sparse_section, section_alias);
}

} // namespace Core::Xbox
