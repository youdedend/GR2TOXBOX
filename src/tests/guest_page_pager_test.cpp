// SPDX-FileCopyrightText: Copyright 2026 GR2port contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/guest_page_pager.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
class TempFilePager final : public Core::Xbox::GuestPagePager {
public:
    TempFilePager() : file_(std::tmpfile()) {}
    ~TempFilePager() override { if (file_) std::fclose(file_); }

    Core::Xbox::GuestPagePagerResult EvictPage(std::uint64_t address) override {
        (void)address;
        calls_.push_back("evict");
        if (!file_) return Core::Xbox::GuestPagePagerResult::Refused("temp-file write failed");
        const std::array<unsigned char, 1> byte{0x5a};
        if (std::fwrite(byte.data(), 1, byte.size(), file_) != byte.size() || std::fflush(file_) != 0)
            return Core::Xbox::GuestPagePagerResult::Refused("temp-file write failed");
        return {true, {}};
    }

    Core::Xbox::GuestPagePagerResult PageIn(std::uint64_t address) override {
        (void)address;
        calls_.push_back("page-in");
        if (!file_) return Core::Xbox::GuestPagePagerResult::Refused("temp-file read failed");
        std::rewind(file_);
        unsigned char byte{};
        if (std::fread(&byte, 1, 1, file_) != 1 || byte != 0x5a)
            return Core::Xbox::GuestPagePagerResult::Refused("temp-file read failed");
        return {true, {}};
    }

    Core::Xbox::GuestPagePagerResult ProtectPage(std::uint64_t, std::uint32_t) override {
        calls_.push_back("protect");
        return Core::Xbox::GuestPagePagerResult::Refused("fake protection not part of this sequence");
    }

    [[nodiscard]] const std::vector<std::string>& Calls() const { return calls_; }

private:
    std::FILE* file_{};
    std::vector<std::string> calls_;
};
}

int main() {
    TempFilePager fake;
    Core::Xbox::GuestPagePager& pager = fake;
    const auto evicted = pager.EvictPage(0);
    const auto paged_in = pager.PageIn(0);
    const bool ok = evicted.accepted && paged_in.accepted &&
                    fake.Calls() == std::vector<std::string>{"evict", "page-in"};
    std::printf("guest page pager interface test: %s\n", ok ? "passed" : "failed");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
