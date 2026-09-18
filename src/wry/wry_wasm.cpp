/* No wry backend in the browser — wry has none. This is the seam, answering
 * the portable API with "there is no webview here".
 *
 * Part of the C++ port of lb-wry 0.53.3 (see src/wry/readme.md).
 *
 * A page already is a webview, so the thing that would go here is not a port
 * of anything in the crate: it is an `<iframe>` positioned over the gpui
 * canvas through `EM_JS`, the way `window_wasm.cpp` drives the rest of the
 * DOM. Most of the API would then be missing anyway — a cross-origin frame
 * answers no `evaluate_script`, hands over no document title, and serves no
 * custom protocol — so what it would buy is `load_url`, `set_bounds` and
 * `set_visible`, and nothing else here promises those without the rest.
 */
#include "wry/wry.h"

namespace wry {

using base::logf;
using base::Str;

static void Unsupported() {
    static bool said = false;
    if (!said) {
        said = true;
        logf("wry: no webview backend on this platform\n");
    }
}

WebView* WebViewNew(void*, const WebViewAttributes*, bool) {
    Unsupported();
    return nullptr;
}
void WebViewFree(WebView*) {}
Str WebViewId(WebView*) {
    return {};
}
bool WebViewEval(WebView*, Str) {
    return false;
}
bool WebViewEvalWithCallback(WebView*, Str, void*, void (*)(void*, Str)) {
    return false;
}
Str WebViewUrlTemp(WebView*) {
    return {};
}
bool WebViewLoadUrl(WebView*, Str) {
    return false;
}
bool WebViewLoadUrlWithHeaders(WebView*, Str, const Header*, int) {
    return false;
}
bool WebViewLoadHtml(WebView*, Str) {
    return false;
}
bool WebViewReload(WebView*) {
    return false;
}
bool WebViewBounds(WebView*, Rect*) {
    return false;
}
bool WebViewSetBounds(WebView*, Rect) {
    return false;
}
bool WebViewSetVisible(WebView*, bool) {
    return false;
}
bool WebViewFocus(WebView*) {
    return false;
}
bool WebViewFocusParent(WebView*) {
    return false;
}
bool WebViewZoom(WebView*, double) {
    return false;
}
bool WebViewSetBackgroundColor(WebView*, Rgba) {
    return false;
}
bool WebViewSetTheme(WebView*, Theme) {
    return false;
}
bool WebViewSetMemoryUsageLevel(WebView*, MemoryUsageLevel) {
    return false;
}
bool WebViewReparent(WebView*, void*) {
    return false;
}
bool WebViewSetTrafficLightInset(WebView*, Position) {
    return false;
}
bool WebViewPrint(WebView*) {
    return false;
}
bool WebViewClearAllBrowsingData(WebView*) {
    return false;
}
bool WebViewCookies(WebView*, Vec<Cookie>* out) {
    CookieListFree(out);
    return false;
}
bool WebViewCookiesForUrl(WebView*, Str, Vec<Cookie>* out) {
    CookieListFree(out);
    return false;
}
bool WebViewSetCookie(WebView*, const Cookie*) {
    return false;
}
bool WebViewDeleteCookie(WebView*, const Cookie*) {
    return false;
}
void WebViewOpenDevtools(WebView*) {}
void WebViewCloseDevtools(WebView*) {}
bool WebViewIsDevtoolsOpen(WebView*) {
    return false;
}
void Respond(RequestResponder*, const Response*) {}

Str WebViewVersionTemp() {
    return {};
}
bool WebViewAvailable() {
    return false;
}

} // namespace wry
