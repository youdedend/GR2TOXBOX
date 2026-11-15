// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
namespace winrt {
struct hresult_error { std::wstring message() const; };
std::string to_string(const std::wstring&);
namespace Windows::ApplicationModel::Core {
struct CoreApplication { static bool MainView(); };
}
namespace Windows::Graphics::Display {
struct DisplayInformation {
    static DisplayInformation GetForCurrentView();
    explicit operator bool() const;
    int ResolutionScale() const;
};
namespace Core {
struct HdmiDisplayMode {
    explicit operator bool() const;
    unsigned ResolutionWidthInRawPixels() const;
    unsigned ResolutionHeightInRawPixels() const;
};
struct HdmiDisplayInformation {
    static HdmiDisplayInformation GetForCurrentView();
    explicit operator bool() const;
    HdmiDisplayMode GetCurrentDisplayMode() const;
};
}
}
}
