// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The two desktop shell calls that end up in the packaged build's object files.
//
// CommandLineToArgvW is shell32 and GetAsyncKeyState is a user32 call that reads
// global key state, which a packaged app may not do. Neither is used for anything
// on the console, but both are *referenced*: CLI11 calls CommandLineToArgvW from
// compute_win32_argv(), which its no-argument parse() overload uses, and the
// screenshot path calls GetAsyncKeyState. A reference is enough to fail the
// compile and then the link.
//
// So this header supplies both for GR2_XBOX_UWP:
//
//   * CommandLineToArgvW splits the command line with the documented rules
//     (whitespace separates, quotes group, a run of backslashes before a quote
//     escapes it). Implemented rather than stubbed because a wrong split silently
//     truncates an argument.
//   * GetAsyncKeyState reports "not held". The console's screenshot chord comes
//     through the gamepad path (src/uwp/chords.cpp), which never asks this.

#pragma once

#if defined(GR2_XBOX_UWP)

#include <windows.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace WinCompat {

/// Splits `command_line` into arguments, with CommandLineToArgvW's own rules.
///
/// The return value is one LocalAlloc block holding the string data, with the
/// pointer table carved off the front of it -- which is what the real call does,
/// and it matters here: CLI11 owns the result with a unique_ptr whose deleter is
/// LocalFree, so returning anything else (new[], say) would be a heap mismatch.
inline wchar_t** CommandLineToArgvW(const wchar_t* command_line, int* argument_count) {
    const wchar_t* p = command_line != nullptr ? command_line : L"";

    // Collected first, then laid out once, so the size is known before allocating.
    // std::vector<wchar_t> per argument would be simpler and would cost one
    // allocation per argument; this is called at most once per process.
    // The arguments are contiguous in the source only when there are no quotes, so
    // the general case needs a copy. Build the text in a scratch buffer and record
    // each argument's span within it.
    std::vector<wchar_t> scratch;
    std::vector<std::pair<std::size_t, std::size_t>> spans; // [start, end) in scratch

    while (*p != L'\0') {
        while (*p == L' ' || *p == L'\t') {
            ++p;
        }
        if (*p == L'\0') {
            break;
        }
        const std::size_t start = scratch.size();
        bool in_quotes = false;
        int backslashes = 0;
        for (; *p != L'\0'; ++p) {
            if (*p == L'\\') {
                ++backslashes;
                continue;
            }
            if (*p == L'"') {
                // 2n backslashes then a quote means n backslashes and a literal
                // quote; 2n+1 means n backslashes and the end (or start) of a
                // quoted run.
                scratch.insert(scratch.end(), static_cast<std::size_t>(backslashes / 2), L'\\');
                if ((backslashes % 2) != 0) {
                    scratch.push_back(L'"');
                } else {
                    in_quotes = !in_quotes;
                }
                backslashes = 0;
                continue;
            }
            scratch.insert(scratch.end(), static_cast<std::size_t>(backslashes), L'\\');
            backslashes = 0;
            if (!in_quotes && (*p == L' ' || *p == L'\t')) {
                break;
            }
            scratch.push_back(*p);
        }
        scratch.insert(scratch.end(), static_cast<std::size_t>(backslashes), L'\\');
        spans.emplace_back(start, scratch.size());
    }

    // Lay out: the pointer table, then each argument's text with its terminator.
    const std::size_t count = spans.size();
    std::size_t text_chars = 0;
    for (const auto& span : spans) {
        text_chars += (span.second - span.first) + 1; // + terminator
    }

    const std::size_t table_bytes = (count + 1) * sizeof(wchar_t*);
    const std::size_t text_bytes = text_chars * sizeof(wchar_t);
    auto* block = static_cast<std::byte*>(
        LocalAlloc(LMEM_FIXED, table_bytes + text_bytes));
    if (block == nullptr) {
        if (argument_count != nullptr) {
            *argument_count = 0;
        }
        return nullptr;
    }

    auto** table = reinterpret_cast<wchar_t**>(block);
    auto* text = reinterpret_cast<wchar_t*>(block + table_bytes);
    std::size_t at = 0;
    for (std::size_t i = 0; i < count; ++i) {
        table[i] = text + at;
        for (std::size_t c = spans[i].first; c < spans[i].second; ++c) {
            text[at++] = scratch[c];
        }
        text[at++] = L'\0';
    }
    table[count] = nullptr;

    if (argument_count != nullptr) {
        *argument_count = static_cast<int>(count);
    }
    return table;
}

/// Not held. A packaged app cannot read global key state; see the note above.
inline SHORT GetAsyncKeyState(int /*virtual_key*/) {
    return 0;
}

} // namespace WinCompat

// CLI11 and the screenshot path name these unqualified, so bring them into scope
// exactly as <windows.h> would have. Deliberately not `using namespace WinCompat`:
// only these two names are being stood in for.
using WinCompat::CommandLineToArgvW;
using WinCompat::GetAsyncKeyState;

#endif // GR2_XBOX_UWP
