/* crates/webview/src/lib.rs — the `gpui-wry` crate.
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

/** An owned, UI-thread-local handle to the raw wry webview. Copies prolong
    the native control's lifetime, like Rust's
   `WebViewHandle(Rc<wry::WebView>)`. Every copy must be dropped before the
   parent window is destroyed. */
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

/** `gpui_wry::WebView`. */
struct WebView {
    WebViewHandle owned;
    bool visible = true;
    // Where the element last was, which is the box the OS control is moved
    // to. Rust keeps this too, filled by the `canvas` in its render.
    Bounds bounds = {};
    // The box already handed to wry, so an unchanged frame moves nothing.
    Bounds applied = {};
    bool hasApplied = false;
    // Whether the window subscription that blurs the page on an outside
    // click has been made. Rust installs its `on_mouse_event` every paint,
    // for that frame; a subscription here outlives the frame, so it is made
    // once.
    bool subscribed = false;

    ~WebView();

    /** The press that lands anywhere but on the webview: the page gives the
        focus back to the window, so typing goes where it looks like it
        goes. Rust does this from inside the element's paint. */
    static void OnWindowMouseDown(WebView* self, Ctx* cx,
                                  const MouseDownEvent* ev);
};

/** `cx.new(|cx| WebView::new(wry::WebViewBuilder::new()…build_as_child(..)))`:
    make the wry webview as a child of this window's OS window and start it
    at an empty box, the way Rust's `set_bounds(Rect::default())` does — the
    element gives it a real one on the first frame.

    The handle is invalid if there is no webview runtime on this machine (or
    no backend on this platform), having logged why; a caller that wants to
    say something better than an empty box can ask first with
    `wry::WebViewAvailable()`.

    A note for a caller that makes one lazily, from inside a Render: this
    blocks, and it blocks by running the window's message loop, exactly as
    Rust's `build_as_child` does. A WM_PAINT can arrive while it waits, so the
    frame that started the creation may be built again before the creation
    returns. Set whatever flag says "already started" *before* calling, the
    way examples/webview.cpp does, and the second pass will skip it. */
Entity<WebView> WebViewNew(Ctx* cx, const wry::WebViewAttributes* attrs);

/** `WebView::show` / `hide` / `visible`. Hiding moves the focus back to the
    parent window first, as Rust does. */
void WebViewShow(WebView* self);
void WebViewHide(WebView* self);
bool WebViewVisible(const WebView* self);
/** `WebView::bounds`: the box the element had last frame. */
Bounds WebViewBounds(const WebView* self);
/** `WebView::load_url`. */
void WebViewLoadUrl(WebView* self, Str url);
/** `WebView::back` — `history.back()` evaluated in the page, which is what
    Rust does rather than asking the webview to go back. */
void WebViewBack(WebView* self);
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

} // namespace gpui

#endif // GPUI_WEBVIEW_WEBVIEW_H_
