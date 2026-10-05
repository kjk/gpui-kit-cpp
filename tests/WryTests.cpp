/* wry/src/webview2/mod.rs's own test module: `checks_if_custom_protocol_uri`,
 * over the URI work-around a custom protocol goes through on Windows.
 *
 * The two `replace` helpers beside it carry no test upstream; the round trip
 * is asserted here because they are what the request handler and the initial
 * navigation each use one half of, and a mismatch between them would only
 * show up as a page that never loads. */

#include "Test.h"

static void AssignWebViewHandle(WebViewHandle* to, const WebViewHandle* from) {
    *to = *from;
}

void TestWryUri() {
    TestSuite("wry_uri");

    // gpui-wry's owned raw handle is copyable even when empty. A real handle
    // follows the same operations while retaining the shared native view.
    WebView empty;
    WebViewHandle handle = WebViewGetHandle(&empty);
    utassert(!handle.IsValid());
    utassert(handle.Raw() == nullptr);
    WebViewHandle copy = handle;
    utassert(!copy.IsValid());
    // Keep the self-assignment path covered without spelling `copy = copy`,
    // which Clang diagnoses under -Wself-assign-overloaded before it inlines.
    AssignWebViewHandle(&copy, &copy);
    utassert(copy.Raw() == nullptr);
    WebViewHandle assigned;
    assigned = copy;
    utassert(!assigned.IsValid());

    // `WebViewAttributes::default`, including the Darwin fields that live in
    // Rust's separate `PlatformSpecificWebViewAttributes` builder state.
    wry::WebViewAttributes attrs;
    utassert(attrs.visible);
    utassert(attrs.acceptFirstMouse == false);
    utassert(attrs.allowLinkPreview);
    utassert(!attrs.hasDataStoreIdentifier);
    utassert(!attrs.hasTrafficLightInset);
    utassert(!attrs.hasBackgroundThrottling);
    utassert(attrs.webviewConfiguration == nullptr);
    utassert(attrs.hasBounds);
    utassert(attrs.bounds.position.x == 0);
    utassert(attrs.bounds.position.y == 0);
    utassert(attrs.bounds.size.width == 200);
    utassert(attrs.bounds.size.height == 200);
    utassert(attrs.downloadStartedHandler != nullptr);
    Str downloadPath = StrL("download.bin");
    utassert(attrs.downloadStartedHandler(attrs.ctx, Str(), &downloadPath));
    utassert(attrs.downloadCompletedHandler == nullptr);
    utassert(attrs.dragDropHandler == nullptr);
    utassert(attrs.webviewEnvironment == nullptr);

    wry::Cookie cookie;
    utassert(cookie.session);
    utassert(!cookie.hasExpires);
    utassert(!cookie.hasMaxAge);
    utassert(!cookie.hasHttpOnly);
    utassert(!cookie.hasSecure);
    utassert(!cookie.hasSameSite);
    wry::Cookie matching;
    matching.domain = StrL("example.com");
    matching.path = StrL("/private");
    utassert(
        wry::MacCookieMatchesUrl(&matching, StrL("http"), StrL("example.com")));
    utassert(!wry::MacCookieMatchesUrl(&matching, StrL("https"),
                                       StrL("sub.example.com")));
    utassert(!wry::MacCookieMatchesUrl(&matching, StrL("https"), Str()));
    matching.hasSecure = true;
    matching.secure = true;
    utassert(!wry::MacCookieMatchesUrl(&matching, StrL("http"),
                                       StrL("example.com")));
    utassert(wry::MacCookieMatchesUrl(&matching, StrL("https"),
                                      StrL("example.com")));
    matching.domain = StrL("localhost");
    utassert(
        wry::MacCookieMatchesUrl(&matching, StrL("http"), StrL("localhost")));
    utassert(
        !wry::MacCookieMatchesUrl(&matching, StrL("ftp"), StrL("localhost")));
    utassert(StrEq(wry::MacDownloadFileNameTemp(StrL("r.tar.gz"), 0),
                   StrL("r.tar.gz")));
    utassert(StrEq(wry::MacDownloadFileNameTemp(StrL("r.tar.gz"), 1),
                   StrL("r (1).tar.gz")));
    utassert(StrEq(wry::MacDownloadFileNameTemp(StrL("README"), 2),
                   StrL("README (2)")));
    utassert(StrEq(wry::MacDownloadFileNameTemp(StrL(".config"), 1),
                   StrL(" (1).config")));
    Vec<wry::Cookie> cookies;
    cookie.name = StrDup(StrL("session"));
    cookie.value = StrDup(StrL("value"));
    cookie.domain = StrDup(StrL("example.com"));
    cookie.path = StrDup(StrL("/"));
    VecAppend(cookies, cookie);
    wry::CookieListFree(&cookies);
    utassert(len(cookies) == 0);

    // The crate's own case, verbatim.
    Str scheme = StrL("http");
    Str uri = StrL("http://wry.localhost/path/to/page");
    utassert(wry::IsWorkAroundUri(uri, scheme, StrL("wry")));
    utassert(!wry::IsWorkAroundUri(uri, scheme, StrL("asset")));

    // The prefix the filter is built from.
    utassert(base::StrEq(wry::WorkAroundUriPrefix(scheme, StrL("wry")),
                         StrL("http://wry.")));
    utassert(base::StrEq(wry::WorkAroundUriPrefix(StrL("https"), StrL("asset")),
                         StrL("https://asset.")));

    // What the initial navigation does to a `wry://` url, and what the
    // request handler undoes before the handler sees it.
    Str original = StrL("wry://localhost/path/to/page");
    Str applied = wry::ApplyUriWorkAround(original, scheme, StrL("wry"));
    utassert(base::StrEq(applied, StrL("http://wry.localhost/path/to/page")));
    utassert(base::StrEq(wry::RevertUriWorkAround(applied, scheme, StrL("wry")),
                         original.s));

    // Rust uses `str::replace`, not `strip_prefix`: nested URLs are rewritten
    // too, and revert makes the complete string round-trip.
    Str nested = StrL("wry://host/?next=wry://other#back=wry://host/");
    Str nestedApplied = wry::ApplyUriWorkAround(nested, scheme, StrL("wry"));
    utassert(base::StrEq(
        nestedApplied,
        StrL("http://wry.host/?next=http://wry.other#back=http://wry.host/")));
    utassert(base::StrEq(
        wry::RevertUriWorkAround(nestedApplied, scheme, StrL("wry")), nested));

    // A URI in another protocol is left where it is, both ways round.
    Str other = StrL("https://example.com/x");
    utassert(base::StrEq(wry::ApplyUriWorkAround(other, scheme, StrL("wry")),
                         other.s));
    utassert(base::StrEq(wry::RevertUriWorkAround(other, scheme, StrL("wry")),
                         other.s));

    // The https variant is a different prefix, so an http-tunnelled URI is
    // not one of its.
    utassert(!wry::IsWorkAroundUri(uri, StrL("https"), StrL("wry")));
}

