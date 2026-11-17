// SPDX-FileCopyrightText: Copyright 2026 GR2port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace Core::Xbox {

struct GuestPagePagerResult {
    bool accepted{};
    std::string refusal_reason;

    [[nodiscard]] static GuestPagePagerResult Refused(std::string reason) {
        return {false, std::move(reason)};
    }
};

/// Boundary for guest-page residency operations. A refusal is explicit and
/// carries a plain-English reason; callers must not treat it as success.
class GuestPagePager {
public:
    virtual ~GuestPagePager() = default;
    virtual GuestPagePagerResult EvictPage(std::uint64_t address) = 0;
    virtual GuestPagePagerResult PageIn(std::uint64_t address) = 0;
    virtual GuestPagePagerResult ProtectPage(std::uint64_t address, std::uint32_t mode) = 0;
};

/// Creates the UWP pager bound to AddressSpace's already-mapped sparse section
/// alias. Its current operations explicitly refuse pager work; AddressSpace's
/// existing whole-section mapping and protection path remain unchanged.
[[nodiscard]] std::unique_ptr<GuestPagePager> CreateUwpGuestPagePager(
    std::uintptr_t sparse_section, void* section_alias);

} // namespace Core::Xbox
