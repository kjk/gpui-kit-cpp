/* wry/src/webkitgtk/ — mod.rs, web_context.rs, drag_drop.rs and
 * synthetic_mouse_events.rs: the WebKitGTK backend.
 *
 * Part of the C++ port of lb-wry 0.53.3 (see src/wry/readme.md).
 *
 * WebKitGTK is a soft dependency, the second after libcurl: cmd/build.ts
 * defines GPUI_HAVE_WEBKITGTK when `pkg-config webkit2gtk-4.1` answers, and
 * without it the bottom of this file answers the portable API with "there is
 * no webview here", which is all this file was before.
 *
 * What is Rust's and what is not. The Rust crate's X11 path (`new_x11`) is
 * ported: GDK opens its own connection to the X server, a container window
 * is made inside the parent through it, and a GTK window with a vertical box
 * and the WebKitWebView in it is put in that container. Three things differ,
 * each because this tree's Linux window is not tao's:
 *
 *   - GTK's main loop is not the application's. tao's event loop *is* GTK's,
 *     so Rust never pumps anything. Here the X11 loop in
 *     src/gpui/window_linux.cpp turns GLib's default main context through
 *     `EventLoopPrepare` / `EventLoopDispatch` — prepare, query, poll, check,
 *     dispatch, the protocol GLib documents for embedding its context in a
 *     foreign loop — so GTK, GDK's own X connection and WebKit's IPC with
 *     its web process all run on this thread between X11 events.
 *   - The GTK window is a GtkPlug, not a GtkWindow whose GdkWindow has been
 *     swapped for a foreign wrapper of the container. GTK 3 gives a foreign
 *     GdkWindow no frame clock and selects no input on it, so a page in one
 *     neither repaints nor sees the mouse unless something outside GTK drives
 *     both; tao never noticed because its windows are GTK's own. A plug is
 *     GTK's sanctioned way into a foreign X11 window: a real GDK toplevel
 *     made *inside* the container, with a frame clock and its own events.
 *     This file is the embedder side of that, as little of XEmbed as a plug
 *     needs: map it, size it, and hand it the X focus.
 *   - A webview that is not a child gets a container too, sized to the
 *     parent and kept that way from the parent's ConfigureNotify. Rust wraps
 *     the parent itself, which here is the gpui window: GDK would select
 *     ButtonPress on a window gpui already selects it on, which X refuses.
 *
 * The X focus. X delivers keys to the focus window, not to the GTK widget
 * that has GTK's focus, so `WebViewFocus` (and a click on the page, which is
 * what WebView2 and WKWebView do of their own accord) moves the X focus to the
 * plug, and `WebViewFocusParent` gives it back — only if the page had it, so
 * that a hidden or dropped webview never steals the focus from another
 * application.
 */
#include "wry/wry.h"

#if defined(GPUI_HAVE_WEBKITGTK) && GPUI_HAVE_WEBKITGTK

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gdk/gdkx.h>
#include <webkit2/webkit2.h>
#include <X11/Xlib.h>
#include <math.h>

// The GTK 3 / WebKitGTK 4.1 API this port targets is the one Rust's
// `webkit2gtk` crate binds, a few of whose calls (the scrollbar appearance,
// `run_javascript` before 2.40) are deprecated in later releases. They still
// work; the warning would be an error under -Werror.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace wry {

using base::logf;
using base::Str;
using base::StrDup;
using base::StrFree;

// <X11/Xlib.h> spells `None` as a macro, so the enumerators named that are
// written here as their value-initialised enum.
static constexpr CookieSameSite kSameSiteNone = CookieSameSite();

static const char* const kWebViewIdKey = "webview_id";
static const char* const kIpcScript =
    "Object.defineProperty(window, 'ipc', { value: Object.freeze({ "
    "postMessage: function(x) { "
    "window.webkit.messageHandlers['ipc'].postMessage(x) } }) })";

// One GCallback type for every signal, whatever its arguments: the cast GLib
// wants, through void* so that -Wcast-function-type has nothing to say.
template <typename F>
static GCallback Cb(F f) {
    return (GCallback)(void*)f;
}

// A C string for GLib. Empty for a null Str — use CStrOrNull where null is
// Rust's `None`.
static const char* CStrTemp(Str s) {
    if (!s.s || len(s) <= 0) {
        return "";
    }
    Str z = base::StrDupTemp(s);
    return z.s ? z.s : "";
}

static const char* CStrOrNull(Str s) {
    return s.s ? CStrTemp(s) : nullptr;
}

static Str FromCTemp(const char* s) {
    return s ? base::StrDupTemp(Str(s)) : Str();
}

// ─── GTK, once ───────────────────────────────────────────────────────────

static bool gGtkTried = false;
static bool gGtkOk = false;

// tao calls `gtk::init` before any window exists; nothing here does, so the
// first webview does it. GDK must reach the X server this tree's window is
// on: with WAYLAND_DISPLAY also set (any Wayland desktop, WSLg) it would
// otherwise pick Wayland, where there is no X11 window to go inside.
static bool EnsureGtk() {
    if (gGtkTried) {
        return gGtkOk;
    }
    gGtkTried = true;
    gdk_set_allowed_backends("x11");
    // The host already chose its locale; GTK would only call setlocale again.
    gtk_disable_setlocale();
    if (!gtk_init_check(nullptr, nullptr)) {
        logf("wry: GTK could not open the X display\n");
        return false;
    }
    GdkDisplay* display = gdk_display_get_default();
    if (!display || !GDK_IS_X11_DISPLAY(display)) {
        logf("wry: GTK did not open an X11 display\n");
        return false;
    }
    // The default context is this thread's from here on: only its owner may
    // check and dispatch it, which is what EventLoopDispatch does.
    if (!g_main_context_acquire(g_main_context_default())) {
        logf("wry: GLib's main context belongs to another thread\n");
        return false;
    }
    gGtkOk = true;
    return true;
}

static ::Display* XDisplayOf(GdkDisplay* display) {
    return gdk_x11_display_get_xdisplay(GDK_X11_DISPLAY(display));
}

// X errors on GDK's connection end the process unless trapped, and a window
// of ours can vanish with its parent at any time.
struct XTrap {
    GdkDisplay* display;
    explicit XTrap(GdkDisplay* d) : display(d) {
        gdk_x11_display_error_trap_push(display);
    }
    ~XTrap() { gdk_x11_display_error_trap_pop_ignored(display); }
};

// ─── the loop GTK runs on ────────────────────────────────────────────────

static Vec<PollFd> gPollFds;
static int gPollCount = 0;
static int gMaxPriority = 0;
static bool gPrepared = false;

static_assert(sizeof(PollFd) == sizeof(GPollFD), "GPollFD layout");

int EventLoopPrepare(PollFd** fds, int* timeoutMs) {
    *fds = nullptr;
    if (!gGtkOk) {
        return 0;
    }
    GMainContext* context = g_main_context_default();
    g_main_context_prepare(context, &gMaxPriority);
    int timeout = -1;
    // Query says how many descriptors there are even when they do not fit,
    // and is asked again with room for them all.
    int room = gPollFds.cap > 0 ? gPollFds.cap : 0;
    for (;;) {
        int n = g_main_context_query(context, gMaxPriority, &timeout,
                                     (GPollFD*)gPollFds.els, room);
        if (n <= room) {
            gPollCount = n;
            break;
        }
        if (!VecReserve(gPollFds, n)) {
            gPollCount = 0;
            break;
        }
        room = n;
    }
    for (int i = 0; i < gPollCount; i++) {
        gPollFds.els[i].revents = 0;
    }
    gPrepared = true;
    if (timeout >= 0 && (*timeoutMs < 0 || timeout < *timeoutMs)) {
        *timeoutMs = timeout;
    }
    *fds = gPollFds.els;
    return gPollCount;
}

void EventLoopDispatch() {
    if (!gPrepared) {
        return;
    }
    gPrepared = false;
    GMainContext* context = g_main_context_default();
    if (g_main_context_check(context, gMaxPriority, (GPollFD*)gPollFds.els,
                             gPollCount)) {
        g_main_context_dispatch(context);
    }
}

// Rust's cookie calls spin `gtk::main_iteration` until the answer is in.
static void PumpUntil(const bool* done) {
    while (!*done) {
        g_main_context_iteration(nullptr, TRUE);
    }
}

// ─── the webview ─────────────────────────────────────────────────────────

struct WebView;

// `related_webviews`: a page's `window.open` that the handler allowed, in a
// GTK window of its own.
struct RelatedWebView {
    GtkWidget* window;
    WebView* webview;
};

struct DownloadState;

enum class DragState : uint8_t {
    Left,
    Entered,
    Leaving,
};

struct WebView {
    Str id;
    WebKitWebView* webview = nullptr; // our reference; the widget may die
    WebKitWebContext* context = nullptr;
    WebKitUserContentManager* manager = nullptr;
    GCancellable* cancellable = nullptr;

