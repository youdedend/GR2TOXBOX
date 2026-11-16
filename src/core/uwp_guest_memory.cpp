// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/uwp_guest_memory.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <sys/types.h>
#endif

namespace Core::Xbox {
namespace {
constexpr std::uint64_t AlignUp(std::uint64_t n) { return (n + GuestMemoryModel::PageSize - 1) & ~(GuestMemoryModel::PageSize - 1); }
constexpr std::uint64_t AlignDown(std::uint64_t n) { return n & ~(GuestMemoryModel::PageSize - 1); }
std::uint64_t HashPage(const std::uint8_t* data) {
    std::uint64_t h = 14695981039346656037ull;
    for (std::uint64_t i = 0; i < GuestMemoryModel::PageSize; ++i) h = (h ^ data[i]) * 1099511628211ull;
    return h;
}
bool SeekSwap(std::FILE* file, std::uint64_t offset) {
#if defined(_WIN32)
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}
} // namespace

bool IsValidGuestRange(std::uint64_t address, std::uint64_t size) noexcept {
    return address != 0 && size != 0 && address < GuestAddressLimit &&
           size <= GuestAddressLimit - address;
}

struct GuestMemoryModel::Impl {
    struct Page {
        std::vector<std::uint8_t> resident;
        std::uint64_t swap_offset{};
        std::uint64_t hash{};
        unsigned protection{};
        bool committed{};
        bool evicted{};
        bool hash_valid{};
    };
    struct Region { std::uint64_t size{}; std::vector<Page> pages; };
    std::uint64_t begin{}, end{}, ceiling{}, committed{}, evicted{}, out{}, in{};
    std::map<std::uint64_t, Region> regions;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> free_ranges;
    std::FILE* swap{};

    Impl(std::uint64_t b, std::uint64_t e, std::uint64_t c)
        : begin(b), end(e), ceiling(std::min(c, std::uint64_t{5119} * 1024 * 1024)) {
        if (b < e && b % PageSize == 0 && e % PageSize == 0 && IsValidGuestRange(b, e - b))
            free_ranges.emplace_back(b, e);
    }
    ~Impl() { if (swap) std::fclose(swap); }

