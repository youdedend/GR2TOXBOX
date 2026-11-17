// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/uwp_guest_memory.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <memory>

namespace {
int passed = 0, failed = 0;
void Check(bool condition, const char* name) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); ++failed; }
    else ++passed;
}
using Core::Xbox::GuestMemoryModel;
constexpr auto P = GuestMemoryModel::PageSize;
class TempDirectorySwap final : public Core::Xbox::GuestSwapStore {
public:
    TempDirectorySwap() {
        path_ = std::filesystem::temp_directory_path() /
            ("gr2-uwp-swap-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(path_);
        file_ = path_ / "pages.bin";
    }
    ~TempDirectorySwap() override {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    bool WritePage(std::uint64_t slot, const void* data, std::uint64_t size) override {
        std::fstream f(file_, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) f.open(file_, std::ios::binary | std::ios::out);
        f.seekp(static_cast<std::streamoff>(slot));
        f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        f.flush(); return f.good();
    }
    bool ReadPage(std::uint64_t slot, void* data, std::uint64_t size) override {
        std::ifstream f(file_, std::ios::binary);
        if (!f) return false;
        f.seekg(static_cast<std::streamoff>(slot));
        f.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
        return f.gcount() == static_cast<std::streamsize>(size);
    }
    void CorruptFirstByte() {
        std::fstream f(file_, std::ios::binary | std::ios::in | std::ios::out);
        char byte{}; f.read(&byte, 1); byte ^= 0x5a; f.seekp(0); f.write(&byte, 1); f.flush();
    }
private:
    std::filesystem::path path_, file_;
};

void RangeSearchAndCoalescing() {
    GuestMemoryModel m(0x10000, 0x20000, 8 * P); std::string e;
    std::uint64_t a = 0, b = 0, c = 0;
    bool ok = m.Reserve(a, 2 * P, e) && m.Reserve(b, 3 * P, e) && m.Release(a, 2 * P, e) && m.Release(b, 3 * P, e);
    ok = ok && m.Reserve(c, 5 * P, e) && c == 0x10000;
    Check(ok, "first-fit search and adjacent placeholder coalescing");
}
void ProtectRoundTrip() {
    GuestMemoryModel m(0x10000, 0x18000, 4 * P); std::string e; std::uint64_t a = 0;
    std::array<unsigned char, 4> input{1, 3, 5, 7}, output{};
    bool ok = m.Reserve(a, P, e) && m.Commit(a, P, GuestMemoryModel::ReadAccess | GuestMemoryModel::WriteAccess, e) &&
        m.Write(a, input.data(), input.size(), e) && m.Protect(a, P, GuestMemoryModel::ReadAccess, e) &&
        m.Read(a, output.data(), output.size(), e) && output == input;
    const bool write_blocked = !m.Write(a, input.data(), 1, e);
    ok = ok && write_blocked && m.Protect(a, P, GuestMemoryModel::ReadAccess | GuestMemoryModel::ExecuteAccess, e);
    Check(ok, "writable-to-readonly-to-executable protection round trip");
}
void EvictionPreservesProtection() {
    GuestMemoryModel m(0x10000, 0x18000, 4 * P, std::make_unique<TempDirectorySwap>()); std::string e; std::uint64_t a = 0;
    std::array<unsigned char, 4> input{9, 8, 7, 6}, output{};
    bool ok = m.Reserve(a, P, e) && m.Commit(a, P, GuestMemoryModel::ReadAccess | GuestMemoryModel::WriteAccess, e) &&
        m.Write(a, input.data(), input.size(), e) && m.Protect(a, P, GuestMemoryModel::ReadAccess, e) &&
        m.EvictCold(P) == P;
    const auto before = m.GetStats();
    ok = ok && before.pages_out == 1 && before.evicted_bytes == P &&
        m.Read(a, output.data(), output.size(), e) && output == input;
    const auto after = m.GetStats();
    ok = ok && after.pages_in == 1 && after.committed_bytes == P &&
        !m.Write(a, input.data(), 1, e);
    Check(ok, "cold-page swap integrity, page-in, and preserved readonly permission");
}
void HashMismatchIsReportedWithAddress() {
    auto store = std::make_unique<TempDirectorySwap>();
    auto* raw_store = store.get();
    GuestMemoryModel m(0x20000, 0x28000, 4 * P, std::move(store));
    std::string e; std::uint64_t a = 0;
    std::array<unsigned char, 4> input{0x31, 0x42, 0x53, 0x64}, output{};
    bool ok = m.Reserve(a, P, e) && m.Commit(a, P, GuestMemoryModel::ReadAccess | GuestMemoryModel::WriteAccess, e) &&
        m.Write(a, input.data(), input.size(), e) && m.Protect(a, P, GuestMemoryModel::ReadAccess, e) &&
        m.EvictCold(P) == P;
    raw_store->CorruptFirstByte();
    const bool rejected = !m.Read(a, output.data(), output.size(), e);
    const auto stats = m.GetStats();
    ok = ok && rejected && e.find("0x20000") != std::string::npos && stats.hash_errors == 1 &&
         stats.pages_out == 1 && stats.pages_in == 1;
    Check(ok, "corrupt swap page is rejected with address and hash-error counter");
}

void ExecutablePagesAreNeverEvicted() {
    GuestMemoryModel m(0x10000, 0x18000, 4 * P); std::string e; std::uint64_t a = 0;
    bool ok = m.Reserve(a, P, e) && m.Commit(a, P, GuestMemoryModel::ReadAccess | GuestMemoryModel::ExecuteAccess, e) &&
        m.EvictCold(P) == 0 && m.GetStats().pages_out == 0;
    Check(ok, "executable pages remain resident under pressure");
}
void OutOfRangeAddressNamesTheAddress() {
    GuestMemoryModel m(0x10000, 0x20000, 4 * P); std::string e;
    std::uint64_t a = Core::Xbox::GuestAddressLimit;
    const bool rejected = !m.Reserve(a, P, e) && e.find("0x10000000000") != std::string::npos;
    Check(rejected, "40-bit range refusal includes the offending guest address");
}
void CeilingStopsCommitWithPlainLanguage() {
    GuestMemoryModel m(0x10000, 0x18000, P); std::string e; std::uint64_t a = 0;
    bool ok = m.Reserve(a, 2 * P, e) && !m.Commit(a, 2 * P, GuestMemoryModel::ReadAccess, e);
    const auto stats = m.GetStats();
    ok = ok && e.find("ceiling reached") != std::string::npos &&
         e.find("8192 bytes") != std::string::npos && stats.committed_bytes == 0 &&
         m.TelemetryLine().find("ceiling=") != std::string::npos;
    Check(ok, "memory ceiling produces a readable hard stop and telemetry");
}
}
int main() {
    RangeSearchAndCoalescing(); ProtectRoundTrip(); EvictionPreservesProtection();
    HashMismatchIsReportedWithAddress(); ExecutablePagesAreNeverEvicted(); OutOfRangeAddressNamesTheAddress();
    CeilingStopsCommitWithPlainLanguage();
    std::printf("UWP guest memory host tests: %d passed, %d failed\n", passed, failed);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