    // `X11Data`, plus the plug.
    bool hasX11 = false;
    bool isChild = false;
    GdkDisplay* display = nullptr;
    ::Display* xdisplay = nullptr;
    ::Window x11Window = 0;
    ::Window parentWindow = 0;
    GtkWidget* gtkWindow = nullptr;
    bool followsParent = false;

    bool inspectorOpen = false;

    // `pending_scripts`: an eval before the first navigation commits has
    // nothing to run in, so it is held and replayed on commit. Rust keeps an
    // `Option<Vec<String>>` and takes the option to close it.
    Vec<Str> pendingScripts;
    bool pendingOpen = true;

    void* ctx = nullptr;
    void (*ipcHandler)(void* ctx, Str url, Str body) = nullptr;
    bool (*navigationHandler)(void* ctx, Str url) = nullptr;
    void (*documentTitleChangedHandler)(void* ctx, Str title) = nullptr;
    void (*onPageLoadHandler)(void* ctx, PageLoadEvent event,
                              Str url) = nullptr;
    NewWindowResponse (*newWindowReqHandler)(
        void* ctx, Str url, const NewWindowFeatures* features,
        WebView** createdWebView) = nullptr;
    DownloadStartedHandler downloadStartedHandler = nullptr;
    DownloadCompletedHandler downloadCompletedHandler = nullptr;
    DragDropHandler dragDropHandler = nullptr;
    WebViewPageClick pageClick = nullptr;
    void* pageClickUser = nullptr;

    // drag_drop.rs's `DragDropController`.
    DragState dragState = DragState::Left;
    Vec<Str> dragPaths;
    bool hasDragPaths = false;
    int dragX = 0;
    int dragY = 0;
    guint dragLeaveIdle = 0;

    // synthetic_mouse_events.rs's `BackForwardState`.
    uint8_t bfState = 0;

    Vec<RelatedWebView> related;
    Vec<DownloadState*> downloads;
};

static void FreeStrs(Vec<Str>* v) {
    for (int i = 0; i < v->len; i++) {
        StrFree(v->els[i]);
    }
    VecReset(*v);
}

static Str UriTemp(WebKitWebView* webview) {
    return FromCTemp(webkit_web_view_get_uri(webview));
}

// ─── scripts ─────────────────────────────────────────────────────────────

struct EvalCall {
    void* ctx;
    void (*callback)(void* ctx, Str result);
};

static void OnEvalDone(GObject* source, GAsyncResult* result, gpointer data) {
    EvalCall* call = (EvalCall*)data;
    GError* error = nullptr;
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    JSCValue* value = webkit_web_view_evaluate_javascript_finish(
        WEBKIT_WEB_VIEW(source), result, &error);
#else
    WebKitJavascriptResult* js = webkit_web_view_run_javascript_finish(
        WEBKIT_WEB_VIEW(source), result, &error);
    JSCValue* value = nullptr;
    if (js) {
        value =
            (JSCValue*)g_object_ref(webkit_javascript_result_get_js_value(js));
        webkit_javascript_result_unref(js);
    }
#endif
    bool cancelled =
        error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    // `result.map(|r| r.js_value().and_then(|js| js.to_json(0)))`, and an
    // empty string for anything that did not make it.
    char* json = value ? jsc_value_to_json(value, 0) : nullptr;
    if (call && call->callback && !cancelled) {
        call->callback(call->ctx, json ? Str(json) : Str());
    }
    g_free(json);
    if (value) {
        g_object_unref(value);
    }
    if (error) {
        g_error_free(error);
    }
    delete call;
}

static void RunJavascript(WebView* wv, Str js, void* ctx,
                          void (*callback)(void* ctx, Str result)) {
    EvalCall* call = nullptr;
    if (callback) {
        call = new EvalCall{ctx, callback};
    }
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    webkit_web_view_evaluate_javascript(wv->webview, CStrTemp(js), -1, nullptr,
                                        nullptr, wv->cancellable,
                                        call ? OnEvalDone : nullptr, call);
#else
    webkit_web_view_run_javascript(wv->webview, CStrTemp(js), wv->cancellable,
                                   call ? OnEvalDone : nullptr, call);
#endif
}

static void FlushPendingScripts(WebView* wv) {
    if (!wv->pendingOpen) {
        return;
    }
    wv->pendingOpen = false;
    for (int i = 0; i < wv->pendingScripts.len; i++) {
        RunJavascript(wv, wv->pendingScripts[i], nullptr, nullptr);
    }
    FreeStrs(&wv->pendingScripts);
}

// `InnerWebView::init`.
static bool AddUserScript(WebView* wv, Str js, bool forMainFrameOnly) {
    if (!wv->manager) {
        logf("wry: the webview has no user content manager\n");
        return false;
    }
    WebKitUserScript* script = webkit_user_script_new(
        CStrTemp(js),
        forMainFrameOnly ? WEBKIT_USER_CONTENT_INJECT_TOP_FRAME
                         : WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(wv->manager, script);
    webkit_user_script_unref(script);
    return true;
}

// ─── the X focus ─────────────────────────────────────────────────────────

static ::Window PlugXid(WebView* wv) {
    if (!wv->gtkWindow) {
        return 0;
    }
    GdkWindow* window = gtk_widget_get_window(wv->gtkWindow);
    return window ? gdk_x11_window_get_xid(window) : 0;
}

static bool OwnsXFocus(WebView* wv) {
    ::Window focus = 0;
    int revert = 0;
    XGetInputFocus(wv->xdisplay, &focus, &revert);
    return focus != 0 && (focus == PlugXid(wv) || focus == wv->x11Window);
}

static void GiveXFocus(WebView* wv, Time time) {
    if (!wv->hasX11) {
        return;
    }
    ::Window plug = PlugXid(wv);
    if (!plug) {
        return;
    }
    XTrap trap(wv->display);
    XSetInputFocus(wv->xdisplay, plug, RevertToParent, time);
    XFlush(wv->xdisplay);
}

// ─── signal handlers ─────────────────────────────────────────────────────

static void OnClose(WebKitWebView* webview, gpointer) {
    // `connect_close(|webview| webview.destroy())`: `window.close()`.
    gtk_widget_destroy(GTK_WIDGET(webview));
}

static void OnTitle(GObject*, GParamSpec*, gpointer data) {
    WebView* wv = (WebView*)data;
    if (wv->documentTitleChangedHandler) {
        wv->documentTitleChangedHandler(
            wv->ctx, FromCTemp(webkit_web_view_get_title(wv->webview)));
    }
}

static void OnLoadChanged(WebKitWebView* webview, WebKitLoadEvent event,
                          gpointer data) {
    WebView* wv = (WebView*)data;
    if (event == WEBKIT_LOAD_COMMITTED) {
        if (wv->onPageLoadHandler) {
            wv->onPageLoadHandler(wv->ctx, PageLoadEvent::Started,
                                  UriTemp(webview));
        }
        FlushPendingScripts(wv);
    } else if (event == WEBKIT_LOAD_FINISHED && wv->onPageLoadHandler) {
        wv->onPageLoadHandler(wv->ctx, PageLoadEvent::Finished,
                              UriTemp(webview));
    }
}

static gboolean OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision* decision,
                               WebKitPolicyDecisionType type, gpointer data) {
    WebView* wv = (WebView*)data;
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION ||
        !wv->navigationHandler) {
        return FALSE;
    }
    WebKitNavigationAction* action =
        webkit_navigation_policy_decision_get_navigation_action(
            WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    WebKitURIRequest* request =
        action ? webkit_navigation_action_get_request(action) : nullptr;
    const char* uri = request ? webkit_uri_request_get_uri(request) : nullptr;
    if (!uri) {
        return FALSE;
    }
    if (wv->navigationHandler(wv->ctx, FromCTemp(uri))) {
        webkit_policy_decision_use(decision);
    } else {
        webkit_policy_decision_ignore(decision);
    }
    return TRUE;
}

static void OnScriptMessage(WebKitUserContentManager*,
                            WebKitJavascriptResult* result, gpointer data) {
    WebView* wv = (WebView*)data;
    JSCValue* value =
        result ? webkit_javascript_result_get_js_value(result) : nullptr;
    if (!value || !wv->ipcHandler) {
        return;
    }
    char* body = jsc_value_to_string(value);
    wv->ipcHandler(wv->ctx, UriTemp(wv->webview), body ? Str(body) : Str());
    g_free(body);
}

static gboolean OnInspectorBringToFront(WebKitWebInspector*, gpointer data) {
    ((WebView*)data)->inspectorOpen = true;
    return FALSE;
}

static void OnInspectorClosed(WebKitWebInspector*, gpointer data) {
    ((WebView*)data)->inspectorOpen = false;
}

// synthetic_mouse_events.rs.
static const uint8_t kBack = 1;
static const uint8_t kForward = 2;

