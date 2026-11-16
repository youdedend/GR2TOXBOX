#pragma once
#include "base.h"
namespace winrt {struct auto_revoke_t {};inline constexpr auto_revoke_t auto_revoke{};}
namespace winrt::Windows::UI::Core {
enum class CoreProcessEventsOption {ProcessAllIfPresent};
struct CoreDispatcher {void ProcessEvents(CoreProcessEventsOption) const;};
struct CoreWindow {
    static CoreWindow GetForCurrentThread(); CoreDispatcher Dispatcher() const;
    struct Revoker {};
    template<class F> Revoker Closed(winrt::auto_revoke_t,F) const {return {};}
};
}
