// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
using HRESULT = std::int32_t;
#define SUCCEEDED(hr) ((hr) >= 0)
#define FAILED(hr) ((hr) < 0)
namespace ABI::Windows::Foundation { struct Rect { float X, Y, Width, Height; }; }
namespace ABI::Windows::UI::Core {
struct ICoreWindow {
    virtual HRESULT get_Bounds(ABI::Windows::Foundation::Rect*) = 0;
};
}
struct DXGI_MATRIX_3X2_F { float _11, _12, _21, _22, _31, _32; };
struct IDXGISwapChain2 {
    virtual HRESULT SetMatrixTransform(const DXGI_MATRIX_3X2_F*) = 0;
    virtual HRESULT SetSourceSize(unsigned, unsigned) = 0;
};
