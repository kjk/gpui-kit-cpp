/* crates/webview/src/lib.rs — the `gpui-webview` crate.
 *
 * A webview inside a gpui window: `src/wry` is the webview, this is the view
 * that gives it a box in the element tree and keeps it there.
 *
 *     struct Page {
 *         Entity<WebView> web;
 *
 *         static El* Render(Page* self, Ctx* cx) {
 *             if (!self->web.id.IsValid()) {
 *                 wry::WebViewAttributes attrs;
 *                 attrs.url = StrL("https://example.com");
 *                 self->web = WebViewNew(cx, &attrs);
 *             }
 *             return Div(cx->a)->SizeFull()->Child(WebViewEl(self->web, cx));
 *         }
 *     };
 *
 * The same warning as the Rust crate's readme: the webview is an OS control
 * over the window, not something gpui draws. It covers whatever the element
 * tree puts behind it, it does not scroll with a container, and it has no
 * corner radius — so it wants a box of its own, or a window of its own.
 *
 * Two deviations from the Rust, both in the same place:
 *
 *   - Rust builds the `wry::WebView` outside and hands it to `WebView::new`,
 *     because only the caller has the `Window` to parent it into. `Ctx` is
 *     that window here, so `WebViewNew` takes the attributes and builds it.
 *   - `impl Render for WebView` is a view whose whole body is the element,
 *     and gpui needs an entity to be a view before it can be a child. An
 *     element here is a value, so `WebViewEl` hands one back and the owning
 *     view puts it where it likes. The entity is still an entity — it is
 *     what the mouse subscription is bound to, and dropping it is what
 *     closes the webview.
 */

#ifndef GPUI_WEBVIEW_WEBVIEW_H_
#define GPUI_WEBVIEW_WEBVIEW_H_

#include "gpui/gpui.h"
#include "wry/wry.h"

namespace gpui {

struct WebView;
struct WebViewHandleState;
struct WebViewBackdrop;

/** An owned, UI-thread-local handle to the raw wry webview. Copies prolong
    the native control's lifetime, like Rust's
    `WebViewHandle(Rc<wry::WebView>)`. Drop copies before destroying their
    native parent. The Linux GPUI host detaches live handles when closing
    its X11 window. */
struct WebViewHandle {
    WebViewHandle() = default;
    WebViewHandle(const WebViewHandle& other);
    WebViewHandle& operator=(const WebViewHandle& other);
    ~WebViewHandle();

    /** `WebViewHandle::raw`. Null for an empty handle. */
    wry::WebView* Raw() const;
    bool IsValid() const { return Raw() != nullptr; }

  private:
    WebViewHandleState* state = nullptr;

    explicit WebViewHandle(WebViewHandleState* state);
    friend Entity<WebView> WebViewNew(Ctx* cx,
                                      const wry::WebViewAttributes* attrs);
};

/** `gpui_webview::WebView`. */
struct WebView {
    WebViewHandle owned;
    FocusHandle focus = {};
    bool visible = true;
    // The OS view, which a clip can hide without changing `visible`.
    bool nativeShown = false;
    // The last geometry update failed. The failure is logged once, until
    // a later update succeeds.
    bool geometryFailed = false;
    // Where the element last was, which is the box the OS control is moved
    // to. Rust keeps this too, filled by the `canvas` in its render.
    Bounds bounds = {};
    // The box already handed to wry, so an unchanged frame moves nothing,
    // with the visible part of it and the GPUI scale it was applied at.
    Bounds applied = {};
    Bounds appliedVisible = {};
    float appliedScale = 0;
    bool hasApplied = false;
    Vec<Bounds> visibleRegion;
    bool hasVisibleRegion = false;
    bool visibleRegionNeedsUpdate = false;
#if GPUI_WRY_INPUT_REGION || GPUI_OS_WINDOWS
#define GPUI_WEBVIEW_LIVE_OVERLAY 1
    bool liveOverlay = false;
#else
#define GPUI_WEBVIEW_LIVE_OVERLAY 0
#endif
    WebViewBackdrop* backdrop = nullptr;
    // `windows::NativeClip`: the region last set on wry's host window, as
    // left, top, right, bottom in its device pixels. Windows only.
    int32_t clip[4] = {};
    bool hasClip = false;
    // Linux `scale_factor`: the GPUI scale the page zoom was last matched
    // to. Other platforms share the window scale and skip the zoom.
    float pageScale = 0;
    bool hasPageScale = false;
    // `WebView::zoom`: the page zoom last asked for, before display-scale
    // compensation. `1` is the page's natural size.
    double zoom = 1;
    // The window the page-click forward posts into, and this entity, so a
    // click queued from GTK is dropped once either is gone.
    Window* window = nullptr;
    App* app = nullptr;
    EntityId selfId = {};
    ~WebView();

