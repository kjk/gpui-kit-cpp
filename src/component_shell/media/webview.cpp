// crates/component-shell/src/shell/media/webview.rs
//
// A native page in a script view. The state owns the webview, so the
// document survives renders; WebView places it. Page events are queued
// (1024 events, messages over 1 MiB dropped) and drained on the window.

#include "component_shell/families.h"
#include "component_shell/media/mod.h"
#include "ui/theme.h"
#include "webview/webview.h"

#include <math.h>

namespace gpui::component_shell::media::webview {

constexpr int kEventQueue = 1024;
constexpr int kMessageLimit = 1024 * 1024;

// `about:` is how the engines start a blank page. Anything else must be
// http or https with a host, so `file:` cannot reach the disk.
bool WebDocumentNavigable(Str url) {
    if (StrStartsWith(url, "about:")) return true;
    int colon = -1;
    for (int i = 0; i < len(url); i++) {
        char c = url.s[i];
        if (c == ':') {
            colon = i;
            break;
        }
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
        if (!ok) return false;
    }
    if (colon <= 0) return false;
    Str scheme = Str(url.s, colon);
    if (!StrEqI(scheme, "http") && !StrEqI(scheme, "https")) return false;
    if (colon + 3 > len(url) || url.s[colon + 1] != '/' ||
        url.s[colon + 2] != '/')
        return false;
    int host = colon + 3;
    int authEnd = host;
    while (authEnd < len(url) && url.s[authEnd] != '/' &&
           url.s[authEnd] != '?' && url.s[authEnd] != '#')
        authEnd++;
    for (int i = host; i < authEnd; i++) {
        if (url.s[i] == '@') host = i + 1;
    }
    if (host >= authEnd) return false;
    if (url.s[host] == '[') {
        int close = host + 1;
        while (close < authEnd && url.s[close] != ']') close++;
        return close < authEnd && close > host + 1;
    }
    int end = host;
    while (end < authEnd && url.s[end] != ':') end++;
    return end > host;
}

enum class PageTheme : uint8_t {
    Light,
    Dark,
    System
};

struct PageEvent {
    uint8_t kind = 0; // 0 message, 1 load
    Str text;
};

struct Bridge {
    bool dead = false;
    bool posted = false;
    bool draining = false;
    bool dropAfterDrain = false;
    App* dropApp = nullptr;
    EntityId dropId = {};
    EntityId selfId = {};
    Window* window = nullptr;
    App* app = nullptr;
    ShellRuntime* runtime = nullptr;
    shell::ComponentCallback message = {};
    shell::ComponentCallback load = {};
    Vec<PageEvent> queue;

    ~Bridge() {
        for (int i = 0; i < queue.len; i++) StrFree(queue.els[i].text);
    }

    static void OnDrain(Bridge* self, Ctx* cx, const void*);
};

struct ShellWebView {
    App* app = nullptr;
    Entity<WebView> view;
    Entity<Bridge> bridge;
    bool followApp = true;
    PageTheme chosen = PageTheme::Light;
    bool hasApplied = false;
    PageTheme applied = PageTheme::Light;

