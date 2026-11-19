#include "gpui/platform.h"

#include <roapi.h>
#include <windows.ui.viewmanagement.h>

namespace gpui {

bool PlatAutoHideScrollbars() {
    using ABI::Windows::UI::ViewManagement::IUISettings5;

    // Resolve the WinRT entry points from the system DLL so the build does
    // not need a new import library.
    HMODULE combase = GetModuleHandleW(L"combase.dll");
    if (!combase) return false;
    using CreateReference =
        HRESULT(WINAPI*)(PCWSTR, UINT32, HSTRING_HEADER*, HSTRING*);
    using Activate = HRESULT(WINAPI*)(HSTRING, IInspectable**);
    using Initialize = HRESULT(WINAPI*)(RO_INIT_TYPE);
    using Uninitialize = void(WINAPI*)();
    auto createReference = (CreateReference)(void*)GetProcAddress(
        combase, "WindowsCreateStringReference");
    auto activate =
        (Activate)(void*)GetProcAddress(combase, "RoActivateInstance");
    auto initialize =
        (Initialize)(void*)GetProcAddress(combase, "RoInitialize");
    auto uninitialize =
        (Uninitialize)(void*)GetProcAddress(combase, "RoUninitialize");
    if (!createReference || !activate || !initialize || !uninitialize) {
        return false;
    }

    constexpr WCHAR name[] = L"Windows.UI.ViewManagement.UISettings";
    HSTRING_HEADER header = {};
    HSTRING className = nullptr;
    if (FAILED(createReference(name, (UINT32)(ARRAYSIZE(name) - 1), &header,
                               &className))) {
        return false;
    }
    HRESULT hr = initialize(RO_INIT_SINGLETHREADED);
    if (hr == RPC_E_CHANGED_MODE) hr = initialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    IInspectable* object = nullptr;
    IUISettings5* settings = nullptr;
    hr = activate(className, &object);
    if (SUCCEEDED(hr)) {
        hr = object->QueryInterface(IID_PPV_ARGS(&settings));
        object->Release();
    }
    boolean autoHide = false;
    if (SUCCEEDED(hr)) {
        hr = settings->get_AutoHideScrollBars(&autoHide);
        settings->Release();
    }
    uninitialize();
    return SUCCEEDED(hr) && autoHide;
}

} // namespace gpui
