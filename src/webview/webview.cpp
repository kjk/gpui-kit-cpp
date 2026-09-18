/* crates/webview/src/lib.rs — see webview.h. */

#include "webview/webview.h"

#include "gpui/platform.h"

namespace gpui {

struct WebViewHandleState {
    int refs = 1;
    wry::WebView* raw = nullptr;
};

static void WebViewHandleRetain(WebViewHandleState* state) {
    if (state) {
        state->refs++;
    }
}

static void WebViewHandleRelease(WebViewHandleState* state) {
    if (!state || --state->refs > 0) {
        return;
    }
    wry::WebViewFree(state->raw);
    delete state;
}

WebViewHandle::WebViewHandle(WebViewHandleState* value) : state(value) {}

WebViewHandle::WebViewHandle(const WebViewHandle& other) : state(other.state) {
    WebViewHandleRetain(state);
}

WebViewHandle& WebViewHandle::operator=(const WebViewHandle& other) {
    if (this == &other) {
        return *this;
    }
    WebViewHandleRetain(other.state);
    WebViewHandleRelease(state);
    state = other.state;
    return *this;
}

WebViewHandle::~WebViewHandle() {
    WebViewHandleRelease(state);
}

wry::WebView* WebViewHandle::Raw() const {
    return state ? state->raw : nullptr;
}

WebView::~WebView() {
    // `impl Drop for WebView` hides it. Releasing `owned` after this body is
    // the Rc drop: an outstanding WebViewHandle postpones native destruction.
    if (wry::WebView* raw = owned.Raw()) {
        wry::WebViewFocusParent(raw);
        wry::WebViewSetVisible(raw, false);
    }
}

void WebView::OnWindowMouseDown(WebView* self, Ctx* cx,
                                const MouseDownEvent* ev) {
    (void)cx;
    wry::WebView* raw = self->owned.Raw();
    if (!raw || !ev) {
        return;
    }
    Point p = {ev->x, ev->y};
    if (!self->bounds.Contains(p)) {
        wry::WebViewFocusParent(raw);
    }
}

Entity<WebView> WebViewNew(Ctx* cx, const wry::WebViewAttributes* attrs) {
    // `EntityNewState`, not `EntityNew`: a `WebView` has no Render of its
    // own here, since `WebViewEl` hands the element to whoever wants it.
    Entity<WebView> handle = EntityNewState<WebView>(cx->app);
    WebView* self = handle.Get(cx->app);
    if (!self) {
        return handle;
    }
    void* window = PlatWindowHandle(cx->win);
    if (!window) {
        logf(
            "webview: this window has no OS handle to parent a webview into\n");
        return handle;
    }
    wry::WebViewAttributes copy = *attrs;
    // `WebView::new` starts it at nothing and lets the element place it.
    copy.bounds = wry::Rect{wry::LogicalPosition(0, 0), wry::LogicalSize(0, 0)};
    wry::WebView* raw = wry::WebViewNew(window, &copy, /*asChild=*/true);
    if (raw) {
        WebViewHandleState* state = new WebViewHandleState();
        state->raw = raw;
        self->owned = WebViewHandle(state);
    }
    self->visible = raw != nullptr;
    return handle;
}

void WebViewShow(WebView* self) {
    wry::WebView* raw = WebViewRaw(self);
    if (!raw) {
        return;
    }
    wry::WebViewSetVisible(raw, true);
    self->visible = true;
}

void WebViewHide(WebView* self) {
    wry::WebView* raw = WebViewRaw(self);
    if (!raw) {
        return;
    }
    wry::WebViewFocusParent(raw);
    wry::WebViewSetVisible(raw, false);
    self->visible = false;
}

bool WebViewVisible(const WebView* self) {
    return self && self->visible;
}

Bounds WebViewBounds(const WebView* self) {
    return self ? self->bounds : Bounds{};
}

void WebViewLoadUrl(WebView* self, Str url) {
    if (wry::WebView* raw = WebViewRaw(self)) {
        wry::WebViewLoadUrl(raw, url);
    }
}

void WebViewBack(WebView* self) {
    if (wry::WebView* raw = WebViewRaw(self)) {
        wry::WebViewEval(raw, StrL("history.back();"));
    }
}

wry::WebView* WebViewRaw(const WebView* self) {
    return self ? self->owned.Raw() : nullptr;
}

WebViewHandle WebViewGetHandle(const WebView* self) {
    return self ? self->owned : WebViewHandle();
}

// The element's prepaint: layout has decided where the box is, so the OS
// control is moved there. Rust does this in `Element::prepaint`, which runs
// before the frame is drawn; the paint pass is where an element here first
// knows its box, and the two are a frame apart only if something else in the
// same frame reads the webview's bounds.
static void PaintWebView(PaintCtx* ctx, El* e, void* user) {
    (void)ctx;
    WebView* self = (WebView*)user;
    Bounds b = e->Bounds();
    self->bounds = b;
    wry::WebView* raw = self->owned.Raw();
    if (!raw || !self->visible) {
        return;
    }
    bool same = self->hasApplied && self->applied.x == b.x &&
                self->applied.y == b.y && self->applied.w == b.w &&
                self->applied.h == b.h;
    if (same) {
        return;
    }
    self->applied = b;
    self->hasApplied = true;
    wry::Rect r;
    r.position = wry::LogicalPosition(b.x, b.y);
    r.size = wry::LogicalSize(b.w, b.h);
    wry::WebViewSetBounds(raw, r);
}

El* WebViewEl(Entity<WebView> view, Ctx* cx) {
    WebView* self = view.Get(cx->app);
    // `Style { size: Size::full(), flex_shrink: 1., ..Default }`.
    El* e = Div(cx->a)->SizeFull();
    if (!self) {
        return e;
    }
    if (!self->subscribed) {
        self->subscribed = true;
        WindowOnMouseDown(cx->win, ListenTo(view, &WebView::OnWindowMouseDown));
    }
    e->customPaint = PaintWebView;
    e->customUser = self;
    return e;
}

} // namespace gpui