    auto Find(std::uint64_t address) { return regions.upper_bound(address) == regions.begin() ? regions.end() : std::prev(regions.upper_bound(address)); }
    bool Covered(std::uint64_t a, std::uint64_t n) {
        if (!IsValidGuestRange(a, n) || a % PageSize || n % PageSize) return false;
        auto it = Find(a);
        if (it == regions.end() || a < it->first) return false;
        const auto region_end = it->first + it->second.size;
        return a < region_end && n <= region_end - a;
    }
    bool PageIn(Page& p, std::string& error) {
        if (!p.evicted) return true;
        if (!swap) { error = "Guest paging backing store is unavailable"; return false; }
        p.resident.resize(PageSize);
        if (!SeekSwap(swap, p.swap_offset) || std::fread(p.resident.data(), 1, PageSize, swap) != PageSize) {
            error = "Could not page in a guest memory page"; p.resident.clear(); return false;
        }
        ++in;
        if (p.hash_valid && HashPage(p.resident.data()) != p.hash) {
            error = "Guest memory swap integrity check failed"; p.resident.clear(); return false;
        }
        p.evicted = false; p.hash_valid = false; evicted -= PageSize; committed += PageSize;
        return true;
    }
};

GuestMemoryModel::GuestMemoryModel(std::uint64_t begin, std::uint64_t end, std::uint64_t ceiling)
    : impl_(new Impl(begin, end, ceiling)) {}
GuestMemoryModel::~GuestMemoryModel() { delete impl_; }
GuestMemoryModel::GuestMemoryModel(GuestMemoryModel&& other) noexcept : impl_(std::exchange(other.impl_, nullptr)) {}
GuestMemoryModel& GuestMemoryModel::operator=(GuestMemoryModel&& other) noexcept {
    if (this != &other) { delete impl_; impl_ = std::exchange(other.impl_, nullptr); }
    return *this;
}

bool GuestMemoryModel::Reserve(std::uint64_t& address, std::uint64_t size, std::string& error) {
    if (!impl_ || !size || size % PageSize) { error = "Guest reservation size must be a non-zero page multiple"; return false; }
    if (address) {
        if (!IsValidGuestRange(address, size)) { std::ostringstream s; s << "Guest address 0x" << std::hex << address << " is outside the 40-bit address space"; error = s.str(); return false; }
        if (address % PageSize) { error = "Guest reservation address is not page-aligned"; return false; }
        for (auto it = impl_->free_ranges.begin(); it != impl_->free_ranges.end(); ++it) {
            if (address >= it->first && address - it->first <= it->second - it->first && size <= it->second - address) {
                const auto start = it->first, end = it->second;
                it = impl_->free_ranges.erase(it);
                auto pos = it;
                if (start < address) pos = impl_->free_ranges.insert(pos, {start, address}) + 1;
                if (address + size < end) impl_->free_ranges.insert(pos, {address + size, end});
                impl_->regions.emplace(address, Impl::Region{size, std::vector<Impl::Page>(size / PageSize)}); return true;
            }
        }
        error = "Requested guest address range is not free"; return false;
    }
    for (auto it = impl_->free_ranges.begin(); it != impl_->free_ranges.end(); ++it) {
        if (it->second - it->first < size) continue;
        address = it->first; it->first += size;
        if (it->first == it->second) impl_->free_ranges.erase(it);
        impl_->regions.emplace(address, Impl::Region{size, std::vector<Impl::Page>(size / PageSize)}); return true;
    }
    error = "Guest address space has no free range large enough"; return false;
}

bool GuestMemoryModel::Release(std::uint64_t address, std::uint64_t size, std::string& error) {
    if (!impl_ || !impl_->Covered(address, size)) { std::ostringstream s; s << "Invalid guest range at 0x" << std::hex << address; error = s.str(); return false; }
    auto it = impl_->Find(address);
    if (address != it->first || size != it->second.size) { error = "Release must match a complete reservation"; return false; }
    for (const auto& p : it->second.pages) if (p.committed) {
        if (p.evicted) impl_->evicted -= PageSize; else impl_->committed -= PageSize;
    }
    impl_->regions.erase(it); impl_->free_ranges.emplace_back(address, address + size);
    std::sort(impl_->free_ranges.begin(), impl_->free_ranges.end());
    std::vector<std::pair<std::uint64_t, std::uint64_t>> merged;
    for (auto r : impl_->free_ranges) {
        if (!merged.empty() && merged.back().second == r.first) merged.back().second = r.second;
        else merged.push_back(r);
    }
    impl_->free_ranges.swap(merged); return true;
}

bool GuestMemoryModel::Commit(std::uint64_t address, std::uint64_t size, unsigned protection, std::string& error) {
    if (!impl_ || !impl_->Covered(address, size) || protection == NoAccess ||
        ((protection & WriteAccess) && (protection & ExecuteAccess))) {
        std::ostringstream s; s << "Invalid or W^X-violating guest commit at 0x" << std::hex << address; error = s.str(); return false;
    }
    if (size > impl_->ceiling - std::min(impl_->ceiling, impl_->committed)) {
        error = "Guest memory ceiling reached; reduce the guest memory allocation or lower graphics load."; return false;
    }
    auto it = impl_->Find(address); auto first = (address - it->first) / PageSize; auto count = size / PageSize;
    for (std::uint64_t i = first; i < first + count; ++i) {
        auto& p = it->second.pages[i];
        if (!p.committed) {
            p.resident.assign(PageSize, 0); p.committed = true; impl_->committed += PageSize;
        } else if (p.evicted && !impl_->PageIn(p, error)) return false;
        p.protection = protection;
    }
    return true;
}

bool GuestMemoryModel::Protect(std::uint64_t address, std::uint64_t size, unsigned protection, std::string& error) {
    if (!impl_ || !impl_->Covered(address, size) ||
        ((protection & WriteAccess) && (protection & ExecuteAccess))) {
        std::ostringstream s; s << "Invalid or W^X-violating guest protection range at 0x" << std::hex << address; error = s.str(); return false;
    }
    auto it = impl_->Find(address); auto first = (address - it->first) / PageSize; auto count = size / PageSize;
    for (std::uint64_t i = first; i < first + count; ++i) {
        auto& p = it->second.pages[i];
        if (p.protection && p.evicted && !impl_->PageIn(p, error)) return false;
        p.protection = protection;
    }
    return true;
}

bool GuestMemoryModel::Write(std::uint64_t address, const void* bytes, std::uint64_t size, std::string& error) {
    if (!bytes || !size || !impl_ || !IsValidGuestRange(address, size)) { error = "Invalid guest write address"; return false; }
    auto it = impl_->Find(address); if (it == impl_->regions.end() || address < it->first || address >= it->first + it->second.size || size > it->first + it->second.size - address) { error = "Guest write is outside a reservation"; return false; }
    const auto* src = static_cast<const std::uint8_t*>(bytes); std::uint64_t done = 0;
    while (done < size) {
        const auto off = address + done - it->first, pi = off / PageSize, within = off % PageSize, n = std::min(size - done, PageSize - within);
        auto& p = it->second.pages[pi]; if (!p.committed || !(p.protection & WriteAccess) || (p.protection & ExecuteAccess)) { error = "Guest write refused by page protection"; return false; }
        if (p.evicted && !impl_->PageIn(p, error)) return false;
        std::memcpy(p.resident.data() + within, src + done, n); done += n;
    }
    return true;
}

bool GuestMemoryModel::Read(std::uint64_t address, void* bytes, std::uint64_t size, std::string& error) {
    if (!bytes || !size || !impl_ || !IsValidGuestRange(address, size)) { error = "Invalid guest read address"; return false; }
    auto it = impl_->Find(address); if (it == impl_->regions.end() || address < it->first || address >= it->first + it->second.size || size > it->first + it->second.size - address) { error = "Guest read is outside a reservation"; return false; }
    auto* dst = static_cast<std::uint8_t*>(bytes); std::uint64_t done = 0;
    while (done < size) {
        const auto off = address + done - it->first, pi = off / PageSize, within = off % PageSize, n = std::min(size - done, PageSize - within);
        auto& p = it->second.pages[pi]; if (!p.committed || (!(p.protection & ReadAccess) && !(p.protection & ExecuteAccess))) { error = "Guest read refused by page protection"; return false; }
        if (p.evicted && !impl_->PageIn(p, error)) return false;
        std::memcpy(dst + done, p.resident.data() + within, n); done += n;
    }
    return true;
}

std::uint64_t GuestMemoryModel::EvictCold(std::uint64_t maximum_bytes) {
    if (!impl_ || maximum_bytes < PageSize) return 0;
    if (!impl_->swap) impl_->swap = std::tmpfile(); // unlinked/delete-on-close host backing
    if (!impl_->swap) return 0;
    std::uint64_t released = 0, slot = 0;
    for (auto& [base, region] : impl_->regions) {
        (void)base;
        for (auto& p : region.pages) {
            if (released >= maximum_bytes) return released;
            if (!p.committed || p.evicted || !(p.protection & ReadAccess) || (p.protection & ExecuteAccess)) continue;
            if (!SeekSwap(impl_->swap, slot) || std::fwrite(p.resident.data(), 1, PageSize, impl_->swap) != PageSize || std::fflush(impl_->swap) != 0) continue;
            p.swap_offset = slot; slot += PageSize; p.hash = HashPage(p.resident.data()); p.hash_valid = true;
            p.resident.clear(); p.resident.shrink_to_fit(); p.evicted = true;
            impl_->committed -= PageSize; impl_->evicted += PageSize; ++impl_->out; released += PageSize;
        }
    }
    return released;
}

GuestMemoryModel::Stats GuestMemoryModel::GetStats() const noexcept {
    if (!impl_) return {};
    return {impl_->committed, impl_->evicted, impl_->out, impl_->in, impl_->ceiling};
}
std::string GuestMemoryModel::TelemetryLine() const {
    const auto s = GetStats(); std::ostringstream out;
    out << "Guest memory: committed=" << (s.committed_bytes >> 20) << " MiB, evicted=" << (s.evicted_bytes >> 20)
        << " MiB, pages out=" << s.pages_out << ", pages in=" << s.pages_in << ", ceiling=" << (s.ceiling_bytes >> 20) << " MiB";
    return out.str();
}

} // namespace Core::Xbox
