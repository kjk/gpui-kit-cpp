/* crates/webview/src/lib.rs — see webview.h. */

#include "webview/webview.h"

#include "gpui/platform.h"
#include "sys/executor.h"

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
        wry::WebViewSetPageClick(raw, nullptr, nullptr);
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

// wry's own loop, for the platforms whose webview runs on one the OS loop does
// not already turn (WebKitGTK's GLib context under X11). The two pollfd
// shapes are the same struct written twice, because wry may not name gpui's.
static_assert(sizeof(PlatPollFd) == sizeof(wry::PollFd), "pollfd layout");

static int WryLoopPrepare(PlatPollFd** fds, int* timeoutMs) {
    wry::PollFd* raw = nullptr;
    int n = wry::EventLoopPrepare(&raw, timeoutMs);
    *fds = (PlatPollFd*)raw;
    return n;
}

static void WryLoopDispatch() {
    wry::EventLoopDispatch();
}

Entity<WebView> WebViewNew(Ctx* cx, const wry::WebViewAttributes* attrs) {
    PlatAddLoopSource(WryLoopPrepare, WryLoopDispatch);
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
        // Linux forwards presses on the page. Other backends leave the
        // callback unset.
        wry::WebViewSetPageClick(raw, &WebView::OnPageClick, self);
    }
    self->window = cx->win;
    self->app = cx->app;
    self->selfId = handle.id;
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

void WebViewForward(WebView* self) {
    if (wry::WebView* raw = WebViewRaw(self)) {
        wry::WebViewEval(raw, StrL("history.forward();"));
    }
}

struct PageClickJob {
    App* app = nullptr;
    EntityId id = {};
    Window* win = nullptr;
    MouseButton button = MouseButton::Left;
    float x = 0;
    float y = 0;
};

static bool AppHasWindow(App* app, Window* win) {
    if (!app || !win) {
        return false;
    }
    for (Window* each : app->windows) {
        if (each == win) {
            return true;
        }
    }
    return false;
}

static void RunPageClick(PageClickJob* job) {
    if (job->app && EntityGet(job->app, job->id) &&
        AppHasWindow(job->app, job->win)) {
        PlatformInput down = {};
        down.kind = PlatformInputKind::MouseDown;
        down.mouseDown.button = job->button;
        down.mouseDown.x = job->x;
        down.mouseDown.y = job->y;
        down.mouseDown.clickCount = 1;
        WindowDispatchInput(job->win, &down);
        PlatformInput up = {};
        up.kind = PlatformInputKind::MouseUp;
        up.mouseUp.button = job->button;
        up.mouseUp.x = job->x;
        up.mouseUp.y = job->y;
        up.mouseUp.clickCount = 1;
        WindowDispatchInput(job->win, &up);
    }
    delete job;
}

void WebView::OnPageClick(void* user, int button, float x, float y,
                          float gdkScale) {
    WebView* self = (WebView*)user;
    if (!self || !self->window) {
        return;
    }
    MouseButton mapped = MouseButton::Left;
    if (button == 2) {
        mapped = MouseButton::Middle;
    } else if (button == 3) {
        mapped = MouseButton::Right;
    } else if (button != 1) {
        return;
    }
    // GTK reports the press in its own pixels. Divide by the window scale
    // the way `forward_mouse_down` does, then add the webview's origin.
    float gpuiScale = self->window->paint.dpi / 96.f;
    if (gpuiScale <= 0) {
        gpuiScale = 1;
    }
    float scale = gdkScale / gpuiScale;
    PageClickJob* job = new PageClickJob();
    job->app = self->app;
    job->id = self->selfId;
    job->win = self->window;
    job->button = mapped;
    job->x = self->bounds.x + x * scale;
    job->y = self->bounds.y + y * scale;
    // GTK may be inside its own iteration. GPUI hears the click once that
    // returns, which is what Rust's `cx.spawn` is for.
    ExecPost(MkFunc0(&RunPageClick, job));
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
    WebView* self = (WebView*)user;
    Bounds b = e->Bounds();
    self->bounds = b;
    wry::WebView* raw = self->owned.Raw();
    if (!raw || !self->visible) {
        return;
    }
    // `match_scale_factor`: GDK's factor is an integer, so a fractional
    // GPUI scale would otherwise draw the page large. Other platforms
    // return without changing the zoom.
    float gpuiScale = ctx && ctx->dpi > 0 ? ctx->dpi / 96.f : 1.f;
    if (!self->hasPageScale || self->pageScale != gpuiScale) {
        if (wry::WebViewMatchPageScale(raw, gpuiScale)) {
            self->pageScale = gpuiScale;
            self->hasPageScale = true;
        }
    }
    bool same = self->hasApplied && self->applied.x == b.x &&
                self->applied.y == b.y && self->applied.w == b.w &&
                self->applied.h == b.h;
    if (same) {
        return;
    }
    self->applied = b;
    self->hasApplied = true;
    // Logical DIPs. Upstream Linux passes device pixels because lb-wry then
    // multiplies by GDK's factor. `WebViewSetBounds` on Linux already does
    // that conversion for the X11 child, so doing it here would double it.
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
    // `HitboxBehavior::BlockMouse`: the native view covers this box, so
    // GPUI content under it must not take the press.
    e->StopMouseDown();
    e->customPaint = PaintWebView;
    e->customUser = self;
    return e;
}

} // namespace gpui