    ~ShellWebView();
    bool ApplyTheme(App* app);
};

static void PostDrain(Bridge* self) {
    if (!self || self->posted || self->dead || !self->window) return;
    self->posted = true;
    Listener listener;
    listener.SetFn(&Bridge::OnDrain);
    listener.view = self->selfId;
    WindowPost(self->window, listener);
}

static void Enqueue(Bridge* self, uint8_t kind, Str text) {
    if (!self || self->dead) return;
    if (self->queue.len >= kEventQueue) {
        logf("dropped a WebView event; the script is not keeping up\n");
        return;
    }
    PageEvent event;
    event.kind = kind;
    event.text = StrDup(text);
    VecAppend(self->queue, event);
    PostDrain(self);
}

static void OnIpc(void* ctx, Str, Str body) {
    if (len(body) > kMessageLimit) {
        logf("dropped a %d byte WebView message; the limit is %d\n", len(body),
             kMessageLimit);
        return;
    }
    Enqueue((Bridge*)ctx, 0, body);
}

static void OnPageLoad(void* ctx, wry::PageLoadEvent event, Str url) {
    if (event != wry::PageLoadEvent::Finished) return;
    Enqueue((Bridge*)ctx, 1, url);
}

static bool OnNavigate(void*, Str url) {
    return WebDocumentNavigable(url);
}

static void Deliver(Bridge* self, Ctx* cx, const PageEvent& event) {
    shell::ComponentCallback callback =
        event.kind == 1 ? self->load : self->message;
    if (!callback.IsSet() || !self->runtime || !event.text.s) return;
    const char* context = event.kind == 1 ? "WebView load callback failed"
                                          : "WebView message callback failed";
    Arena* arena = ArenaNew();
    shell::ComponentDataValue argument =
        shell::ComponentDataValue::String(StrDup(arena, event.text));
    callback.InvokeAndReport(self->runtime, context, &argument, 1, cx->win,
                             cx->app);
    ArenaDelete(arena);
}

void Bridge::OnDrain(Bridge* self, Ctx* cx, const void*) {
    if (!self || self->dead) return;
    self->draining = true;
    self->posted = false;
    int count = self->queue.len;
    for (int i = 0; i < count; i++) {
        PageEvent event = self->queue.els[i];
        self->queue.els[i].text = {};
        if (!self->dead) Deliver(self, cx, event);
        StrFree(event.text);
        if (self->dropAfterDrain) {
            for (int j = i + 1; j < self->queue.len; j++) {
                StrFree(self->queue.els[j].text);
                self->queue.els[j].text = {};
            }
            self->queue.len = 0;
            App* app = self->dropApp;
            EntityId id = self->dropId;
            self->dropAfterDrain = false;
            EntityDrop(app, id);
            return;
        }
    }
    int rest = self->queue.len - count;
    if (rest < 0) rest = 0;
    for (int i = 0; i < rest; i++)
        self->queue.els[i] = self->queue.els[count + i];
    self->queue.len = rest;
    self->draining = false;
    if (rest) PostDrain(self);
}

ShellWebView::~ShellWebView() {
    Bridge* bridgeState = bridge.Get(app);
    if (bridgeState) bridgeState->dead = true;
    if (!app) return;
    if (view.id.IsValid()) {
        EntityId id = view.id;
        view.id = {};
        EntityDrop(app, id);
    }
    if (!bridge.id.IsValid()) return;
    if (bridgeState && bridgeState->draining) {
        bridgeState->dropAfterDrain = true;
        bridgeState->dropApp = app;
        bridgeState->dropId = bridge.id;
        bridge.id = {};
        return;
    }
    EntityId id = bridge.id;
    bridge.id = {};
    EntityDrop(app, id);
}

bool ShellWebView::ApplyTheme(App* themeApp) {
    PageTheme theme =
        followApp ? (ThemeGet(themeApp) == ThemeMode::Dark ? PageTheme::Dark
                                                           : PageTheme::Light)
                  : chosen;
    if (hasApplied && applied == theme) return true;
    wry::Theme native = theme == PageTheme::Dark    ? wry::Theme::Dark
                        : theme == PageTheme::Light ? wry::Theme::Light
                                                    : wry::Theme::Auto;
    if (!WebViewSetPageTheme(view.Get(themeApp), native)) return false;
    applied = theme;
    hasApplied = true;
    return true;
}

static bool CreationFailed(shell::StateBuild* build) {
    Str reason = wry::WebViewLastError();
    Str why = reason.s && len(reason) ? reason : StrL("no native webview");
    TempStr message = fmt("WebViewState could not create a webview: %s", why);
    if (reason.s && StrContains(reason, StrL("X11"))) {
        message =
            fmt("%s. In a Wayland session, start gpui-shell with "
                "`WAYLAND_DISPLAY=` to run under XWayland",
                Str(message));
    }
    return build->Fail(message);
}

static bool NewWebViewState(shell::StateBuild* build,
                            const ComponentArgument* args, int count) {
    if (count != 1 || !args ||
        args[0].kind != shell::ComponentArgumentKind::String) {
        return build->Fail(StrL("WebViewState expects one http or https URL"));
    }
    Str url = args[0].string;
    if (!WebDocumentNavigable(url)) {
        return build->Fail(
            fmt("WebViewState expects an http or https URL, got `%s`", url));
    }
    ShellWebView* state = build->New<ShellWebView>();
    state->app = build->app;
    state->bridge = EntityNewState<Bridge>(build->app);
    Bridge* bridge = state->bridge.Get(build->app);
    if (!bridge) return CreationFailed(build);
    bridge->window = build->window;
    bridge->app = build->app;
    bridge->selfId = state->bridge.id;

    wry::WebViewAttributes attrs;
    attrs.url = url;
    attrs.ctx = bridge;
    attrs.navigationHandler = &OnNavigate;
    attrs.ipcHandler = &OnIpc;
    attrs.onPageLoadHandler = &OnPageLoad;
    Ctx cx = {};
    cx.app = build->app;
    cx.win = build->window;
    state->view = WebViewNew(&cx, &attrs);
    WebView* page = state->view.Get(build->app);
    if (!page || !WebViewRaw(page)) return CreationFailed(build);
    if (!state->ApplyTheme(build->app)) {
        return build->Fail(
            StrL("WebViewState could not create a webview: set_theme failed"));
    }
    return true;
}

static WebView* PageOf(void* state) {
    ShellWebView* self = (ShellWebView*)state;
    return self ? self->view.Get(self->app) : nullptr;
}

static bool OneString(shell::StateCall* call,
                      const shell::ComponentDataValue* args, int count,
                      const char* context, Str* out) {
    if (count == 1 && args && args[0].kind == shell::DataKind::String) {
        *out = args[0].string;
        return true;
    }
    return call->Fail(fmt("%s expects one string", Str(context)));
}

static bool RequireUrl(shell::StateCall* call, Str url, const char* context) {
    if (WebDocumentNavigable(url)) return true;
    return call->Fail(
        fmt("%s expects an http or https URL, got `%s`", Str(context), url));
}

static bool LoadUrl(shell::StateCall* call, void* state,
                    const shell::ComponentDataValue* args, int count) {
    Str url;
    if (!OneString(call, args, count, "WebViewState.load_url", &url) ||
        !RequireUrl(call, url, "WebViewState.load_url"))
        return false;
    wry::WebView* raw = WebViewRaw(PageOf(state));
    if (!raw || !wry::WebViewLoadUrl(raw, url)) {
        return call->Fail(StrL("WebViewState.load_url could not navigate"));
    }
    call->out = shell::ComponentDataValue::Null();
    return true;
}

static bool Url(shell::StateCall* call, void* state,
                const shell::ComponentDataValue*, int count) {
    if (count != 0)
        return call->Fail(StrL("WebViewState.url expects no arguments"));
    wry::WebView* raw = WebViewRaw(PageOf(state));
    Str url = raw ? wry::WebViewUrlTemp(raw) : Str{};
    if (!url.s) return call->Fail(StrL("WebViewState.url could not be read"));
    call->out = shell::ComponentDataValue::String(StrDup(call->a, url));
    return true;
}

static bool History(shell::StateCall* call, void* state, int count,
                    const char* name, const char* script) {
    if (count != 0) {
        return call->Fail(fmt("%s expects no arguments", Str(name)));
    }
    wry::WebView* raw = WebViewRaw(PageOf(state));
    if (!raw || !wry::WebViewEval(raw, Str(script))) {
        return call->Fail(fmt("%s failed", Str(name)));
    }
    call->out = shell::ComponentDataValue::Null();
    return true;
}

static bool Back(shell::StateCall* call, void* state,
                 const shell::ComponentDataValue*, int count) {
    return History(call, state, count, "WebViewState.back", "history.back();");
}

static bool Forward(shell::StateCall* call, void* state,
                    const shell::ComponentDataValue*, int count) {
    return History(call, state, count, "WebViewState.forward",
                   "history.forward();");
}

static bool Reload(shell::StateCall* call, void* state,
                   const shell::ComponentDataValue*, int count) {
    if (count != 0) {
        return call->Fail(StrL("WebViewState.reload expects no arguments"));
    }
    wry::WebView* raw = WebViewRaw(PageOf(state));
    if (!raw || !wry::WebViewReload(raw)) {
        return call->Fail(StrL("WebViewState.reload failed"));
    }
    call->out = shell::ComponentDataValue::Null();
    return true;
}

static bool SetZoom(shell::StateCall* call, void* state,
                    const shell::ComponentDataValue* args, int count) {
    if (count != 1 || !args || args[0].kind != shell::DataKind::Number ||
        !isfinite(args[0].number) || !(args[0].number > 0)) {
        return call->Fail(
            StrL("WebViewState.set_zoom expects a positive number, where 1 is "
                 "the page's natural size"));
    }
    if (!WebViewSetPageZoom(PageOf(state), args[0].number)) {
        return call->Fail(StrL("WebViewState.set_zoom could not be applied"));
    }
    call->out = shell::ComponentDataValue::Null();
    return true;
}

static bool Zoom(shell::StateCall* call, void* state,
                 const shell::ComponentDataValue*, int count) {
    if (count != 0)
        return call->Fail(StrL("WebViewState.zoom expects no arguments"));
    WebView* page = PageOf(state);
    if (!page) return call->Fail(StrL("WebViewState.zoom could not be read"));
    call->out = shell::ComponentDataValue::Number(WebViewPageZoom(page));
    return true;
}

static bool SetTheme(shell::StateCall* call, void* state,
                     const shell::ComponentDataValue* args, int count) {
    Str name;
    if (!OneString(call, args, count, "WebViewState.set_theme", &name))
        return false;
    ShellWebView* self = (ShellWebView*)state;
    if (StrEq(name, "auto")) {
        self->followApp = true;
    } else if (StrEq(name, "light")) {
        self->followApp = false;
        self->chosen = PageTheme::Light;
    } else if (StrEq(name, "dark")) {
        self->followApp = false;
        self->chosen = PageTheme::Dark;
    } else if (StrEq(name, "system")) {
        self->followApp = false;
        self->chosen = PageTheme::System;
    } else {
        return call->Fail(
            fmt("WebViewState.set_theme expects \"auto\", \"light\", \"dark\" "
                "or \"system\", got `%s`",
                name));
    }
    if (!self->ApplyTheme(call->app)) {
        return call->Fail(StrL("WebViewState.set_theme could not be applied"));
    }
    call->out = shell::ComponentDataValue::Null();
    return true;
}

static bool EvaluateScript(shell::StateCall* call, void* state,
                           const shell::ComponentDataValue* args, int count) {
    Str script;
    if (!OneString(call, args, count, "WebViewState.evaluate_script", &script))
        return false;
    wry::WebView* raw = WebViewRaw(PageOf(state));
    if (!raw || !wry::WebViewEval(raw, script)) {
        return call->Fail(StrL("WebViewState.evaluate_script could not run"));
    }
    call->out = shell::ComponentDataValue::Null();
    return true;
}

struct Op {
    enum Kind : uint8_t {
        Message,
        Load
    } kind = Message;
    ComponentArgument callback = {};
};

static El* Materialize(MaterializeRequest* request) {
    const ComponentArgument* argument = request->PayloadAs<ComponentArgument>();
    if (!argument) {
        return request->Fail(StrL("WebView received an incompatible payload"));
    }
    if (request->ChildrenLen() != 0) {
        return request->Fail(StrL("WebView does not accept children"));
    }
    ShellWebView* state =
        request->StateAs<ShellWebView>(*argument, "WebViewState");
    if (!state) return nullptr;
    Bridge* bridge = state->bridge.Get(request->cx->app);
    if (!bridge) return request->Fail(StrL("WebViewState has no page"));
    // A render that drops a listener stops its delivery.
    bridge->runtime = request->runtime;
    bridge->message = {};
    bridge->load = {};
    for (int i = 0; i < request->MethodCount(); i++) {
        shell::RecordedComponentMethod method = request->Method(i);
        const Op* op = shell::PayloadAs<Op>(method.payload);
        if (!op) continue;
        shell::ComponentCallback callback = request
                                                ->ResolveCallback(op->callback);
        if (op->kind == Op::Message)
            bridge->message = callback;
        else
            bridge->load = callback;
    }
    if (!state->ApplyTheme(request->cx->app)) {
        return request->Fail(StrL("WebView could not apply its theme"));
    }
    El* box = Div(request->cx->a)->Child(WebViewEl(state->view, request->cx));
    return request->ApplyStyle(box);
}

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Entity) {
        return build->Fail(StrL("WebView expects one WebViewState"));
    }
    *build->New<ComponentArgument>() = args[0];
    return true;
}