// webkitgtk/'s string and number work (wry.cpp), and the parts of the Linux
// backend that answer without a display: the loop source before any webview
// exists, and whether the build has WebKitGTK at all.
void TestWryWebKitGtk() {
    TestSuite("wry_webkitgtk");

    // `new_gtk`'s proxy URI.
    wry::ProxyConfig proxy;
    utassert(len(wry::ProxyUriTemp(&proxy)) == 0);
    proxy.kind = wry::ProxyKind::Http;
    proxy.host = StrL("localhost");
    proxy.port = StrL("8080");
    utassert(
        base::StrEq(wry::ProxyUriTemp(&proxy), StrL("http://localhost:8080")));
    proxy.kind = wry::ProxyKind::Socks5;
    utassert(base::StrEq(wry::ProxyUriTemp(&proxy),
                         StrL("socks5://localhost:8080")));

    // drag_drop.rs's `path_buf_from_uri`.
    utassert(
        base::StrEq(wry::PathFromFileUriTemp(StrL("file:///tmp/a%20b.txt")),
                    StrL("/tmp/a b.txt")));
    utassert(base::StrEq(wry::PathFromFileUriTemp(StrL("/no/scheme")),
                         StrL("/no/scheme")));
    // UTF-8 comes back as the bytes it encodes; a stray % stays.
    utassert(base::StrEq(wry::PathFromFileUriTemp(StrL("file:///h%C3%A9/100%")),
                         StrL("/h\xC3\xA9/100%")));
    utassert(base::StrEq(wry::PathFromFileUriTemp(StrL("file:///x%zz")),
                         StrL("/x%zz")));

    // web_context.rs's download name: split at the *first* dot.
    Str stem;
    Str ext;
    wry::DownloadFileNameParts(StrL("https://x/r.tar.gz"), StrL("r.tar.gz"),
                               &stem, &ext);
    utassert(base::StrEq(stem, StrL("r")));
    utassert(base::StrEq(ext, StrL(".tar.gz")));
    wry::DownloadFileNameParts(StrL("https://x/README"), StrL("README"), &stem,
                               &ext);
    utassert(base::StrEq(stem, StrL("README")));
    utassert(len(ext) == 0);
    // The comment's own example: a nameless data: URL's payload is no name.
    wry::DownloadFileNameParts(StrL("data:attachment/text,sometext"),
                               StrL("text,sometext"), &stem, &ext);
    utassert(base::StrEq(stem, StrL("Unknown")));
    utassert(len(ext) == 0);
    wry::DownloadFileNameParts(StrL("data:attachment/text,sometext"),
                               StrL("notes.txt"), &stem, &ext);
    utassert(base::StrEq(stem, StrL("notes")));
    utassert(base::StrEq(ext, StrL(".txt")));

    // `scale_factor_from_x11`: 96 dpi is 1, and a screen that does not know
    // its size does not divide by zero.
    utassert(wry::ScaleFactorFromScreen(1920, 508) > 0.99 &&
             wry::ScaleFactorFromScreen(1920, 508) < 1.01);
    utassert(wry::ScaleFactorFromScreen(3840, 508) > 1.99);
    utassert(wry::ScaleFactorFromScreen(1920, 0) == 1.0);

    // synthetic_mouse_events.rs: button 8 is DOM button 3, 9 is 4.
    wry::SyntheticMouseEvent ev;
    ev.pressed = true;
    ev.button = 8;
    ev.x = 10;
    ev.y = 20;
    ev.buttons = 9;
    ev.ctrlKey = true;
    Str js = wry::SyntheticMouseEventJsTemp(&ev);
    utassert(StrFind(js, "document.elementFromPoint(10,20)") >= 0);
    utassert(StrFind(js, "new MouseEvent('mousedown'") >= 0);
    utassert(StrFind(js, "button: 3,") >= 0);
    utassert(StrFind(js, "buttons: 9,") >= 0);
    utassert(StrFind(js, "screenY: window.screenY + 20,") >= 0);
    utassert(StrFind(js, "ctrlKey: true,") >= 0);
    utassert(StrFind(js, "altKey: false,") >= 0);
    utassert(StrFind(js, "\"mousedown\" === \"mouseup\"") >= 0);
    ev.pressed = false;
    ev.button = 9;
    js = wry::SyntheticMouseEventJsTemp(&ev);
    utassert(StrFind(js, "new MouseEvent('mouseup'") >= 0);
    utassert(StrFind(js, "button: 4,") >= 0);

    // No webview yet, so there is no second loop to turn on any platform,
    // and the host's timeout is left alone.
    wry::PollFd* fds = (wry::PollFd*)&ev;
    int timeoutMs = 250;
    utassert(wry::EventLoopPrepare(&fds, &timeoutMs) == 0);
    utassert(fds == nullptr);
    utassert(timeoutMs == 250);
    wry::EventLoopDispatch();

#if GPUI_OS_LINUX
#if defined(GPUI_HAVE_WEBKITGTK) && GPUI_HAVE_WEBKITGTK
    // "2.44.1": the library answers without a display.
    utassert(len(wry::WebViewVersionTemp()) >= 5);
#else
    // Built without WebKitGTK: the soft dependency's absence is a stub that
    // says so, not a missing symbol.
    utassert(len(wry::WebViewVersionTemp()) == 0);
    utassert(!wry::WebViewAvailable());
#endif
#endif
}
