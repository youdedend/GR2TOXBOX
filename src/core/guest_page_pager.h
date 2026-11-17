// SPDX-FileCopyrightText: Copyright 2026 GR2port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
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

inline constexpr std::uint64_t GuestPagerPageSize = 16 * 1024;
enum GuestPageMode : std::uint32_t {
    GuestPageRead = 1u << 0,
    GuestPageWrite = 1u << 1,
    GuestPageExecute = 1u << 2,
};

/// Host/OS boundary used by the production pager algorithm. The test adapter
/// uses a real temporary file; the UWP adapter uses LocalState's sparse file.
class GuestPagePagerBacking {
public:
    virtual ~GuestPagePagerBacking() = default;
    virtual bool WriteSlot(std::uint64_t slot, std::uint64_t address,
                           std::uint32_t protection, std::uint64_t size,
                           std::uint64_t& hash) = 0;
    virtual bool ReadSlot(std::uint64_t slot, std::uint64_t address,
                          std::uint64_t size) = 0;
    virtual bool HashPage(std::uint64_t address, std::uint64_t size,
                          std::uint64_t& hash) = 0;
    virtual bool ProtectPage(std::uint64_t address, std::uint32_t mode) = 0;
    virtual void MarkSlotIn(std::uint64_t slot) = 0;
    virtual bool UnmapPage(std::uint64_t address) = 0;
};

/// Shared eviction algorithm used by AddressSpace and the host fake backing.
/// `address` to EvictPage is the mapping requesting space; it is excluded as a
/// victim while the coldest eligible page is selected.
class GuestPagePagerEngine : public GuestPagePager {
public:
    explicit GuestPagePagerEngine(std::unique_ptr<GuestPagePagerBacking> backing);
    void TrackPage(std::uint64_t address, std::uint64_t slot, std::uint32_t protection,
                   bool executable, bool locked);
    void UpdateProtection(std::uint64_t address, std::uint32_t protection);
    void SetLocked(std::uint64_t address, bool locked);
    [[nodiscard]] bool IsEvicted(std::uint64_t address) const;

    GuestPagePagerResult EvictPage(std::uint64_t address) override;
    GuestPagePagerResult PageIn(std::uint64_t address) override;
    GuestPagePagerResult ProtectPage(std::uint64_t address, std::uint32_t mode) override;

protected:
    struct Page {
        std::uint64_t address{};
        std::uint64_t slot{};
        std::uint64_t age{};
        std::uint32_t protection{};
        bool executable{};
        bool locked{};
        bool mid_write{};
        bool evicted{};
        bool hash_valid{};
        std::uint64_t hash{};
    };
    std::unique_ptr<GuestPagePagerBacking> backing_;
    mutable std::mutex mutex_;
    std::map<std::uint64_t, Page> pages_;
    std::uint64_t age_clock_{};
};

/// UWP implementation bound to AddressSpace's already-mapped sparse section
/// alias and its LocalState swap file.
struct GuestPagePagerHostOps {
    void* user{};
    bool (*unmap_page)(void* user, std::uint64_t address){};
    bool (*protect_page)(void* user, std::uint64_t address, std::uint32_t mode){};
};
[[nodiscard]] std::unique_ptr<GuestPagePagerEngine> CreateUwpGuestPagePager(
    std::uintptr_t sparse_section, void* section_alias, std::uintptr_t swap_file,
    GuestPagePagerHostOps host_ops);

} // namespace Core::Xbox
