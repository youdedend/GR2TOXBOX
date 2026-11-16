#pragma once
#include "d3d12.h"
struct IDXGIObject : IUnknown {};
struct IDXGIFactory6 : IDXGIObject {};
struct IDXGIAdapter4 : IDXGIObject {};
struct IDXGISwapChain4 : IDXGIObject {virtual HRESULT GetBuffer(UINT,const GUID&,void**)=0;virtual UINT GetCurrentBackBufferIndex()=0;};