static void DispatchSyntheticMouse(WebView* wv, GdkEventButton* event,
                                   bool pressed) {
    SyntheticMouseEvent ev;
    ev.pressed = pressed;
    ev.button = (int)event->button;
    ev.x = (int)event->x;
    ev.y = (int)event->y;
    guint state = event->state;
    ev.buttons = ((state & GDK_BUTTON1_MASK) ? 1 : 0) +
                 ((state & GDK_BUTTON3_MASK) ? 2 : 0) +
                 ((state & GDK_BUTTON2_MASK) ? 4 : 0) +
                 ((wv->bfState & kBack) ? 8 : 0) +
                 ((wv->bfState & kForward) ? 16 : 0);
    // `event.click_count().unwrap_or(1)`.
    guint clicks = 1;
    if (!gdk_event_get_click_count((GdkEvent*)event, &clicks)) {
        clicks = 1;
    }
    ev.detail = (int)clicks;
    ev.ctrlKey = (state & GDK_CONTROL_MASK) != 0;
    ev.altKey = (state & GDK_MOD1_MASK) != 0;
    ev.shiftKey = (state & GDK_SHIFT_MASK) != 0;
    ev.metaKey = (state & GDK_SUPER_MASK) != 0;
    RunJavascript(wv, SyntheticMouseEventJsTemp(&ev), nullptr, nullptr);
}

static gboolean OnButtonPress(GtkWidget*, GdkEventButton* event,
                              gpointer data) {
    WebView* wv = (WebView*)data;
    // Not in the Rust: a click on the page takes the keyboard there, which
    // WebView2 and WKWebView do of their own accord and X11 does not.
    if (wv->hasX11) {
        GiveXFocus(wv, event->time);
        gtk_widget_grab_focus(GTK_WIDGET(wv->webview));
    }
    if (event->button == 8 || event->button == 9) {
        wv->bfState |= event->button == 8 ? kBack : kForward;
        DispatchSyntheticMouse(wv, event, true);
        return TRUE;
    }
    // Buttons 1-3 also go to GPUI. The page still receives the press.
    if (wv->pageClick && event->type == GDK_BUTTON_PRESS &&
        (event->button == 1 || event->button == 2 || event->button == 3)) {
        float scale =
            (float)gtk_widget_get_scale_factor(GTK_WIDGET(wv->webview));
        wv->pageClick(wv->pageClickUser, (int)event->button, (float)event->x,
                      (float)event->y, scale);
    }
    return FALSE;
}

static gboolean OnButtonRelease(GtkWidget*, GdkEventButton* event,
                                gpointer data) {
    WebView* wv = (WebView*)data;
    if (event->button == 8 || event->button == 9) {
        wv->bfState &= (uint8_t)~(event->button == 8 ? kBack : kForward);
        DispatchSyntheticMouse(wv, event, false);
        return TRUE;
    }
    return FALSE;
}

// drag_drop.rs.
static void OnDragDataReceived(GtkWidget*, GdkDragContext*, gint, gint,
                               GtkSelectionData* data, guint info, guint,
                               gpointer user) {
    WebView* wv = (WebView*)user;
    if (info != 2) {
        return;
    }
    gchar** uris = gtk_selection_data_get_uris(data);
    Vec<Str> paths;
    for (int i = 0; uris && uris[i]; i++) {
        VecAppend(paths, StrDup(PathFromFileUriTemp(Str(uris[i]))));
    }
    g_strfreev(uris);
    wv->dragState = DragState::Entered;
    DragDropEvent ev;
    ev.kind = DragDropKind::Enter;
    ev.paths = paths.els;
    ev.pathCount = paths.len;
    ev.x = wv->dragX;
    ev.y = wv->dragY;
    wv->dragDropHandler(wv->ctx, &ev);
    FreeStrs(&wv->dragPaths);
    wv->dragPaths = paths;
    wv->hasDragPaths = true;
}

static gboolean OnDragMotion(GtkWidget*, GdkDragContext*, gint x, gint y, guint,
                             gpointer user) {
    WebView* wv = (WebView*)user;
    if (wv->dragState == DragState::Entered) {
        DragDropEvent ev;
        ev.kind = DragDropKind::Over;
        ev.x = x;
        ev.y = y;
        wv->dragDropHandler(wv->ctx, &ev);
    } else {
        wv->dragX = x;
        wv->dragY = y;
    }
    return FALSE;
}

static gboolean OnDragDrop(GtkWidget*, GdkDragContext* context, gint x, gint y,
                           guint time, gpointer user) {
    WebView* wv = (WebView*)user;
    if (wv->dragState != DragState::Leaving || !wv->hasDragPaths) {
        return FALSE;
    }
    Vec<Str> paths = wv->dragPaths;
    wv->dragPaths = Vec<Str>();
    wv->hasDragPaths = false;
    gdk_drop_finish(context, TRUE, time);
    wv->dragState = DragState::Left;
    DragDropEvent ev;
    ev.kind = DragDropKind::Drop;
    ev.paths = paths.els;
    ev.pathCount = paths.len;
    ev.x = x;
    ev.y = y;
    bool handled = wv->dragDropHandler(wv->ctx, &ev);
    FreeStrs(&paths);
    return handled ? TRUE : FALSE;
}

static gboolean OnDragLeaveIdle(gpointer user) {
    WebView* wv = (WebView*)user;
    wv->dragLeaveIdle = 0;
    if (wv->dragState == DragState::Leaving) {
        wv->dragState = DragState::Left;
        DragDropEvent ev;
        ev.kind = DragDropKind::Leave;
        wv->dragDropHandler(wv->ctx, &ev);
    }
    return G_SOURCE_REMOVE;
}

static void OnDragLeave(GtkWidget*, GdkDragContext*, guint, gpointer user) {
    WebView* wv = (WebView*)user;
    if (wv->dragState == DragState::Left) {
        return;
    }
    // GTK sends drag-leave before drag-drop, so whether this is a leave or
    // the start of a drop is only known once the drop has had its turn.
    wv->dragState = DragState::Leaving;
    if (!wv->dragLeaveIdle) {
        wv->dragLeaveIdle = g_idle_add(OnDragLeaveIdle, wv);
    }
}

// ─── downloads (web_context.rs) ──────────────────────────────────────────

// Rust shares one `failed` flag between every download of a context, so a
// failed download marks every later one failed too; it is per download here.
struct DownloadState {
    WebView* wv;
    WebKitDownload* download;
    bool failed;
};

static void ForgetDownload(DownloadState* state) {
    WebView* wv = state->wv;
    for (int i = 0; i < wv->downloads.len; i++) {
        if (wv->downloads[i] == state) {
            wv->downloads.els[i] = wv->downloads.els[wv->downloads.len - 1];
            wv->downloads.len--;
            break;
        }
    }
    g_signal_handlers_disconnect_by_data(state->download, state);
    g_object_unref(state->download);
    delete state;
}

static Str DownloadUriTemp(WebKitDownload* download) {
    WebKitURIRequest* request = webkit_download_get_request(download);
    return request ? FromCTemp(webkit_uri_request_get_uri(request)) : Str();
}

static gboolean OnDecideDestination(WebKitDownload* download, gchar* suggested,
                                    gpointer data) {
    DownloadState* state = (DownloadState*)data;
    WebView* wv = state->wv;
    Str uri = DownloadUriTemp(download);
    if (!uri.s || !wv->downloadStartedHandler) {
        return TRUE;
    }
    // `dirs::download_dir()`, else the current directory.
    const char* dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    char* cwd = nullptr;
    if (!dir) {
        cwd = g_get_current_dir();
        dir = cwd;
    }
    Str stem;
    Str ext;
    DownloadFileNameParts(uri, suggested ? Str(suggested) : Str(), &stem, &ext);
    char* path = g_build_filename(
        dir, CStrTemp(base::FormatTemp("%s%s", stem, ext)), nullptr);
    // WebView2 does not overwrite a file but numbers the new one; Rust does
    // the same here.
    for (int counter = 1; g_file_test(path, G_FILE_TEST_EXISTS); counter++) {
        g_free(path);
        path = g_build_filename(
            dir, CStrTemp(base::FormatTemp("%s (%d)%s", stem, counter, ext)),
            nullptr);
    }
    g_free(cwd);
    Str dest = FromCTemp(path);
    g_free(path);
    if (wv->downloadStartedHandler(wv->ctx, uri, &dest)) {
        webkit_download_set_destination(download, CStrTemp(dest));
    } else {
        webkit_download_cancel(download);
    }
    return TRUE;
}

static void OnDownloadFailed(WebKitDownload*, GError*, gpointer data) {
    ((DownloadState*)data)->failed = true;
}

