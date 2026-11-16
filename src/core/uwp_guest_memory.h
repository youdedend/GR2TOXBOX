// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Core::Xbox {

// Guest virtual addresses are 40-bit even when the host uses a wider pointer.
inline constexpr std::uint64_t GuestAddressLimit = std::uint64_t{1} << 40;
[[nodiscard]] bool IsValidGuestRange(std::uint64_t address, std::uint64_t size) noexcept;

// Dependency-free host model for address-space and pager invariants. The UWP
// AddressSpace currently consumes the range validator below; the modeled backing
// and pager are intentionally separate until their Windows section/LocalState
// adapter is wired in. This lets CI test the state machine without a console or
// Windows SDK without claiming that the model is the production pager.
class GuestMemoryModel {
public:
    static constexpr std::uint64_t PageSize = 4096;
    enum Protection : unsigned { NoAccess = 0, ReadAccess = 1, WriteAccess = 2, ExecuteAccess = 4 };
    struct Stats {
        std::uint64_t committed_bytes{};
        std::uint64_t evicted_bytes{};
        std::uint64_t pages_out{};
        std::uint64_t pages_in{};
        std::uint64_t ceiling_bytes{};
    };

    explicit GuestMemoryModel(std::uint64_t begin, std::uint64_t end,
                              std::uint64_t ceiling_bytes);
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
    std::uint64_t EvictCold(std::uint64_t maximum_bytes);
    [[nodiscard]] Stats GetStats() const noexcept;
    [[nodiscard]] std::string TelemetryLine() const;

private:
    struct Impl;
    Impl* impl_{};
};

} // namespace Core::Xbox
