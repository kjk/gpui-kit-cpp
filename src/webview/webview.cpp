/* crates/webview/src/lib.rs — see webview.h. */

#include "webview/webview.h"

#include "gpui/paint.h"
#include "gpui/paint_backend.h"
#include "gpui/platform.h"
#include "gpui/scene.h"
#include "sys/executor.h"

namespace gpui {

static bool AppHasWindow(App* app, Window* win);
static void MatchWebViewScale(WebView* self, wry::WebView* raw, float scale);

static float WebViewScale(const PaintCtx* paint) {
    return paint && paint->dpi > 0 ? paint->dpi / 96.f : 1.f;
}

#if GPUI_WRY_SNAPSHOT
struct WebViewSnapshotRequest;

struct WebViewBackdrop {
    Vec<Bounds> cutouts;
    RenderImage* image = nullptr;
    Str png;
    Size size = {};
    float scale = 0;
    int refresh = 0;
    WebViewSnapshotRequest* pending = nullptr;
};

struct WebViewSnapshotRequest {
    WebView* view;
    Size size;
    float scale;
};

static void DropWebViewBackdrop(WebView* view) {
    WebViewBackdrop* backdrop = view->backdrop;
    if (!backdrop) return;
    view->backdrop = nullptr;
    if (backdrop->pending) backdrop->pending->view = nullptr;
    if (AppHasWindow(view->app, view->window)) {
        WindowCancelTimer(view->window, backdrop->refresh);
    }
    RenderImageRelease(backdrop->image);
    StrFree(backdrop->png);
    delete backdrop;
}
#endif

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
#if GPUI_WRY_SNAPSHOT
    DropWebViewBackdrop(this);
#endif
    // `impl Drop for WebView` hides it. Releasing `owned` after this body is
    // the Rc drop: an outstanding WebViewHandle postpones native destruction.
    if (wry::WebView* raw = owned.Raw()) {
        wry::WebViewSetPageClick(raw, nullptr, nullptr);
        wry::WebViewFocusParent(raw);
        wry::WebViewSetVisible(raw, false);
    }
    // The last frame can still contain the webview's focus entry or
    // native mouse blocker.
    if (AppHasWindow(app, window)) {
        AppInvalidate(window);
    }
}

