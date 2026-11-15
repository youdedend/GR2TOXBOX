// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"
struct ID3D12Device { virtual HRESULT GetDeviceRemovedReason() = 0; };
