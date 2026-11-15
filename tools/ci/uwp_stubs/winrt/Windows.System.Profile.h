// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "base.h"
namespace winrt::Windows::System::Profile {
struct AnalyticsVersionInfo { std::wstring DeviceFamily() const; std::wstring DeviceFamilyVersion() const; };
struct AnalyticsInfo { static AnalyticsVersionInfo VersionInfo(); };
}