static void OnDownloadFinished(WebKitDownload* download, gpointer data) {
    DownloadState* state = (DownloadState*)data;
    WebView* wv = state->wv;
    Str uri = DownloadUriTemp(download);
    if (wv->downloadCompletedHandler && uri.s) {
        Str path = state->failed
                       ? Str()
                       : FromCTemp(webkit_download_get_destination(download));
        wv->downloadCompletedHandler(wv->ctx, uri, path.s ? &path : nullptr,
                                     !state->failed);
    }
    ForgetDownload(state);
}

static void OnDownloadStarted(WebKitWebContext*, WebKitDownload* download,
                              gpointer data) {
    WebView* wv = (WebView*)data;
    DownloadState* state = new DownloadState{wv, download, false};
    g_object_ref(download);
    VecAppend(wv->downloads, state);
    g_signal_connect(download, "decide-destination", Cb(OnDecideDestination),
                     state);
    g_signal_connect(download, "failed", Cb(OnDownloadFailed), state);
    g_signal_connect(download, "finished", Cb(OnDownloadFinished), state);
}

// ─── custom protocols (web_context.rs) ───────────────────────────────────

struct ProtocolHandler {
    Str name;
    Str webviewId;
    void* ctx;
    void (*handler)(void* ctx, Str id, const Request* request,
                    RequestResponder* responder);
};

static void FreeProtocolHandler(gpointer data) {
    ProtocolHandler* p = (ProtocolHandler*)data;
    StrFree(p->name);
    StrFree(p->webviewId);
    delete p;
}

struct RequestResponder {
    WebKitURISchemeRequest* request;
};

static void CollectHeader(const char* name, const char* value, gpointer data) {
    Header h;
    h.name = FromCTemp(name);
    h.value = FromCTemp(value);
    VecAppend(*(Vec<Header>*)data, h);
}

static void OnUriScheme(WebKitURISchemeRequest* request, gpointer data) {
    ProtocolHandler* p = (ProtocolHandler*)data;
    const char* uri = webkit_uri_scheme_request_get_uri(request);
    if (!uri) {
        GError* error = g_error_new_literal(G_FILE_ERROR, G_FILE_ERROR_EXIST,
                                            "Could not get uri.");
        webkit_uri_scheme_request_finish_error(request, error);
        g_error_free(error);
        return;
    }
    Vec<Header> headers;
    SoupMessageHeaders* h = webkit_uri_scheme_request_get_http_headers(request);
    if (h) {
        soup_message_headers_foreach(h, CollectHeader, &headers);
    }
    const char* method = webkit_uri_scheme_request_get_http_method(request);
    Request req;
    req.uri = FromCTemp(uri);
    req.method = method ? FromCTemp(method) : StrL("GET");
    req.headers = headers.els;
    req.headerCount = headers.len;
    // The body is the `linux-body` feature's, which gpui-wry does not turn
    // on: Rust hands its handler an empty one.

    // `request.web_view().data(WEBVIEW_ID)`.
    Str id;
    WebKitWebView* webview = webkit_uri_scheme_request_get_web_view(request);
    if (webview) {
        id = FromCTemp(
            (const char*)g_object_get_data(G_OBJECT(webview), kWebViewIdKey));
    }
    RequestResponder* responder = new RequestResponder{request};
    g_object_ref(request);
    p->handler(p->ctx, id, &req, responder);
    VecReset(headers);
}

struct PendingResponse {
    WebKitURISchemeRequest* request;
    int status;
    Vec<Header> headers; // heap strings
    GBytes* body;
};

static gboolean FinishResponse(gpointer data) {
    PendingResponse* r = (PendingResponse*)data;
    GInputStream* input = g_memory_input_stream_new_from_bytes(r->body);
    WebKitURISchemeResponse* response = webkit_uri_scheme_response_new(
        input, (gint64)g_bytes_get_size(r->body));
    webkit_uri_scheme_response_set_status(response, (guint)r->status, nullptr);
    SoupMessageHeaders* headers =
        soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
    for (int i = 0; i < r->headers.len; i++) {
        const char* name = CStrTemp(r->headers[i].name);
        const char* value = CStrTemp(r->headers[i].value);
        if (g_ascii_strcasecmp(name, "content-type") == 0) {
            webkit_uri_scheme_response_set_content_type(response, value);
        }
        soup_message_headers_append(headers, name, value);
    }
    webkit_uri_scheme_response_set_http_headers(response, headers);
    webkit_uri_scheme_request_finish_with_response(r->request, response);
    g_object_unref(response);
    g_object_unref(input);
    g_object_unref(r->request);
    g_bytes_unref(r->body);
    for (int i = 0; i < r->headers.len; i++) {
        StrFree(r->headers[i].name);
        StrFree(r->headers[i].value);
    }
    VecReset(r->headers);
    delete r;
    return G_SOURCE_REMOVE;
}

void Respond(RequestResponder* responder, const Response* response) {
    if (!responder) {
        return;
    }
    // Everything is copied here, on the caller's thread, and the request is
    // finished on the main context — `MainContext::default().invoke`, which
    // runs at once when this is already the main thread.
    PendingResponse* r = new PendingResponse();
    r->request = responder->request;
    r->status = response ? response->status : 500;
    if (response) {
        for (int i = 0; i < response->headerCount; i++) {
            Header h;
            h.name = StrDup(response->headers[i].name);
            h.value = StrDup(response->headers[i].value);
            VecAppend(r->headers, h);
        }
    }
    r->body = g_bytes_new(response ? response->body : nullptr,
                          response && response->body ? response->bodyLen : 0);
    delete responder;
    g_main_context_invoke(nullptr, FinishResponse, r);
}

// ─── construction ────────────────────────────────────────────────────────

// `WebContextImpl::new` / `new_ephemeral` / `create_context`.
static WebKitWebContext* NewWebContext(const WebViewAttributes* attrs) {
    WebKitWebContext* context = nullptr;
    if (attrs->incognito) {
        context = webkit_web_context_new_ephemeral();
    } else if (len(attrs->dataDirectory) > 0) {
        const char* dir = CStrTemp(attrs->dataDirectory);
        WebKitWebsiteDataManager* manager = webkit_website_data_manager_new(
            "base-data-directory", dir, nullptr);
        WebKitCookieManager* cookies =
            webkit_website_data_manager_get_cookie_manager(manager);
        if (cookies) {
            char* path = g_build_filename(dir, "cookies", nullptr);
            webkit_cookie_manager_set_persistent_storage(
                cookies, path, WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
            g_free(path);
        }
        context = webkit_web_context_new_with_website_data_manager(manager);
        g_object_unref(manager);
    } else {
        context = webkit_web_context_new();
    }
    // Automation stays off, so the `ApplicationInfo` Rust builds for it is
    // never used and is not built here.
    webkit_web_context_set_automation_allowed(context, FALSE);
    return context;
}

// Rust's `WebContext::register_uri_scheme`, which also marks the scheme
// secure.
static void RegisterProtocols(WebView* wv, const WebViewAttributes* attrs) {
    WebKitSecurityManager* security =
        webkit_web_context_get_security_manager(wv->context);
    for (int i = 0; i < attrs->customProtocolCount; i++) {
        const CustomProtocol& cp = attrs->customProtocols[i];
        bool duplicate = false;
        for (int j = 0; j < i; j++) {
            if (base::StrEq(attrs->customProtocols[j].name, cp.name)) {
                duplicate = true;
            }
        }
        if (duplicate || !cp.handler) {
            // `Error::DuplicateCustomProtocol`.
            logf("wry: custom protocol '%s' skipped\n", cp.name);
            continue;
        }
        const char* scheme = CStrTemp(cp.name);
        if (security) {
            webkit_security_manager_register_uri_scheme_as_secure(security,
                                                                  scheme);
        }
        ProtocolHandler* p = new ProtocolHandler();
        p->name = StrDup(cp.name);
        p->webviewId = StrDup(wv->id);
        p->ctx = cp.ctx;
        p->handler = cp.handler;
        webkit_web_context_register_uri_scheme(wv->context, scheme, OnUriScheme,
                                               p, FreeProtocolHandler);
    }
}

static void SetWebviewSettings(WebView* wv, const WebViewAttributes* attrs) {
    // Disable input preedit, so fcitx's editor anchors at the caret.
    WebKitInputMethodContext* im =
        webkit_web_view_get_input_method_context(wv->webview);
    if (im) {
        webkit_input_method_context_set_enable_preedit(im, FALSE);
    }
    webkit_web_context_set_use_system_appearance_for_scrollbars(wv->context,
                                                                FALSE);
    WebKitSettings* settings = webkit_web_view_get_settings(wv->webview);
    if (!settings) {
        return;
    }
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_enable_webaudio(settings, TRUE);
    webkit_settings_set_enable_back_forward_navigation_gestures(
        settings, attrs->backForwardNavigationGestures);
    if (attrs->clipboard) {
        webkit_settings_set_javascript_can_access_clipboard(settings, TRUE);
    }
    webkit_settings_set_enable_page_cache(settings, TRUE);
    webkit_settings_set_user_agent(settings, CStrOrNull(attrs->userAgent));
    if (attrs->devtools) {
        webkit_settings_set_enable_developer_extras(settings, TRUE);
    }
    if (attrs->javascriptDisabled) {
        webkit_settings_set_enable_javascript(settings, FALSE);
    }
}

static WebView* NewGtk(GtkWidget* container, const WebViewAttributes* attrs,
                       WebKitWebView* relatedView);

// The window's own destroy is already under way when it is the one that
// says the related view is gone, and must not be started again.
static void FreeRelated(WebView* wv, int index, bool destroyWindow);

static void OnRelatedWindowDestroy(GtkWidget* window, gpointer data) {
    WebView* wv = (WebView*)data;
    for (int i = 0; i < wv->related.len; i++) {
        if (wv->related[i].window == window) {
            FreeRelated(wv, i, false);
            return;
        }
    }
}

static GtkWidget* OnCreate(WebKitWebView* webview,
                           WebKitNavigationAction* action, gpointer data) {
    WebView* wv = (WebView*)data;
    WebKitURIRequest* request = webkit_navigation_action_get_request(action);
    const char* uri = request ? webkit_uri_request_get_uri(request) : nullptr;
    if (!uri || !wv->newWindowReqHandler) {
        return nullptr;
    }
    NewWindowFeatures features;
    features.opener = wv;
    WebView* created = nullptr;
    NewWindowResponse response =
        wv->newWindowReqHandler(wv->ctx, FromCTemp(uri), &features, &created);
    if (response == NewWindowResponse::Create) {
        return created && created->webview ? GTK_WIDGET(created->webview)
                                           : nullptr;
    }
    if (response != NewWindowResponse::Allow) {
        return nullptr;
    }
    // Rust asks the opener's toplevel for its `gtk::Application` and opens
    // an `ApplicationWindow` in it, which panics when the toplevel is not an
    // application window — and in the X11 path it never is. A plain GTK
    // window is what that code means: a related view in a window of its own.
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), uri);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), box);
    gtk_widget_show_all(window);
    WebViewAttributes defaults;
    WebView* related = NewGtk(box, &defaults, webview);
    if (!related) {
        gtk_widget_destroy(window);
        return nullptr;
    }
    VecAppend(wv->related, RelatedWebView{window, related});
    g_signal_connect(window, "destroy", Cb(OnRelatedWindowDestroy), wv);
    return GTK_WIDGET(related->webview);
}

