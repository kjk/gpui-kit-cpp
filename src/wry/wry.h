/* wry/src/lib.rs — the public API of the `wry` crate.
 *
 * `src/wry/` is a C++ port of lb-wry 0.53.3, the webview crate
 * gpui-kit's `crates/webview` (`gpui-wry`) drives. See
 * src/wry/readme.md for the file-for-file map and for what is deliberately
 * not ported, and cmd/versions.ts (`wry`) for the pinned version.
 *
 *     wry::WebViewAttributes attrs;
 *     attrs.url = StrL("https://example.com");
 *     wry::WebView* wv = wry::WebViewNew(hwnd, &attrs, true);
 *     wry::WebViewSetBounds(wv, {{0, 0, true}, {800, 600, true}});
 *
 * Like src/taffy and src/markdown this is a port of a crate that has never
 * heard of gpui: it is written against base.h and its own header, and names
 * no gpui type. The parent window arrives as an opaque handle — Rust takes a
 * `raw_window_handle::HasWindowHandle`, here it is the `HWND` (or `NSView*`,
 * or GTK container) as a `void*`.
 *
 * Three shapes of the Rust do not survive the crossing, and each is written
 * out rather than approximated:
 *
 *   - `Result<T>`. There are no exceptions here, so every call that can fail
 *     answers `bool` (or null) and logs what went wrong through `logf`. The
 *     error *kinds* of `error.rs` are not modelled: nothing in the crate
 *     branches on one.
 *   - A closure that captures. Every handler is a plain function pointer plus
 *     the one `void*` its group carries, which is what `Func1` would give and
 *     what the tree writes anywhere a callback takes more than one argument.
 *   - The builder. `WebViewBuilder` is Rust's way of filling in
 *     `WebViewAttributes` one `with_*` at a time; a struct with defaults says
 *     the same thing in C++, so `WebViewAttributes` *is* the builder and
 *     `WebViewNew` is `build()` / `build_as_child()`.
 */

#ifndef GPUI_WRY_WRY_H_
#define GPUI_WRY_WRY_H_

#include "base.h"