    /** The press that lands anywhere but on the webview: the page gives the
        focus back to the window, so typing goes where it looks like it
        goes. Rust does this from inside the element's paint. */
    static void OnWindowMouseDown(WebView* self, Ctx* cx,
                                  const MouseDownEvent* ev);
    // `linux::forward_mouse_down`. wry calls this with the page's own
    // coordinates; the body posts a GPUI press.
    static void OnPageClick(void* user, int button, float x, float y,
                            float gdkScale);
};

/** `cx.new(|cx| WebView::new(wry::WebViewBuilder::new()…build_as_child(..)))`:
    make the wry webview as a child of this window's OS window and start it
    at an empty box, the way Rust's `set_bounds(Rect::default())` does — the
    element gives it a real one on the first frame.

    If the native runtime is unavailable, the entity renders an empty box.
    Callers can check for a live surface with `WebViewRaw()` or check for
    runtime availability first with `wry::WebViewAvailable()`.

    A note for a caller that makes one lazily, from inside a Render: this
    blocks, and it blocks by running the window's message loop, exactly as
    Rust's `build_as_child` does. A WM_PAINT can arrive while it waits, so the
    frame that started the creation may be built again before the creation
    returns. Set whatever flag says "already started" *before* calling, the
    way examples/webview.cpp does, and the second pass will skip it. */
Entity<WebView> WebViewNew(Ctx* cx, const wry::WebViewAttributes* attrs);

/** `WebView::show` / `hide` / `visible`. Showing takes effect when the element
    next paints. Hiding moves the focus back to the parent window first. */
void WebViewShow(WebView* self);
void WebViewHide(WebView* self);
bool WebViewVisible(const WebView* self);
/** `WebView::bounds`: the box the element had last frame. */
Bounds WebViewBounds(const WebView* self);
/** `Focusable::focus_handle`: the webview's handle in the GPUI focus tree.
    Native keyboard focus is controlled through the raw wry webview. */
FocusHandle WebViewFocusHandle(const WebView* self);
/** `WebView::load_url`. */
void WebViewLoadUrl(WebView* self, Str url);
/** `WebView::back` — `history.back()` evaluated in the page, which is what
    Rust does rather than asking the webview to go back. */
void WebViewBack(WebView* self);
/** `WebView::forward` — `history.forward()`, the same way as `back`. */
void WebViewForward(WebView* self);
/** `WebView::set_zoom` / `zoom`. The engine keeps the factor across
    navigations. On Linux it multiplies the scale match. */
bool WebViewSetPageZoom(WebView* self, double zoom);
double WebViewPageZoom(const WebView* self);
/** `WebView::set_theme`. */
bool WebViewSetPageTheme(WebView* self, wry::Theme theme);
/** `WebView::raw`: the wry webview, for everything this façade does not
    wrap. Null until one has been made. */
wry::WebView* WebViewRaw(const WebView* self);
/** `WebView::handle`: an owned copy that may outlive the entity. Dropping
    the entity hides the child view, while native destruction waits for this
    and every other outstanding handle. */
WebViewHandle WebViewGetHandle(const WebView* self);

/** `WebViewElement`: the box the webview is kept in. Fills its parent, and
    moves the OS control to wherever layout put it. */
El* WebViewEl(Entity<WebView> view, Ctx* cx);

// macOS WebKit truncates a WKWebView's size to whole points, which leaves a
// strip of background along the right or bottom edge at fractional sizes
// (`composition.rs`). Only there is the frame handed to wry rounded.
#if GPUI_OS_MAC
#define GPUI_WEBVIEW_WHOLE_POINT_FRAME 1
#else
#define GPUI_WEBVIEW_WHOLE_POINT_FRAME 0
#endif

#if GPUI_OS_LINUX
#define GPUI_WEBVIEW_DEVICE_GEOMETRY 1
#else
#define GPUI_WEBVIEW_DEVICE_GEOMETRY 0
#endif

/** `composition::document_frame`: the document box with its width and
    height rounded up to whole points, its left and top edges in place so
    the page does not shift. The clip and hit box stay the unrounded box.
    Rust works in the container's bottom-up coordinates and so moves the
    origin down by the growth; a wry rect is top-left, so the origin stays. */
Bounds WebViewDocumentFrame(Bounds document);
// Subtract later overlay boxes from the native viewport. Output coordinates
// are local to the viewport, as consumed by the native child container.
void WebViewVisibleRegion(Bounds viewport, const Bounds* boxes, int count,
                          Vec<Bounds>* out);

// Windows clips a partially visible webview with a window region on wry's
// host window (`windows.rs`). Linux and macOS clip through wry's child
// container.
#if GPUI_OS_WINDOWS
#define GPUI_WEBVIEW_NATIVE_CLIP 1
#else
#define GPUI_WEBVIEW_NATIVE_CLIP 0
#endif

/** `windows::window_geometry`: the device-pixel rect for wry's host window
    and, when part of the document is hidden, the region (left, top, right,
    bottom) local to that window that keeps only `visible`. wry rounds the
    window origin and extent independently, so a fully visible far edge
    uses that extent rather than the difference of two rounded positions.
    Returns false, leaving `clip` unspecified, when nothing is hidden. */
bool WebViewWindowGeometry(Bounds document, Bounds visible, double scale,
                           wry::Rect* bounds, int32_t clip[4]);

#if GPUI_WEBVIEW_NATIVE_CLIP
/** `NativeClip::set_geometry`: full document bounds in device pixels, then
    the region that clips them to `visible`. False when the host window,
    its DPI or the region could not be had. */
bool WebViewNativeClipSet(WebView* self, wry::WebView* raw, Bounds document,
                          Bounds visible);
/** `NativeClip::clear`: drop the region, if one was set. */
bool WebViewNativeClipClear(WebView* self, wry::WebView* raw);
bool WebViewNativeVisibleRegionSet(WebView* self, wry::WebView* raw,
                                   const Bounds* pieces, int count);
#endif

} // namespace gpui

#endif // GPUI_WEBVIEW_WEBVIEW_H_