// `InnerWebView::new_gtk`.
static WebView* NewGtk(GtkWidget* container, const WebViewAttributes* attrs,
                       WebKitWebView* relatedView) {
    WebView* wv = new WebView();
    wv->ctx = attrs->ctx;
    wv->ipcHandler = attrs->ipcHandler;
    wv->navigationHandler = attrs->navigationHandler;
    wv->documentTitleChangedHandler = attrs->documentTitleChangedHandler;
    wv->onPageLoadHandler = attrs->onPageLoadHandler;
    wv->newWindowReqHandler = attrs->newWindowReqHandler;
    wv->downloadStartedHandler = attrs->downloadStartedHandler;
    wv->downloadCompletedHandler = attrs->downloadCompletedHandler;
    wv->dragDropHandler = attrs->dragDropHandler;
    wv->cancellable = g_cancellable_new();

    // A related view shares its opener's context, as WebKit requires.
    wv->context = relatedView ? webkit_web_view_get_context(relatedView)
                              : NewWebContext(attrs);
    if (relatedView) {
        g_object_ref(wv->context);
    }
    Str proxy = ProxyUriTemp(&attrs->proxyConfig);
    if (proxy.s) {
        WebKitWebsiteDataManager* manager =
            webkit_web_context_get_website_data_manager(wv->context);
        if (manager) {
            WebKitNetworkProxySettings* settings =
                webkit_network_proxy_settings_new(CStrTemp(proxy), nullptr);
            webkit_website_data_manager_set_network_proxy_settings(
                manager, WEBKIT_NETWORK_PROXY_MODE_CUSTOM, settings);
            webkit_network_proxy_settings_free(settings);
        }
    }
    // `with_extensions_path` is WebViewBuilderExtUnix's; the attribute is
    // shared with the Windows builder.
    if (len(attrs->extensionPath) > 0) {
        webkit_web_context_set_web_extensions_directory(
            wv->context, CStrTemp(attrs->extensionPath));
    }

    // `create_webview`.
    wv->manager = webkit_user_content_manager_new();
    WebKitWebsitePolicies* policies = nullptr;
    if (attrs->autoplay) {
        policies = webkit_website_policies_new_with_policies(
            "autoplay", WEBKIT_AUTOPLAY_ALLOW, nullptr);
    }
    GObject* object =
        relatedView
            ? (GObject*)g_object_new(
                  WEBKIT_TYPE_WEB_VIEW, "user-content-manager", wv->manager,
                  "is-controlled-by-automation", FALSE, "website-policies",
                  policies, "related-view", relatedView, nullptr)
            : (GObject*)g_object_new(
                  WEBKIT_TYPE_WEB_VIEW, "user-content-manager", wv->manager,
                  "is-controlled-by-automation", FALSE, "website-policies",
                  policies, "web-context", wv->context, nullptr);
    if (policies) {
        g_object_unref(policies);
    }
    wv->webview = WEBKIT_WEB_VIEW(object);
    g_object_ref_sink(wv->webview);

    if (attrs->transparent) {
        GdkRGBA clear = {0, 0, 0, 0};
        webkit_web_view_set_background_color(wv->webview, &clear);
    } else if (attrs->hasBackgroundColor) {
        // Rust hands the u8 channels to `gdk::RGBA::new` as they are, which
        // makes any non-zero channel a full one; GDK wants 0..1.
        GdkRGBA c = {
            attrs->backgroundColor.r / 255.0, attrs->backgroundColor.g / 255.0,
            attrs->backgroundColor.b / 255.0, attrs->backgroundColor.a / 255.0};
        webkit_web_view_set_background_color(wv->webview, &c);
    }

    SetWebviewSettings(wv, attrs);

    // `attach_handlers`.
    GObject* view = G_OBJECT(wv->webview);
    g_signal_connect(view, "close", Cb(OnClose), wv);
    gtk_widget_add_events(GTK_WIDGET(wv->webview),
                          GDK_BUTTON1_MOTION_MASK | GDK_BUTTON_PRESS_MASK);
    g_signal_connect(view, "button-press-event", Cb(OnButtonPress), wv);
    g_signal_connect(view, "button-release-event", Cb(OnButtonRelease), wv);
    if (wv->documentTitleChangedHandler) {
        g_signal_connect(view, "notify::title", Cb(OnTitle), wv);
    }
    g_signal_connect(view, "load-changed", Cb(OnLoadChanged), wv);
    if (wv->newWindowReqHandler) {
        g_signal_connect(view, "create", Cb(OnCreate), wv);
    }
    if (wv->navigationHandler) {
        g_signal_connect(view, "decide-policy", Cb(OnDecidePolicy), wv);
    }
    if (wv->downloadStartedHandler || wv->downloadCompletedHandler) {
        g_signal_connect(wv->context, "download-started", Cb(OnDownloadStarted),
                         wv);
    }

    // `attach_ipc_handler`: connect before registering, as WebKit says.
    g_signal_connect(wv->manager, "script-message-received::ipc",
                     Cb(OnScriptMessage), wv);
    webkit_user_content_manager_register_script_message_handler(wv->manager,
                                                                "ipc");

    if (wv->dragDropHandler) {
        g_signal_connect(view, "drag-data-received", Cb(OnDragDataReceived),
                         wv);
        g_signal_connect(view, "drag-motion", Cb(OnDragMotion), wv);
        g_signal_connect(view, "drag-drop", Cb(OnDragDrop), wv);
        g_signal_connect(view, "drag-leave", Cb(OnDragLeave), wv);
    }

    // `add_to_container`: always a GtkBox here.
    gtk_box_pack_start(GTK_BOX(container), GTK_WIDGET(wv->webview), TRUE, TRUE,
                       0);

    // `attach_inspector_handlers`.
    WebKitWebInspector* inspector = webkit_web_view_get_inspector(wv->webview);
    if (inspector) {
        g_signal_connect(inspector, "bring-to-front",
                         Cb(OnInspectorBringToFront), wv);
        g_signal_connect(inspector, "closed", Cb(OnInspectorClosed), wv);
    }

    // `attributes.id`, else the webview's address written out.
    wv->id =
        len(attrs->id) > 0
            ? StrDup(attrs->id)
            : StrDup(base::FormatTemp("%d", (long long)(intptr_t)wv->webview));
    g_object_set_data_full(G_OBJECT(wv->webview), kWebViewIdKey,
                           g_strdup(CStrTemp(wv->id)), g_free);

    AddUserScript(wv, Str(kIpcScript), true);
    for (int i = 0; i < attrs->initializationScriptCount; i++) {
        AddUserScript(wv, attrs->initializationScripts[i].script,
                      attrs->initializationScripts[i].forMainFrameOnly);
    }

    // Custom protocols, before the first navigation can ask for one. A
    // related view shares a context whose schemes are already registered.
    if (!relatedView) {
        RegisterProtocols(wv, attrs);
    }

    if (attrs->url.s) {
        if (attrs->headerCount > 0) {
            WebViewLoadUrlWithHeaders(wv, attrs->url, attrs->headers,
                                      attrs->headerCount);
        } else {
            webkit_web_view_load_uri(wv->webview, CStrTemp(attrs->url));
        }
    } else if (attrs->html.s) {
        webkit_web_view_load_html(wv->webview, CStrTemp(attrs->html), nullptr);
    }

    if (attrs->visible) {
        gtk_widget_show_all(GTK_WIDGET(wv->webview));
    }
    if (attrs->focused) {
        gtk_widget_grab_focus(GTK_WIDGET(wv->webview));
    }
    return wv;
}