namespace wry {

using base::Str;
using base::Vec;

// ─── dpi ─────────────────────────────────────────────────────────────────
//
// wry takes its geometry from the `dpi` crate, where a position or a size is
// either logical (DIPs, scaled by the window's DPI on the way in) or physical
// (device pixels, used as they are). Rust models that as an enum over two
// generic structs; here the discriminant is the `logical` field, since both
// arms hold the same two numbers.

struct Position {
    double x = 0;
    double y = 0;
    bool logical = true;
};

struct Size {
    double width = 0;
    double height = 0;
    bool logical = true;
};

/** `wry::Rect` — where a child webview sits inside its parent. */
struct Rect {
    Position position;
    Size size;
};

inline Position LogicalPosition(double x, double y) {
    return Position{x, y, true};
}
inline Position PhysicalPosition(double x, double y) {
    return Position{x, y, false};
}
inline Size LogicalSize(double w, double h) {
    return Size{w, h, true};
}
inline Size PhysicalSize(double w, double h) {
    return Size{w, h, false};
}

// ─── small types ─────────────────────────────────────────────────────────

/** `wry::RGBA`, which Rust spells as a `(u8, u8, u8, u8)` tuple. */
struct Rgba {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

/** `wry::Theme` — what `prefers-color-scheme` reports to the page. */
enum class Theme : uint8_t {
    Dark,
    Light,
    Auto,
};

/** `wry::PageLoadEvent`. */
enum class PageLoadEvent : uint8_t {
    Started,
    Finished,
};

/** `webview2::ScrollBarStyle` — Windows only, ignored elsewhere. */
enum class ScrollBarStyle : uint8_t {
    Default,
    FluentOverlay,
};

/** `wry::MemoryUsageLevel` — Windows only. */
enum class MemoryUsageLevel : uint8_t {
    Normal,
    Low,
};

/** `wry::BackgroundThrottlingPolicy`. Only WebKit currently honours it,
    and only on macOS 14 or later. */
enum class BackgroundThrottlingPolicy : uint8_t {
    Disabled,
    Suspend,
    Throttle,
};

/** `proxy.rs`: `ProxyConfig` and its endpoint, folded into one struct
    because `ProxyKind::None` is what `Option<ProxyConfig>` said. */
enum class ProxyKind : uint8_t {
    None,
    Http,
    Socks5,
};

struct ProxyConfig {
    ProxyKind kind = ProxyKind::None;
    Str host;
    Str port;
};

/** `wry::InitializationScript`. */
struct InitializationScript {
    Str script;
    // Windows always injects into subframes as well; the field is here
    // because the API has it and the other backends honour it.
    bool forMainFrameOnly = true;
};

/** One HTTP header, as `http::HeaderMap` carries it. */
struct Header {
    Str name;
    Str value;
};

/** `http::Request<Vec<u8>>` as a custom protocol handler receives it. The
    strings and the body belong to the caller and last only for the call. */
struct Request {
    Str method;
    Str uri;
    const Header* headers = nullptr;
    int headerCount = 0;
    const uint8_t* body = nullptr;
    int bodyLen = 0;
};

/** `http::Response<Cow<'static, [u8]>>` as a handler answers with. The body
    and the header strings are read before `Respond` returns. */
struct Response {
    int status = 200;
    const Header* headers = nullptr;
    int headerCount = 0;
    const uint8_t* body = nullptr;
    int bodyLen = 0;
};

/** `wry::RequestAsyncResponder`. A handler either answers inside the call or
    keeps the responder and answers later — from another thread if it likes,
    which is why `Respond` hops to the thread that owns the webview. Exactly
    one `Respond` per responder; not answering leaves the request hanging. */
struct RequestResponder;
void Respond(RequestResponder* responder, const Response* response);

/** An entry of `WebViewAttributes::custom_protocols`. `id` is the webview's
    own id, which is what Rust passes as the first argument. */
struct CustomProtocol {
    Str name;
    void* ctx = nullptr;
    void (*handler)(void* ctx, Str id, const Request* request,
                    RequestResponder* responder) = nullptr;
};

struct WebView;

/** `wry::NewWindowResponse`. For Create, the handler writes the target
    WebView through its final out-parameter. */
enum class NewWindowResponse : uint8_t {
    Allow,
    Create,
    Deny,
};

/** `wry::NewWindowFeatures`. `opener` supplies the source WebView and its
    environment through `WebViewEnvironmentRaw`; `hasPosition` / `hasSize`
    are Rust's `Option`. */
struct NewWindowFeatures {
    bool hasPosition = false;
    double x = 0;
    double y = 0;
    bool hasSize = false;
    double width = 0;
    double height = 0;
    WebView* opener = nullptr;
    /** macOS `NewWindowOpener::target_configuration`, borrowed during the
        callback and accepted by `webviewConfiguration`. Null on Windows. */
    void* targetConfiguration = nullptr;
};

/** `with_download_started_handler`. `path` starts as WebView2's suggested
    absolute UTF-8 path and may be replaced for the duration of the call.
    Return true to accept the download at that path, false to cancel it. */
using DownloadStartedHandler = bool (*)(void* ctx, Str url, Str* path);

/** `with_download_completed_handler`. `path` is null unless WebView2 reports
    a successfully completed download, matching Rust's `Option<PathBuf>`.
    All strings are borrowed for the duration of the call. */
using DownloadCompletedHandler = void (*)(void* ctx, Str url, const Str* path,
                                          bool success);

/** `wry::DragDropEvent`. Paths are present for Enter and Drop and borrowed
    only for the callback. Position is relative to the WebView2 child window. */
enum class DragDropKind : uint8_t {
    Enter,
    Over,
    Drop,
    Leave,
};

struct DragDropEvent {
    DragDropKind kind = DragDropKind::Leave;
    const Str* paths = nullptr;
    int pathCount = 0;
    int32_t x = 0;
    int32_t y = 0;
};

using DragDropHandler = bool (*)(void* ctx, const DragDropEvent* event);

/** The `cookie::Cookie` fields WebView exposes, without depending on the Rust
    cookie/time crates. Strings returned by a query are heap-owned and freed
    together with the list by `CookieListFree`; input strings are borrowed. */
enum class CookieSameSite : uint8_t {
    None,
    Lax,
    Strict,
};

struct Cookie {
    Str name;
    Str value;
    Str domain;
    Str path;
    bool hasHttpOnly = false;
    bool httpOnly = false;
    bool hasSecure = false;
    bool secure = false;
    bool hasSameSite = false;
    CookieSameSite sameSite = CookieSameSite::Lax;
    bool session = true;
    bool hasExpires = false;
    int64_t expiresUnixSeconds = 0;
    bool hasMaxAge = false;
    int64_t maxAgeSeconds = 0;
};

void CookieListFree(Vec<Cookie>* cookies);

// ─── attributes ──────────────────────────────────────────────────────────

/** `wry::WebViewAttributes` and, below the line, the Windows half of
    `WebViewBuilderExtWindows`. Every default here is the one
    `WebViewAttributes::default()` sets.

    Nothing in this struct is copied until `WebViewNew`, and only what a
    backend keeps outlives it: the strings and vectors must stay alive for
    that call, and the handler `ctx` for as long as the webview does. For a
    Rust `Option<String>`, a null `Str::s` is `None`; a present empty `Str` is
    `Some("")`. */
struct WebViewAttributes {
    static bool AllowDownload(void*, Str, Str*) { return true; }

