// SPDX-License-Identifier: GPL-2.0-or-later
#include "uwp/blackbox_log.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fileapifromapp.h>
#include <d3d12.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include "common/scm_rev.h"
#if defined(GR2_XBOX_UWP)
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Profile.h>
#endif

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace Gr2Port::Blackbox {
namespace {
constexpr std::size_t PathSize = 1024, RingSize = 64 * 1024;
constexpr std::uint64_t GuestBase = 0x100000000ull, GuestSize = 5ull << 30;
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<HANDLE>::is_always_lock_free);
std::mutex log_mutex;
std::once_flag initialized;
HANDLE local_boot = INVALID_HANDLE_VALUE, usb_boot = INVALID_HANDLE_VALUE;
std::atomic<HANDLE> local_crash{INVALID_HANDLE_VALUE}, usb_crash{INVALID_HANDLE_VALUE};
std::atomic_flag crashing = ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> frame{0}, pso{0}, guest_base{GuestBase}, guest_size{GuestSize};
std::atomic<std::uint32_t> pass{0};
std::atomic<std::uint64_t> pso_count{0};
std::uint64_t module_size = 0;
char ring[RingSize]{};
std::size_t ring_begin = 0, ring_used = 0;
ULONGLONG start_tick = 0, last_probe = 0;

// Stack-only formatting; deliberately no fmt, printf, iostreams or allocation on
// the crash path. Each line is a single kernel WriteFile call per destination.
struct Line {
    char data[2048]{};
    DWORD used = 0;
    void Text(const char* s) noexcept {
        while (*s && used < sizeof(data) - 2) data[used++] = *s++;
    }
    void Hex(std::uint64_t v) noexcept {
        Text("0x");
        for (int n = 60; n >= 0; n -= 4) {
            if (used < sizeof(data) - 2) data[used++] = "0123456789abcdef"[(v >> n) & 15];
        }
    }
    void Dec(std::uint64_t v) noexcept {
        char digits[24]; unsigned count = 0;
        do { digits[count++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        while (count && used < sizeof(data) - 2) data[used++] = digits[--count];
    }
};
void Raw(HANDLE h, const char* p, DWORD n, bool flush) noexcept {
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
    while (n) {
        DWORD written = 0;
        if (!WriteFile(h, p, n, &written, nullptr) || !written) break;
        p += written; n -= written;
    }
    if (flush) FlushFileBuffers(h);
}
void CrashLine(Line& l) noexcept {
    l.Text("\r\n");
    Raw(local_crash.load(std::memory_order_acquire), l.data, l.used, true);
    Raw(usb_crash.load(std::memory_order_acquire), l.data, l.used, true);
}
bool Path(wchar_t (&out)[PathSize], const wchar_t* root, const wchar_t* name) noexcept {
    const auto a = std::wcslen(root), b = std::wcslen(name);
    if (a + b + 2 > PathSize) return false;
    std::wmemcpy(out, root, a); out[a] = L'\\';
    std::wmemcpy(out + a + 1, name, b + 1); return true;
}
HANDLE Open(const wchar_t* path, DWORD mode) noexcept {
    CREATEFILE2_EXTENDED_PARAMETERS params{};
    params.dwSize = sizeof(params);
    // No CRT/user-space buffering. NO_BUFFERING is NOT suitable for variable
    // length text (it requires sector-aligned offsets, addresses and sizes).
    params.dwFileFlags = FILE_FLAG_WRITE_THROUGH;
    return CreateFile2FromAppW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              mode, &params);
}
bool OpenPair(const wchar_t* root, HANDLE& boot, std::atomic<HANDLE>& crash) noexcept {
    wchar_t path[PathSize]{}, previous[PathSize]{};
    if (!Path(path, root, L"gr2toxbox_boot.log") ||
        !Path(previous, root, L"gr2toxbox_prev.log")) return false;
    DeleteFileFromAppW(previous);
    // Missing prior log is expected on the first boot. Never truncate it when
    // rotation fails for another reason (e.g. a removed/read-only USB drive).
    if (!MoveFileFromAppW(path, previous) && GetLastError() != ERROR_FILE_NOT_FOUND) return false;
    HANDLE h = Open(path, CREATE_ALWAYS);
    if (h == INVALID_HANDLE_VALUE) return false;
    boot = h;
    if (Path(path, root, L"gr2toxbox_crash.log")) {
        crash.store(Open(path, CREATE_ALWAYS), std::memory_order_release);
    }
    return true;
}
void Remember(const char* text, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        if (ring_used == RingSize) {
            // Drop a whole oldest line rather than replaying a broken UTF-8 tail.
            do {
                const char ch = ring[ring_begin];
                ring_begin = (ring_begin + 1) % RingSize; --ring_used;
                if (ch == '\n') break;
            } while (ring_used);
        }
        ring[(ring_begin + ring_used++) % RingSize] = text[i];
    }
}
bool Attach(const wchar_t* root) noexcept {
    if (usb_boot != INVALID_HANDLE_VALUE) return true;
    wchar_t directory[PathSize]{};
    if (!Path(directory, root, L"GR2TOXBOX_logs")) return false;
    if (!CreateDirectoryFromAppW(directory, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    if (!OpenPair(directory, usb_boot, usb_crash)) return false;
    const auto first = (std::min)(ring_used, RingSize - ring_begin);
    Raw(usb_boot, ring + ring_begin, static_cast<DWORD>(first), false);
    Raw(usb_boot, ring, static_cast<DWORD>(ring_used - first), true);
    return true;
}
const char* ExceptionName(DWORD code) noexcept {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    default: return nullptr;
    }
}
// Desktop VEH is first-chance; UWP calls this as an SEH filter at explicit
// boundaries. Always continue search: never swallow or relabel recoverable faults.
LONG CALLBACK OnException(EXCEPTION_POINTERS* info) noexcept {
    const auto* record = info->ExceptionRecord;
    const char* name = ExceptionName(record->ExceptionCode);
    if (!name || crashing.test_and_set(std::memory_order_acquire)) return EXCEPTION_CONTINUE_SEARCH;
    const auto module = reinterpret_cast<std::uint64_t>(&__ImageBase);
    Line head;
#if defined(GR2_XBOX_UWP)
    head.Text("SEH_BOUNDARY ");
#else
    head.Text("FIRST_CHANCE ");
#endif
     head.Text(name); head.Text(" code="); head.Hex(record->ExceptionCode);
    head.Text(" thread="); head.Dec(GetCurrentThreadId()); head.Text(" module_base="); head.Hex(module);
    head.Text(" rip="); head.Hex(info->ContextRecord->Rip);
    if (info->ContextRecord->Rip >= module && info->ContextRecord->Rip - module < module_size) {
        head.Text(" rva=+"); head.Hex(info->ContextRecord->Rip - module);
    } else { head.Text(" outside_main_module"); }
    CrashLine(head);
    if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
        && record->NumberParameters >= 2) {
        const auto kind = record->ExceptionInformation[0], va = record->ExceptionInformation[1];
        const auto base = guest_base.load(std::memory_order_relaxed), size = guest_size.load(std::memory_order_relaxed);
        Line fault; fault.Text("access="); fault.Text(kind == 0 ? "Read" : kind == 1 ? "Write" : kind == 8 ? "Execute" : "Unknown");
        fault.Text(" fault_va="); fault.Hex(va); fault.Text(" in_guest_arena=");
        const bool inside = va >= base && va - base < size;
        fault.Text(inside ? "yes guest_offset=" : "no"); if (inside) fault.Hex(va - base);
        CrashLine(fault);
    }
    const CONTEXT& c = *info->ContextRecord;
#define REG(r) { Line l; l.Text(#r "="); l.Hex(c.r); CrashLine(l); }
    REG(Rax) REG(Rbx) REG(Rcx) REG(Rdx) REG(Rsi) REG(Rdi) REG(Rbp) REG(Rsp)
    REG(R8) REG(R9) REG(R10) REG(R11) REG(R12) REG(R13) REG(R14) REG(R15) REG(Rip)
    REG(EFlags) REG(SegCs) REG(SegSs) REG(SegDs) REG(SegEs) REG(SegFs) REG(SegGs)
#undef REG
    Line crumb; crumb.Text("breadcrumbs frame="); crumb.Dec(frame.load(std::memory_order_relaxed));
    crumb.Text(" pass="); crumb.Dec(pass.load(std::memory_order_relaxed));
    crumb.Text(" pso="); crumb.Hex(pso.load(std::memory_order_relaxed)); CrashLine(crumb);
    CONTEXT context = c;
    for (unsigned n = 0; n < 48 && context.Rip; ++n) {
        DWORD64 image = 0;
        const auto fn = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
        Line l; l.Text("frame["); if (n < 10) l.Text("0"); l.Dec(n); l.Text("] rip="); l.Hex(context.Rip);
        if (fn) { l.Text(" image="); l.Hex(image); l.Text(" rva=+"); l.Hex(context.Rip - image); }
        else { l.Text(" JIT/guest-or-leaf (no unwind metadata); stopped safely"); }
        CrashLine(l);
        // Do not dereference arbitrary guest/JIT stack pointers as leaf frames.
        if (!fn) break;
        const auto old_rsp = context.Rsp, old_rip = context.Rip;
        bool valid = true;
#if defined(_MSC_VER)
        __try {
#endif
            PVOID handler_data = nullptr; DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, fn, &context,
                             &handler_data, &establisher, nullptr);
#if defined(_MSC_VER)
        } __except (EXCEPTION_EXECUTE_HANDLER) { valid = false; }
#endif
        if (!valid || context.Rsp <= old_rsp || context.Rip == old_rip) break;
    }
    crashing.clear(std::memory_order_release);
    return EXCEPTION_CONTINUE_SEARCH;
}
class Sink final : public spdlog::sinks::base_sink<std::mutex> {
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        Write(formatted.data(), formatted.size(), msg.log_level >= spdlog::level::warn);
    }
    void flush_() override { Write("", 0, true); }
};
}
void RunGuarded(void (*callback)(void*), void* context) {
#if defined(_MSC_VER) && defined(GR2_XBOX_UWP)
    // This function deliberately has no objects needing C++ unwinding (C2712).
    __try {
        callback(context);
    } __except (OnException(GetExceptionInformation())) {
        // OnException always returns EXCEPTION_CONTINUE_SEARCH.
    }
#else
    callback(context);
#endif
}
void Initialize() noexcept {
    try { std::call_once(initialized, [] {
        start_tick = GetTickCount64();
        wchar_t root[PathSize]{};
#if defined(GR2_XBOX_UWP)
        try {
            const auto path = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
            if (path.size() < PathSize) std::wmemcpy(root, path.c_str(), path.size());
        } catch (...) { /* USB ring still works if LocalState is unavailable. */ }
#else
        // Desktop counterpart: no shell32/GetModuleFileName dependency.
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", root, PathSize) >= PathSize) root[0] = 0;
        wchar_t dir[PathSize]{};
        if (root[0] && Path(dir, root, L"GR2TOXBOX")) {
            CreateDirectoryFromAppW(dir, nullptr); std::wcscpy(root, dir);
        }
#endif
        if (root[0]) OpenPair(root, local_boot, local_crash);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            reinterpret_cast<const char*>(&__ImageBase) + __ImageBase.e_lfanew);
        module_size = nt->OptionalHeader.SizeOfImage;
#if !defined(GR2_XBOX_UWP)
        ULONG guarantee = 32 * 1024;
        SetThreadStackGuarantee(&guarantee);
        AddVectoredExceptionHandler(1, OnException);
#else
        Milestone("crash_capture=SEH_boundaries; no public UWP VEH/stack-guarantee API; worker/stack-overflow capture not guaranteed");
#endif
        Line l; l.Text("boot commit="); l.Text(Common::g_scm_rev);
#if defined(NDEBUG)
        l.Text(" config=Release");
#else
        l.Text(" config=Debug");
#endif
        l.Text(" target=D3D12 FL12_0 SM6.4");
#if defined(GR2_COMPILED_SHADER_COUNT)
        l.Text(" compiled_shaders="); l.Dec(GR2_COMPILED_SHADER_COUNT);
#endif
        l.Text(" pso_created=0"); Write(l.data, l.used, true);
#if defined(GR2_XBOX_UWP)
        try {
            const auto info = winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo();
            const auto family = winrt::to_string(info.DeviceFamily());
            const auto version = winrt::to_string(info.DeviceFamilyVersion());
            Line system; system.Text("OS/device_family="); system.Text(family.c_str());
            system.Text(" version="); system.Text(version.c_str()); system.Text(" memory_ceiling=");
            system.Dec(winrt::Windows::System::MemoryManager::AppMemoryUsageLimit());
            Write(system.data, system.used, true);
        } catch (...) { Milestone("OS/device memory query unavailable"); }
#endif
        ProbeUsb();
    }); } catch (...) { /* Logging must not prevent boot. */ }
}
void Write(const char* text, std::size_t size, bool flush) noexcept {
    try {
        std::lock_guard lock(log_mutex);
        Line prefix; prefix.Text("t="); prefix.Dec(GetTickCount64() - start_tick); prefix.Text("ms ");
        const auto emit = [&](const char* p, std::size_t n) {
            Remember(p, n);
            Raw(local_boot, p, static_cast<DWORD>(n), flush);
            Raw(usb_boot, p, static_cast<DWORD>(n), flush);
        };
        if (size) { emit(prefix.data, prefix.used); emit(text, size); emit("\r\n", 2); }
        else { Raw(local_boot, "", 0, flush); Raw(usb_boot, "", 0, flush); }
    } catch (...) {}
}
void Milestone(const char* text) noexcept { Write(text, std::strlen(text), true); }
void ProbeUsb() noexcept {
    try {
        std::lock_guard lock(log_mutex);
        const auto now = GetTickCount64();
        if (usb_boot != INVALID_HANDLE_VALUE || (last_probe && now - last_probe < 2000)) return;
        last_probe = now;
        wchar_t root[] = L"D:\\";
        for (wchar_t drive = L'D'; drive <= L'Z'; ++drive) {
            root[0] = drive;
            // Do not create folders speculatively on a non-existing drive.
            WIN32_FILE_ATTRIBUTE_DATA attributes{};
            if (GetFileAttributesExFromAppW(root, GetFileExInfoStandard, &attributes) &&
                (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && Attach(root)) break;
        }
    } catch (...) {}
}
void SetGameRoot(const wchar_t* path) noexcept {
    Initialize();
    if (!path) return;
    try {
        const int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        char utf8[PathSize * 3]{};
        if (n > 0 && n <= static_cast<int>(sizeof(utf8))) {
            WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, n, nullptr, nullptr);
            Milestone("game_root follows:"); Milestone(utf8);
        }
        std::lock_guard lock(log_mutex); Attach(path);
    } catch (...) {}
}
void RecordGuestArena(const void* base, std::uint64_t size, bool success) noexcept {
    guest_base.store(reinterpret_cast<std::uint64_t>(base), std::memory_order_relaxed);
    guest_size.store(size, std::memory_order_relaxed);
    Line l; l.Text("guest_arena reservation="); l.Text(success ? "success" : "FAILED");
    l.Text(" base="); l.Hex(reinterpret_cast<std::uint64_t>(base)); l.Text(" bytes="); l.Dec(size);
    Write(l.data, l.used, true);
}
void Frame(std::uint64_t value) noexcept {
    frame.store(value, std::memory_order_relaxed);
    if (value == 1) {
        Line l; l.Text("first submitted frame; startup_pso_count="); l.Dec(pso_count.load(std::memory_order_relaxed));
        Write(l.data, l.used, true);
    }
    ProbeUsb();
}
void Pass(std::uint32_t value) noexcept { pass.store(value, std::memory_order_relaxed); }
void Pso(std::uint64_t value) noexcept { pso.store(value, std::memory_order_relaxed); }
void PsoCreated(std::uint64_t hash) noexcept {
    pso_count.fetch_add(1, std::memory_order_relaxed); Pso(hash);
}
std::shared_ptr<spdlog::sinks::sink> MakeSink() { Initialize(); return std::make_shared<Sink>(); }
} // namespace Gr2Port::Blackbox
void BlackboxRecordDeviceRemoved(ID3D12Device* device) noexcept {
    if (!device) return;
    const HRESULT hr = device->GetDeviceRemovedReason();
    char text[] = "D3D12 GetDeviceRemovedReason=0x00000000";
    constexpr auto start = sizeof(text) - 9;
    for (unsigned i = 0; i < 8; ++i) text[start + i] = "0123456789abcdef"[(static_cast<unsigned>(hr) >> (28 - 4 * i)) & 15];
    Gr2Port::Blackbox::Milestone(text);
}
