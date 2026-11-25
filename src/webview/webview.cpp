/* crates/webview/src/lib.rs — see webview.h. */

#include "webview/webview.h"

#include "gpui/platform.h"
#include "sys/executor.h"

namespace gpui {

static bool AppHasWindow(App* app, Window* win);

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
    // The last frame can still contain the webview's mouse blocker.
    if (visible && AppHasWindow(app, window)) {
        AppInvalidate(window);
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
        self->visible = false;
        self->nativeShown = false;
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
    // What wry made is what the OS shows now: created hidden, the native
    // view is not shown, and the first paint has to show it.
    self->nativeShown = raw != nullptr && copy.visible;
    return handle;
}

void WebViewShow(WebView* self) {
    wry::WebView* raw = WebViewRaw(self);
    if (!raw) {
        return;
    }
    bool changed = !self->visible;
    wry::WebViewSetVisible(raw, true);
    self->visible = true;
    self->nativeShown = true;
    if (changed && AppHasWindow(self->app, self->window)) {
        AppInvalidate(self->window);
    }
}

void WebViewHide(WebView* self) {
    wry::WebView* raw = WebViewRaw(self);
    if (!raw) {
        return;
    }
    bool changed = self->visible;
    wry::WebViewFocusParent(raw);
    wry::WebViewSetVisible(raw, false);
    self->visible = false;
    self->nativeShown = false;
    if (changed && AppHasWindow(self->app, self->window)) {
        AppInvalidate(self->window);
    }
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

bool WebViewSetPageZoom(WebView* self, double zoom) {
    wry::WebView* raw = WebViewRaw(self);
    if (!self || !raw) {
        return false;
    }
    // Stored first, as Rust's `zoom` cell is, so a later scale match on
    // Linux multiplies this factor even when the engine call fails.
    self->zoom = zoom;
    return wry::WebViewSetUserZoom(raw, zoom);
}

double WebViewPageZoom(const WebView* self) {
    return self ? self->zoom : 1;
}

bool WebViewSetPageTheme(WebView* self, wry::Theme theme) {
    wry::WebView* raw = WebViewRaw(self);
    if (!raw) {
        return false;
    }
    return wry::WebViewSetTheme(raw, theme);
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

static Bounds ClipIntersect(Bounds a, Bounds b) {
    float x = a.x > b.x ? a.x : b.x;
    float y = a.y > b.y ? a.y : b.y;
    float right = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    float bottom = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return {x, y, right > x ? right - x : 0.f, bottom > y ? bottom - y : 0.f};
}

static bool BoundsSame(Bounds a, Bounds b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// WebKit truncates the view size to whole points. Expand the child, not the
// clip, and keep its top edge fixed.
Bounds WebViewDocumentFrame(Bounds document) {
    return {document.x, document.y, ceilf(document.w), ceilf(document.h)};
}

// dpi's `to_physical::<i32>`: rounds half away from zero.
static int32_t ToPhysicalPx(double logical, double scale) {
    double v = logical * scale;
    return (int32_t)(v < 0 ? v - 0.5 : v + 0.5);
}

bool WebViewWindowGeometry(Bounds document, Bounds visible, double scale,
                           wry::Rect* bounds, int32_t clip[4]) {
    int32_t originX = ToPhysicalPx(document.x, scale);
    int32_t originY = ToPhysicalPx(document.y, scale);
    int32_t width = ToPhysicalPx(document.w, scale);
    int32_t height = ToPhysicalPx(document.h, scale);
    int32_t startX = ToPhysicalPx(visible.x, scale);
    int32_t startY = ToPhysicalPx(visible.y, scale);
    int32_t endX = ToPhysicalPx(visible.x + visible.w, scale);
    int32_t endY = ToPhysicalPx(visible.y + visible.h, scale);
    auto clamp = [](int32_t v, int32_t hi) {
        return std::min(std::max(v, 0), hi);
    };
    // Wry rounds the window origin and extent independently. For a fully
    // visible far edge, use that extent instead of subtracting two rounded
    // positions.
    clip[0] = clamp(startX - originX, width);
    clip[1] = clamp(startY - originY, height);
    clip[2] = visible.x + visible.w >= document.x + document.w
                  ? width
                  : clamp(endX - originX, width);
    clip[3] = visible.y + visible.h >= document.y + document.h
                  ? height
                  : clamp(endY - originY, height);
    bounds->position = wry::PhysicalPosition(originX, originY);
    bounds->size = wry::PhysicalSize(width, height);
    return clip[0] != 0 || clip[1] != 0 || clip[2] != width ||
           clip[3] != height;
}

static void HideNative(WebView* self, wry::WebView* raw) {
    if (!self->nativeShown) {
        return;
    }
    wry::WebViewSetVisible(raw, false);
    self->nativeShown = false;
}

// The element's prepaint: layout has decided where the box is, so the OS
// control is moved there. Rust does this in `Element::prepaint`, which runs
// before the frame is drawn; the paint pass is where an element here first
// knows its box, and the two are a frame apart only if something else in the
// same frame reads the webview's bounds.
//
// `set_geometry`: the content mask is the clip. An empty clip hides the
// native view and leaves the page loaded. Each desktop backend clips the
// native child while keeping the page at the full document dimensions.
static bool ApplyGeometry(WebView* self, wry::WebView* raw, Bounds document,
                          Bounds visible) {
#if GPUI_WEBVIEW_NATIVE_CLIP
    if (!BoundsSame(visible, document)) {
        return WebViewNativeClipSet(self, raw, document, visible);
    }
    // `set_bounds` drops the region before taking the full document bounds.
    if (!WebViewNativeClipClear(self, raw)) {
        return false;
    }
#else
    (void)self;
    (void)visible;
#endif
    // Logical DIPs. Upstream Linux passes device pixels because lb-wry
    // then multiplies by GDK's factor. `WebViewSetBounds` on Linux
    // already does that conversion for the X11 child, so doing it here
    // would double it.
    Bounds frame = document;
#if GPUI_WEBVIEW_WHOLE_POINT_FRAME
    frame = WebViewDocumentFrame(document);
#endif
    wry::Rect r;
    r.position = wry::LogicalPosition(frame.x, frame.y);
    r.size = wry::LogicalSize(frame.w, frame.h);
#if GPUI_WRY_CLIPPED_CHILD
    wry::Rect viewport = {wry::LogicalPosition(visible.x, visible.y),
                          wry::LogicalSize(visible.w, visible.h)};
    return wry::WebViewSetClippedBounds(raw, r, viewport);
#else
    return wry::WebViewSetBounds(raw, r);
#endif
}

static void PaintWebView(PaintCtx* ctx, El* e, void* user) {
    WebView* self = (WebView*)user;
    Bounds document = e->Bounds();
    self->bounds = document;
    wry::WebView* raw = self->owned.Raw();
    if (!raw) {
        return;
    }
    // `match_scale_factor`: GDK's factor is an integer, so a fractional
    // GPUI scale would otherwise draw the page large. The script's page
    // zoom multiplies that factor. Other platforms return without
    // changing the zoom.
    float gpuiScale = ctx && ctx->dpi > 0 ? ctx->dpi / 96.f : 1.f;
    if (!self->hasPageScale || self->pageScale != gpuiScale) {
        if (wry::WebViewMatchPageScale(raw, gpuiScale)) {
            self->pageScale = gpuiScale;
            self->hasPageScale = true;
        }
    }
    if (!self->visible) {
        HideNative(self, raw);
        return;
    }
    Bounds visible = ctx && ctx->hasHitMask
                         ? ClipIntersect(document, ctx->hitMask)
                         : document;
    bool empty = visible.w <= 0.f || visible.h <= 0.f;
    if (empty) {
        HideNative(self, raw);
        return;
    }
    bool same = self->hasApplied && BoundsSame(self->applied, document) &&
                BoundsSame(self->appliedVisible, visible) &&
                self->appliedScale == gpuiScale;
    if (!same || self->geometryFailed) {
        if (!ApplyGeometry(self, raw, document, visible)) {
            if (!self->geometryFailed) {
                self->geometryFailed = true;
                logf("WebView geometry update failed\n");
            }
            HideNative(self, raw);
            return;
        }
        self->applied = document;
        self->appliedVisible = visible;
        self->appliedScale = gpuiScale;
        self->hasApplied = true;
        self->geometryFailed = false;
    }
    if (!self->nativeShown) {
        wry::WebViewSetVisible(raw, true);
        self->nativeShown = true;
    }
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
    if (self->visible) e->StopMouseDown();
    e->customPaint = PaintWebView;
    e->customUser = self;
    return e;
}

} // namespace gpui