    /** Passed to a custom protocol handler; defaults to the container HWND
        written out, the way Rust defaults it. */
    Str id;
    /** `WebContext::new(data_directory)`. On Windows a web context is its
        data directory and nothing else, so the directory is the field. */
    Str dataDirectory;
    Str userAgent;
    bool visible = true;
    bool transparent = false;
    bool hasBackgroundColor = false;
    Rgba backgroundColor;
    /** Loaded if set. Data URLs are not supported; use `html`. */
    Str url;
    /** Headers for `url`, if any. */
    const Header* headers = nullptr;
    int headerCount = 0;
    /** Loaded if `url` is empty. */
    Str html;
    bool zoomHotkeysEnabled = false;
    const InitializationScript* initializationScripts = nullptr;
    int initializationScriptCount = 0;
    const CustomProtocol* customProtocols = nullptr;
    int customProtocolCount = 0;

    /** The `void*` every handler below is called with. */
    void* ctx = nullptr;
    /** `window.ipc.postMessage(s)` from the page. `url` is the source. */
    void (*ipcHandler)(void* ctx, Str url, Str body) = nullptr;
    /** Return false to cancel the navigation. */
    bool (*navigationHandler)(void* ctx, Str url) = nullptr;
    void (*documentTitleChangedHandler)(void* ctx, Str title) = nullptr;
    void (*onPageLoadHandler)(void* ctx, PageLoadEvent event,
                              Str url) = nullptr;
    /** Wry's default closure accepts every download at its suggested path. */
    DownloadStartedHandler downloadStartedHandler = AllowDownload;
    DownloadCompletedHandler downloadCompletedHandler = nullptr;
    DragDropHandler dragDropHandler = nullptr;
    /** `window.open`. Null denies every request, which is what wry's
        `NewWindowRequested` handler does when no closure is set. */
    NewWindowResponse (*newWindowReqHandler)(
        void* ctx, Str url, const NewWindowFeatures* features,
        WebView** createdWebView) = nullptr;

