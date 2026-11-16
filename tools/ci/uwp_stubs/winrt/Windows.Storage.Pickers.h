#pragma once
#include "Windows.Storage.h"
#include "Windows.Foundation.h"
namespace winrt::Windows::Storage::Pickers {
enum class PickerLocationId {ComputerFolder};
struct FileTypes {void Append(const wchar_t*) const;};
struct FolderPicker {
    FolderPicker();void SuggestedStartLocation(PickerLocationId) const;
    FileTypes FileTypeFilter() const;
    Foundation::IAsyncOperation<StorageFolder> PickSingleFolderAsync() const;
};
}
