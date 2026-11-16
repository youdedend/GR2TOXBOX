#pragma once
#include "Windows.Storage.h"
#include "Windows.Foundation.h"
namespace winrt::Windows::Storage::AccessCache {
struct StorageItemAccessList {
    bool ContainsItem(const wchar_t*) const;
    Foundation::IAsyncOperation<StorageFolder> GetFolderAsync(const wchar_t*) const;
    void AddOrReplace(const wchar_t*,const StorageFolder&) const;
    void Remove(const wchar_t*) const;
};
struct StorageApplicationPermissions {static StorageItemAccessList FutureAccessList();};
}