    bool clipboard = false;
#if defined(DEBUG) || defined(_DEBUG)
    bool devtools = true;
#else
    bool devtools = false;
#endif
    /** macOS only; kept so an attribute set is portable. */
    bool acceptFirstMouse = false;
    bool backForwardNavigationGestures = false;
    bool incognito = false;
    bool autoplay = true;
    ProxyConfig proxyConfig;
    bool focused = true;
    /** Rust's `bounds: Option<Rect>`; defaults to `Some(200×200)`. */
    bool hasBounds = true;
    /** Only honoured by a child webview when `hasBounds` is true. */
    Rect bounds = {{0, 0, true}, {200, 200, true}};
    /** `with_background_throttling`; false is Rust's `None`. */
    bool hasBackgroundThrottling = false;
    BackgroundThrottlingPolicy backgroundThrottling =
        BackgroundThrottlingPolicy::Disabled;
    bool javascriptDisabled = false;

    // ── WebViewBuilderExtDarwin / WebViewBuilderExtMacos ──
    /** A persistent WKWebsiteDataStore identifier. macOS 14+ only;
        incognito takes precedence. */
    bool hasDataStoreIdentifier = false;
    uint8_t dataStoreIdentifier[16] = {};
    /** Only affects a non-child webview in a hidden-titlebar window. */
    bool hasTrafficLightInset = false;
    Position trafficLightInset;
    /** `with_allow_link_preview`; WebKit's default is true. */
    bool allowLinkPreview = true;
    /** Optional `WKWebViewConfiguration*`, kept opaque so this header remains
        portable and independent of Objective-C headers. The caller retains
        it through `WebViewNew`. */
    void* webviewConfiguration = nullptr;

