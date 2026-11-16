#pragma once
#include "base.h"
#include <cstddef>
namespace winrt::Windows::Foundation {
enum class AsyncStatus {Started,Completed,Canceled,Error};
template<class T> struct IAsyncOperation {
    IAsyncOperation(std::nullptr_t); explicit operator bool() const;
    IAsyncOperation& operator=(std::nullptr_t);
    AsyncStatus Status() const; T GetResults() const; void Cancel() const;
};
}
