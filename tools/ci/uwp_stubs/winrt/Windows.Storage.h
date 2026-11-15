// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "base.h"
namespace winrt::Windows::Storage {
struct StorageFolder { std::wstring Path() const; };
struct ApplicationData { static ApplicationData Current(); StorageFolder LocalFolder() const; };
}