// `dpi`'s `to_physical` / `to_logical` with an integer target, which rounds.
static int ToPhysical(double v, bool logical, double scale) {
    return (int)lround(logical ? v * scale : v);
}

static int ToLogical(double v, bool logical, double scale) {
    return (int)lround(logical ? v : v / scale);
}

// `create_container_x11_window`.
static ::Window CreateContainer(::Display* xdpy, ::Window parent,
                                const WebViewAttributes* attrs, bool asChild) {
    int x = 0;
    int y = 0;
    int w = 1;
    int h = 1;
    if (asChild) {
        XWindowAttributes pa = {};
        XGetWindowAttributes(xdpy, parent, &pa);
        double scale = pa.screen ? ScaleFactorFromScreen(pa.screen->width,
                                                         pa.screen->mwidth)
                                 : 1.0;
        if (attrs->hasBounds) {
            const Rect& b = attrs->bounds;
            x = ToPhysical(b.position.x, b.position.logical, scale);
            y = ToPhysical(b.position.y, b.position.logical, scale);
            w = ToPhysical(b.size.width, b.size.logical, scale);
            h = ToPhysical(b.size.height, b.size.logical, scale);
        }
    } else {
        // Not Rust's: the parent itself would be wrapped (see the top of the
        // file), so the container fills it instead.
        XWindowAttributes pa = {};
        XGetWindowAttributes(xdpy, parent, &pa);
        w = pa.width;
        h = pa.height;
    }
    // Rust's `unwrap_or((1, 1))` keeps X from a zero size only when there are
    // no bounds; gpui-wry asks for an empty box first, and X answers a zero
    // width with BadValue, so every size is at least one pixel.
    w = std::max(w, 1);
    h = std::max(h, 1);
    ::Window window = XCreateSimpleWindow(xdpy, parent, x, y, (unsigned)w,
                                          (unsigned)h, 0, 0, 0);
    if (attrs->visible) {
        XMapWindow(xdpy, window);
    }
    return window;
}

// The embedder's half of XEmbed: GtkPlug marks itself mapped and waits for
// the embedder to map it, and is as big as the embedder makes it.
static void PlaceMappedPlug(WebView* wv) {
    ::Window plug = PlugXid(wv);
    if (!plug) {
        return;
    }
    XWindowAttributes ca = {};
    XTrap trap(wv->display);
    if (!XGetWindowAttributes(wv->xdisplay, wv->x11Window, &ca)) {
        return;
    }
    XMoveResizeWindow(wv->xdisplay, plug, 0, 0, (unsigned)std::max(ca.width, 1),
                      (unsigned)std::max(ca.height, 1));
    XMapWindow(wv->xdisplay, plug);
    XFlush(wv->xdisplay);
}

static void ResizeContainer(WebView* wv, int x, int y, int w, int h) {
    w = std::max(w, 1);
    h = std::max(h, 1);
    {
        XTrap trap(wv->display);
        XMoveResizeWindow(wv->xdisplay, wv->x11Window, x, y, (unsigned)w,
                          (unsigned)h);
        ::Window plug = PlugXid(wv);
        if (plug) {
            XMoveResizeWindow(wv->xdisplay, plug, 0, 0, (unsigned)w,
                              (unsigned)h);
        }
        XFlush(wv->xdisplay);
    }
    // `window.resize` and `size_allocate`, as Rust's set_bounds does.
    gtk_window_resize(GTK_WINDOW(wv->gtkWindow), w, h);
    GtkAllocation a = {0, 0, w, h};
    gtk_widget_size_allocate(wv->gtkWindow, &a);
}

// A webview that is not a child follows its parent's size.
static GdkFilterReturn ParentFilter(GdkXEvent* xevent, GdkEvent*,
                                    gpointer data) {
    WebView* wv = (WebView*)data;
    XEvent* ev = (XEvent*)xevent;
    if (ev->type == ConfigureNotify && ev->xconfigure
                                               .window == wv->parentWindow) {
        ResizeContainer(wv, 0, 0, ev->xconfigure.width, ev->xconfigure.height);
    }
    return GDK_FILTER_CONTINUE;
}

WebView* WebViewNew(void* parentWindow, const WebViewAttributes* attrs,
                    bool asChild) {
    if (!attrs) {
        return nullptr;
    }
    ::Window parent = (::Window)(uintptr_t)parentWindow;
    if (!parent) {
        // `Error::UnsupportedWindowHandle`.
        logf("wry: a webview needs an X11 window to go in\n");
        return nullptr;
    }
    if (!EnsureGtk()) {
        return nullptr;
    }
    GdkDisplay* display = gdk_display_get_default();
    ::Display* xdpy = XDisplayOf(display);

    ::Window container = CreateContainer(xdpy, parent, attrs, asChild);
    if (!container) {
        logf("wry: could not make the webview's X11 window\n");
        return nullptr;
    }
    // `create_gtk_window`, as a plug (see the top of the file).
    GtkWidget* plug = gtk_plug_new_for_display(display, container);
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(plug), vbox);

    WebView* wv = NewGtk(vbox, attrs, nullptr);
    // "If the webview starts as hidden, we will need about 3 calls to
    // `webview.set_visible` with alternating value. Calling
    // gtk_window.show_all() then hiding it again seems to fix the issue."
    gtk_widget_show_all(plug);
    wv->display = display;
    wv->xdisplay = xdpy;
    wv->x11Window = container;
    wv->parentWindow = parent;
    wv->gtkWindow = plug;
    wv->isChild = asChild;
    PlaceMappedPlug(wv);
    if (!attrs->visible) {
        WebViewSetVisible(wv, false);
    }
    wv->hasX11 = true;
    if (!asChild) {
        XTrap trap(display);
        XSelectInput(xdpy, parent, StructureNotifyMask);
        gdk_window_add_filter(nullptr, ParentFilter, wv);
        wv->followsParent = true;
    }
    return wv;
}

static void FreeInner(WebView* wv) {
    if (!wv) {
        return;
    }
    g_cancellable_cancel(wv->cancellable);
    if (wv->followsParent) {
        gdk_window_remove_filter(nullptr, ParentFilter, wv);
    }
    while (wv->related.len > 0) {
        FreeRelated(wv, wv->related.len - 1, true);
    }
    while (wv->downloads.len > 0) {
        ForgetDownload(wv->downloads[wv->downloads.len - 1]);
    }
    if (wv->dragLeaveIdle) {
        g_source_remove(wv->dragLeaveIdle);
        wv->dragLeaveIdle = 0;
    }
    // Every handler that names this struct goes before the objects do, since
    // WebKit may still have a callback queued for them.
    g_signal_handlers_disconnect_by_data(wv->webview, wv);
    g_signal_handlers_disconnect_by_data(wv->manager, wv);
    g_signal_handlers_disconnect_by_data(wv->context, wv);
    WebKitWebInspector* inspector = webkit_web_view_get_inspector(wv->webview);
    if (inspector) {
        g_signal_handlers_disconnect_by_data(inspector, wv);
    }
    // `impl Drop for InnerWebView`, then `impl Drop for X11Data`: the
    // webview, the X11 window, the GTK window — the plug first here, since
    // it lives inside the X11 window rather than wrapping it.
    gtk_widget_destroy(GTK_WIDGET(wv->webview));
    if (wv->hasX11) {
        gtk_widget_destroy(wv->gtkWindow);
        XTrap trap(wv->display);
        XDestroyWindow(wv->xdisplay, wv->x11Window);
        XFlush(wv->xdisplay);
    }
    g_object_unref(wv->webview);
    g_object_unref(wv->manager);
    g_object_unref(wv->context);
    g_object_unref(wv->cancellable);
    FreeStrs(&wv->pendingScripts);
    FreeStrs(&wv->dragPaths);
    VecReset(wv->related);
    VecReset(wv->downloads);
    StrFree(wv->id);
    delete wv;
}

