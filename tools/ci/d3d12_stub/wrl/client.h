#pragma once
#include <cstddef>
#include "d3d12.h"
namespace Microsoft { namespace WRL {
template <typename T>
class ComPtr {
public:
    T* operator->() const { return ptr_; }
    T* Get() const { return ptr_; }
    T** GetAddressOf() { return &ptr_; }
    void** GetAddressOfVoid() { return reinterpret_cast<void**>(&ptr_); }
    explicit operator bool() const { return ptr_ != nullptr; }
    bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }
    bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
    template <typename U> HRESULT As(U** out) const {
        if (!ptr_) return -1;
        return ptr_->QueryInterface(GUID{}, reinterpret_cast<void**>(out));
    }
    template <typename U> HRESULT As(ComPtr<U>* out) const {
        if (!ptr_) return -1;
        return ptr_->QueryInterface(GUID{}, out->GetAddressOfVoid());
    }
    void Attach(T* p) { ptr_ = p; }
    T* Detach() { T* p = ptr_; ptr_ = nullptr; return p; }
    void Reset() { if (ptr_) ptr_->Release(); ptr_ = nullptr; }
private:
    T* ptr_ = nullptr;
};
}}