static bool RecordListener(PayloadBuild* build, const ComponentArgument* args,
                           int count, Op::Kind kind, const char* name) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Callback) {
        return build->Fail(fmt("%s expects a listener", Str(name)));
    }
    Op* op = build->New<Op>();
    op->kind = kind;
    op->callback = args[0];
    return true;
}

static bool RecordMessage(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    return RecordListener(build, args, count, Op::Message,
                          "WebView.on_message");
}

static bool RecordLoad(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    return RecordListener(build, args, count, Op::Load, "WebView.on_load");
}

static constexpr ArgumentDescriptor kStateArgs[] = {{"url", SchemaString()}};
static constexpr shell::StateMethodDescriptor kStateMethods[] = {
    {"load_url", "(url: string): void", false, &LoadUrl},
    {"url", "(): string", true, &Url},
    {"back", "(): void", false, &Back},
    {"forward", "(): void", false, &Forward},
    {"reload", "(): void", false, &Reload},
    {"set_zoom", "(zoom: number): void", false, &SetZoom},
    {"zoom", "(): number", true, &Zoom},
    {"set_theme", "(theme: \"auto\" | \"light\" | \"dark\" | \"system\"): void",
     false, &SetTheme},
    {"evaluate_script", "(script: string): void", false, &EvaluateScript},
};
static constexpr shell::StateDescriptor kState = {
    "WebViewState",
    "WebViewState",
    kStateArgs,
    "A retained native web page loaded from an http or https URL. The page "
    "posts text to the script with `window.ipc.postMessage(text)`.",
    &NewWebViewState,
    kStateMethods,
};