static void FreeRelated(WebView* wv, int index, bool destroyWindow) {
    RelatedWebView r = wv->related[index];
    wv->related.els[index] = wv->related.els[wv->related.len - 1];
    wv->related.len--;
    g_signal_handlers_disconnect_by_data(r.window, wv);
    FreeInner(r.webview);
    if (destroyWindow) {
        gtk_widget_destroy(r.window);
    }
}

void WebViewFree(WebView* wv) {
    FreeInner(wv);
}

// ─── the public API ──────────────────────────────────────────────────────

Str WebViewId(WebView* wv) {
    return wv ? wv->id : Str();
}

bool WebViewEval(WebView* wv, Str js) {
    return WebViewEvalWithCallback(wv, js, nullptr, nullptr);
}

bool WebViewEvalWithCallback(WebView* wv, Str js, void* ctx,
                             void (*callback)(void* ctx, Str result)) {
    if (!wv) {
        return false;
    }
    if (wv->pendingOpen) {
        // As in Rust, a held script runs without its callback.
        VecAppend(wv->pendingScripts, StrDup(js));
        return true;
    }
    RunJavascript(wv, js, ctx, callback);
    return true;
}

Str WebViewUrlTemp(WebView* wv) {
    return wv ? UriTemp(wv->webview) : Str();
}

bool WebViewLoadUrl(WebView* wv, Str url) {
    if (!wv) {
        return false;
    }
    webkit_web_view_load_uri(wv->webview, CStrTemp(url));
    return true;
}

bool WebViewLoadUrlWithHeaders(WebView* wv, Str url, const Header* headers,
                               int headerCount) {
    if (!wv) {
        return false;
    }
    WebKitURIRequest* request = webkit_uri_request_new(CStrTemp(url));
    SoupMessageHeaders* h = webkit_uri_request_get_http_headers(request);
    if (h) {
        for (int i = 0; i < headerCount; i++) {
            soup_message_headers_append(h, CStrTemp(headers[i].name),
                                        CStrTemp(headers[i].value));
        }
    }
    webkit_web_view_load_request(wv->webview, request);
    g_object_unref(request);
    return true;
}

bool WebViewLoadHtml(WebView* wv, Str html) {
    if (!wv) {
        return false;
    }
    webkit_web_view_load_html(wv->webview, CStrTemp(html), nullptr);
    return true;
}

bool WebViewReload(WebView* wv) {
    if (!wv) {
        return false;
    }
    webkit_web_view_reload(wv->webview);
    return true;
}

bool WebViewBounds(WebView* wv, Rect* out) {
    if (!wv || !out) {
        return false;
    }
    *out = Rect{};
    if (wv->hasX11) {
        XWindowAttributes a = {};
        XTrap trap(wv->display);
        if (XGetWindowAttributes(wv->xdisplay, wv->x11Window, &a)) {
            out->position = LogicalPosition(a.x, a.y);
            out->size = LogicalSize(a.width, a.height);
        }
        return true;
    }
    GtkAllocation a = {};
    gtk_widget_get_allocation(GTK_WIDGET(wv->webview), &a);
    out->size = LogicalSize(a.width, a.height);
    return true;
}

bool WebViewSetBounds(WebView* wv, Rect bounds) {
    if (!wv) {
        return false;
    }
    double scale = gtk_widget_get_scale_factor(GTK_WIDGET(wv->webview));
    int w = ToLogical(bounds.size.width, bounds.size.logical, scale);
    int h = ToLogical(bounds.size.height, bounds.size.logical, scale);
    int x = ToLogical(bounds.position.x, bounds.position.logical, scale);
    int y = ToLogical(bounds.position.y, bounds.position.logical, scale);
    if (wv->hasX11 && !wv->followsParent) {
        // GTK moves and sizes in logical pixels and X in device pixels.
        int s = (int)scale;
        ResizeContainer(wv, x * s, y * s, w * s, h * s);
    }
    return true;
}

bool WebViewSetVisible(WebView* wv, bool visible) {
    if (!wv) {
        return false;
    }
    // `set_visible_x11`, the webview, then `set_visible_gtk`.
    if (wv->hasX11 && wv->isChild) {
        XTrap trap(wv->display);
        if (visible) {
            XMapWindow(wv->xdisplay, wv->x11Window);
        } else {
            XUnmapWindow(wv->xdisplay, wv->x11Window);
        }
    }
    if (visible) {
        gtk_widget_show_all(GTK_WIDGET(wv->webview));
    } else {
        gtk_widget_hide(GTK_WIDGET(wv->webview));
    }
    if (wv->hasX11 && wv->isChild) {
        if (visible) {
            gtk_widget_show_all(wv->gtkWindow);
            PlaceMappedPlug(wv);
        } else {
            gtk_widget_hide(wv->gtkWindow);
        }
    }
    return true;
}

bool WebViewFocus(WebView* wv) {
    if (!wv) {
        return false;
    }
    // The X focus moves only within this application: from the parent
    // window, or already somewhere in the webview.
    if (wv->hasX11) {
        ::Window focus = 0;
        int revert = 0;
        XGetInputFocus(wv->xdisplay, &focus, &revert);
        if (focus == wv->parentWindow || OwnsXFocus(wv)) {
            GiveXFocus(wv, CurrentTime);
        }
    }
    gtk_widget_grab_focus(GTK_WIDGET(wv->webview));
    return true;
}

bool WebViewFocusParent(WebView* wv) {
    if (!wv) {
        return false;
    }
    // Rust focuses the webview's own GDK parent window, which in the X11
    // path is its GTK window; the window that should have the keyboard back
    // is the gpui one.
    if (wv->hasX11 && OwnsXFocus(wv)) {
        XTrap trap(wv->display);
        XSetInputFocus(wv->xdisplay, wv->parentWindow, RevertToParent,
                       CurrentTime);
        XFlush(wv->xdisplay);
    }
    return true;
}

bool WebViewZoom(WebView* wv, double scaleFactor) {
    if (!wv) {
        return false;
    }
    webkit_web_view_set_zoom_level(wv->webview, scaleFactor);
    return true;
}

bool WebViewMatchPageScale(WebView* wv, float gpuiScale) {
    if (!wv || !wv->webview) {
        return false;
    }
    int gdk = gtk_widget_get_scale_factor(GTK_WIDGET(wv->webview));
    if (gdk < 1) {
        gdk = 1;
    }
    if (gpuiScale <= 0) {
        gpuiScale = 1;
    }
    return WebViewZoom(wv, (double)gpuiScale / (double)gdk);
}

void WebViewSetPageClick(WebView* wv, WebViewPageClick fn, void* user) {
    if (!wv) {
        return;
    }
    wv->pageClick = fn;
    wv->pageClickUser = user;
}

bool WebViewSetBackgroundColor(WebView* wv, Rgba color) {
    if (!wv) {
        return false;
    }
    GdkRGBA c = {color.r / 255.0, color.g / 255.0, color.b / 255.0,
                 color.a / 255.0};
    webkit_web_view_set_background_color(wv->webview, &c);
    return true;
}

// `WebViewExtWindows` / `WebViewExtMacOS`: nothing of the kind on Linux.
bool WebViewSetTheme(WebView*, Theme) {
    return false;
}
bool WebViewSetMemoryUsageLevel(WebView*, MemoryUsageLevel) {
    return false;
}
bool WebViewSetTrafficLightInset(WebView*, Position) {
    return false;
}

// `WebViewExtUnix::reparent` takes a GTK container, which nothing here has.
// What the one caller means is moving the view to another X11 window, and
// that is the container's to do.
bool WebViewReparent(WebView* wv, void* parentWindow) {
    ::Window parent = (::Window)(uintptr_t)parentWindow;
    if (!wv || !wv->hasX11 || !parent || wv->followsParent) {
        return false;
    }
    XTrap trap(wv->display);
    XReparentWindow(wv->xdisplay, wv->x11Window, parent, 0, 0);
    XFlush(wv->xdisplay);
    wv->parentWindow = parent;
    return true;
}

bool WebViewPrint(WebView* wv) {
    if (!wv) {
        return false;
    }
    WebKitPrintOperation* op = webkit_print_operation_new(wv->webview);
    webkit_print_operation_run_dialog(op, nullptr);
    g_object_unref(op);
    return true;
}

bool WebViewClearAllBrowsingData(WebView* wv) {
    if (!wv) {
        return false;
    }
    WebKitWebsiteDataManager* manager =
        webkit_web_context_get_website_data_manager(wv->context);
    if (manager) {
        webkit_website_data_manager_clear(manager, WEBKIT_WEBSITE_DATA_ALL, 0,
                                          nullptr, nullptr, nullptr);
    }
    return true;
}

// ─── cookies ─────────────────────────────────────────────────────────────

