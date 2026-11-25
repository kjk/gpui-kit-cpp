/* crates/webview/src/windows.rs — a partially visible webview clipped with a
 * window region on wry's host window. The geometry is the portable
 * `WebViewWindowGeometry` in webview.cpp. */

#include "webview/webview.h"

#include <math.h>

namespace gpui {

// The leading entries of ICoreWebView2Controller, up to get_ParentWindow,
// which is all windows.rs asks of the controller. wry_win.cpp declares the
// whole interface; only the slot order matters here, and none of the
// entries before get_ParentWindow is called.
struct WebViewControllerParentWindow : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_IsVisible(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsVisible(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Bounds(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Bounds(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ZoomFactor(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ZoomFactor(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveFocus(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_GotFocus(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_GotFocus(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_LostFocus(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_LostFocus(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND* parentWindow) = 0;
};

// `host_window`: Wry creates the controller with its own child HWND as the
// parent.
static HWND WebViewHostWindow(wry::WebView* raw) {
    auto* controller =
        (WebViewControllerParentWindow*)wry::WebViewControllerRaw(raw);
    HWND hwnd = nullptr;
    if (!controller || FAILED(controller->get_ParentWindow(&hwnd))) {
        return nullptr;
    }
    return hwnd;
}

// user32!GetDpiForWindow is Windows 10 1607 and later, so it is resolved at
// runtime, as window_win.cpp does. 0 when it is missing or fails.
static UINT WebViewHostDpi(HWND hwnd) {
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn getDpiForWindow = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        HMODULE user = GetModuleHandleW(L"user32.dll");
        if (user) {
            getDpiForWindow =
                (GetDpiForWindowFn)GetProcAddress(user, "GetDpiForWindow");
        }
    }
    return getDpiForWindow ? getDpiForWindow(hwnd) : 0;
}

// `NativeClip::apply`. Coordinates are local to the Wry child HWND and are
// in device pixels. On success Windows owns the region; on failure it is
// deleted here.
static bool WebViewApplyClip(WebView* self, HWND hwnd, const int32_t* clip) {
    if (!clip && !self->hasClip && !self->hasVisibleRegion) {
        return true;
    }
    if (clip && self->hasClip && !self->hasVisibleRegion &&
        memcmp(clip, self->clip, sizeof(self->clip)) == 0) {
        return true;
    }
    HRGN region = nullptr;
    if (clip) {
        region = CreateRectRgn(clip[0], clip[1], clip[2], clip[3]);
        if (!region) {
            return false;
        }
    }
    if (SetWindowRgn(hwnd, region, TRUE) == 0) {
        if (region) {
            DeleteObject(region);
        }
        return false;
    }
    if (clip) {
        memcpy(self->clip, clip, sizeof(self->clip));
    }
    self->hasClip = clip != nullptr;
    return true;
}

bool WebViewNativeClipSet(WebView* self, wry::WebView* raw, Bounds document,
                          Bounds visible) {
    HWND hwnd = WebViewHostWindow(raw);
    if (!hwnd) {
        return false;
    }
    UINT dpi = WebViewHostDpi(hwnd);
    if (dpi == 0) {
        return false;
    }
    wry::Rect bounds;
    int32_t clip[4] = {};
    bool clipped = WebViewWindowGeometry(document, visible, (double)dpi / 96.,
                                         &bounds, clip);
    if (!wry::WebViewSetBounds(raw, bounds)) {
        return false;
    }
    return WebViewApplyClip(self, hwnd, clipped ? clip : nullptr);
}

bool WebViewNativeClipClear(WebView* self, wry::WebView* raw) {
    if (!self->hasClip && !self->hasVisibleRegion) {
        return true;
    }
    HWND hwnd = WebViewHostWindow(raw);
    if (!hwnd) {
        return false;
    }
    return WebViewApplyClip(self, hwnd, nullptr);
}

bool WebViewNativeVisibleRegionSet(WebView* self, wry::WebView* raw,
                                   const Bounds* pieces, int count) {
    if (!pieces) {
        return WebViewNativeClipSet(self, raw, self->applied,
                                    self->appliedVisible);
    }
    HWND hwnd = WebViewHostWindow(raw);
    UINT dpi = hwnd ? WebViewHostDpi(hwnd) : 0;
    if (!dpi) return false;
    double scale = (double)dpi / 96.;
    wry::Rect window;
    int32_t clip[4] = {};
    Bounds viewport = self->appliedVisible;
    if (!WebViewWindowGeometry(self->applied, viewport, scale, &window, clip)) {
        clip[2] = (int32_t)window.size.width;
        clip[3] = (int32_t)window.size.height;
    }
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (!region) return false;
    auto px = [&](double value, double origin, double windowOrigin,
                  double extent, int low, int high) {
        if (value <= 0.) return low;
        if (value >= extent) return high;
        int rounded = (int)round((origin + value) * scale) - (int)windowOrigin;
        return std::min(high, std::max(low, rounded));
    };
    bool ok = true;
    for (int i = 0; i < count && ok; i++) {
        const Bounds& part = pieces[i];
        HRGN slice =
            CreateRectRgn(px(part.x, viewport.x, window.position.x, viewport.w,
                             clip[0], clip[2]),
                          px(part.y, viewport.y, window.position.y, viewport.h,
                             clip[1], clip[3]),
                          px(part.Right(), viewport.x, window.position.x,
                             viewport.w, clip[0], clip[2]),
                          px(part.Bottom(), viewport.y, window.position.y,
                             viewport.h, clip[1], clip[3]));
        if (!slice) {
            ok = false;
        } else {
            ok = CombineRgn(region, region, slice, RGN_OR) != ERROR;
            DeleteObject(slice);
        }
    }
    if (!ok || !SetWindowRgn(hwnd, region, TRUE)) {
        DeleteObject(region);
        return false;
    }
    return true;
}

} // namespace gpui
