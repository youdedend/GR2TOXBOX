// SPDX-FileCopyrightText: Copyright 2026 GR2port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/guest_page_pager.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace {
class TempFileBacking final : public Core::Xbox::GuestPagePagerBacking {
public:
    TempFileBacking() : file_(std::tmpfile()) {}
    ~TempFileBacking() override { if (file_) std::fclose(file_); }

    void AddPage(std::uint64_t address, std::array<unsigned char, Core::Xbox::GuestPagerPageSize> bytes) {
        resident_[address] = bytes;
    }

    bool WriteSlot(std::uint64_t slot, std::uint64_t address, std::uint32_t protection,
                   std::uint64_t size) override {
        auto page = resident_.find(address);
        if (!file_ || page == resident_.end() || size != page->second.size() ||
            std::fseek(file_, static_cast<long>(slot), SEEK_SET) != 0 ||
            std::fwrite(page->second.data(), 1, page->second.size(), file_) != page->second.size() ||
            std::fflush(file_) != 0)
            return false;
        slot_out_[slot] = true;
        saved_protection_[address] = protection;
        return true;
    }

    bool UnmapPage(std::uint64_t address) override {
        return unmapped_.insert(address).second;
    }

    [[nodiscard]] bool SlotOut(std::uint64_t slot) const {
        auto it = slot_out_.find(slot);
        return it != slot_out_.end() && it->second;
    }
    [[nodiscard]] bool Unmapped(std::uint64_t address) const { return unmapped_.contains(address); }
    [[nodiscard]] std::uint32_t SavedProtection(std::uint64_t address) const {
        auto it = saved_protection_.find(address);
        return it == saved_protection_.end() ? 0 : it->second;
    }

private:
    std::FILE* file_{};
    std::map<std::uint64_t, std::array<unsigned char, Core::Xbox::GuestPagerPageSize>> resident_;
    std::map<std::uint64_t, bool> slot_out_;
    std::map<std::uint64_t, std::uint32_t> saved_protection_;
    std::set<std::uint64_t> unmapped_;
};
}

int main() {
    using namespace Core::Xbox;
    auto backing = std::make_unique<TempFileBacking>();
    auto* store = backing.get();
    backing->AddPage(0x10000, {});
    backing->AddPage(0x20000, {});
    backing->AddPage(0x30000, {});
    backing->AddPage(0x40000, {});
    GuestPagePagerEngine pager(std::move(backing));

    pager.TrackPage(0x10000, 0x50000, GuestPageRead | GuestPageExecute, true, false);
    pager.TrackPage(0x20000, 0x60000, GuestPageRead, false, true);
    pager.TrackPage(0x30000, 0x70000, GuestPageRead, false, false);
    pager.TrackPage(0x40000, 0x80000, GuestPageRead, false, false);
    const auto result = pager.EvictPage(0);

    const bool ok = result.accepted && pager.IsEvicted(0x30000) &&
                    store->SlotOut(0x70000) && store->Unmapped(0x30000) &&
                    store->SavedProtection(0x30000) == GuestPageRead &&
                    !pager.IsEvicted(0x10000) && !store->Unmapped(0x10000) &&
                    !pager.IsEvicted(0x20000) && !store->Unmapped(0x20000) &&
                    !pager.IsEvicted(0x40000) && !store->Unmapped(0x40000);
    std::printf("guest page pager eviction test: %s\n", ok ? "passed" : "failed");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