static WebKitCookieManager* CookieManagerOf(WebView* wv) {
    WebKitWebsiteDataManager* data =
        wv ? webkit_web_view_get_website_data_manager(wv->webview) : nullptr;
    return data ? webkit_website_data_manager_get_cookie_manager(data)
                : nullptr;
}

// `cookie_from_soup_cookie`.
static Cookie CookieFromSoup(SoupCookie* c) {
    Cookie out;
    out.name = StrDup(Str(soup_cookie_get_name(c)));
    out.value = StrDup(Str(soup_cookie_get_value(c)));
    const char* domain = soup_cookie_get_domain(c);
    const char* path = soup_cookie_get_path(c);
    out.domain = domain ? StrDup(Str(domain)) : Str();
    out.path = path ? StrDup(Str(path)) : Str();
    out.hasHttpOnly = true;
    out.httpOnly = soup_cookie_get_http_only(c);
    out.hasSecure = true;
    out.secure = soup_cookie_get_secure(c);
    out.hasSameSite = true;
    SoupSameSitePolicy policy = soup_cookie_get_same_site_policy(c);
    out.sameSite =
        policy == SOUP_SAME_SITE_POLICY_LAX
            ? CookieSameSite::Lax
            : (policy == SOUP_SAME_SITE_POLICY_STRICT ? CookieSameSite::Strict
                                                      : kSameSiteNone);
    GDateTime* expires = soup_cookie_get_expires(c);
    if (expires) {
        out.session = false;
        // `OffsetDateTime::from_unix_timestamp(..).ok()`: a time outside
        // the `time` crate's range leaves the expiry unset.
        int64_t t = g_date_time_to_unix(expires);
        if (t >= -377705116800LL && t <= 253402300799LL) {
            out.hasExpires = true;
            out.expiresUnixSeconds = t;
        }
    }
    return out;
}

// `cookie_into_soup_cookie`.
static SoupCookie* CookieToSoup(const Cookie* c) {
    SoupCookie* out = soup_cookie_new(
        CStrTemp(c->name), CStrTemp(c->value), CStrTemp(c->domain),
        CStrTemp(c->path), c->hasMaxAge ? (int)c->maxAgeSeconds : -1);
    if (c->hasExpires) {
        GDateTime* dt = g_date_time_new_from_unix_utc(c->expiresUnixSeconds);
        if (dt) {
            soup_cookie_set_expires(out, dt);
            g_date_time_unref(dt);
        }
    }
    if (c->hasHttpOnly) {
        soup_cookie_set_http_only(out, c->httpOnly);
    }
    if (c->hasSameSite) {
        soup_cookie_set_same_site_policy(
            out, c->sameSite == CookieSameSite::Lax
                     ? SOUP_SAME_SITE_POLICY_LAX
                     : (c->sameSite == CookieSameSite::Strict
                            ? SOUP_SAME_SITE_POLICY_STRICT
                            : SOUP_SAME_SITE_POLICY_NONE));
    }
    if (c->hasSecure) {
        soup_cookie_set_secure(out, c->secure);
    }
    return out;
}

struct CookieWait {
    bool done = false;
    bool ok = false;
    GList* cookies = nullptr;
    bool all = false;
};

static void OnCookies(GObject* source, GAsyncResult* result, gpointer data) {
    CookieWait* wait = (CookieWait*)data;
    GError* error = nullptr;
#if WEBKIT_CHECK_VERSION(2, 42, 0)
    if (wait->all) {
        wait->cookies = webkit_cookie_manager_get_all_cookies_finish(
            WEBKIT_COOKIE_MANAGER(source), result, &error);
    } else
#endif
    {
        wait->cookies = webkit_cookie_manager_get_cookies_finish(
            WEBKIT_COOKIE_MANAGER(source), result, &error);
    }
    wait->ok = error == nullptr;
    if (error) {
        g_error_free(error);
    }
    wait->done = true;
}

static bool CookiesInner(WebView* wv, const char* uri, Vec<Cookie>* out) {
    if (!out) {
        return false;
    }
    CookieListFree(out);
    WebKitCookieManager* manager = CookieManagerOf(wv);
    if (!manager) {
        return false;
    }
    CookieWait wait;
    if (uri) {
        webkit_cookie_manager_get_cookies(manager, uri, nullptr, OnCookies,
                                          &wait);
    } else {
#if WEBKIT_CHECK_VERSION(2, 42, 0)
        wait.all = true;
        webkit_cookie_manager_get_all_cookies(manager, nullptr, OnCookies,
                                              &wait);
#else
        logf("wry: listing every cookie needs WebKitGTK 2.42\n");
        return false;
#endif
    }
    PumpUntil(&wait.done);
    for (GList* l = wait.cookies; l; l = l->next) {
        VecAppend(*out, CookieFromSoup((SoupCookie*)l->data));
    }
    g_list_free_full(wait.cookies, (GDestroyNotify)soup_cookie_free);
    return wait.ok;
}

bool WebViewCookies(WebView* wv, Vec<Cookie>* out) {
    return CookiesInner(wv, nullptr, out);
}

bool WebViewCookiesForUrl(WebView* wv, Str url, Vec<Cookie>* out) {
    return CookiesInner(wv, CStrTemp(url), out);
}

struct CookieChange {
    bool done = false;
    bool ok = false;
    bool add = true;
};

static void OnCookieChanged(GObject* source, GAsyncResult* result,
                            gpointer data) {
    CookieChange* change = (CookieChange*)data;
    GError* error = nullptr;
    gboolean ok = change->add
                      ? webkit_cookie_manager_add_cookie_finish(
                            WEBKIT_COOKIE_MANAGER(source), result, &error)
                      : webkit_cookie_manager_delete_cookie_finish(
                            WEBKIT_COOKIE_MANAGER(source), result, &error);
    change->ok = ok && !error;
    if (error) {
        g_error_free(error);
    }
    change->done = true;
}

static bool ChangeCookie(WebView* wv, const Cookie* cookie, bool add) {
    WebKitCookieManager* manager = CookieManagerOf(wv);
    if (!manager || !cookie) {
        return false;
    }
    SoupCookie* soup = CookieToSoup(cookie);
    CookieChange change;
    change.add = add;
    if (add) {
        webkit_cookie_manager_add_cookie(manager, soup, nullptr,
                                         OnCookieChanged, &change);
    } else {
        webkit_cookie_manager_delete_cookie(manager, soup, nullptr,
                                            OnCookieChanged, &change);
    }
    PumpUntil(&change.done);
    soup_cookie_free(soup);
    return change.ok;
}

bool WebViewSetCookie(WebView* wv, const Cookie* cookie) {
    return ChangeCookie(wv, cookie, true);
}

bool WebViewDeleteCookie(WebView* wv, const Cookie* cookie) {
    return ChangeCookie(wv, cookie, false);
}

// ─── devtools ────────────────────────────────────────────────────────────

void WebViewOpenDevtools(WebView* wv) {
    WebKitWebInspector* inspector =
        wv ? webkit_web_view_get_inspector(wv->webview) : nullptr;
    if (inspector) {
        webkit_web_inspector_show(inspector);
        // `bring-to-front` is not received in this case.
        wv->inspectorOpen = true;
    }
}

void WebViewCloseDevtools(WebView* wv) {
    WebKitWebInspector* inspector =
        wv ? webkit_web_view_get_inspector(wv->webview) : nullptr;
    if (inspector) {
        webkit_web_inspector_close(inspector);
    }
}

bool WebViewIsDevtoolsOpen(WebView* wv) {
    return wv && wv->inspectorOpen;
}

// ─── the runtime ─────────────────────────────────────────────────────────

Str WebViewVersionTemp() {
    return base::FormatTemp("%d.%d.%d", (int)webkit_get_major_version(),
                            (int)webkit_get_minor_version(),
                            (int)webkit_get_micro_version());
}

bool WebViewAvailable() {
    return EnsureGtk();
}

} // namespace wry

#pragma GCC diagnostic pop

#else // no WebKitGTK

// Built without libwebkit2gtk-4.1-dev: the portable API answers "there is no
// webview here", and EventLoopPrepare never gives the X11 loop anything to
// poll.

namespace wry {

using base::logf;
using base::Str;

static void Unsupported() {
    static bool said = false;
    if (!said) {
        said = true;
        logf("wry: built without WebKitGTK, so there is no webview here\n");
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
bool WebViewMatchPageScale(WebView*, float) {
    return false;
}
void WebViewSetPageClick(WebView*, WebViewPageClick, void*) {}
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

int EventLoopPrepare(PollFd** fds, int* timeoutMs) {
    (void)timeoutMs;
    *fds = nullptr;
    return 0;
}
void EventLoopDispatch() {}

Str WebViewVersionTemp() {
    return {};
}
bool WebViewAvailable() {
    return false;
}

} // namespace wry

#endif