    // ── WebViewBuilderExtWindows ──
    /** Null means wry's own default arguments; non-null empty is explicit. */
    Str additionalBrowserArgs;
    bool browserAcceleratorKeys = true;
    bool defaultContextMenus = true;
    bool hasTheme = false;
    Theme theme = Theme::Auto;
    /** Serve custom protocols over `https://<scheme>.host/` rather than
        `http://<scheme>.host/`. */
    bool useHttpsScheme = false;
    ScrollBarStyle scrollBarStyle = ScrollBarStyle::Default;
    bool browserExtensionsEnabled = false;
    /** Null is Rust's `None`; non-null empty is an explicit empty path. */
    Str extensionPath;
    /** Optional borrowed `ICoreWebView2Environment*`, normally obtained from
        `WebViewEnvironmentRaw`. Windows AddRefs it during construction. */
    void* webviewEnvironment = nullptr;
};

// ─── the webview ─────────────────────────────────────────────────────────

/** `WebViewBuilder::build` (asChild false) / `build_as_child` (asChild true).
    `parentWindow` is the platform window handle: an `HWND` on Windows.

    A webview built as a child sits at `attrs.bounds` inside the parent and is
    moved by `WebViewSetBounds`; one built otherwise fills the parent and
    follows it, which it does by subclassing the parent window.

    Returns null if the webview could not be created, having logged why. */
WebView* WebViewNew(void* parentWindow, const WebViewAttributes* attrs,
                    bool asChild);
/** `Drop for WebView`. */
void WebViewFree(WebView* webview);

/** `WebView::id`. */
Str WebViewId(WebView* webview);
/** `WebView::evaluate_script`. */
bool WebViewEval(WebView* webview, Str js);
/** `WebView::evaluate_script_with_callback`. The result is the script's
    value as JSON, and the callback runs on the thread that owns the
    webview. */
bool WebViewEvalWithCallback(WebView* webview, Str js, void* ctx,
                             void (*callback)(void* ctx, Str result));
/** `WebView::url`. Temp-arena backed; empty if it could not be read. */
Str WebViewUrlTemp(WebView* webview);
/** `WebView::load_url`. */
bool WebViewLoadUrl(WebView* webview, Str url);
/** `WebView::load_url_with_headers`. */
bool WebViewLoadUrlWithHeaders(WebView* webview, Str url, const Header* headers,
                               int headerCount);
/** `WebView::load_html`. */
bool WebViewLoadHtml(WebView* webview, Str html);
/** `WebView::reload`. */
bool WebViewReload(WebView* webview);
/** `WebView::bounds`. Physical, the way Rust reports them. */
bool WebViewBounds(WebView* webview, Rect* out);
/** `WebView::set_bounds`. */
bool WebViewSetBounds(WebView* webview, Rect bounds);
/** `WebView::set_visible`. */
bool WebViewSetVisible(WebView* webview, bool visible);
/** `WebView::focus`. */
bool WebViewFocus(WebView* webview);
/** `WebView::focus_parent`. */
bool WebViewFocusParent(WebView* webview);
/** `WebView::zoom`. */
bool WebViewZoom(WebView* webview, double scaleFactor);
/** `WebView::set_background_color`. */
bool WebViewSetBackgroundColor(WebView* webview, Rgba color);
/** `WebViewExtWindows::set_theme`. */
bool WebViewSetTheme(WebView* webview, Theme theme);
/** `WebViewExtWindows::set_memory_usage_level`. */
bool WebViewSetMemoryUsageLevel(WebView* webview, MemoryUsageLevel level);
/** `WebViewExtWindows::reparent`. */
bool WebViewReparent(WebView* webview, void* parentWindow);
/** `WebViewExtMacOS::set_traffic_light_inset`. Other platforms answer false;
    a child webview answers true without changing the window, as wry does. */
bool WebViewSetTrafficLightInset(WebView* webview, Position position);
/** `WebView::print`. */
bool WebViewPrint(WebView* webview);
/** `WebView::clear_all_browsing_data`. */
bool WebViewClearAllBrowsingData(WebView* webview);
/** `WebView::cookies` and `cookies_for_url`. `out` must be empty or a list
    previously returned here; it is replaced and owns all returned strings. */
bool WebViewCookies(WebView* webview, Vec<Cookie>* out);
bool WebViewCookiesForUrl(WebView* webview, Str url, Vec<Cookie>* out);
bool WebViewSetCookie(WebView* webview, const Cookie* cookie);
bool WebViewDeleteCookie(WebView* webview, const Cookie* cookie);
/** `WebView::open_devtools` / `close_devtools` / `is_devtools_open`. */
void WebViewOpenDevtools(WebView* webview);
void WebViewCloseDevtools(WebView* webview);
bool WebViewIsDevtoolsOpen(WebView* webview);

#if GPUI_OS_WINDOWS
/** Borrowed WebView2 objects corresponding to `WebViewExtWindows`. They stay
    valid until `WebViewFree`; an environment passed back in attributes is
    AddRef'd by `WebViewNew`. */
void* WebViewControllerRaw(WebView* webview);
void* WebViewEnvironmentRaw(WebView* webview);
void* WebViewNativeRaw(WebView* webview);
#endif

// ─── the custom protocol work-around ─────────────────────────────────────
//
// WebView2 only knows the standard schemes, so `wry://path` is tunnelled as
// `http://wry.path` and turned back at the edges — `is_work_around_uri` and
// the two `replace` helpers beside it in webview2/mod.rs. They are out here,
// rather than inside the backend, because they are string work with a test
// of the crate's own over them (`checks_if_custom_protocol_uri`). Every URI
// they hand back is temp-arena backed.

/** `{http_or_https}://{protocol}.` — what a custom protocol's URIs are
    rewritten to start with. */
Str WorkAroundUriPrefix(Str httpOrHttps, Str protocol);
/** Whether this URI is one of that protocol's, rewritten. */
bool IsWorkAroundUri(Str uri, Str httpOrHttps, Str protocol);
/** `{protocol}://x` -> `{http_or_https}://{protocol}.x`, and the reverse. */
Str ApplyUriWorkAround(Str uri, Str httpOrHttps, Str protocol);
Str RevertUriWorkAround(Str uri, Str httpOrHttps, Str protocol);

/** `wry::webview_version`. Temp-arena backed; empty when there is no webview
    runtime on this machine, which is the one thing worth checking before
    building a view around one. */
Str WebViewVersionTemp();
/** Whether a webview can be created at all on this platform and machine. */
bool WebViewAvailable();

} // namespace wry

#endif // GPUI_WRY_WRY_H_
