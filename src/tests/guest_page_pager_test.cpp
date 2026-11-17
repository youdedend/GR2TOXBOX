// SPDX-FileCopyrightText: Copyright 2026 GR2port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/guest_page_pager.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

namespace {
using namespace Core::Xbox;
using PageBytes = std::array<unsigned char, GuestPagerPageSize>;

std::uint64_t Hash(const PageBytes& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) hash = (hash ^ byte) * 1099511628211ull;
    return hash;
}

class TempFileBacking final : public GuestPagePagerBacking {
public:
    TempFileBacking() : file_(std::tmpfile()) {}
    ~TempFileBacking() override { if (file_) std::fclose(file_); }

    void AddPage(std::uint64_t address, PageBytes bytes) { resident_[address] = bytes; }

    bool WriteSlot(std::uint64_t slot, std::uint64_t address, std::uint32_t protection,
                   std::uint64_t size, std::uint64_t& hash) override {
        const auto page = resident_.find(address);
        if (!file_ || page == resident_.end() || size != page->second.size() ||
            std::fseek(file_, static_cast<long>(slot), SEEK_SET) != 0)
            return false;
        hash = Hash(page->second);
        if (std::fwrite(page->second.data(), 1, page->second.size(), file_) != page->second.size() ||
            std::fflush(file_) != 0)
            return false;
        slot_out_[slot] = true;
        saved_protection_[address] = protection;
        return true;
    }

    bool ReadSlot(std::uint64_t slot, std::uint64_t address, std::uint64_t size) override {
        auto page = resident_.try_emplace(address).first;
        if (!file_ || size != page->second.size() || std::fseek(file_, static_cast<long>(slot), SEEK_SET) != 0 ||
            std::fread(page->second.data(), 1, page->second.size(), file_) != page->second.size())
            return false;
        return true;
    }

    bool HashPage(std::uint64_t address, std::uint64_t, std::uint64_t& hash) override {
        const auto page = resident_.find(address);
        if (page == resident_.end()) return false;
        hash = Hash(page->second);
        return true;
    }

    void MarkSlotIn(std::uint64_t slot) override { slot_out_[slot] = false; }

    bool UnmapPage(std::uint64_t address) override {
        const bool removed = resident_.erase(address) == 1;
        if (removed) unmapped_.insert(address);
        return removed;
    }

    [[nodiscard]] bool PageEquals(std::uint64_t address, const PageBytes& expected) const {
        const auto it = resident_.find(address);
        return it != resident_.end() && it->second == expected;
    }

    bool CorruptSlot(std::uint64_t slot) {
        if (!file_ || std::fseek(file_, static_cast<long>(slot), SEEK_SET) != 0) return false;
        int byte = std::fgetc(file_);
        if (byte == EOF || std::fseek(file_, static_cast<long>(slot), SEEK_SET) != 0 ||
            std::fputc(byte ^ 0xff, file_) == EOF)
            return false;
        return std::fflush(file_) == 0;
    }

    [[nodiscard]] bool SlotOut(std::uint64_t slot) const {
        const auto it = slot_out_.find(slot);
        return it != slot_out_.end() && it->second;
    }
    [[nodiscard]] bool Unmapped(std::uint64_t address) const { return unmapped_.contains(address); }
    [[nodiscard]] std::uint32_t SavedProtection(std::uint64_t address) const {
        const auto it = saved_protection_.find(address);
        return it == saved_protection_.end() ? 0 : it->second;
    }

private:
    std::FILE* file_{};
    std::map<std::uint64_t, PageBytes> resident_;
    std::map<std::uint64_t, bool> slot_out_;
    std::map<std::uint64_t, std::uint32_t> saved_protection_;
    std::set<std::uint64_t> unmapped_;
};

PageBytes Pattern(unsigned char seed) {
    PageBytes bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(seed + i * 17);
    return bytes;
}

bool TestEviction() {
    auto backing = std::make_unique<TempFileBacking>();
    auto* store = backing.get();
    backing->AddPage(0x10000, Pattern(1));
    backing->AddPage(0x20000, Pattern(2));
    backing->AddPage(0x30000, Pattern(3));
    backing->AddPage(0x40000, Pattern(4));
    GuestPagePagerEngine pager(std::move(backing));

    pager.TrackPage(0x10000, 0x50000, GuestPageRead | GuestPageExecute, true, false);
    pager.TrackPage(0x20000, 0x60000, GuestPageRead, false, true);
    pager.TrackPage(0x30000, 0x70000, GuestPageRead, false, false);
    pager.TrackPage(0x40000, 0x80000, GuestPageRead, false, false);
    const auto result = pager.EvictPage(0);

    return result.accepted && pager.IsEvicted(0x30000) && store->SlotOut(0x70000) &&
           store->Unmapped(0x30000) && store->SavedProtection(0x30000) == GuestPageRead &&
           !pager.IsEvicted(0x10000) && !store->Unmapped(0x10000) &&
           !pager.IsEvicted(0x20000) && !store->Unmapped(0x20000) &&
           !pager.IsEvicted(0x40000) && !store->Unmapped(0x40000);
}

bool TestPageInRoundTrip() {
    const auto original = Pattern(93);
    auto backing = std::make_unique<TempFileBacking>();
    auto* store = backing.get();
    store->AddPage(0x90000, original);
    GuestPagePagerEngine pager(std::move(backing));
    pager.TrackPage(0x90000, 0xA0000, GuestPageRead, false, false);

    const auto evicted = pager.EvictPage(0);
    const auto paged_in = pager.PageIn(0x90000);
    return evicted.accepted && paged_in.accepted && !pager.IsEvicted(0x90000) &&
           !store->SlotOut(0xA0000) && store->PageEquals(0x90000, original);
}

bool TestCorruptPageFailsWithAddress() {
    auto backing = std::make_unique<TempFileBacking>();
    auto* store = backing.get();
    store->AddPage(0xB0000, Pattern(121));
    GuestPagePagerEngine pager(std::move(backing));
    pager.TrackPage(0xB0000, 0xC0000, GuestPageRead, false, false);

    const auto evicted = pager.EvictPage(0);
    if (!evicted.accepted || !store->CorruptSlot(0xC0000)) return false;
    const auto paged_in = pager.PageIn(0xB0000);
    return !paged_in.accepted && paged_in.refusal_reason.find("0xb0000") != std::string::npos &&
           pager.IsEvicted(0xB0000);
}
} // namespace

int main() {
    const bool eviction = TestEviction();
    const bool round_trip = TestPageInRoundTrip();
    const bool corruption = TestCorruptPageFailsWithAddress();
    const bool ok = eviction && round_trip && corruption;
    std::printf("guest page pager tests: eviction=%s page-in=%s corruption=%s\n",
                eviction ? "passed" : "failed", round_trip ? "passed" : "failed",
                corruption ? "passed" : "failed");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
