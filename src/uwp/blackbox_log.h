// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
struct ID3D12Device;
namespace spdlog { namespace sinks { class sink; } }
namespace Gr2Port::Blackbox {
// Call once, before SDL/D3D12. Files remain open until process exit so the crash
// path never races a closed/reused handle. Does not reserve or commit guest RAM.
void Initialize() noexcept;
// UWP has no public VEH API. This legal SEH boundary logs faults escaping the
// callback and continues exception search; it neither handles nor hides them.
// Only the current thread/callback is covered (not arbitrary worker threads).
void RunGuarded(void (*callback)(void*), void* context);
void Write(const char* text, std::size_t size, bool flush = false) noexcept;
void Milestone(const char* text) noexcept;
// Called when a user-selected game folder becomes available. Normal-thread only.
void SetGameRoot(const wchar_t* path) noexcept;
void ProbeUsb() noexcept;
void RecordGuestArena(const void* base, std::uint64_t size, bool success) noexcept;
void Frame(std::uint64_t frame) noexcept;
void Pass(std::uint32_t pass) noexcept;
void Pso(std::uint64_t hash) noexcept;
void PsoCreated(std::uint64_t hash) noexcept;
std::shared_ptr<spdlog::sinks::sink> MakeSink();
}
// Safe on the ordinary error path, not called from the exception handler.
void BlackboxRecordDeviceRemoved(ID3D12Device* device) noexcept;