void WebView::OnWindowMouseDown(WebView* self, Ctx* cx,
                                const MouseDownEvent* ev) {
    (void)cx;
    wry::WebView* raw = self->owned.Raw();
    if (!raw || !self->nativeShown || !ev) {
        return;
    }
    Point p = {ev->x, ev->y};
    bool hitsView = self->appliedVisible.Contains(p);
    if (hitsView && self->hasVisibleRegion) {
        hitsView = false;
        p.x -= self->appliedVisible.x;
        p.y -= self->appliedVisible.y;
        for (const Bounds& piece : self->visibleRegion) {
            if (piece.Contains(p)) {
                hitsView = true;
                break;
            }
        }
    }
    if (!hitsView) {
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

static Entity<WebView> CreateWebViewEntity(Ctx* cx) {
    if (!cx || !cx->app) return {};
    PlatAddLoopSource(WryLoopPrepare, WryLoopDispatch);
    // `EntityNewState`, not `EntityNew`: a `WebView` has no Render of its
    // own here, since `WebViewEl` hands the element to whoever wants it.
    Entity<WebView> handle = EntityNewState<WebView>(cx->app);
    WebView* self = handle.Get(cx->app);
    if (!self) {
        return handle;
    }
    self->window = cx->win;
    self->app = cx->app;
    self->selfId = handle.id;
    self->focus = FocusHandleNew(cx);
    self->visible = false;
    return handle;
}

void WebView::AttachNative(wry::WebView* raw) {
    auto* state = new WebViewHandleState();
    state->raw = raw;
    owned = WebViewHandle(state);
    visible = true;
    // Linux forwards presses on the page and compensates for the difference
    // between the GPUI and GTK scale. Other backends use the parent scale.
    wry::WebViewSetPageClick(raw, &WebView::OnPageClick, this);
    MatchWebViewScale(this, raw, WebViewScale(&window->paint));
}

static wry::Rect EmptyWebViewBounds() {
    return {wry::LogicalPosition(0, 0), wry::LogicalSize(0, 0)};
}

Entity<WebView> WebViewNew(Ctx* cx, const wry::WebViewAttributes* attrs) {
    Entity<WebView> handle = CreateWebViewEntity(cx);
    if (!handle.IsValid()) return handle;
    App* app = cx->app;
    void* window = PlatWindowHandle(cx->win);
    if (!window) {
        logf(
            "webview: this window has no OS handle to parent a webview into\n");
        return handle;
    }
    wry::WebViewAttributes copy = attrs ? *attrs : wry::WebViewAttributes{};
    // `WebView::new` starts it at nothing and lets the element place it.
    copy.bounds = EmptyWebViewBounds();
    copy.visible = false;
    wry::WebView* raw = wry::WebViewNew(window, &copy, /*asChild=*/true);
    if (raw) {
        if (WebView* self = handle.Get(app);
            self && AppHasWindow(app, self->window)) {
            self->AttachNative(raw);
        } else {
            wry::WebViewFree(raw);
        }
    }
    return handle;
}

Entity<WebView> WebViewAdopt(Ctx* cx, wry::WebView* raw) {
    Entity<WebView> handle = CreateWebViewEntity(cx);
    if (!raw) return handle;
    WebView* self = handle.IsValid() ? handle.Get(cx->app) : nullptr;
    if (!self || !PlatWindowHandle(self->window)) {
        wry::WebViewFree(raw);
        return handle;
    }
    // Keep the loaded control and let the GPUI element place it.
    wry::WebViewSetBounds(raw, EmptyWebViewBounds());
    wry::WebViewSetVisible(raw, false);
    self->AttachNative(raw);
    return handle;
}

static bool ApplyWebViewVisibility(WebView* self, wry::WebView* raw,
                                   bool visible) {
    if (!wry::WebViewSetVisible(raw, visible)) {
        if (visible && !self->nativeShown) {
            // The host may have mapped before its browser rejected showing.
            wry::WebViewSetVisible(raw, false);
        }
        return false;
    }
    self->nativeShown = visible;
    return true;
}

bool WebViewSetVisible(WebView* self, bool visible) {
    wry::WebView* raw = WebViewRaw(self);
    if (!raw) return false;
    bool changed = self->visible != visible || self->nativeShown != visible;
    bool focusOk = visible || !self->visible || wry::WebViewFocusParent(raw);
    if (!ApplyWebViewVisibility(self, raw, visible)) return false;
    self->visible = visible;
#if GPUI_WRY_SNAPSHOT
    if (!visible) DropWebViewBackdrop(self);
#endif
    if (changed && AppHasWindow(self->app, self->window)) {
        AppInvalidate(self->window);
    }
    return focusOk;
}

void WebViewShow(WebView* self) {
    WebViewSetVisible(self, true);
}

void WebViewHide(WebView* self) {
    WebViewSetVisible(self, false);
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
    WebView* view = Entity<WebView>{job->id}.Get(job->app);
    if (view && view->nativeShown && AppHasWindow(job->app, job->win)) {
        PlatformInput down = {};
        down.kind = PlatformInputKind::MouseDown;
        down.mouseDown.button = job->button;
        down.mouseDown.x = job->x;
        down.mouseDown.y = job->y;
        down.mouseDown.clickCount = 1;
        down.mouseDown.fromNativeSurface = true;
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

FocusHandle WebViewFocusHandle(const WebView* self) {
    return self ? self->focus : FocusHandle{};
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

void WebViewVisibleRegion(Bounds viewport, const Bounds* boxes, int count,
                          Vec<Bounds>* out) {
    VecClear(*out);
    if (viewport.w <= 0 || viewport.h <= 0) return;
    VecAppend(*out, viewport);
    for (int b = 0; b < count && len(*out); b++) {
        for (int i = len(*out) - 1; i >= 0; i--) {
            Bounds piece = (*out)[i];
            Bounds cut = ClipIntersect(piece, boxes[b]);
            if (cut.w <= 0 || cut.h <= 0) continue;
            VecRemoveAt(*out, i);
            const Bounds rest[] = {
                {piece.x, piece.y, piece.w, cut.y - piece.y},
                {piece.x, cut.Bottom(), piece.w, piece.Bottom() - cut.Bottom()},
                {piece.x, cut.y, cut.x - piece.x, cut.h},
                {cut.Right(), cut.y, piece.Right() - cut.Right(), cut.h},
            };
            for (Bounds part : rest) {
                if (part.w > 0 && part.h > 0) VecAppend(*out, part);
            }
        }
    }
    for (Bounds& piece : *out) {
        piece.x -= viewport.x;
        piece.y -= viewport.y;
    }
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

static bool HideNative(WebView* self, wry::WebView* raw) {
    return !self->nativeShown || ApplyWebViewVisibility(self, raw, false);
}

static void HideOmittedWebView(WebView* self, Ctx*, const void*) {
    if (wry::WebView* raw = self->owned.Raw()) HideNative(self, raw);
#if GPUI_WRY_SNAPSHOT
    DropWebViewBackdrop(self);
#endif
}

static bool ShowNative(WebView* self, wry::WebView* raw) {
    return self->nativeShown || ApplyWebViewVisibility(self, raw, true);
}

static void ShowPaintedWebView(WebView* self, wry::WebView* raw) {
    if (!ShowNative(self, raw)) return;
    Entity<WebView> owner{self->selfId};
    WindowTrackPaintedNativeSurface(
        self->window, ListenTo(owner, &HideOmittedWebView),
        ListenTo(owner, &WebView::OnWindowMouseDown));
}

static wry::Rect NativeWebViewRect(Bounds box, float scale) {
#if GPUI_WEBVIEW_DEVICE_GEOMETRY
    return {wry::PhysicalPosition(box.x * scale, box.y * scale),
            wry::PhysicalSize(box.w * scale, box.h * scale)};
#else
    (void)scale;
    return {wry::LogicalPosition(box.x, box.y), wry::LogicalSize(box.w, box.h)};
#endif
}

// Keep the hitbox in paint order, but leave it empty until the platform
// has placed and shown the surface.
static int ReserveWebViewHit(PaintCtx* ctx, FocusHandle focus) {
    HitRect hit;
    hit.focusId = focus.id;
    hit.popupDismissScope = ctx->popupDismissScope;
    hit.stopMouseDown = true;
    hit.paintLayer = ctx->paintLayer;
    hit.sceneContext = scene::CurrentContext(ctx);
    int index = len(ctx->hits);
    return VecAppend(ctx->hits, hit) ? index : -1;
}

#if GPUI_WRY_CLIPPED_CHILD || GPUI_WEBVIEW_NATIVE_CLIP
struct WebViewSurfacePaint {
    WebView* view;
    PaintCtx* paint;
    int hit;
};

static void WebViewSnapshotComplete(void* user, const uint8_t* bytes,
                                    int count) {
    auto* request = static_cast<WebViewSnapshotRequest*>(user);
    WebView* view = request->view;
    Size size = request->size;
    float scale = request->scale;
    delete request;
    if (!view) return;
    WebViewBackdrop& backdrop = *view->backdrop;
    backdrop.pending = nullptr;
    // A slow capture can outlive its timer. Let the next paint restart it.
    if (!backdrop.refresh && AppHasWindow(view->app, view->window)) {
        AppInvalidate(view->window);
    }
    if (!bytes || size.w != view->applied.w || size.h != view->applied.h ||
        scale != view->appliedScale || !AppHasWindow(view->app, view->window)) {
        return;
    }
    Str png((const char*)bytes, count);
    if (backdrop.image && backdrop.size.w == size.w &&
        backdrop.size.h == size.h && backdrop.scale == scale &&
        StrEq(backdrop.png, png)) {
        return;
    }
    RenderImage* image = RenderImageDecode(view->app->paint, bytes, count);
    if (!image) return;
    RenderImageRelease(backdrop.image);
    StrFree(backdrop.png);
    backdrop.image = image;
    backdrop.png = StrDup(png);
    backdrop.size = size;
    backdrop.scale = scale;
    AppInvalidate(view->window);
}

static void RefreshWebViewBackdrop(WebView* view, Ctx* cx, const void*) {
    if (view->backdrop) {
        view->backdrop->refresh = 0;
        AppInvalidate(cx->win);
    }
}

static void UpdateWebViewBackdrop(WebView* view, wry::WebView* raw, bool shaped,
                                  bool changed) {
    if (!shaped) {
        bool redraw = view->backdrop && view->backdrop->image;
        DropWebViewBackdrop(view);
        if (redraw) AppInvalidate(view->window);
        return;
    }
    if (!view->backdrop) view->backdrop = new WebViewBackdrop();
    WebViewBackdrop& backdrop = *view->backdrop;
    if (changed || !len(backdrop.cutouts)) {
        Bounds local = {0, 0, view->appliedVisible.w, view->appliedVisible.h};
        WebViewVisibleRegion(local, view->visibleRegion.els,
                             len(view->visibleRegion), &backdrop.cutouts);
        if (backdrop.image) AppInvalidate(view->window);
    }
    // Refresh while an overlay covers the live browser. Repaint immediately
    // when the first shot arrives, then at most once per changed snapshot.
    if (!backdrop.refresh && !backdrop.pending && view->nativeShown &&
        AppHasWindow(view->app, view->window)) {
        constexpr int kRefreshMs = 66;
        backdrop.refresh = WindowSetTimeout(
            view->window, kRefreshMs,
            ListenTo(Entity<WebView>{view->selfId}, &RefreshWebViewBackdrop));
        backdrop.pending = new WebViewSnapshotRequest{
            view, {view->applied.w, view->applied.h}, view->appliedScale};
        wry::WebViewSnapshot(raw, backdrop.pending, &WebViewSnapshotComplete);
    }
}

static void FinishNativeWebView(void* user, const Bounds* overlays, int count) {
    auto* surface = static_cast<WebViewSurfacePaint*>(user);
    WebView* self = surface->view;
    wry::WebView* raw = self->owned.Raw();
    if (!raw) return;
    Bounds viewport = self->appliedVisible;
    Vec<Bounds> region;
    if (count) WebViewVisibleRegion(viewport, overlays, count, &region);
    bool shaped =
        count && !(len(region) == 1 &&
                   BoundsSame(region[0], {0, 0, viewport.w, viewport.h}));
    bool changed = self->visibleRegionNeedsUpdate ||
                   self->hasVisibleRegion != shaped ||
                   (shaped && len(region) != len(self->visibleRegion));
#if GPUI_WEBVIEW_LIVE_OVERLAY
    PaintCtx* paint = surface->paint;
    // Shadows and borders can reach the page without an input cutout.
    bool liveOverlay =
        len(paint->deferredPaint) && PaintEnableNativeOverlay(paint);
    changed = changed || self->liveOverlay != liveOverlay;
#endif
    for (int i = 0; shaped && !changed && i < len(region); i++) {
        changed = !BoundsSame(region[i], self->visibleRegion[i]);
    }
    if (changed) {
#if GPUI_WEBVIEW_NATIVE_CLIP
        Bounds empty = {};
        bool applied =
            WebViewNativeVisibleRegionSet(self, raw,
                                          liveOverlay || !shaped ? nullptr
                                          : len(region)          ? region.els
                                                                 : &empty,
                                          liveOverlay ? 0 : len(region));
#else
        Vec<wry::Rect> pieces;
        if (shaped) {
            for (const Bounds& piece : region) {
                VecAppend(pieces, NativeWebViewRect(piece, self->appliedScale));
            }
        }
        wry::Rect empty = {};
        const wry::Rect* clip = !shaped       ? nullptr
                                : len(pieces) ? pieces.els
                                              : &empty;
#if GPUI_WRY_INPUT_REGION
        bool applied =
            liveOverlay ? wry::WebViewSetInputRegion(raw, clip, len(pieces))
                        : wry::WebViewSetVisibleRegion(raw, clip, len(pieces));
#else
        bool applied = wry::WebViewSetVisibleRegion(raw, clip, len(pieces));
#endif
#endif
        if (!applied) {
            HideNative(self, raw);
            DropWebViewBackdrop(self);
            return;
        }
        self->visibleRegion = region;
        self->hasVisibleRegion = shaped;
        self->visibleRegionNeedsUpdate = false;
#if GPUI_WEBVIEW_LIVE_OVERLAY
        self->liveOverlay = liveOverlay;
#endif
    }
    ShowPaintedWebView(self, raw);
    if (self->nativeShown && surface->hit >= 0) {
        surface->paint->hits[surface->hit].bounds = viewport;
    }
#if GPUI_WEBVIEW_LIVE_OVERLAY
    if (liveOverlay) {
        DropWebViewBackdrop(self);
        return;
    }
#endif
    UpdateWebViewBackdrop(self, raw, shaped, changed);
}
#endif

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
                          Bounds visible, float scale) {
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
    // X11 takes the device-pixel box of the GPUI parent. The other
    // backends take their window's logical coordinates.
    Bounds frame = document;
#if GPUI_WEBVIEW_WHOLE_POINT_FRAME
    frame = WebViewDocumentFrame(document);
#endif
    wry::Rect r = NativeWebViewRect(frame, scale);
#if GPUI_WRY_CLIPPED_CHILD
    return wry::WebViewSetClippedBounds(raw, r,
                                        NativeWebViewRect(visible, scale));
#else
    return wry::WebViewSetBounds(raw, r);
#endif
}

static bool UpdateWebViewGeometry(WebView* self, wry::WebView* raw,
                                  Bounds document, Bounds visible, float scale,
                                  bool* unchanged = nullptr) {
    bool same = self->hasApplied && !self->geometryFailed &&
                BoundsSame(self->applied, document) &&
                BoundsSame(self->appliedVisible, visible) &&
                self->appliedScale == scale;
    if (unchanged) *unchanged = same;
    if (same) return true;
    if (!ApplyGeometry(self, raw, document, visible, scale)) {
        if (!self->geometryFailed) logf("WebView geometry update failed\n");
        self->geometryFailed = true;
        return false;
    }
    self->applied = document;
    self->appliedVisible = visible;
    self->appliedScale = scale;
    self->visibleRegionNeedsUpdate = self->hasVisibleRegion;
    self->hasApplied = true;
    self->geometryFailed = false;
    return true;
}

static void MatchWebViewScale(WebView* self, wry::WebView* raw, float scale) {
    // `match_scale_factor`: GDK's factor is an integer, so a fractional
    // GPUI scale would otherwise draw the page large. The script's page
    // zoom multiplies that factor. Other platforms return without
    // changing the zoom.
    if (!self->hasPageScale || self->pageScale != scale) {
        if (wry::WebViewMatchPageScale(raw, scale)) {
            self->pageScale = scale;
            self->hasPageScale = true;
        }
    }
}

static bool PrepareWebViewGeometry(WebView* self, wry::WebView* raw,
                                   Bounds document, Bounds visible, float scale,
                                   bool* unchanged = nullptr) {
    if (!self->visible || visible.w <= 0 || visible.h <= 0) {
        bool ok = HideNative(self, raw);
#if GPUI_WRY_CLIPPED_CHILD
        // composition::set_geometry prepares the full layout while the
        // native container is hidden. It only becomes visible with a clip.
        if (ok && self->visible && document.w > 0 && document.h > 0) {
            ok = UpdateWebViewGeometry(self, raw, document, document, scale);
        }
#endif
#if GPUI_WRY_SNAPSHOT
        DropWebViewBackdrop(self);
#endif
        return ok;
    }
    if (UpdateWebViewGeometry(self, raw, document, visible, scale, unchanged))
        return true;
    HideNative(self, raw);
#if GPUI_WRY_SNAPSHOT
    DropWebViewBackdrop(self);
#endif
    return false;
}

// A direct setter only has a document and clip. Any holes left by deferred
// overlays in the last element paint must be removed before applying it.
static bool ClearWebViewOverlay(WebView* self, wry::WebView* raw) {
    if (self->hasVisibleRegion) {
#if GPUI_WEBVIEW_NATIVE_CLIP
        self->hasApplied = false;
        if (!WebViewNativeClipClear(self, raw)) return false;
#elif GPUI_WRY_CLIPPED_CHILD
        if (!wry::WebViewSetVisibleRegion(raw, nullptr, 0)) return false;
#else
        (void)raw;
#endif
        VecClear(self->visibleRegion);
        self->hasVisibleRegion = false;
        self->visibleRegionNeedsUpdate = false;
    }
#if GPUI_WRY_SNAPSHOT
    DropWebViewBackdrop(self);
#endif
    return true;
}

bool WebViewSetBounds(WebView* self, wry::Rect bounds) {
    wry::WebView* raw = WebViewRaw(self);
    if (!raw || !AppHasWindow(self->app, self->window) ||
        !ClearWebViewOverlay(self, raw)) {
        return false;
    }
    self->hasApplied = false;
#if GPUI_WRY_CLIPPED_CHILD
    float scale = WebViewScale(&self->window->paint);
    double positionScale = bounds.position.logical ? 1. : scale;
    double sizeScale = bounds.size.logical ? 1. : scale;
    Bounds document = {(float)(bounds.position.x / positionScale),
                       (float)(bounds.position.y / positionScale),
                       (float)(bounds.size.width / sizeScale),
                       (float)(bounds.size.height / sizeScale)};
    return ApplyGeometry(self, raw, document, document, scale);
#else
#if GPUI_WEBVIEW_NATIVE_CLIP
    if (!WebViewNativeClipClear(self, raw)) return false;
#endif
    return wry::WebViewSetBounds(raw, bounds);
#endif
}

bool WebViewSetGeometry(WebView* self, Bounds document, Bounds clip) {
    wry::WebView* raw = WebViewRaw(self);
    if (!raw || !AppHasWindow(self->app, self->window) ||
        !ClearWebViewOverlay(self, raw)) {
        return false;
    }
    float scale = WebViewScale(&self->window->paint);
    MatchWebViewScale(self, raw, scale);
    Bounds visible = ClipIntersect(document, clip);
    if (!PrepareWebViewGeometry(self, raw, document, visible, scale)) {
        return false;
    }
    return !self->visible || visible.w <= 0 || visible.h <= 0 ||
           ShowNative(self, raw);
}

static void PaintWebView(PaintCtx* ctx, El* e, void* user) {
    WebView* self = (WebView*)user;
    Bounds document = e->Bounds();
    self->bounds = document;
    wry::WebView* raw = self->owned.Raw();
    if (!raw) return;
    float gpuiScale = WebViewScale(ctx);
    MatchWebViewScale(self, raw, gpuiScale);
    Bounds visible = ctx && ctx->hasHitMask
                         ? ClipIntersect(document, ctx->hitMask)
                         : document;
    bool same = false;
    if (!PrepareWebViewGeometry(self, raw, document, visible, gpuiScale,
                                &same) ||
        !self->visible || visible.w <= 0 || visible.h <= 0) {
        return;
    }
    int hit = ReserveWebViewHit(ctx, self->focus);
#if GPUI_WRY_CLIPPED_CHILD || GPUI_WEBVIEW_NATIVE_CLIP
    const WebViewBackdrop* backdrop = self->backdrop;
    if (same && backdrop && backdrop->image && backdrop->size.w == document.w &&
        backdrop->size.h == document.h && backdrop->scale == gpuiScale) {
        Bounds frame = document;
#if GPUI_WEBVIEW_WHOLE_POINT_FRAME
        frame = WebViewDocumentFrame(frame);
#endif
        for (Bounds cutout : backdrop->cutouts) {
            cutout.x += visible.x;
            cutout.y += visible.y;
            RenderImageDraw(ctx, backdrop->image, cutout, frame, 0, 0.f, false);
        }
    }
    auto* surface = ArenaNew<WebViewSurfacePaint>(
        e->arena, WebViewSurfacePaint{self, ctx, hit});
    VecAppend(ctx->nativeSurfacePaints,
              PaintCtx::NativeSurfacePaint{&FinishNativeWebView, surface,
                                           len(ctx->nativeOverlayBounds)});
#else
    ShowPaintedWebView(self, raw);
    if (self->nativeShown && hit >= 0) ctx->hits[hit].bounds = visible;
#endif
}

El* WebViewEl(Entity<WebView> view, Ctx* cx) {
    WebView* self = view.Get(cx->app);
    // `Style { size: Size::full(), flex_shrink: 1., ..Default }`.
    El* e = Div(cx->a)->SizeFull();
    if (!self || self->window != cx->win) {
        return e;
    }
    e->TrackFocus(self->focus)->TabStop(false)->FocusOnPress();
    e->customPaint = PaintWebView;
    e->customUser = self;
    return e;
}

} // namespace gpui
