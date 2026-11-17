// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Core::Xbox {

// Guest virtual addresses are 40-bit even when the host uses a wider pointer.
inline constexpr std::uint64_t GuestAddressLimit = std::uint64_t{1} << 40;
[[nodiscard]] bool IsValidGuestRange(std::uint64_t address, std::uint64_t size) noexcept;

// Shared guest-page residency state machine. The WindowsStore adapter owns the
// section views and the host tests inject a temporary-file backing store through
// the same pager interface.
class GuestSwapStore {
public:
    virtual ~GuestSwapStore() = default;
    virtual bool WritePage(std::uint64_t slot, const void* data, std::uint64_t size) = 0;
    virtual bool ReadPage(std::uint64_t slot, void* data, std::uint64_t size) = 0;
};

class GuestMemoryModel {
public:
    static constexpr std::uint64_t PageSize = 4096;
    enum Protection : unsigned { NoAccess = 0, ReadAccess = 1, WriteAccess = 2, ExecuteAccess = 4 };
    struct Stats {
        std::uint64_t committed_bytes{};
        std::uint64_t evicted_bytes{};
        std::uint64_t pages_out{};
        std::uint64_t pages_in{};
        std::uint64_t hash_errors{};
        std::uint64_t ceiling_bytes{};
        std::uint64_t free_headroom_bytes{};
    };

    explicit GuestMemoryModel(std::uint64_t begin, std::uint64_t end,
                              std::uint64_t ceiling_bytes,
                              std::unique_ptr<GuestSwapStore> swap_store = {});
    ~GuestMemoryModel();
    GuestMemoryModel(GuestMemoryModel&&) noexcept;
    GuestMemoryModel& operator=(GuestMemoryModel&&) noexcept;
    GuestMemoryModel(const GuestMemoryModel&) = delete;
    GuestMemoryModel& operator=(const GuestMemoryModel&) = delete;

    // First-fit free-range search; address==0 asks for any range.
    bool Reserve(std::uint64_t& address, std::uint64_t size, std::string& error);
    bool Release(std::uint64_t address, std::uint64_t size, std::string& error);
    bool Commit(std::uint64_t address, std::uint64_t size, unsigned protection,
                std::string& error);
    bool Protect(std::uint64_t address, std::uint64_t size, unsigned protection,
                 std::string& error);
    bool Write(std::uint64_t address, const void* bytes, std::uint64_t size,
               std::string& error);
    bool Read(std::uint64_t address, void* bytes, std::uint64_t size,
              std::string& error);
    bool SetPinned(std::uint64_t address, std::uint64_t size, bool pinned,
                   std::string& error);
    std::uint64_t EvictCold(std::uint64_t maximum_bytes);
    [[nodiscard]] Stats GetStats() const noexcept;
    [[nodiscard]] std::string TelemetryLine() const;

private:
    struct Impl;
    Impl* impl_{};
};

} // namespace Core::Xbox