static constexpr ArgumentDescriptor kConstructorArgs[] = {
    {"state", SchemaEntity("WebViewState")}};
static constexpr ConstructorDescriptor kConstructors[] = {
    {"WebView", kConstructorArgs, &Construct}};
static constexpr ArgumentDescriptor kMessageArgs[] = {
    {"listener", SchemaCallback("(message: string, cx: Context) => void")}};
static constexpr ArgumentDescriptor kLoadArgs[] = {
    {"listener", SchemaCallback("(url: string, cx: Context) => void")}};
static constexpr MethodDescriptor kMethods[] = {
    {"on_message", kMessageArgs,
     "Receives the text a page sends with `window.ipc.postMessage(text)`.",
     &RecordMessage},
    {"on_load", kLoadArgs,
     "Receives the URL of each document that finishes loading, whether the "
     "script or the page navigated.",
     &RecordLoad},
};
static constexpr ComponentDescriptor kWebView = {
    "WebView", kConstructors, kMethods,
    "Places a WebViewState's page in the layout. Shell style sizes it; "
    "children are rejected. The native page draws above GPUI content it "
    "overlaps on Windows, and on Linux needs an X11 or XWayland window.",
    &Materialize};

} // namespace gpui::component_shell::media::webview

namespace gpui::component_shell {

bool RegisterMediaWebView(shell::ComponentRegistry* registry,
                          shell::RegistryError* error) {
    return registry->RegisterState(&media::webview::kState, error) &&
           registry->Register(&media::webview::kWebView, error);
}

} // namespace gpui::component_shell
