/* Engine-independent shell semantics, ported from crates/shell/src/{value,
 * metrics, capability, policy, scope, spec, snapshot}.rs. */

#include "Test.h"

#include <stdio.h>
#if !GPUI_OS_WINDOWS
#include <unistd.h>
#endif

using namespace gpui::shell;

// ExecWaitIdle drains the C++ pool. cx.spawn's Promise.then lives on the
// QuickJS job queue, so a host call that settled on a worker can leave the
// script's live tasks queued until those jobs run.
static bool ShellSettle(ShellRuntime* runtime, int timeoutMs) {
    for (int waited = 0; waited <= timeoutMs; waited++) {
        ExecDrain();
        if (runtime) {
            ShellError jobs = {};
            runtime->DrainJobs(1024, &jobs);
            ShellErrorClear(&jobs);
        }
        if (ExecPending() == 0 && ExecQueued() == 0 &&
            (!runtime || runtime->LiveTasks() == 0)) {
            return true;
        }
        PlatSleepMs(1);
    }
    return false;
}

static void BridgedValuesMatchJavaScriptConversions() {
    utassert(!BridgedIsTruthy(Bridged::Nil()));
    utassert(!BridgedIsTruthy(Bridged::Bool(false)));
    utassert(BridgedIsTruthy(Bridged::Bool(true)));
    utassert(!BridgedIsTruthy(Bridged::Number(0)));
    utassert(!BridgedIsTruthy(Bridged::Number(NAN)));
    utassert(BridgedIsTruthy(Bridged::Number(-2)));
    utassert(!BridgedIsTruthy(Bridged::String(StrL(""))));
    utassert(BridgedIsTruthy(Bridged::String(StrL("0"))));

    Hsla color = {};
    utassert(BridgedAsColor(Bridged::String(StrL("#123")), &color));
    Rgba rgb = HslaToRgba(color);
    utassert(rgb.r <= 0x11 && 0x11 - rgb.r <= 1);
    utassert(rgb.g <= 0x22 && 0x22 - rgb.g <= 1);
    utassert(rgb.b <= 0x33 && 0x33 - rgb.b <= 1 && rgb.a == 0xff);
    utassert(BridgedAsColor(Bridged::String(StrL("#10203080")), &color));
    rgb = HslaToRgba(color);
    utassert(rgb.r <= 0x10 && 0x10 - rgb.r <= 1);
    utassert(rgb.g <= 0x20 && 0x20 - rgb.g <= 1);
    utassert(rgb.b <= 0x30 && 0x30 - rgb.b <= 1);
    utassert(rgb.a == 0x80);

    ShellError error = {};
    utassert(!BridgedAsColor(Bridged::String(StrL("#12")), &color, &error));
    utassert(error.IsSet());
    ShellErrorClear(&error);
    utassert(!BridgedAsF32(Bridged::Bool(true), nullptr, &error));
    utassert(StrStartsWith(error.message, StrL("expected a number")));
    ShellErrorClear(&error);
}

static void RuntimeMetricsSeparateScriptNativeAndFrames() {
    Metrics metrics = {};
    MetricsAdd(&metrics, MetricsTimerKind::ScriptRender, 100);
    MetricsAdd(&metrics, MetricsTimerKind::ScriptRender, 300);
    MetricsAdd(&metrics, MetricsTimerKind::Native, 80);
    MetricsAdd(&metrics, MetricsTimerKind::Materialize, 40);
    MetricsAdd(&metrics, MetricsTimerKind::FrameScript, 10);
    RuntimeMetrics reading = MetricsRead(&metrics);
    utassert(reading.scriptRenders == 2);
    utassert(reading.MeanScriptRenderNanos() == 200);
    utassert(reading.MeanNativeNanos() == 40);
    utassert(reading.MeanScriptOnlyNanos() == 160);
    utassert(reading.slowestScriptRenderNanos == 300);
    utassert(reading.materializations == 1);
    utassert(reading.MeanMaterializeNanos() == 50);
    // A frame script call adds its time to the materialize total without
    // moving the materialize count, and is counted on its own.
    utassert(reading.frameScriptCalls == 1);

    RuntimeMetrics earlier = {};
    earlier.scriptRenders = 1;
    earlier.scriptRenderNanos = 120;
    earlier.nativeNanos = 100; // Later counter is smaller: saturate.
    earlier.materializations = 4;
    RuntimeMetrics delta = reading.Since(earlier);
    utassert(delta.scriptRenders == 1);
    utassert(delta.scriptRenderNanos == 280);
    utassert(delta.nativeNanos == 0);
    utassert(delta.materializations == 0);
    utassert(delta.slowestScriptRenderNanos == 300);
}

static void CapabilitiesAreDenyFirstAndScoped() {
    Capabilities denied;
    utassert(!denied.HasReadAccess());
    utassert(!denied.HasWriteAccess());
    utassert(!denied.HasStorage());
    utassert(!denied.MayReach(StrL("example.com")));
    utassert(!denied.MayRun(StrL("git")));
    utassert(!denied.IsClipboardReadable());
    utassert(!denied.IsClipboardWritable());
    utassert(!denied.MayExit());

    Str commands[] = {StrL("git"), StrL("bun")};
    ExecuteGrant execute = ExecuteGrant::Allowed(commands, 2);
    Capabilities allowed;
    allowed.SetExecute(execute)
        .AddNetworkHost(StrL("EXAMPLE.COM"))
        .Storage(true)
        .ClipboardRead(true)
        .ClipboardWrite(true)
        .Exit(true);
    utassert(allowed.MayRun(StrL("git")));
    utassert(!allowed.MayRun(StrL("Git")));
    utassert(allowed.MayReach(StrL("example.com")));
    utassert(allowed.MayRequest(StrL("https"), StrL("example.com"), 443, true,
                                StrL("DELETE"), StrL("/anything")));
    utassert(allowed.HasStorage() && allowed.IsClipboardReadable());
    utassert(allowed.IsClipboardWritable() && allowed.MayExit());

    HttpRequestGrant api(StrL("api.example.com"));
    api.AddMethod(StrL("get"))
        .AddMethod(StrL("POST"))
        .AddPath(StrL("/health"))
        .AddPathPrefix(StrL("/v1/items"));
    Capabilities scoped;
    scoped.AddHttpRequest(api);
    utassert(scoped.MayRequest(StrL("HTTPS"), StrL("API.EXAMPLE.COM"), 0, false,
                               StrL("GET"), StrL("/health")));
    utassert(scoped.MayRequest(StrL("https"), StrL("api.example.com"), 443,
                               true, StrL("post"), StrL("/v1/items/7")));
    utassert(!scoped.MayRequest(StrL("https"), StrL("api.example.com"), 444,
                                true, StrL("GET"), StrL("/health")));
    utassert(!scoped.MayRequest(StrL("https"), StrL("api.example.com"), 443,
                                true, StrL("PATCH"), StrL("/health")));
    utassert(!scoped.MayRequest(StrL("https"), StrL("api.example.com"), 443,
                                true, StrL("GET"), StrL("/v1/itemset")));
}

static void FilesystemGrantsReturnRootRelativeAuthority() {
#if GPUI_OS_WINDOWS
    Str root = StrL("C:/safe");
    Str absolute = StrL("c:\\safe\\dir\\file.txt");
#else
    Str root = StrL("/safe");
    Str absolute = StrL("/safe/dir/file.txt");
#endif
    Capabilities capabilities;
    capabilities.AddReadRoot(root).AddWriteRoot(root);
    CapabilityPath path = {};
    CapabilityError error = {};
    utassert(capabilities
                 .ResolvePath(absolute, CapabilityAccess::Read, &path, &error));
    utassert(StrEq(path.root, root));
    utassert(StrEq(path.relative, StrL("dir/file.txt")));
    path.Free();
    utassert(capabilities.ResolvePath(StrL("inside/../file.txt"),
                                      CapabilityAccess::Write, &path, &error));
    utassert(StrEq(path.root, root));
    utassert(StrEq(path.relative, StrL("file.txt")));
    path.Free();
    utassert(!capabilities.ResolvePath(StrL("../escape.txt"),
                                       CapabilityAccess::Read, &path, &error));
    utassert(error.kind == CapabilityErrorKind::OutsideRoots);
    CapabilityErrorFree(&error);

    Capabilities none;
    utassert(!none.ResolvePath(StrL("file.txt"), CapabilityAccess::Read, &path,
                               &error));
    utassert(error.kind == CapabilityErrorKind::NotGranted);
    Arena* arena = ArenaNew();
    utassert(StrStartsWith(CapabilityErrorMessage(arena, error),
                           StrL("filesystem read is not granted")));
    ArenaDelete(arena);
    CapabilityErrorFree(&error);
}

static void PoliciesFreezeCapabilityGrants() {
    PolicySetDefault(nullptr);
    Policy* old = PolicyDefault();
    utassert(!PolicyCapabilities(old).HasStorage());

    Capabilities wider;
    wider.Storage(true).ClipboardRead(true);
    PolicyUpdateDefaultCapabilities(wider);
    Policy* fresh = PolicyDefault();
    utassert(!PolicyCapabilities(old).HasStorage());
    utassert(PolicyCapabilities(fresh).HasStorage());
    utassert(PolicyCapabilities(fresh).IsClipboardReadable());
    PolicyRelease(old);
    PolicyRelease(fresh);
    PolicySetDefault(nullptr);
}

static void ScopeGenerationsExpireAdoptAndRefuseReentry() {
    App app;
    Window window;
    uint64_t first = 0;
    {
        CallScopeGuard outer = ScopeEnter(&window, &app, ScopePhase::Render);
        first = outer.Generation();
        utassert(first != 0 && ScopeCurrentGeneration() == first);
        utassert(ScopeCurrentPhase() == ScopePhase::Render);
        utassert(!ScopePhaseAllowsNotify(ScopeCurrentPhase()));
        ScopeHostContext host = ScopeHostForGeneration(first);
        utassert(host.IsSet() && host.GetWindow() == &window &&
                 host.GetApp() == &app);
        utassert(!ScopeCurrentHost().IsSet());
        ShellError error = {};
        utassert(!ScopeHostForGeneration(first, &error).IsSet());
        utassert(error.IsSet());
        ShellErrorClear(&error);
    }
    ShellError stale = {};
    utassert(!ScopeHostForGeneration(first, &stale).IsSet());
    utassert(StrEq(stale.message, ScopeStaleContextMessage()));
    ShellErrorClear(&stale);

    {
        CallScopeGuard layout = ScopeEnter(&window, &app, ScopePhase::Layout);
        utassert(layout.Generation() != first);
        ScopeAdopt(first);
        ScopeHostContext adopted = ScopeHostForGeneration(first);
        utassert(adopted.IsSet());
    }
    utassert(!ScopeHasCurrent());
}

static Component Kind(ComponentKind kind, Str text = {}) {
    Component component = {};
    component.kind = kind;
    component.text = text;
    return component;
}

static void SpecElementsAreSingleUseValues() {
    SpecArena arena;
    SpecId parent = arena.Push(Kind(ComponentKind::Div));
    SpecId other = arena.Push(Kind(ComponentKind::Div));
    SpecId child = arena.Push(Kind(ComponentKind::Text, StrL("hi")));
    SpecError error = {};
    utassert(arena.Attach(parent, child));
    utassert(!arena.Attach(other, child, &error));
    utassert(error.kind == SpecErrorKind::AlreadyParented);
    utassert(StrEq(error.component, StrL("text")));
    SpecOp style = {};
    style.kind = SpecOpKind::NullaryStyle;
    style.name = StrL("flex");
    utassert(!arena.PushOp(child, style, &error));
    utassert(error.kind == SpecErrorKind::AlreadyParented);

    SpecId detached = arena.Push(Kind(ComponentKind::Div));
    utassert(arena.Claim(detached));
    utassert(arena.PushOp(detached, style));
    utassert(!arena.Attach(parent, detached, &error));
    utassert(error.kind == SpecErrorKind::Claimed);
    utassert(!arena.Claim(detached, &error));
    utassert(error.kind == SpecErrorKind::Claimed);
    utassert(!arena.Attach(parent, parent, &error));
    utassert(error.kind == SpecErrorKind::SelfParent);

    Component view = Kind(ComponentKind::ChildView);
    view.handle = 42;
    SpecId mounted = 0;
    utassert(arena.PushChildView(view, &mounted));
    utassert(!arena.PushChildView(view, nullptr, &error));
    utassert(error.kind == SpecErrorKind::DuplicateChildView);
    utassert(arena.ClaimVirtualItems(8, 10));
    utassert(!arena.ClaimVirtualItems(3, 10));

    SpecId expired = parent;
    arena.Reset();
    utassert(arena.IsEmpty());
    utassert(!arena.PushOp(expired, style, &error));
    utassert(error.kind == SpecErrorKind::Expired);
}

static void SpecsAndSnapshotsDumpWithoutEnteringTheVm() {
    SpecArena* specs = new SpecArena();
    SpecId root = specs->Push(Kind(ComponentKind::VFlex));
    SpecOp flex = {};
    flex.kind = SpecOpKind::NullaryStyle;
    flex.name = StrL("flex");
    utassert(specs->PushOp(root, flex));
    SpecId label = specs->Push(Kind(ComponentKind::Text, StrL("Save")));
    utassert(specs->Attach(root, label));
    SpecId collapsible = specs->Push(Kind(ComponentKind::Collapsible));
    SpecId body = specs->Push(Kind(ComponentKind::Text, StrL("body")));
    utassert(specs->Claim(body));
    SpecOp slot = {};
    slot.kind = SpecOpKind::Slot;
    slot.name = StrL("content");
    slot.node = body;
    utassert(specs->PushOp(collapsible, slot));
    utassert(specs->Attach(root, collapsible));

    struct LeaseState {
        int refs = 0;
        int retired = 0;
        uint64_t generation = 0;
    } lease;
    SnapshotRuntimeLease runtime = {};
    runtime.state = &lease;
    runtime.retain = [](void* state) { ((LeaseState*)state)->refs++; };
    runtime.release = [](void* state) { ((LeaseState*)state)->refs--; };
    runtime.retireCallbacks = [](void* state, uint64_t generation) {
        LeaseState* lease = (LeaseState*)state;
        lease->retired++;
        lease->generation = generation;
    };
    RenderSnapshot* snapshot = new RenderSnapshot(77, root, specs, runtime);
    utassert(snapshot->Len() == 4 && !snapshot->IsEmpty());
    utassert(lease.refs == 1 && lease.retired == 0);
    Arena* output = ArenaNew();
    Str tree = snapshot->DebugTree(output);
    utassert(StrEq(tree, StrL("v_flex .flex\n"
                              "  text \"Save\"\n"
                              "  Collapsible\n"
                              "    @content\n"
                              "      text \"body\"\n")));
    ArenaDelete(output);
    delete snapshot;
    utassert(lease.refs == 0 && lease.retired == 1);
    utassert(lease.generation == 77);
}

static void ThemeTokenNamesAndValuesComeFromTheTheme() {
    utassert(SeqStrCount(ThemeColorTokenNames()) == 17);
    utassert(SeqStrCount(ThemeSpacingTokenNames()) == 7);
    utassert(SeqStrCount(ThemeRadiusTokenNames()) == 6);
    App app;
    component::Init(&app);
    ThemeTokensSync(&app);
    float value = -1;
    utassert(ThemeTokenSpacing(StrL("md"), &value));
    utassert(value >= 0);
    utassert(!ThemeTokenSpacing(StrL("middle"), &value));
    Hsla color = {};
    utassert(ThemeTokenColor(StrL("background"), &color));
    utassert(!ThemeTokenColor(StrL("not_a_token"), &color));
    AppGlobalClear(&app);
}

static void RenderContextExposesFrozenGenerationBoundTheme() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    ViewType* type =
        runtime
            ? runtime->LoadSource(
                  StrL("context-theme.js"),
                  StrL("import { div, View } from 'gpui-kit'; export default "
                       "class Themed extends View { render(cx) { const "
                       "theme = cx.theme(); if (!Object.isFrozen(theme) || "
                       "!Object.isFrozen(theme.colors) || "
                       "!Object.isFrozen(theme.spacing) || "
                       "!Object.isFrozen(theme.radius)) throw new "
                       "Error('theme snapshot must be deeply frozen'); if "
                       "(this.savedTheme) this.savedTheme(); else "
                       "this.savedTheme = cx.theme; return "
                       "div().text_color(theme.foreground).bg(theme.colors."
                       "surface).p(theme.spacing.md).rounded(theme.radius."
                       "md).child('semantic'); } }"),
                  &error)
            : nullptr;
    ViewObject* object =
        type && runtime
            ? runtime->Instantiate(type, &window, &app, nullptr, &error)
            : nullptr;
    Arena* output = ArenaNew();
    Str first = object && runtime
                    ? runtime->RenderToSpec(output, object, &window, &app, {},
                                            nullptr, &error)
                    : Str{};
    utassert(!error.IsSet() && StrContains(first, StrL(".text_color(\"#")) &&
             StrContains(first, StrL(".p(12)")));
    output->Reset();
    Str second = object && runtime
                     ? runtime->RenderToSpec(output, object, &window, &app, {},
                                             nullptr, &error)
                     : Str{};
    (void)second;
    utassert(StrContains(error.message, StrL("cx is no longer valid")));
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    ArenaDelete(output);
    if (runtime) runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static void ScriptThemesAndOpenUrlsFollowHostScopeRules() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    ViewType* type =
        runtime
            ? runtime->LoadSource(
                  StrL("set-theme.js"),
                  StrL(
                      "import { div, View } from 'gpui'; import { set_theme } "
                      "from 'gpui-base';\n"
                      "const colors = { background:'#010203', "
                      "foreground:'#fafafa', surface:'#111111', "
                      "surface_foreground:'#f0f0f0', primary:'#222222', "
                      "primary_foreground:'#eeeeee', secondary:'#333333', "
                      "secondary_foreground:'#dddddd', muted:'#444444', "
                      "muted_foreground:'#cccccc', accent:'#555555', "
                      "accent_foreground:'#bbbbbb', destructive:'#666666', "
                      "destructive_foreground:'#aaaaaa', border:'#777777', "
                      "input:'#888888', ring:'#999999' };\n"
                      "export default class Themed extends View {\n"
                      "  init() { set_theme({ appearance:'dark', tokens:{ "
                      "colors, "
                      "spacing:{xxs:1,xs:2,sm:3,md:13,lg:21,xl:34,xxl:55}, "
                      "radius:{none:0,sm:2,md:9,lg:12,xl:18,full:999} } }); }\n"
                      "  render(cx) { const t = cx.theme(); if (t.appearance "
                      "!== 'dark' || t.background !== '#010203' || "
                      "t.spacing.md !== 13 || t.radius.md !== 9) throw new "
                      "Error('installed theme was not returned'); return "
                      "div().p(t.spacing.md).rounded(t.radius.md); }\n"
                      "}"),
                  &error)
            : nullptr;
    ViewObject* object =
        type && runtime
            ? runtime->Instantiate(type, &window, &app, nullptr, &error)
            : nullptr;
    const BaseTheme* theme = BaseThemeGlobal(&app);
    utassert(!error.IsSet() && theme &&
             theme->appearance == BaseThemeAppearance::Dark &&
             theme->tokens.spacing.md == 13 && theme->tokens.radius.md == 9 &&
             theme->tokens.colors.background.r == 1 &&
             theme->tokens.colors.background.g == 2 &&
             theme->tokens.colors.background.b == 3);
    Arena* output = ArenaNew();
    Str spec = object && runtime
                   ? runtime->RenderToSpec(output, object, &window, &app, {},
                                           nullptr, &error)
                   : Str{};
    utassert(!error.IsSet() && StrContains(spec, StrL(".p(13)")) &&
             StrContains(spec, StrL(".rounded(9)")));
    ViewObjectRelease(object);
    ViewTypeRelease(type);

    type =
        runtime
            ? runtime->LoadSource(
                  StrL("theme-in-render.js"),
                  StrL(
                      "import { div, View } from 'gpui'; import { set_theme } "
                      "from 'gpui-base'; export default class BadTheme extends "
                      "View { render() { set_theme({}); return div(); } }"),
                  &error)
            : nullptr;
    object = type && runtime
                 ? runtime->Instantiate(type, &window, &app, nullptr, &error)
                 : nullptr;
    output->Reset();
    if (object && runtime) {
        runtime
            ->RenderToSpec(output, object, &window, &app, {}, nullptr, &error);
    }
    utassert(
        StrContains(error.message, StrL("cannot run during render or layout")));
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    ShellErrorClear(&error);

    type = runtime
               ? runtime->LoadSource(
                     StrL("bad-url.js"),
                     StrL("import { div, View } from 'gpui'; export default "
                          "class BadUrl extends View { render(cx) { "
                          "cx.open_url('file:///tmp/no'); return div(); } }"),
                     &error)
               : nullptr;
    object = type && runtime
                 ? runtime->Instantiate(type, &window, &app, nullptr, &error)
                 : nullptr;
    output->Reset();
    if (object && runtime) {
        runtime
            ->RenderToSpec(output, object, &window, &app, {}, nullptr, &error);
    }
    utassert(StrContains(error.message, StrL("absolute HTTP(S) URL")));
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    ArenaDelete(output);
    if (runtime) runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static shell::CallbackId FirstCallback(const RenderSnapshot* snapshot) {
    if (!snapshot || !snapshot->Specs()) return UINT64_MAX;
    const SpecNode* root = snapshot->Specs()->Node(snapshot->Root());
    if (!root) return UINT64_MAX;
    for (SpecId childId : root->children) {
        const SpecNode* child = snapshot->Specs()->Node(childId);
        if (!child) continue;
        for (const SpecOp& op : child->ops) {
            if (op.kind == SpecOpKind::Callback) return op.callback;
        }
    }
    return UINT64_MAX;
}

static void RuntimeLoadsRendersAndRetiresCallbacks() {
    App app;
    Window window;
    window.app = &app;
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr && !error.IsSet());
    if (!runtime) return;

    Str source = StrL(
        "import { View } from 'gpui';\n"
        "import { v_flex, Button } from 'gpui-base';\n"
        "export default class Main extends View {\n"
        "  render(cx) {\n"
        "    return v_flex().id('root').p(12).items_center()\n"
        "      .child('hello')\n"
        "      .child(Button.new('save')\n"
        "        .on_click((event) => { globalThis.shellClicks = "
        "event.click_count; })\n"
        "        .child('Save'));\n"
        "  }\n"
        "}\n");
    ViewType* type = runtime
                         ->LoadSource(StrL("runtime-test.js"), source, &error);
    utassert(type != nullptr && !error.IsSet());
    ViewObject* object =
        type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
             : nullptr;
    utassert(object != nullptr && !error.IsSet());
    RenderSnapshot* snapshot =
        object
            ? runtime->BuildSnapshot(object, &window, &app, {}, nullptr, &error)
            : nullptr;
    utassert(snapshot != nullptr && !error.IsSet());
    if (snapshot) {
        Arena* arena = ArenaNew();
        Str tree = snapshot->DebugTree(arena);
        utassert(StrEq(tree, StrL("v_flex :id(\"root\") .p(12) .items_center\n"
                                  "  text \"hello\"\n"
                                  "  Button \"save\" :on_click(fn)\n"
                                  "    text \"Save\"\n")));
        ArenaDelete(arena);
        utassert(runtime->LiveCallbacks() == 1);
        shell::CallbackId callback = FirstCallback(snapshot);
        utassert(callback != UINT64_MAX);
        if (callback != UINT64_MAX) {
            ClickEvent event = {};
            event.clickCount = 3;
            runtime->DispatchClick(callback, event, &window, &app);
            utassert(runtime
                         ->Eval(StrL("if (globalThis.shellClicks !== 3) throw "
                                     "new Error('callback did not run')"),
                                StrL("callback-check.js"), &error));
            utassert(!error.IsSet());
        }
        delete snapshot;
        utassert(runtime->LiveCallbacks() == 0);
    }

    ViewObjectRelease(object);
    ViewTypeRelease(type);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static void RuntimeAbortsFailedSnapshotTransactions() {
    App app;
    Window window;
    window.app = &app;
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    if (!runtime) return;
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "export default class Main extends View {\n"
        "  render(cx) {\n"
        "    const child = div();\n"
        "    return div().on_click(() => {}).child(child).child(child);\n"
        "  }\n"
        "}\n");
    ViewType* type = runtime
                         ->LoadSource(StrL("failed-render.js"), source, &error);
    ViewObject* object =
        type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
             : nullptr;
    RenderSnapshot* snapshot =
        object
            ? runtime->BuildSnapshot(object, &window, &app, {}, nullptr, &error)
            : nullptr;
    utassert(snapshot == nullptr);
    utassert(error.IsSet());
    utassert(StrFind(error.message, StrL("already added to a parent")) >= 0);
    utassert(StrFind(error.message, StrL("failed-render.js")) >= 0);
    utassert(runtime->LiveCallbacks() == 0);
    delete snapshot;
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static bool WriteTestModule(const char* name, const char* source) {
    FILE* file = fopen(name, "wb");
    if (!file) return false;
    size_t len = strlen(source);
    bool ok = fwrite(source, 1, len, file) == len;
    fclose(file);
    return ok;
}

static void ShellSourceWatchReloadsAtomically() {
    const char* mainName = "shell_watch_main.js";
    const char* notesName = "shell_watch_notes.md";
    remove(mainName);
    remove(notesName);
    utassert(WriteTestModule(
        mainName,
        "import { View, div } from 'gpui'; export default class Main extends "
        "View { render() { return div().child('old'); } }\n"));

    SourceWatcher watcher;
    ShellError error = {};
    utassert(watcher.Init(StrL("."), &error, 0));
    utassert(!error.IsSet());
    utassert(WriteTestModule(notesName, "not source\n"));
    bool changed = true;
    utassert(watcher.PollAt(1, &changed, &error));
    utassert(!changed);
    utassert(WriteTestModule(
        mainName,
        "import { View, div } from 'gpui'; export default class Main extends "
        "View { render() { return div().child('old but changed'); } }\n"));
    utassert(watcher.PollAt(2, &changed, &error));
    utassert(changed);
    utassert(watcher.PollAt(3, &changed, &error));
    utassert(!changed);

    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    ViewType* type =
        runtime ? runtime->LoadApp(StrL("."), Str(mainName), &error) : nullptr;
    Entity<ScriptView> entity =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    El* initial = entity.IsValid()
                      ? EntityRender(&app, &window, frame, entity.id)
                      : nullptr;
    utassert(initial != nullptr);
    ScriptView* view = entity.Get(&app);
    Arena* text = ArenaNew();
    utassert(view && view->snapshot);
    utassert(
        StrFind(view->snapshot->DebugTree(text), StrL("old but changed")) >= 0);

    utassert(WriteTestModule(mainName, "export default class { render( {\n"));
    Ctx cx = {&app, &window, frame, entity.id};
    utassert(!ScriptView::Reload(view, &cx, StrL("."), Str(mainName), &error));
    utassert(error.IsSet());
    utassert(view->snapshot != nullptr);
    text->Reset();
    utassert(
        StrFind(view->snapshot->DebugTree(text), StrL("old but changed")) >= 0);

    utassert(WriteTestModule(
        mainName,
        "import { View, div } from 'gpui'; export default class Main extends "
        "View { render() { return div().child('new live view'); } }\n"));
    utassert(ScriptView::Reload(view, &cx, StrL("."), Str(mainName), &error));
    utassert(!error.IsSet());
    frame->Reset();
    utassert(EntityRender(&app, &window, frame, entity.id) != nullptr);
    text->Reset();
    utassert(StrFind(view->snapshot->DebugTree(text), StrL("new live view")) >=
             0);

    EntityDrop(&app, entity.id);
    if (runtime) runtime->Release();
    ArenaDelete(text);
    ArenaDelete(frame);
    ShellErrorClear(&error);
    AppGlobalClear(&app);
    remove(mainName);
    remove(notesName);
}

static void HostIncrement(HostCall* call) {
    double value = 0;
    if (!call || !call->arguments ||
        !call->arguments->Number(0, &value, &call->error))
        return;
    call->result.SetNumber(value + 1);
}

static void HostEcho(HostCall* call) {
    const HostValue* value = nullptr;
    if (!call || !call->arguments ||
        !call->arguments->Value(0, &value, &call->error))
        return;
    if (!call->result.CopyFrom(*value))
        call->error.Set(StrL("copying the host value failed"));
}

static void HostDouble(HostCall* call) {
    double value = 0;
    if (!call || !call->arguments ||
        !call->arguments->Number(0, &value, &call->error))
        return;
    call->result.SetNumber(value * 2);
}

static bool ShellFixtureFs(FsOperation operation, Str root, Str relative,
                           Str input = {}, bool recursive = false);

static void ShellHostModulesBridgePlainDataAndPromises() {
    ShellClearExportedModules();
    HostError hostError;
    HostModule* reserved = HostModule::New(StrL("path"));
    utassert(!ShellExportModule(reserved, &hostError));
    utassert(hostError.IsSet());
    hostError.Clear();
    reserved->Release();

    HostModule* mismatched =
        HostModule::New(StrL("bad-types"))
            ->Function(StrL("actual"), MkFunc1Void(HostIncrement))
            ->Declarations(StrL("export function declared(): number;"));
    utassert(!ShellExportModule(mismatched, &hostError));
    utassert(StrFind(hostError.message, StrL("actual")) >= 0);
    utassert(StrFind(hostError.message, StrL("declared")) >= 0);
    hostError.Clear();
    mismatched->Release();

    HostModule* module =
        HostModule::New(StrL("calculator"))
            ->Function(StrL("increment"), MkFunc1Void(HostIncrement))
            ->Function(StrL("echo"), MkFunc1Void(HostEcho))
            ->AsyncFunction(StrL("double"), MkFunc1Void(HostDouble))
            ->Declarations(StrL(
                "export function increment(value: number): number;\n"
                "export function echo(value: unknown): unknown;\n"
                "export function double(value: number): Promise<number>;\n"));
    utassert(ShellExportModule(module, &hostError));
    utassert(!hostError.IsSet());
    module->Release();

    App app;
    Window window;
    window.app = &app;
    VecAppend(app.windows, &window);
    component::Init(&app);
    ExecInit();
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "import { increment, echo, double } from 'calculator';\n"
        "globalThis.hostAsync = 'pending';\n"
        "globalThis.hostSync = JSON.stringify(echo({answer:[increment(41), "
        "true, 'ok']}));\n"
        "export default class Main extends View {\n"
        "  init(props, cx) { cx.spawn(async cx => { hostAsync = String(await "
        "double(21)); cx.notify(); }); }\n"
        "  render(cx) { let live; try { live = increment(1); } catch (e) { "
        "live = 'refused:' + e.message; } return div().child(hostSync + '|' + "
        "hostAsync + '|' + live); }\n"
        "}\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("host-module.js"), source, &error)
                : nullptr;
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    window.frameArena = frame;
    utassert(view.IsValid() &&
             EntityRender(&app, &window, frame, view.id) != nullptr);
    utassert(!error.IsSet());
    utassert(
        runtime &&
        runtime->Eval(StrL("if (hostSync !== '{\"answer\":[42,true,\"ok\"]}') "
                           "throw new Error(hostSync)"),
                      StrL("host-sync-check.js"), &error));
    utassert(ShellSettle(runtime, 5000));
    utassert(runtime &&
             runtime->Eval(
                 StrL("if (hostAsync !== '42') throw new Error(hostAsync)"),
                 StrL("host-async-check.js"), &error));

    ShellClearExportedModules();
    ScriptView* live = view.Get(&app);
    Ctx cx = {&app, &window, frame, view.id};
    ScriptView::Refresh(live, &cx);
    frame->Reset();
    utassert(EntityRender(&app, &window, frame, view.id) != nullptr);
    Arena* text = ArenaNew();
    utassert(live && live->snapshot &&
             StrFind(live->snapshot->DebugTree(text), StrL("refused:")) >= 0);
    utassert(
        StrFind(live->snapshot->DebugTree(text), StrL("registered none")) >= 0);

    EntityDrop(&app, view.id);
    app.windows.len = 0;
    ArenaDelete(text);
    ArenaDelete(frame);
    if (runtime) runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
    ShellClearExportedModules();
}

static void ShellTypeDeclarationsMatchRuntimeAndRefreshImportDirectories() {
    HostModules* modules = HostModulesNew();
    HostModule* workspace =
        HostModule::New(StrL("workspace"))
            ->Function(StrL("open"), MkFunc1Void(HostEcho))
            ->AsyncFunction(StrL("search"), MkFunc1Void(HostEcho));
    utassert(HostModulesInsert(modules, workspace));
    workspace->Release();

    StrBuilder declarations;
    ShellTypeDeclarations(&declarations, modules);
    Str text = declarations.TakeStr();
    utassert(text.len > 600000);
    utassert(StrContains(text, StrL("declare module \"gpui-kit\"")) &&
             StrContains(text, StrL("declare module \"gpui\"")) &&
             StrContains(text, StrL("export const Link: ComponentType;")) &&
             StrContains(text, StrL("declare module \"gpui-base\"")) &&
             StrContains(text, StrL("declare module \"gpui-shell\"")) &&
             StrContains(text, StrL("declare module \"gpui-fps\"")));
    utassert(
        StrContains(text, StrL("declare module \"workspace\"")) &&
        StrContains(
            text,
            StrL("export function open(...args: HostValue[]): HostValue;")) &&
        StrContains(text, StrL("export function search(...args: HostValue[]): "
                               "Promise<HostValue>;")));

#if GPUI_OS_WASM
    // Browsers have no writable application directory to refresh. The
    // declarations themselves are still generated and checked above.
    StrFree(text);
    HostModulesRelease(modules);
    return;
#endif

    const char* rootName = "shell_types_test_root";
    remove("shell_types_test_root/gpui-kit.d.ts");
    remove("shell_types_test_root/jsconfig.json");
    remove("shell_types_test_root/nested/gpui-kit.d.ts");
    remove("shell_types_test_root/nested/main.js");
#if GPUI_OS_WINDOWS
    RemoveDirectoryA("shell_types_test_root/nested");
    RemoveDirectoryA(rootName);
#else
    rmdir("shell_types_test_root/nested");
    rmdir(rootName);
#endif
    utassert(ShellFixtureFs(FsOperation::MakeDirectory, Str(rootName),
                            StrL("nested"), {}, true));
    utassert(ShellFixtureFs(FsOperation::Write, Str(rootName),
                            StrL("nested/main.js"),
                            StrL("import { fps_monitor } from 'gpui-fps'; "
                                 "export default fps_monitor();")));

    ShellError error = {};
    int written = 0;
    utassert(
        ShellWriteTypeDeclarations(Str(rootName), modules, &written, &error));
    // ad216356: the root also gets a jsconfig.json, scaffolded once, so an
    // inferred moduleResolution cannot land on the one that never looks in
    // node_modules and the browser's default `lib` cannot collide with
    // gpui-kit.d.ts.
    utassert(!error.IsSet() && written == 3);
    FsResult result;
    Str fsError;
    utassert(FsRun(FsOperation::Read, Str(rootName), StrL("gpui-kit.d.ts"), {},
                   false, &result, &fsError));
    utassert(StrEq(result.bytes, text));
    result.Free();
    utassert(FsRun(FsOperation::Read, Str(rootName),
                   StrL("nested/gpui-kit.d.ts"), {}, false, &result, &fsError));
    utassert(StrEq(result.bytes, text));
    result.Free();
    written = -1;
    utassert(
        ShellWriteTypeDeclarations(Str(rootName), modules, &written, &error));
    utassert(written == 0);
    StrFree(fsError);
    ShellErrorClear(&error);

    utassert(ShellFixtureFs(FsOperation::RemoveFile, Str(rootName),
                            StrL("nested/gpui-kit.d.ts")));
    utassert(ShellFixtureFs(FsOperation::RemoveFile, Str(rootName),
                            StrL("nested/main.js")));
    utassert(ShellFixtureFs(FsOperation::RemoveFile, Str(rootName),
                            StrL("gpui-kit.d.ts")));
    // `an_existing_editor_configuration_is_never_replaced`: the scaffold is
    // written once and then belongs to whoever opens it.
    utassert(FsRun(FsOperation::Read, Str(rootName), StrL("jsconfig.json"), {},
                   false, &result, &fsError));
    utassert(
        StrContains(result.bytes, StrL("\"moduleResolution\": \"bundler\"")) &&
        StrContains(result.bytes, StrL("\"strictNullChecks\": false")));
    result.Free();
    utassert(ShellFixtureFs(FsOperation::Write, Str(rootName),
                            StrL("jsconfig.json"), StrL("{}")));
    written = -1;
    utassert(
        ShellWriteTypeDeclarations(Str(rootName), modules, &written, &error) &&
        written == 1);
    utassert(FsRun(FsOperation::Read, Str(rootName), StrL("jsconfig.json"), {},
                   false, &result, &fsError));
    utassert(StrEq(result.bytes, StrL("{}")));
    result.Free();
    utassert(ShellFixtureFs(FsOperation::RemoveFile, Str(rootName),
                            StrL("gpui-kit.d.ts")));
    utassert(ShellFixtureFs(FsOperation::RemoveFile, Str(rootName),
                            StrL("jsconfig.json")));
    utassert(ShellFixtureFs(FsOperation::RemoveDirectory, Str(rootName),
                            StrL("nested")));
#if GPUI_OS_WINDOWS
    utassert(RemoveDirectoryA(rootName) != 0);
#else
    utassert(rmdir(rootName) == 0);
#endif
    StrFree(text);
    HostModulesRelease(modules);
}

static void RuntimeLoadsOnlyModulesInsideTheApplicationRoot() {
#if GPUI_OS_WASM
    // LoadApp is a hosted filesystem entry point. Browser scripts enter
    // through LoadSource instead, which the surrounding runtime tests cover.
    return;
#endif
    const char* depName = "shell_runtime_dep.js";
    const char* mainName = "shell_runtime_main.js";
    const char* badName = "shell_runtime_bad.js";
    const char* outsideName = "../shell_runtime_outside.js";
    utassert(
        WriteTestModule(depName, "export const label = 'from dependency';\n"));
    utassert(WriteTestModule(mainName,
                             "import { View, div } from 'gpui';\n"
                             "import { label } from './shell_runtime_dep.js';\n"
                             "export default class Main extends View { "
                             "render(cx) { return div().child(label); } }\n"));
    utassert(WriteTestModule(outsideName, "export const escaped = true;\n"));
    utassert(WriteTestModule(
        badName,
        "import { View, div } from 'gpui';\n"
        "import { escaped } from '../shell_runtime_outside.js';\n"
        "export default class Main extends View { render(cx) { return div(); } "
        "}\n"));

    App app;
    Window window;
    window.app = &app;
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    ViewType* type =
        runtime ? runtime->LoadApp(StrL("."), Str(mainName), &error) : nullptr;
    utassert(type != nullptr && !error.IsSet());
    ViewObject* object =
        type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
             : nullptr;
    Arena* arena = ArenaNew();
    Str tree = object ? runtime->RenderToSpec(arena, object, &window, &app, {},
                                              nullptr, &error)
                      : Str{};
    utassert(StrEq(tree, StrL("div\n  text \"from dependency\"\n")));

    ViewType* escaped =
        runtime ? runtime->LoadApp(StrL("."), Str(badName), &error) : nullptr;
    utassert(escaped == nullptr && error.IsSet());
    utassert(
        StrFind(error.message, StrL("outside the application directory")) >= 0);

    ViewTypeRelease(escaped);
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    if (runtime) runtime->Release();
    ShellErrorClear(&error);
    ArenaDelete(arena);
    AppGlobalClear(&app);
    remove(depName);
    remove(mainName);
    remove(badName);
    remove(outsideName);
}

static void PublishedSnapshotsMaterializeToNativeElements() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View } from 'gpui';\n"
        "import { v_flex, Button } from 'gpui-base';\n"
        "export default class Main extends View { render(cx) {\n"
        "  return "
        "v_flex().role('list_box').aria_active_descendant().w(320).h(80).gap_2("
        ").p(12).bg('#123456').rounded(8)\n"
        "    .child('native')\n"
        "    .child(Button.new('disabled').disabled(true).child('No'));\n"
        "} }\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("materialize.js"), source, &error)
                : nullptr;
    ViewObject* object =
        type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
             : nullptr;
    RenderSnapshot* snapshot =
        object
            ? runtime->BuildSnapshot(object, &window, &app, {}, nullptr, &error)
            : nullptr;
    Arena* frame = ArenaNew();
    Ctx cx = {&app, &window, frame, {}};
    El* root =
        snapshot ? ShellMaterialize(&cx, runtime, snapshot, &error) : nullptr;
    utassert(root != nullptr && !error.IsSet());
    if (root) {
        utassert(root->style.display == Display::Flex);
        utassert(root->style.dir == FlexDir::Col);
        utassertnear(root->style.width, 320);
        utassertnear(root->style.height, 80);
        utassertnear(root->style.gapX, 8);
        utassertnear(root->style.gapY, 8);
        utassertnear(root->style.pad.top, 12);
        utassertnear(root->style.radius, 8);
        utassert(root->style.hasBg);
        utassert(root->accessibility.role == AccessibilityRole::ListBox);
        utassert(root->accessibility.activeDescendant);
        utassert(abs((int)root->style.bg.color.r - 0x12) <= 1);
        utassert(abs((int)root->style.bg.color.g - 0x34) <= 1);
        utassert(abs((int)root->style.bg.color.b - 0x56) <= 1);
        utassert(root->first && root->first->kind == ElKind::Text);
        utassert(StrEq(root->first->text, StrL("native")));
        utassert(root->first->next != nullptr);
        utassert(root->first->next->accessibility.disabled);
    }
    ArenaDelete(frame);
    delete snapshot;
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    if (runtime) runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static void ShellMaterializesStateTemplatesInputsAndPaths() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div, PathBuilder, Background } from 'gpui';\n"
        "import { InputState, NumberInput, OtpState, OtpInput } from "
        "'gpui-base';\n"
        "globalThis.numberStep = '';\n"
        "export default class Main extends View {\n"
        "  init() { this.number = InputState.new({ value: '4' }); "
        "this.number.set_step(2); this.otp = OtpState.new(3, { value: '1' }); "
        "}\n"
        "  render(cx) {\n"
        "    const path = PathBuilder.fill().move_to(0, 0).line_to('100%', "
        "0).curve_to('100%', '100%', '50%', '50%').close().build();\n"
        "    return div().children([\n"
        "      div().id('states').w(100).transition('width', { duration: 0 "
        "}).hover(s => s.bg('#112233').p(4)).active(s => "
        "s.bg('#223344')).focus(s => s.opacity(0.5)),\n"
        "      "
        "NumberInput.new(this.number).controls_right().decrement_button(div()."
        "size(10).child('-')).increment_button(div().size(10).child('+')).on_"
        "step(action => { globalThis.numberStep = action; }),\n"
        "      OtpInput.new(this.otp).cell_style(cell => "
        "cell.size(20).bg('#334455')).cell_active_style(cell => "
        "cell.border(2)),\n"
        "      window.paint_path(path, Background.linear_gradient(90, "
        "Background.stop('#000000', 0.25), '#ffffff')).w(100).h(80),\n"
        "    ]);\n"
        "  }\n"
        "}\n");
    ViewType* type = runtime
                         ? runtime->LoadSource(StrL("material-components.js"),
                                               source, &error)
                         : nullptr;
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    window.frameArena = frame;
    El* root =
        view.IsValid() ? EntityRender(&app, &window, frame, view.id) : nullptr;
    utassert(root != nullptr && !error.IsSet());
    El* states = root ? root->first : nullptr;
    El* number = states ? states->next : nullptr;
    El* otp = number ? number->next : nullptr;
    El* path = otp ? otp->next : nullptr;
    utassert(states && states->StyleStates()->hoverSet & StyleFieldBg);
    utassert(states && states->StyleStates()->hoverSet & StyleFieldPad);
    utassert(states && states->StyleStates()->activeSet & StyleFieldBg);
    utassert(states && states->StyleStates()->focusSet & StyleFieldOpacity);
    utassert(states && states->style.width == 100);
    utassert(number && number->accessibility
                               .role == AccessibilityRole::SpinButton);
    El* controls = number && number->first ? number->first->next : nullptr;
    El* increment = controls ? controls->first : nullptr;
    utassert(increment && increment->onClick.IsValid());
    if (increment && increment->onClick.IsValid()) increment->onClick.Call();
    utassert(runtime &&
             runtime
                 ->Eval(StrL("if (globalThis.numberStep !== 'increment') throw "
                             "new Error('number step was not dispatched')"),
                        StrL("number-step-check.js"), &error));
    utassert(otp && otp->first && otp->first->next && otp->first->next->next);
    utassert(otp && otp->first && otp->first->style.hasBg);
    utassert(path && path->customPaint != nullptr);
    utassert(path && path->style.width == 100 && path->style.height == 80);
    EntityDrop(&app, view.id);
    ArenaDelete(frame);
    if (runtime) runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static void ShellRootHostsDialogsSheetsAndToasts() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "import { Button } from 'gpui-base';\n"
        "export default class Main extends View {\n"
        "  render() { return div().children([\n"
        "    Button.new('open-dialog').on_click(() => window.open_dialog(() => "
        "div().id('dialog-content').child('Dialog'))),\n"
        "    Button.new('close-dialog').on_click(() => "
        "window.close_dialog()),\n"
        "    Button.new('open-sheet').on_click(() => "
        "window.open_sheet_at('left', () => "
        "div().id('sheet-content').child('Sheet'))),\n"
        "    Button.new('close-sheet').on_click(() => window.close_sheet()),\n"
        "    Button.new('toast').on_click(() => window.push_toast({ title: "
        "'Saved', description: 'One file', level: 'success', id: 'save', "
        "timeout: null })),\n"
        "    Button.new('remove-toast').on_click(() => "
        "window.remove_toast('save')),\n"
        "  ]); }\n"
        "}\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("shell-root.js"), source, &error)
                : nullptr;
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Entity<ShellRoot> shellRoot =
        view.IsValid() ? ShellRoot::New(&app, view.id) : Entity<ShellRoot>{};
    window.root = shellRoot.id;
    Arena* frame = ArenaNew();
    window.frameArena = frame;
    El* root = shellRoot.IsValid()
                   ? EntityRender(&app, &window, frame, shellRoot.id)
                   : nullptr;
    El* script = root ? root->first : nullptr;
    El* openDialog = script ? script->first : nullptr;
    El* closeDialog = openDialog ? openDialog->next : nullptr;
    El* openSheet = closeDialog ? closeDialog->next : nullptr;
    El* closeSheet = openSheet ? openSheet->next : nullptr;
    El* toast = closeSheet ? closeSheet->next : nullptr;
    El* removeToast = toast ? toast->next : nullptr;
    utassert(root && ShellRootOf(&window, &app) != nullptr);
    utassert(openDialog && openDialog->listener.IsValid());
    ClickEvent click = {};
    if (openDialog && openDialog->listener.IsValid())
        ListenerCall(&app, &window, openDialog->listener, &click);
    Ctx rootCx = {&app, &window, frame, shellRoot.id};
    utassert(ShellRootHasDialog(&rootCx));
    utassert(runtime && runtime->LiveNestedViews() == 0);
    frame->Reset();
    root = EntityRender(&app, &window, frame, shellRoot.id);
    utassert(root && root->first && root->first->next);
    script = root ? root->first : nullptr;
    openDialog = script ? script->first : nullptr;
    closeDialog = openDialog ? openDialog->next : nullptr;
    openSheet = closeDialog ? closeDialog->next : nullptr;
    closeSheet = openSheet ? openSheet->next : nullptr;
    toast = closeSheet ? closeSheet->next : nullptr;
    removeToast = toast ? toast->next : nullptr;
    if (closeDialog && closeDialog->listener.IsValid())
        ListenerCall(&app, &window, closeDialog->listener, &click);
    utassert(!ShellRootHasDialog(&rootCx));
    if (openSheet && openSheet->listener.IsValid())
        ListenerCall(&app, &window, openSheet->listener, &click);
    utassert(ShellRootHasSheet(&rootCx));
    if (closeSheet && closeSheet->listener.IsValid())
        ListenerCall(&app, &window, closeSheet->listener, &click);
    utassert(!ShellRootHasSheet(&rootCx));
    if (toast && toast->listener.IsValid())
        ListenerCall(&app, &window, toast->listener, &click);
    utassert(ShellRootToastCount(&rootCx) == 1);
    if (removeToast && removeToast->listener.IsValid())
        ListenerCall(&app, &window, removeToast->listener, &click);
    component::NotificationListState* notifications =
        WindowNotifications(&rootCx).Get(&rootCx);
    utassert(notifications && notifications->items.len == 1 &&
             notifications->stack.entries.len == 1 &&
             notifications->stack.entries[0]
                     .status == ToastTransitionStatus::Ending);
    ShellRootClearToasts(&rootCx);
    EntityDrop(&app, shellRoot.id);
    ArenaDelete(frame);
    if (runtime) runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static void ScriptViewsReuseSnapshotsUntilNotified() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "globalThis.scriptRenderCount = 0;\n"
        "export default class Main extends View { render(cx) {\n"
        "  globalThis.scriptRenderCount += 1;\n"
        "  return div().child(String(globalThis.scriptRenderCount));\n"
        "} }\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("cached-view.js"), source, &error)
                : nullptr;
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    El* first = EntityRender(&app, &window, frame, view.id);
    utassert(first != nullptr);
    frame->Reset();
    El* second = EntityRender(&app, &window, frame, view.id);
    utassert(second != nullptr);
    utassert(runtime->Eval(StrL("if (globalThis.scriptRenderCount !== 1) throw "
                                "new Error('snapshot was rebuilt')"),
                           StrL("cached-check.js"), &error));
    runtime->InvalidateScriptView(view.id);
    frame->Reset();
    El* third = EntityRender(&app, &window, frame, view.id);
    utassert(third != nullptr);
    utassert(runtime->Eval(StrL("if (globalThis.scriptRenderCount !== 2) throw "
                                "new Error('dirty view was not rebuilt')"),
                           StrL("dirty-check.js"), &error));
    EntityDrop(&app, view.id);
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static void RetainedScriptStateSurvivesFramesAndDispatchesEvents() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View } from 'gpui';\n"
        "import { v_flex, InputState, Input, SliderState, Slider, OtpState, "
        "OtpInput } from 'gpui-base';\n"
        "globalThis.retainedEvents = 0;\n"
        "export default class Main extends View {\n"
        "  init(props, cx) {\n"
        "    this.input = InputState.new({ value: 'first', placeholder: 'type' "
        "});\n"
        "    this.slider = SliderState.new({ min: 0, max: 10, step: 1, value: "
        "3 });\n"
        "    this.otp = OtpState.new(6, { value: '12' });\n"
        "    this.input.on('change', () => { globalThis.retainedEvents += 1; "
        "this.input.set_value('second'); });\n"
        "    this.slider.on('release', () => { globalThis.retainedEvents += "
        "10; });\n"
        "    this.otp.on('complete', () => { globalThis.retainedEvents += 100; "
        "});\n"
        "    globalThis.retainedInput = this.input;\n"
        "  }\n"
        "  render(cx) { return v_flex().children([\n"
        "    Input.new(this.input), Slider.new(this.slider), "
        "OtpInput.new(this.otp)\n"
        "  ]); }\n"
        "}\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("retained-state.js"), source, &error)
                : nullptr;
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    El* root =
        view.IsValid() ? EntityRender(&app, &window, frame, view.id) : nullptr;
    utassert(root != nullptr && !error.IsSet());
    utassert(runtime && runtime->LiveEntities() == 3);
    InputState* input = root && root->first ? root->first->input : nullptr;
    utassert(input != nullptr && StrEq(InputValue(input), StrL("first")));
    utassert(input && input->onChange.IsValid());
    if (input) {
        InputEvent changed = {InputEventKind::Change};
        ListenerCall(&app, &window, input->onChange, &changed);
    }
    utassert(runtime
                 ->Eval(StrL("if (globalThis.retainedEvents !== 1) throw new "
                             "Error('retained event was not dispatched')"),
                        StrL("retained-event-check.js"), &error));
    utassert(input && StrEq(InputValue(input), StrL("second")));
    utassert(runtime->Eval(StrL("globalThis.retainedInput.release()"),
                           StrL("retained-release.js"), &error));
    utassert(runtime->LiveEntities() == 2);
    EntityDrop(&app, view.id);
    utassert(runtime->LiveEntities() == 0);
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static void NestedScriptViewsRetainUpdateRollbackAndRelease() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "import { Button, InputState } from 'gpui-base';\n"
        "globalThis.nestedAction = 'none';\n"
        "globalThis.nestedNext = undefined;\n"
        "class Leaf extends View {\n"
        "  init(props) { this.label = props.label; }\n"
        "  render(cx) { globalThis.nestedLeafRendered = this.label; return "
        "div().child(this.label); }\n"
        "}\n"
        "class Child extends View {\n"
        "  init(props, cx) {\n"
        "    this.state = { label: props.label };\n"
        "    this.leaf = cx.new(Leaf, { label: 'leaf' });\n"
        "  }\n"
        "  update(props) {\n"
        "    if (props.append) this.state.label += props.append;\n"
        "    else this.state.label = props.label;\n"
        "    if (props.fail) {\n"
        "      this.temporary = InputState.new({ value: 'temporary' });\n"
        "      throw new Error('nested update failed');\n"
        "    }\n"
        "  }\n"
        "  render(cx) {\n"
        "    globalThis.nestedRendered = this.state.label;\n"
        "    return div().children([this.state.label, this.leaf]);\n"
        "  }\n"
        "}\n"
        "export default class Main extends View {\n"
        "  init(props, cx) { this.child = cx.new(Child, { label: 'one' }); }\n"
        "  render(cx) {\n"
        "    return div().children([\n"
        "      Button.new('nested-action').on_click(() => {\n"
        "        if (globalThis.nestedAction === 'release')\n"
        "          globalThis.nestedReleased = this.child.release();\n"
        "        else this.child.set_props(globalThis.nestedNext);\n"
        "      }),\n"
        "      this.child,\n"
        "    ]);\n"
        "  }\n"
        "}\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("nested-view.js"), source, &error)
                : nullptr;
    Entity<ScriptView> parent =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    window.frameArena = frame;
    El* root = parent.IsValid() ? EntityRender(&app, &window, frame, parent.id)
                                : nullptr;
    ScriptView* parentView = parent.Get(&app);
    shell::CallbackId callback =
        parentView ? FirstCallback(parentView->snapshot) : UINT64_MAX;
    utassert(root != nullptr && !error.IsSet());
    utassert(runtime && runtime->LiveNestedViews() == 2);
    utassert(callback != UINT64_MAX);
    utassert(
        runtime &&
        runtime->Eval(StrL("if (globalThis.nestedRendered !== 'one' || "
                           "globalThis.nestedLeafRendered !== 'leaf') throw "
                           "new Error('nested init props were not rendered')"),
                      StrL("nested-init-check.js"), &error));

    utassert(runtime &&
             runtime->Eval(StrL("globalThis.nestedNext = { label: 'two' }"),
                           StrL("nested-update-input.js"), &error));
    if (callback != UINT64_MAX) {
        ClickEvent click = {};
        runtime->DispatchClick(callback, click, &window, &app);
    }
    ArenaDelete(frame);
    frame = ArenaNew();
    window.frameArena = frame;
    root = EntityRender(&app, &window, frame, parent.id);
    utassert(root != nullptr);
    utassert(
        runtime &&
        runtime->Eval(StrL("if (globalThis.nestedRendered !== 'two') throw new "
                           "Error('set_props did not rebuild only the child')"),
                      StrL("nested-update-check.js"), &error));

    utassert(runtime &&
             runtime->Eval(
                 StrL("globalThis.nestedNext = { label: 'bad', fail: true }"),
                 StrL("nested-failure-input.js"), &error));
    if (callback != UINT64_MAX) {
        ClickEvent click = {};
        runtime->DispatchClick(callback, click, &window, &app);
    }
    utassert(runtime && runtime->LiveEntities() == 0);
    utassert(runtime && runtime->LiveNestedViews() == 2);
    utassert(runtime &&
             runtime->Eval(StrL("globalThis.nestedNext = { append: '!' }"),
                           StrL("nested-rollback-probe.js"), &error));
    if (callback != UINT64_MAX) {
        ClickEvent click = {};
        runtime->DispatchClick(callback, click, &window, &app);
    }
    ArenaDelete(frame);
    frame = ArenaNew();
    window.frameArena = frame;
    root = EntityRender(&app, &window, frame, parent.id);
    utassert(root != nullptr);
    utassert(
        runtime &&
        runtime->Eval(StrL("if (globalThis.nestedRendered !== 'two!') throw "
                           "new Error('failed update state was not restored')"),
                      StrL("nested-rollback-check.js"), &error));

    utassert(runtime && runtime
                            ->Eval(StrL("globalThis.nestedAction = 'release'"),
                                   StrL("nested-release-input.js"), &error));
    if (callback != UINT64_MAX) {
        ClickEvent click = {};
        runtime->DispatchClick(callback, click, &window, &app);
    }
    utassert(runtime && runtime->LiveNestedViews() == 0);
    utassert(runtime &&
             runtime->Eval(StrL("if (globalThis.nestedReleased !== true) throw "
                                "new Error('release failed')"),
                           StrL("nested-release-check.js"), &error));
    EntityDrop(&app, parent.id);
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static void VirtualListsRenderOneVisibleBatch() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "import { v_virtual_list, VirtualListScrollHandle } from 'gpui-base';\n"
        "globalThis.virtualBatches = 0; globalThis.virtualClick = '';\n"
        "export default class Main extends View {\n"
        "  init(props, cx) { this.scroll = VirtualListScrollHandle.new(); }\n"
        "  render(cx) { return v_virtual_list('rows', 20, 24,\n"
        "    index => 'row-' + index,\n"
        "    range => { globalThis.virtualBatches += 1; const out = [];\n"
        "      for (let i = range.start; i < range.end; i++) "
        "out.push(div().child('row ' + i));\n"
        "      return out;\n"
        "    }).track_scroll(this.scroll).on_item_click(key => { "
        "globalThis.virtualClick = key; }); }\n"
        "}\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("virtual-list.js"), source, &error)
                : nullptr;
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    El* root =
        view.IsValid() ? EntityRender(&app, &window, frame, view.id) : nullptr;
    utassert(root != nullptr && !error.IsSet());
    utassert(runtime
                 ->Eval(StrL("if (globalThis.virtualBatches !== 1) throw new "
                             "Error('virtual list did not render one range')"),
                        StrL("virtual-list-check.js"), &error));
    El* firstRow = root && root->first ? root->first->first : nullptr;
    utassert(firstRow && firstRow->listener.IsValid());
    if (firstRow && firstRow->listener.IsValid()) {
        ClickEvent click = {};
        ListenerCall(&app, &window, firstRow->listener, &click);
    }
    utassert(
        runtime->Eval(StrL("if (globalThis.virtualClick !== 'row-0') throw new "
                           "Error('virtual item key was not dispatched')"),
                      StrL("virtual-list-click-check.js"), &error));
    utassert(runtime->LiveEntities() == 1);
    EntityDrop(&app, view.id);
    utassert(runtime->LiveEntities() == 0);
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static void ShellSandboxWithholdsCompilersAndSharedPrototypeWrites() {
    ShellError error = {};
    ShellSetDevelopmentMode(false);
    ShellRuntime* runtime = ShellRuntime::New(nullptr, &error);
    utassert(runtime != nullptr && !error.IsSet());
    utassert(runtime && runtime
                            ->Eval(StrL("if (typeof eval !== 'undefined' || "
                                        "!Object.isFrozen(Object.prototype) || "
                                        "typeof std !== 'undefined') throw new "
                                        "Error('sandbox surface is open')"),
                                   StrL("sandbox-check.js"), &error));
    utassert(runtime && !runtime->Eval(StrL("new Function('return 1')()"),
                                       StrL("sandbox-function.js"), &error));
    utassert(error.IsSet() && StrContains(error.message, StrL("disabled")));
    ShellErrorClear(&error);
    if (runtime) runtime->Release();

    ShellSetDevelopmentMode(true);
    runtime = ShellRuntime::New(nullptr, &error);
    utassert(
        runtime &&
        runtime->Eval(
            StrL("if (eval('1 + 1') !== 2 || Function('return 3')() !== 3) "
                 "throw new Error('development compiler missing')"),
            StrL("development-mode.js"), &error));
    if (runtime) runtime->Release();
    ShellSetDevelopmentMode(false);
    ShellErrorClear(&error);
}

static void ShellSchedulerResumesPromisesInTaskScope() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "globalThis.taskEvents = 0;\n"
        "export default class Main extends View {\n"
        "  init(props, cx) {\n"
        "    this.once = cx.timer.after(1, cx => { taskEvents += 1; "
        "cx.notify(); });\n"
        "    globalThis.every = cx.timer.every(1, () => { taskEvents += 10; "
        "});\n"
        "    cx.sleep(1).then(() => { taskEvents += 100; });\n"
        "    cx.spawn(async cx => { await cx.sleep(1); taskEvents += 1000; "
        "cx.notify(); });\n"
        "  }\n"
        "  render(cx) { return div().child(String(taskEvents)); }\n"
        "}\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("scheduler.js"), source, &error)
                : nullptr;
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    window.frameArena = frame;
    El* root =
        view.IsValid() ? EntityRender(&app, &window, frame, view.id) : nullptr;
    utassert(root != nullptr && !error.IsSet());
    utassert(runtime && runtime->LiveTasks() == 5);
    for (int i = 0; i < window.timers.len; i++) window.timers[i].dueAt = 0;
    WindowTimerTick(&window);
    utassert(runtime->Eval(StrL("if (taskEvents !== 1111) throw new "
                                "Error('task resumptions were not drained')"),
                           StrL("scheduler-result.js"), &error));
    utassert(runtime->LiveTasks() == 1);
    utassert(runtime->Eval(StrL("globalThis.every.cancel()"),
                           StrL("scheduler-cancel.js"), &error));
    utassert(runtime->LiveTasks() == 0);
    EntityDrop(&app, view.id);
    VecReset(window.timers);
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

static void ShellStorageAndAuthorityFreeModulesWork() {
    const char* storagePath = "shell_storage_test.json";
    remove(storagePath);
    Capabilities granted;
    granted.Storage(true);
    PolicyUpdateDefaultCapabilities(granted);
    Str storageError;
    utassert(ShellSetStoragePath(Str(storagePath), &storageError));
    utassert(!storageError);
    ExecInit();

    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(nullptr, &error);
    utassert(runtime != nullptr && !error.IsSet());
    utassert(runtime &&
             runtime->Eval(
                 StrL("sessionStorage.setItem('temporary', 'yes');"
                      "localStorage.setItem('theme', 'dark');"
                      "if (sessionStorage.getItem('temporary') !== 'yes' || "
                      "localStorage.getItem('theme') !== 'dark' || "
                      "localStorage.length !== 1) "
                      "throw new Error('storage did not round trip')"),
                 StrL("storage.js"), &error));
    utassert(ExecWaitIdle(5000));

    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "import { Buffer } from 'buffer';\n"
        "import path from 'path';\n"
        "import { URL } from 'url';\n"
        "import os from 'os';\n"
        "if (Buffer.from('hello').toString('hex') !== '68656c6c6f') throw new "
        "Error('buffer');\n"
        "if (path.basename(path.join('a', 'b.txt')) !== 'b.txt') throw new "
        "Error('path');\n"
        "if (new URL('https://example.com/a?q=1').searchParams.get('q') !== "
        "'1') throw new Error('url');\n"
        "if (typeof os.platform() !== 'string' || !os.EOL) throw new "
        "Error('os');\n"
        "export default class Main extends View { render(cx) { return "
        "div().child('standard'); } }\n");
    ViewType* type = runtime ? runtime->LoadSource(StrL("standard-modules.js"),
                                                   source, &error)
                             : nullptr;
    utassert(type != nullptr && !error.IsSet());
    ViewTypeRelease(type);
    if (runtime) runtime->Release();
    ShellErrorClear(&error);
    StrFree(storageError);

    FILE* stored = fopen(storagePath, "rb");
    utassert(stored != nullptr);
    if (stored) fclose(stored);
    remove(storagePath);
    Capabilities denied;
    PolicyUpdateDefaultCapabilities(denied);
}

struct StorageTestSettlement {
    int calls = 0;
    bool ok = false;
};

static void RecordStorageSettlement(StorageTestSettlement* state,
                                    StorageOutcome outcome) {
    state->calls++;
    state->ok = outcome.ok;
}

static void SettleStorageTestWaiters(Vec<StorageWaiter*>* ready,
                                     StorageOutcome outcome) {
    for (int i = 0; i < ready->len; i++) {
        StorageWaiter* waiter = (*ready)[i];
        Func1<StorageOutcome> settle = waiter->settle;
        delete waiter;
        settle.Call(outcome);
    }
    VecReset(*ready);
}

static void ShellStorageWritesRevisionsInOrderAndFlushes() {
    remove("shell_storage_queue_test.json");
    Storage storage(true);
    Str storageError;
    utassert(
        storage.SetPath(StrL("shell_storage_queue_test.json"), &storageError));
    utassert(storage.Set(StrL("revision"), StrL("one"), &storageError));
    StorageWrite first;
    utassert(storage.BeginWrite(&first, &storageError));
    utassert(first.revision == 1 && storage.HasWriteInFlight() &&
             StrContains(first.body, StrL("one")));
    utassert(storage.Set(StrL("revision"), StrL("two"), &storageError));

    StorageTestSettlement settlement;
    StorageWaiter* waiter = nullptr;
    bool immediate = false;
    utassert(storage.Wait(MkFunc1(RecordStorageSettlement, &settlement),
                          &waiter, &immediate, &storageError));
    utassert(waiter != nullptr && !immediate && settlement.calls == 0);
    Vec<StorageWaiter*> ready;
    storage.FinishWrite(first.revision, true, &ready);
    utassert(ready.len == 0 && storage.IsDirty());
    first.Free();

    StorageWrite second;
    utassert(storage.BeginWrite(&second, &storageError));
    utassert(second.revision == 2 && StrContains(second.body, StrL("two")));
    storage.FinishWrite(second.revision, true, &ready);
    SettleStorageTestWaiters(&ready, StorageOutcome{true, {}});
    utassert(settlement.calls == 1 && settlement.ok && !storage.IsDirty());
    second.Free();

    StorageWaiter* already = nullptr;
    immediate = false;
    utassert(storage.Wait(MkFunc1(RecordStorageSettlement, &settlement),
                          &already, &immediate, &storageError));
    utassert(immediate && already == nullptr);

    utassert(storage.Set(StrL("revision"), StrL("three"), &storageError));
    StorageWrite failed;
    utassert(storage.BeginWrite(&failed, &storageError));
    StorageTestSettlement failedSettlement;
    utassert(storage.Wait(MkFunc1(RecordStorageSettlement, &failedSettlement),
                          &waiter, &immediate, &storageError));
    utassert(!immediate && waiter != nullptr);
    storage.FinishWrite(failed.revision, false, &ready);
    SettleStorageTestWaiters(&ready, StorageOutcome{false, StrL("disk full")});
    utassert(failedSettlement.calls == 1 && !failedSettlement.ok);
    failed.Free();
    StorageWrite parked;
    utassert(storage.BeginWrite(&parked, &storageError));
    utassert(parked.revision == 0 && storage.IsDirty());
    StorageTestSettlement retrySettlement;
    utassert(storage.Wait(MkFunc1(RecordStorageSettlement, &retrySettlement),
                          &waiter, &immediate, &storageError));
    utassert(!immediate && waiter != nullptr);
    utassert(storage.BeginWrite(&parked, &storageError));
    utassert(parked.revision == 3);
    storage.FinishWrite(parked.revision, true, &ready);
    SettleStorageTestWaiters(&ready, StorageOutcome{true, {}});
    utassert(retrySettlement.calls == 1 && retrySettlement.ok &&
             !storage.IsDirty());
    parked.Free();
    StrFree(storageError);

    const char* path = "shell_storage_flush_test.json";
    remove(path);
    Capabilities granted;
    granted.Storage(true);
    PolicyUpdateDefaultCapabilities(granted);
    utassert(ShellSetStoragePath(Str(path), &storageError));
    App app;
    Window window;
    window.app = &app;
    VecAppend(app.windows, &window);
    component::Init(&app);
    ExecInit();
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "globalThis.storageFlush = 'pending';\n"
        "export default class Main extends View {\n"
        "  init(props, cx) { cx.spawn(async cx => { "
        "localStorage.setItem('revision', 'final'); await "
        "localStorage.flush(); storageFlush = 'flushed'; cx.notify(); }); }\n"
        "  render(cx) { return div().child(storageFlush); }\n"
        "}\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("storage-flush.js"), source, &error)
                : nullptr;
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    window.frameArena = frame;
    El* root =
        view.IsValid() ? EntityRender(&app, &window, frame, view.id) : nullptr;
    utassert(root != nullptr && !error.IsSet());
    utassert(ShellSettle(runtime, 5000));
    utassert(runtime && runtime->Eval(StrL("if (storageFlush !== 'flushed') "
                                           "throw new Error(storageFlush)"),
                                      StrL("storage-flush-result.js"), &error));
    FILE* file = fopen(path, "rb");
    utassert(file != nullptr);
    if (file) fclose(file);
    EntityDrop(&app, view.id);
    app.windows.len = 0;
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
    remove(path);
    StrFree(storageError);
    Capabilities denied;
    PolicyUpdateDefaultCapabilities(denied);
}

static void ShellProcessRunIsBoundedAndPromiseBased() {
#if GPUI_OS_WINDOWS
    Str args[] = {StrL("/D"), StrL("/C"),
                  StrL("echo out & echo err 1>&2 & exit /B 7")};
    ProcessCancellation cancellation;
    ProcessOutput output;
    Str processError;
    utassert(ProcessRunBounded(StrL("cmd.exe"), args, 3, &cancellation, &output,
                               &processError));
    utassert(!processError && output.code == 7);
    utassert(StrEq(StrTrimAscii(output.out), StrL("out")) &&
             StrEq(StrTrimAscii(output.err), StrL("err")));
    output.Free();
    StrFree(processError);

    App app;
    Window window;
    window.app = &app;
    VecAppend(app.windows, &window);
    component::Init(&app);
    ExecInit();
    Str commands[] = {StrL("cmd.exe")};
    ExecuteGrant execute = ExecuteGrant::Allowed(commands, 1);
    Capabilities granted;
    granted.SetExecute(execute);
    PolicyUpdateDefaultCapabilities(granted);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "import process from 'process';\n"
        "globalThis.processResult = 'pending';\n"
        "export default class Main extends View {\n"
        "  init(props, cx) { cx.spawn(async cx => {\n"
        "    const result = await process.run('cmd.exe', ['/D', '/C', 'echo "
        "jsout & echo jserr 1>&2 & exit /B 9']);\n"
        "    processResult = "
        "`${result.code}|${result.stdout.trim()}|${result.stderr.trim()}`; "
        "cx.notify();\n"
        "  }); }\n"
        "  render(cx) { return div().child(processResult); }\n"
        "}\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("process-run.js"), source, &error)
                : nullptr;
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    window.frameArena = frame;
    El* root =
        view.IsValid() ? EntityRender(&app, &window, frame, view.id) : nullptr;
    utassert(root != nullptr && !error.IsSet());
    utassert(ShellSettle(runtime, 5000));
    utassert(runtime &&
             runtime->Eval(StrL("if (processResult !== '9|jsout|jserr') throw "
                                "new Error(processResult)"),
                           StrL("process-result.js"), &error));
    EntityDrop(&app, view.id);
    app.windows.len = 0;
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
    Capabilities denied;
    PolicyUpdateDefaultCapabilities(denied);
#endif
}

static void ShellFilesystemUsesGrantedHandleRelativePaths() {
#if GPUI_OS_WASM
    FsResult unavailable;
    Str unavailableError;
    utassert(!FsRun(FsOperation::Read, StrL("application"), StrL("note.txt"),
                    {}, false, &unavailable, &unavailableError));
    utassert(StrContains(unavailableError, StrL("unavailable in a browser")));
    unavailable.Free();
    StrFree(unavailableError);
    return;
#endif
    const char* rootName = "shell_fs_test_root";
#if GPUI_OS_WINDOWS
    RemoveDirectoryA(rootName);
#else
    rmdir(rootName);
#endif
    FsResult result;
    Str fsError;
    utassert(FsRun(FsOperation::MakeDirectory, Str(rootName),
                   StrL("nested/child"), {}, true, &result, &fsError));
    utassert(FsRun(FsOperation::Write, Str(rootName),
                   StrL("nested/child/note.txt"), StrL("hello"), false, &result,
                   &fsError));
    utassert(FsRun(FsOperation::Read, Str(rootName),
                   StrL("nested/child/note.txt"), {}, false, &result,
                   &fsError));
    utassert(StrEq(result.bytes, StrL("hello")));
    utassert(FsRun(FsOperation::ReadDirectory, Str(rootName),
                   StrL("nested/child"), {}, false, &result, &fsError));
    utassert(result.entries.len == 1 &&
             StrEq(result.entries[0].name, StrL("note.txt")) &&
             !result.entries[0].isDirectory);
    utassert(FsRun(FsOperation::Exists, Str(rootName),
                   StrL("nested/child/note.txt"), {}, false, &result,
                   &fsError) &&
             result.exists);
    utassert(FsRun(FsOperation::RemoveFile, Str(rootName),
                   StrL("nested/child/note.txt"), {}, false, &result,
                   &fsError));
    utassert(FsRun(FsOperation::RemoveDirectory, Str(rootName),
                   StrL("nested/child"), {}, false, &result, &fsError));
    utassert(FsRun(FsOperation::RemoveDirectory, Str(rootName), StrL("nested"),
                   {}, false, &result, &fsError));
    result.Free();
    StrFree(fsError);
#if GPUI_OS_WINDOWS
    utassert(RemoveDirectoryA(rootName) != 0);
#else
    utassert(rmdir(rootName) == 0);
#endif

    const char* jsRoot = "shell_fs_js_test_root";
#if GPUI_OS_WINDOWS
    RemoveDirectoryA(jsRoot);
#else
    rmdir(jsRoot);
#endif
    Capabilities granted;
    granted.AddReadRoot(Str(jsRoot)).AddWriteRoot(Str(jsRoot));
    PolicyUpdateDefaultCapabilities(granted);
    App app;
    Window window;
    window.app = &app;
    VecAppend(app.windows, &window);
    component::Init(&app);
    ExecInit();
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "import fs from 'fs/promises';\n"
        "globalThis.fsResult = 'pending';\n"
        "export default class Main extends View {\n"
        "  init(props, cx) { cx.spawn(async cx => {\n"
        "    await fs.mkdir('nested', { recursive: true });\n"
        "    await fs.writeFile('nested/note.txt', 'hello');\n"
        "    const text = await fs.readFile('nested/note.txt', 'utf8');\n"
        "    const entries = await fs.readdir('nested', { withFileTypes: true "
        "});\n"
        "    const exists = await fs.exists('nested/note.txt');\n"
        "    fsResult = "
        "`${text}|${entries[0].name}|${entries[0].isDirectory()}|${exists}`;\n"
        "    await fs.unlink('nested/note.txt'); await fs.rmdir('nested'); "
        "cx.notify();\n"
        "  }); }\n"
        "  render(cx) { return div().child(fsResult); }\n"
        "}\n");
    ViewType* type =
        runtime ? runtime->LoadSource(StrL("filesystem.js"), source, &error)
                : nullptr;
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    window.frameArena = frame;
    El* root =
        view.IsValid() ? EntityRender(&app, &window, frame, view.id) : nullptr;
    utassert(root != nullptr && !error.IsSet());
    utassert(ShellSettle(runtime, 10000));
    utassert(runtime &&
             runtime->Eval(StrL("if (fsResult !== 'hello|note.txt|false|true') "
                                "throw new Error(fsResult)"),
                           StrL("filesystem-result.js"), &error));
    EntityDrop(&app, view.id);
    app.windows.len = 0;
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
#if GPUI_OS_WINDOWS
    utassert(RemoveDirectoryA(jsRoot) != 0);
#else
    utassert(rmdir(jsRoot) == 0);
#endif
    Capabilities denied;
    PolicyUpdateDefaultCapabilities(denied);
}

static void ShellAssetsStayInsideTheApplicationRoot() {
#if GPUI_OS_WASM
    // Browser assets are the build's preloaded MEMFS snapshot, not roots a
    // shell script may install or mutate at runtime.
    return;
#endif
    const char* rootName = "shell_asset_test_root";
#if GPUI_OS_WINDOWS
    RemoveDirectoryA(rootName);
#else
    rmdir(rootName);
#endif
    FsResult fs;
    Str error;
    utassert(FsRun(FsOperation::MakeDirectory, Str(rootName), StrL("icons"), {},
                   false, &fs, &error));
    utassert(FsRun(FsOperation::Write, Str(rootName), StrL("icons/check.svg"),
                   StrL("<svg/>"), false, &fs, &error));
    AssetsClear();
    {
        AppAssets assets{Str(rootName)};
        utassert(assets.Install());
        Vec<uint8_t> bytes;
        utassert(AssetsLoad(StrL("icons/check.svg"), &bytes));
        utassert(StrEq(Str((char*)bytes.els, bytes.len), StrL("<svg/>")));
        VecReset(bytes);
        Str relative;
        utassert(!assets.Resolve(StrL("../secret.svg"), &relative, &error));
        StrFree(error);
        error = {};
        Vec<Str> names;
        utassert(assets.List(StrL("icons"), &names, &error));
        utassert(names.len == 1 && StrEq(names[0], StrL("check.svg")));
        for (int i = 0; i < names.len; i++) StrFree(names[i]);
        VecReset(names);
    }
    utassert(AssetsRootCount() == 0);
    utassert(FsRun(FsOperation::RemoveFile, Str(rootName),
                   StrL("icons/check.svg"), {}, false, &fs, &error));
    utassert(FsRun(FsOperation::RemoveDirectory, Str(rootName), StrL("icons"),
                   {}, false, &fs, &error));
    fs.Free();
    StrFree(error);
#if GPUI_OS_WINDOWS
    utassert(RemoveDirectoryA(rootName) != 0);
#else
    utassert(rmdir(rootName) == 0);
#endif
}

static void ShellCryptoAndCompressionMatchStandardRuntime() {
    static const uint8_t expected[32] = {
        0xce, 0x63, 0x5c, 0x4e, 0xab, 0xff, 0x5e, 0x4f, 0x56, 0xdb, 0xa8,
        0xfb, 0x1e, 0x39, 0xca, 0x23, 0x55, 0x30, 0xaa, 0x2b, 0x6b, 0x18,
        0x53, 0x3e, 0xef, 0x1a, 0xf3, 0x86, 0x20, 0x16, 0xc5, 0x77,
    };
    uint8_t digest[32];
    Sha256(StrL("shell"), digest);
    utassert(memcmp(digest, expected, sizeof(expected)) == 0);

    for (int gzip = 0; gzip < 2; gzip++) {
        Str compressed;
        Str inflated;
        Str compressionError;
        utassert(ZlibDeflate(StrL("stored compression round trip"), gzip != 0,
                             &compressed, &compressionError));
        utassert(!compressionError && compressed.len > 0);
        utassert(
            ZlibInflate(compressed, gzip != 0, &inflated, &compressionError));
        utassert(!compressionError &&
                 StrEq(inflated, StrL("stored compression round trip")));
        compressed.s[compressed.len - 1] ^= 1;
        utassert(
            !ZlibInflate(compressed, gzip != 0, &inflated, &compressionError));
        utassert(compressionError);
        StrFree(compressionError);
        StrFree(inflated);
        StrFree(compressed);
    }

    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(nullptr, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "import { Buffer } from 'buffer';\n"
        "import { createHash, randomBytes, randomUUID, webcrypto } from "
        "'crypto';\n"
        "import { deflateSync, inflateSync, gzipSync, gunzipSync } from "
        "'zlib';\n"
        "const input = Buffer.from('shell', 'utf8');\n"
        "if (inflateSync(deflateSync(input)).toString() !== 'shell') throw new "
        "Error('deflate');\n"
        "if (gunzipSync(gzipSync(input)).toString() !== 'shell') throw new "
        "Error('gzip');\n"
        "if (createHash('sha256').update(input).digest('hex') !== "
        "'ce635c4eabff5e4f56dba8fb1e39ca235530aa2b6b18533eef1af3862016c577') "
        "throw new Error('sha256');\n"
        "if (Buffer.from(await webcrypto.subtle.digest('SHA-256', "
        "input)).toString('hex') !== "
        "'ce635c4eabff5e4f56dba8fb1e39ca235530aa2b6b18533eef1af3862016c577') "
        "throw new Error('subtle.digest');\n"
        "if (inflateSync(Buffer.from('7801cb48cdc9c957c8402701680308b1', "
        "'hex')).toString() !== 'hello hello hello hello') throw new "
        "Error('fixed Huffman');\n"
        "const words = "
        "['alpha','bravo','charlie','delta','echo','foxtrot','golf','hotel','"
        "india','juliet','kilo','lima','mike','november','oscar','papa','"
        "quebec','romeo','sierra','tango','uniform','victor','whiskey','xray','"
        "yankee','zulu'];\n"
        "let seed = 1, text = ''; for (let i = 0; i < 1000; i++) { seed = "
        "(Math.imul(seed, 1664525) + 1013904223) >>> 0; text += words[seed % "
        "words.length] + ' '; } text = text.slice(0, 1000);\n"
        "const dynamic = "
        "Buffer.from('"
        "789c6d526d76843008bc0a57635d5c53a3d818eddad3f795011b7dfd978f61981998d2"
        "283468954c6b9252983eb69ca4523770c949e85df820e906c5e9e07914a19c2626cecb"
        "c0f4bde58dbe86b48e72d09ebaaa2550bdbe6bd11ad75977991e52e8a5b90fe836a75e"
        "cb4495e797a211e404e5c20b539a9fc95bb9cce6d9c404fc5178d700798fcf4d1ed201"
        "37fd1a0e6161d27171f5080cfa945cbdaae824da1e43bb119b29a021ebb43ba61ca674"
        "edb8c087bd426d680f5940c5cd320110895bbbd0fa7fcd657a21131c1e8669009fbb37"
        "03a7687c612a4110ec4e716f76d1854ae36cb523a030ec41fb7ed848a357933bea83b8"
        "9982b9b31ced84d82fcb7cddc76a9a5c3d70f7d5345e9785488dba19ae1dcdaad7de01"
        "defa9eced9c2ffadeccf8693c1dd75996d01cef208c806d8ca85fd7b5b9fe00f0fa876"
        "ce', 'hex');\n"
        "if (inflateSync(dynamic).toString() !== text) throw new "
        "Error('dynamic Huffman');\n"
        "const random = randomBytes(32); if (random.length !== 32) throw new "
        "Error('randomBytes');\n"
        "const values = new Uint32Array(4); if "
        "(webcrypto.getRandomValues(values) !== values) throw new "
        "Error('getRandomValues');\n"
        "if "
        "(!/"
        "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/"
        ".test(randomUUID())) throw new Error('randomUUID');\n"
        "export default class Main extends View { render(cx) { return "
        "div().child('standard'); } }\n");
    ViewType* type = runtime ? runtime->LoadSource(StrL("standard-runtime.js"),
                                                   source, &error)
                             : nullptr;
    utassert(type != nullptr && !error.IsSet());
    ViewTypeRelease(type);
    if (runtime) runtime->Release();
    ShellErrorClear(&error);
}

static int gShellFetchCalls = 0;
static int gShellFetchMode = 0;
static Str gShellFetchMethod;
static Str gShellFetchBody;
static Str gShellFetchHeaders;

struct ShellFetchAsyncCapture {
    bool called = false;
    bool ok = false;
    FetchResult result;

    void Free() { result.Free(); }
};

static void ShellFetchAsyncDone(ShellFetchAsyncCapture* capture,
                                FetchAsyncResult landed) {
    capture->called = true;
    capture->ok = landed.ok;
    if (landed.result) {
        capture->result = *landed.result;
        *landed.result = {};
    }
}

static bool ShellFetchFixture(const HttpReq& req, HttpRsp* out) {
    gShellFetchCalls++;
    Str url = req.url;
    StrFree(gShellFetchMethod);
    gShellFetchMethod = StrDup(req.method);
    StrFree(gShellFetchBody);
    gShellFetchBody = StrDup(req.body);
    StrBuilder headers;
    for (int i = 0; i < req.nHeaders; i++) {
        headers.Append(req.headers[i].name);
        headers.Append(StrL(":"));
        headers.Append(req.headers[i].value);
        headers.Append(StrL(";"));
    }
    StrFree(gShellFetchHeaders);
    gShellFetchHeaders = headers.TakeStr();

    if (gShellFetchMode == 1 &&
        StrEq(url, StrL("https://api.example.test/start"))) {
        out->status = 302;
        out->redirectUrl = StrDup(StrL("https://cdn.example.test/result"));
        return true;
    }
    if (gShellFetchMode == 2 &&
        StrEq(url, StrL("https://api.example.test/start"))) {
        out->status = 302;
        out->redirectUrl = StrDup(StrL("http://api.example.test/result"));
        return true;
    }
    if (gShellFetchMode == 3 &&
        StrEq(url, StrL("https://api.example.test/start"))) {
        // A 303 turns any method but HEAD into a bodyless GET, which is what
        // makes this redirect same-origin-safe to follow.
        out->status = 303;
        out->redirectUrl = StrDup(StrL("https://api.example.test/result"));
        return true;
    }
    if (StrEq(url, StrL("https://api.example.test/result"))) {
        out->status = 200;
        const char* body = "after";
        memcpy(VecAppendBlanks(out->body, 5), body, 5);
        return true;
    }
    if (StrEq(url, StrL("https://cdn.example.test/result"))) {
        out->status = 201;
        const char* body = "redirected";
        memcpy(VecAppendBlanks(out->body, 10), body, 10);
        return true;
    }
    if (StrEq(url, StrL("https://api.example.test/data"))) {
        out->status = 200;
        const char* body = "{\"answer\":42}";
        memcpy(VecAppendBlanks(out->body, 13), body, 13);
        return true;
    }
    return false;
}

// fetch.rs: only_location_redirect_statuses_are_followed.
static void ShellFetchFollowsOnlyLocationRedirectStatuses() {
    for (int status : {301, 302, 303, 307, 308}) {
        utassert(FetchFollowsLocation(status));
    }
    for (int status : {300, 304, 305, 200, 400}) {
        utassert(!FetchFollowsLocation(status));
    }
}

// fetch.rs: redirect_statuses_apply_fetch_method_and_body_rewrites.
static void ShellFetchRedirectsRewriteMethodAndBody() {
    struct Case {
        int status;
        const char* initial;
        const char* expected;
    };
    static const Case cases[] = {
        {301, "POST", "GET"},  {302, "POST", "GET"},  {303, "PUT", "GET"},
        {307, "POST", "POST"}, {308, "POST", "POST"},
    };
    for (const Case& c : cases) {
        Str method = StrDup(Str(c.initial));
        Str body = StrDup(StrL("side effect"));
        Vec<FetchHeader> headers;
        FetchHeader type;
        type.name = StrDup(StrL("Content-Type"));
        type.value = StrDup(StrL("application/json"));
        VecAppend(headers, type);

        FetchRewriteRedirect(c.status, &method, &headers, &body);

        utassert(StrEq(method, Str(c.expected)));
        if (StrEq(method, StrL("GET"))) {
            utassert(body.len == 0 && headers.len == 0);
        } else {
            utassert(body.len > 0 && headers.len == 1);
        }
        StrFree(method);
        StrFree(body);
        for (int i = 0; i < headers.len; i++) {
            StrFree(headers[i].name);
            StrFree(headers[i].value);
        }
        VecReset(headers);
    }
}

// fetch.rs: origin_comparison_includes_scheme_host_and_effective_port.
static void ShellFetchOriginComparesSchemeHostAndEffectivePort() {
    Str origin = StrL("https://api.example.test:443/v1/quote");
    utassert(FetchSameOrigin(origin, StrL("https://api.example.test/next")));
    utassert(!FetchSameOrigin(origin, StrL("http://api.example.test/next")));
    utassert(!FetchSameOrigin(origin, StrL("https://other.example.test/next")));
    utassert(
        !FetchSameOrigin(origin, StrL("https://api.example.test:8443/next")));
}

// fetch.rs: the four authorize_redirect refusals.
static void ShellFetchRedirectsKeepCredentialsOnTheirOrigin() {
    Capabilities two;
    two.AddNetworkHost(StrL("api.example.test"))
        .AddNetworkHost(StrL("login.example.test"))
        .AddNetworkHost(StrL("cdn.example.test"));

    // authorization_never_follows_a_cross_origin_redirect
    Vec<FetchHeader> bearer;
    FetchHeader authorization;
    authorization.name = StrDup(StrL("Authorization"));
    authorization.value = StrDup(StrL("Bearer secret"));
    VecAppend(bearer, authorization);
    Str error = {};
    utassert(!FetchAuthorizeRedirect(
        two, StrL("GET"), StrL("https://api.example.test/v1/quote"),
        StrL("https://login.example.test/continue"), bearer, &error));
    utassert(StrContains(error, StrL("cross-origin redirect")));
    StrFree(error);
    error = {};
    // The same header does follow a redirect that stays on its origin.
    utassert(FetchAuthorizeRedirect(
        two, StrL("GET"), StrL("https://api.example.test/v1/quote"),
        StrL("https://api.example.test/v2/quote"), bearer, &error));
    StrFree(error);
    error = {};
    StrFree(bearer[0].name);
    StrFree(bearer[0].value);
    VecReset(bearer);

    // https_redirects_never_downgrade_to_plain_http
    Vec<FetchHeader> none;
    utassert(!FetchAuthorizeRedirect(
        two, StrL("GET"), StrL("https://api.example.test/start"),
        StrL("http://api.example.test/continue"), none, &error));
    utassert(StrContains(error, StrL("HTTPS downgrade")));
    StrFree(error);
    error = {};

    // a_post_is_never_replayed_across_origins
    utassert(!FetchAuthorizeRedirect(
        two, StrL("POST"), StrL("https://api.example.test/token"),
        StrL("https://login.example.test/token"), none, &error));
    utassert(StrContains(error, StrL("POST")) &&
             StrContains(error, StrL("cross-origin")));
    StrFree(error);
    error = {};

    // caller_supplied_headers_never_follow_a_cross_origin_redirect
    Vec<FetchHeader> custom;
    FetchHeader key;
    key.name = StrDup(StrL("X-Api-Key"));
    key.value = StrDup(StrL("secret"));
    VecAppend(custom, key);
    utassert(!FetchAuthorizeRedirect(
        two, StrL("GET"), StrL("https://api.example.test/data"),
        StrL("https://cdn.example.test/data"), custom, &error));
    utassert(StrContains(error, StrL("request headers")));
    StrFree(error);
    error = {};
    StrFree(custom[0].name);
    StrFree(custom[0].value);
    VecReset(custom);

    // a_redirect_is_checked_against_http_method_and_path_rules
    HttpRequestGrant allowed(StrL("api.example.test"));
    allowed.AddMethod(StrL("GET")).AddPath(StrL("/allowed"));
    Capabilities scoped;
    scoped.AddHttpRequest(allowed);
    utassert(!FetchAuthorizeRedirect(
        scoped, StrL("GET"), StrL("https://api.example.test/allowed"),
        StrL("https://api.example.test/admin"), none, &error));
    utassert(StrContains(error, StrL("HTTP request")));
    StrFree(error);
}

// fetch.rs: an_http_rule_cannot_be_bypassed_with_an_unlisted_method_or_path,
// and an_http_rule_is_bound_to_scheme_effective_port_and_path_segments.
static void ShellFetchGrantsBindMethodSchemePortAndPath() {
    HttpRequestGrant readOnly(StrL("api.example.test"));
    readOnly.AddMethod(StrL("GET")).AddPath(StrL("/v1/read/profile"));
    Capabilities exact;
    exact.AddHttpRequest(readOnly);
    utassert(FetchAuthorize(StrL("https://api.example.test/v1/read/profile"),
                            StrL("GET"), exact));
    utassert(!FetchAuthorize(StrL("https://api.example.test/v1/read/profile"),
                             StrL("POST"), exact));
    utassert(!FetchAuthorize(StrL("https://api.example.test/v1/write/item"),
                             StrL("POST"), exact));

    HttpRequestGrant prefixed(StrL("api.example.test"));
    prefixed.AddMethod(StrL("GET")).AddPathPrefix(StrL("/v1/account"));
    Capabilities scoped;
    scoped.AddHttpRequest(prefixed);
    utassert(FetchAuthorize(StrL("https://api.example.test/v1/account/profile"),
                            StrL("GET"), scoped));
    utassert(!FetchAuthorize(StrL("http://api.example.test/v1/account/profile"),
                             StrL("GET"), scoped));
    utassert(!FetchAuthorize(
        StrL("https://api.example.test:8443/v1/account/profile"), StrL("GET"),
        scoped));
    utassert(
        !FetchAuthorize(StrL("https://api.example.test/v1/accounts-delete"),
                        StrL("GET"), scoped));
}

static void ShellFetchProhibitsTheHeadersTheClientOwns() {
    for (const char* name : {"Host", "content-length", "Connection", "Expect",
                             "Proxy-Authenticate", "proxy-authorization", "TE",
                             "Trailer", "Transfer-Encoding", "upgrade"}) {
        utassert(FetchHeaderIsProhibited(Str(name)));
    }
    for (const char* name :
         {"Authorization", "Content-Type", "Accept", "X-Api-Key"}) {
        utassert(!FetchHeaderIsProhibited(Str(name)));
    }
}

static void ShellFetchChecksEveryGetTargetBeforeContact() {
    HttpRequestGrant exact(StrL("api.example.test"));
    exact.AddMethod(StrL("GET")).AddPath(StrL("/v1/quote"));
    Capabilities scoped;
    scoped.AddHttpRequest(exact);
    Str fetchError;
    utassert(
        FetchAuthorize(StrL("https://api.example.test/v1/quote?currency=usd"),
                       StrL("GET"), scoped, &fetchError));
    utassert(!fetchError);
    utassert(!FetchAuthorize(StrL("https://api.example.test/v1/private"),
                             StrL("GET"), scoped, &fetchError));
    utassert(fetchError);
    StrFree(fetchError);

    Capabilities both;
    both.AddNetworkHost(StrL("api.example.test"))
        .AddNetworkHost(StrL("cdn.example.test"));
    FetchSetHttpSendForTests(ShellFetchFixture);
    gShellFetchCalls = 0;
    gShellFetchMode = 1;
    FetchRequest request;
    request.url = StrDup(StrL("https://api.example.test/start"));
    FetchResult result;
    utassert(FetchSend(request, both, &result));
    utassert(!result.error && result.status == 201 &&
             StrEq(result.url, StrL("https://cdn.example.test/result")) &&
             StrEq(result.body, StrL("redirected")));
    utassert(gShellFetchCalls == 2);
    result.Free();

    // The callback walk is the path wasm's fetch() uses. Pin that it remains
    // asynchronous and applies the same authorization at every redirect.
    ExecInit();
    gShellFetchCalls = 0;
    ShellFetchAsyncCapture asynchronous;
    utassert(FetchSendAsync(request, both,
                            MkFunc1(ShellFetchAsyncDone, &asynchronous)));
    utassert(!asynchronous.called);
    utassert(ExecWaitIdle(5000));
    utassert(asynchronous.called && asynchronous.ok &&
             asynchronous.result.status == 201 &&
             StrEq(asynchronous.result.url,
                   StrL("https://cdn.example.test/result")) &&
             StrEq(asynchronous.result.body, StrL("redirected")) &&
             gShellFetchCalls == 2);
    asynchronous.Free();

    Capabilities initialOnly;
    initialOnly.AddNetworkHost(StrL("api.example.test"));
    gShellFetchCalls = 0;
    gShellFetchMode = 1;
    utassert(!FetchSend(request, initialOnly, &result));
    utassert(result.error && gShellFetchCalls == 1);
    result.Free();

    gShellFetchCalls = 0;
    gShellFetchMode = 2;
    utassert(!FetchSend(request, initialOnly, &result));
    utassert(result.error &&
             StrContains(result.error, StrL("HTTPS downgrade")) &&
             gShellFetchCalls == 1);
    result.Free();

    // A POST reaches the wire as a POST, carrying its body and its headers.
    gShellFetchCalls = 0;
    gShellFetchMode = 0;
    FetchRequest posted;
    posted.url = StrDup(StrL("https://api.example.test/data"));
    posted.method = StrDup(StrL("POST"));
    posted.body = StrDup(StrL("grant_type=client_credentials"));
    FetchHeader type;
    type.name = StrDup(StrL("Content-Type"));
    type.value = StrDup(StrL("application/x-www-form-urlencoded"));
    VecAppend(posted.headers, type);
    utassert(FetchSend(posted, initialOnly, &result));
    utassert(!result.error && result.status == 200 &&
             StrEq(gShellFetchMethod, StrL("POST")) &&
             StrEq(gShellFetchBody, StrL("grant_type=client_credentials")) &&
             StrContains(gShellFetchHeaders, StrL("Content-Type:application/"
                                                  "x-www-form-urlencoded;")));
    result.Free();

    // A 303 answering that POST continues as a GET with no body, and the
    // header that described the body does not follow it.
    gShellFetchCalls = 0;
    gShellFetchMode = 3;
    StrFree(posted.url);
    posted.url = StrDup(StrL("https://api.example.test/start"));
    utassert(FetchSend(posted, initialOnly, &result));
    utassert(!result.error && result.status == 200 &&
             StrEq(result.body, StrL("after")) && gShellFetchCalls == 2 &&
             StrEq(gShellFetchMethod, StrL("GET")) &&
             gShellFetchBody.len == 0 && gShellFetchHeaders.len == 0);
    result.Free();
    posted.Free();
    request.Free();

    App app;
    Window window;
    window.app = &app;
    VecAppend(app.windows, &window);
    component::Init(&app);
    PolicyUpdateDefaultCapabilities(initialOnly);
    gShellFetchMode = 0;
    gShellFetchCalls = 0;
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    Str source = StrL(
        "import { View, div } from 'gpui';\n"
        "globalThis.fetchResult = 'pending';\n"
        // 9a64d865 makes `method` a token check rather than a two-name list:
        // what may be sent where is the capability policy's decision, which
        // already takes the method.
        "for (const bad of ['', 'a method', '\"GET\"', 'GET/1']) {\n"
        "  let refused = false;\n"
        "  try { fetch('https://api.example.test/data', { method: bad }); } "
        "catch (error) { refused = error.message.includes('is not an HTTP "
        "method'); }\n"
        "  if (!refused) throw new Error(`\\`${bad}\\` was not refused as a "
        "non-method`);\n"
        "}\n"
        "let ownedRefused = false;\n"
        "try { fetch('https://api.example.test/data', { headers: { "
        "'Content-Length': '3' } }); } catch (error) { ownedRefused = "
        "error.message.includes('may not set'); }\n"
        "if (!ownedRefused) throw new Error('a client-owned header was not "
        "refused');\n"
        "let shapeRefused = false;\n"
        "try { fetch('https://api.example.test/data', { body: 42 }); } catch "
        "(error) { shapeRefused = error.message.includes('string or "
        "Uint8Array'); }\n"
        "if (!shapeRefused) throw new Error('a body that is neither string nor "
        "bytes was not refused');\n"
        "let unknownRefused = false;\n"
        "try { fetch('https://api.example.test/data', { mode: 'cors' }); } "
        "catch (error) { unknownRefused = error.message.includes('unknown "
        "option'); }\n"
        "if (!unknownRefused) throw new Error('an unknown option was not "
        "refused');\n"
        "export default class Main extends View {\n"
        "  init(props, cx) { cx.spawn(async cx => {\n"
        "    const posted = await fetch('https://api.example.test/data', { "
        "method: 'post', headers: { 'X-Api-Key': 'k' }, body: 'hello' });\n"
        "    const response = await fetch('https://api.example.test/data');\n"
        "    const text = response.text(), json = response.json();\n"
        "    fetchResult = "
        "`${posted.status}|${response.status}|${response.ok}|${response.url}|${"
        "text instanceof Promise}|${await text}|${(await json).answer}`; "
        "cx.notify();\n"
        "  }); }\n"
        "  render(cx) { return div().child(fetchResult); }\n"
        "}\n");
    ViewType* type2 =
        runtime ? runtime->LoadSource(StrL("fetch.js"), source, &error)
                : nullptr;
    Entity<ScriptView> view =
        type2 ? ScriptView::New(&app, runtime, type2) : Entity<ScriptView>{};
    ViewTypeRelease(type2);
    Arena* frame = ArenaNew();
    window.frameArena = frame;
    El* root =
        view.IsValid() ? EntityRender(&app, &window, frame, view.id) : nullptr;
    utassert(root != nullptr && !error.IsSet());
    utassert(ShellSettle(runtime, 5000));
    utassert(
        runtime &&
        runtime->Eval(
            StrL("if (fetchResult !== "
                 "'200|200|true|https://api.example.test/"
                 "data|true|{\"answer\":42}|42') throw new Error(fetchResult)"),
            StrL("fetch-result.js"), &error));
    // The lowercase `post` reached the policy and the wire upper-cased.
    utassert(StrEq(gShellFetchMethod, StrL("GET")));
    utassert(gShellFetchCalls == 2);
    EntityDrop(&app, view.id);
    app.windows.len = 0;
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
    FetchSetHttpSendForTests(nullptr);
    StrFree(gShellFetchMethod);
    gShellFetchMethod = {};
    StrFree(gShellFetchBody);
    gShellFetchBody = {};
    StrFree(gShellFetchHeaders);
    gShellFetchHeaders = {};
    Capabilities denied;
    PolicyUpdateDefaultCapabilities(denied);
}

static void ShellAccessibilityRolesMirrorUpstream() {
    utassert(AccessibilityRoleNameCount() == 181);
    utassert(AccessibilityRoleFromName(StrL("list_box_option")) ==
             AccessibilityRole::ListBoxOption);
    utassert(AccessibilityRoleFromName(StrL("combo_box")) ==
             AccessibilityRole::ComboBox);
    utassert(AccessibilityRoleFromName(StrL("check_box")) ==
             AccessibilityRole::CheckBox);
    utassert(AccessibilityRoleFromName(StrL("doc_acknowledgements")) ==
             AccessibilityRole::DocAcknowledgements);
    utassert(AccessibilityRoleFromName(StrL("terminal")) ==
             AccessibilityRole::Terminal);
    utassert(AccessibilityRoleFromName(StrL("generic_container")) ==
             AccessibilityRole::None);
    utassert(AccessibilityRoleFromName(StrL("listbox")) ==
             AccessibilityRole::None);
    utassert(AccessibilityRoleFromName(StrL("Button")) ==
             AccessibilityRole::None);
}

static ShellRuntime* gDockRuntime = nullptr;
static ViewType* gDockViewType = nullptr;
static bool gDockBuildFails = false;
static Str gDockRestored = {};
static bool gDockChromeSawLayout = false;

static Entity<ScriptView> BuildDockProbe(Window*, App* app, void*) {
    return gDockBuildFails ? Entity<ScriptView>{}
                           : ScriptView::New(app, gDockRuntime, gDockViewType);
}

static bool SerializeDockProbe(Entity<ScriptView>, App*, void*,
                               StrBuilder* out) {
    out->Append(StrL("{\"filter\":\"unread\",\"sort\":2}"));
    return true;
}

static void DeserializeDockProbe(Entity<ScriptView>, Str json, Window*, App*,
                                 void*) {
    StrFree(gDockRestored);
    gDockRestored = StrDup(json);
}

static El* RenderDockProbeChrome(Ctx* cx, void*, const DockCtx*, El* content) {
    gDockChromeSawLayout =
        ScopeHasCurrent() && ScopeCurrentPhase() == ScopePhase::Layout;
    return Div(cx->a)->Child(content);
}

static const PanelStateNode* FindPanelState(const DockAreaState& state,
                                            Str name) {
    for (int i = 0; i < state.nodes.len; i++) {
        if (StrEq(state.nodes[i].panelName, name)) return &state.nodes[i];
    }
    return nullptr;
}

static void ShellDockPanelsPersistAndChromeRunsInLayoutScope() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    gDockRuntime = ShellRuntime::New(&app, &error);
    gDockViewType =
        gDockRuntime ? gDockRuntime->LoadSource(
                           StrL("dock-panel.js"),
                           StrL("import { View, div } from 'gpui'; export "
                                "default class Panel extends View { render() { "
                                "return div().child('panel'); } }"),
                           &error)
                     : nullptr;
    utassert(gDockRuntime && gDockViewType && !error.IsSet());

    Str name = ShellPanelName(StrL("mail"), StrL("inbox"));
    utassert(StrEq(name, StrL("shell:mail/inbox")));
    utassert(name.s == ShellPanelName(StrL("mail"), StrL("inbox")).s);
    utassert(!StrEq(name, ShellPanelName(StrL("chat"), StrL("inbox"))));

    ShellPanelScript script;
    script.build = BuildDockProbe;
    script.serialize = SerializeDockProbe;
    script.deserialize = DeserializeDockProbe;
    ShellRegisterPanel(&app, StrL("mail"), StrL("inbox"), script);

    Entity<DockState> area = EntityNewState<DockState>(&app);
    DockState* state = area.Get(&app);
    Entity<ScriptView> view =
        ScriptView::New(&app, gDockRuntime, gDockViewType);
    int panel =
        DockAddPanelDef(state, ScriptPanelNew(&app, name, view, &script));
    state->center = DockNewTabs(state);
    DockTabsAdd(state, state->center, panel);
    DockAreaState saved;
    DockDump(state, &saved);
    const PanelStateNode* leaf = FindPanelState(saved, name);
    utassert(leaf && leaf->infoIsJson &&
             StrEq(leaf->info, StrL("{\"filter\":\"unread\",\"sort\":2}")));

    StrBuilder written;
    DockAreaStateWrite(&saved, &written);
    Str layout = written.TakeStr();
    utassert(StrContains(layout,
                         StrL("\"panel\":{\"filter\":\"unread\",\"sort\":2}")));

    Arena* persisted = ArenaNew();
    DockAreaState parsed;
    utassert(DockAreaStateParse(persisted, layout, &parsed));
    Entity<DockState> restoredArea = EntityNewState<DockState>(&app);
    DockState* restored = restoredArea.Get(&app);
    StrFree(gDockRestored);
    utassert(DockLoad(restored, &parsed, persisted, nullptr, &app, &window,
                      restoredArea));
    utassert(StrEq(gDockRestored, StrL("{\"filter\":\"unread\",\"sort\":2}")));
    utassert(restored->panels.len == 1 && restored->panels[0].closable &&
             restored->panels[0].canZoom && restored->panels[0].visible);

    gDockBuildFails = true;
    ShellRegisterPanel(&app, StrL("mail"), StrL("inbox"), script);
    Entity<DockState> failedArea = EntityNewState<DockState>(&app);
    DockState* failed = failedArea.Get(&app);
    utassert(DockLoad(failed, &parsed, persisted, nullptr, &app, &window,
                      failedArea));
    DockAreaState failedSaved;
    DockDump(failed, &failedSaved);
    const PanelStateNode* failedLeaf = FindPanelState(failedSaved, name);
    utassert(
        failedLeaf && failedLeaf->infoIsJson &&
        StrEq(failedLeaf->info, StrL("{\"filter\":\"unread\",\"sort\":2}")));
    gDockBuildFails = false;

    Arena* frame = ArenaNew();
    Ctx cx = {&app, &window, frame, {}};
    ShellDockChrome chrome;
    chrome.dock = RenderDockProbeChrome;
    ScriptDockSkin skin(chrome);
    DockCtx dock;
    dock.cx = &cx;
    dock.placement = DockPlacement::Left;
    dock.size = 240;
    dock.open = true;
    dock.collapsible = true;
    gDockChromeSawLayout = false;
    El* wrapped = skin.Renderer()
                      ->dock(&cx, skin.Renderer()->data, &dock, Div(frame));
    utassert(wrapped && gDockChromeSawLayout);
    StrBuilder dockData;
    ShellDockData(&dock, &dockData);
    Str dockJson = dockData.TakeStr();
    utassert(StrEq(dockJson,
                   StrL("{\"placement\":\"left\",\"size\":240,\"open\":true,"
                        "\"collapsible\":true}")));

    StrFree(dockJson);
    ArenaDelete(frame);
    StrFree(layout);
    StrFree(gDockRestored);
    gDockRestored = {};
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ViewTypeRelease(gDockViewType);
    gDockViewType = nullptr;
    gDockRuntime->Release();
    gDockRuntime = nullptr;
    ShellErrorClear(&error);
    ArenaDelete(persisted);
}

static bool ShellFixtureFs(FsOperation operation, Str root, Str relative,
                           Str input, bool recursive) {
    FsResult result;
    Str error;
    bool ok =
        FsRun(operation, root, relative, input, recursive, &result, &error);
    result.Free();
    StrFree(error);
    return ok;
}

static bool AuthorizePlugin(const PluginManifest*, void* data) {
    return *(bool*)data;
}

static void ShellPluginManifestsDiscoverAuthorizeAndUnload() {
    const Str valid = StrL(
        "{"
        "\"id\":\"com.example.inbox\","
        "\"name\":\"Inbox\","
        "\"version\":\"1.2.0\","
        "\"shell-version\":\"0.1.0\","
        "\"entry\":\"main.js\","
        "\"capabilities\":{"
        "\"fs\":{\"read\":[\"${pluginDir}\",\"${dataDir}\"],\"write\":[\"${"
        "dataDir}\"],\"execute\":[\"git\"]},"
        "\"network\":{\"hosts\":[\"api.example.com\"],\"http\":[{\"host\":"
        "\"readonly.example.com\",\"methods\":[\"GET\"],\"paths\":[\"/v1/"
        "account\"],\"path_prefixes\":[\"/v1/quotes/\"]}]},"
        "\"storage\":true,\"clipboard\":{\"write\":true},\"process\":{\"exit\":"
        "true}"
        "}}");
    ShellError error = {};
    PluginManifest manifest;
    utassert(PluginManifestParse(valid, &manifest, &error));
    utassert(!error.IsSet() && StrEq(manifest.id, StrL("com.example.inbox")) &&
             StrEq(manifest.name, StrL("Inbox")) &&
             StrEq(manifest.version, StrL("1.2.0")) &&
             StrEq(manifest.shellVersion, StrL("0.1.0")) &&
             StrEq(manifest.entry, StrL("main.js")));
    Capabilities granted = manifest
                               .Grant(StrL("plugin-root"), StrL("data-root"));
    utassert(granted.HasReadAccess() && granted.HasWriteAccess() &&
             granted.HasStorage() && granted.MayRun(StrL("git")) &&
             granted.MayReach(StrL("API.EXAMPLE.COM")) &&
             granted
                 .MayRequest(StrL("https"), StrL("readonly.example.com"), 443,
                             false, StrL("GET"), StrL("/v1/quotes/MSFT")) &&
             granted.IsClipboardWritable() && granted.MayExit());

    PluginManifest omitted;
    utassert(PluginManifestParse(StrL("{\"id\":\"com.example.empty\",\"name\":"
                                      "\"Empty\",\"entry\":\"main.js\"}"),
                                 &omitted, &error));
    Capabilities defaultGrant = omitted.Grant(StrL("plugin"), StrL("data"));
    utassert(defaultGrant.HasStorage() && !defaultGrant.HasReadAccess() &&
             !defaultGrant.MayExit());

    PluginManifest badField;
    utassert(!PluginManifestParse(
        StrL("{\"id\":\"com.example.bad\",\"name\":\"Bad\",\"entry\":\"main."
             "js\",\"capabilites\":{}}"),
        &badField, &error));
    utassert(StrContains(error.message, StrL("unknown field")));
    ShellErrorClear(&error);
    PluginManifest badId;
    utassert(!PluginManifestParse(
        StrL("{\"id\":\"../bad\",\"name\":\"Bad\",\"entry\":\"main.js\"}"),
        &badId, &error));
    utassert(StrContains(error.message, StrL("invalid `id`")));
    ShellErrorClear(&error);
    PluginManifest badEntry;
    utassert(!PluginManifestParse(StrL("{\"id\":\"com.example.bad\",\"name\":"
                                       "\"Bad\",\"entry\":\"../main.js\"}"),
                                  &badEntry, &error));
    utassert(StrContains(error.message, StrL("invalid `entry`")));
    ShellErrorClear(&error);
    PluginManifest badPlaceholder;
    utassert(!PluginManifestParse(
        StrL("{\"id\":\"com.example.bad\",\"name\":\"Bad\",\"entry\":\"main."
             "js\",\"capabilities\":{\"fs\":{\"read\":[\"${otherDir}\"]}}}"),
        &badPlaceholder, &error));
    utassert(StrContains(error.message, StrL("unknown placeholder")));
    ShellErrorClear(&error);
    PluginManifest future;
    utassert(!PluginManifestParse(
        StrL("{\"id\":\"com.example.future\",\"name\":\"Future\",\"shell-"
             "version\":\"0.7.0\",\"entry\":\"main.js\"}"),
        &future, &error));
    utassert(StrContains(error.message, StrL("not compatible")));
    ShellErrorClear(&error);

    PluginManifest older;
    utassert(PluginManifestParse(
        StrL("{\"id\":\"com.example.older\",\"name\":\"Older\",\"shell-"
             "version\":\"0.0.0\",\"entry\":\"main.js\"}"),
        &older, &error));
    utassert(HostIsReservedSpecifier(StrL("gpui-kit")) &&
             HostIsReservedSpecifier(StrL("gpui")));

#if GPUI_OS_WASM
    // Manifest parsing and grants are platform-independent and checked above.
    // Discovery and installation require hosted filesystem roots.
    return;
#endif

    const Str container = StrL("shell_plugin_container_test");
    const Str pluginDir = StrL("shell_plugin_container_test/mail");
    const Str brokenDir = StrL("shell_plugin_container_test/broken");
    const Str dataDir = StrL("shell_plugin_data_test");
    remove("shell_plugin_container_test/mail/main.js");
    remove("shell_plugin_container_test/mail/gpui-shell.json");
    remove("shell_plugin_container_test/broken/gpui-shell.json");
#if GPUI_OS_WINDOWS
    RemoveDirectoryA("shell_plugin_container_test/mail");
    RemoveDirectoryA("shell_plugin_container_test/broken");
    RemoveDirectoryA("shell_plugin_container_test");
#else
    rmdir("shell_plugin_container_test/mail");
    rmdir("shell_plugin_container_test/broken");
    rmdir("shell_plugin_container_test");
#endif
    utassert(ShellFixtureFs(FsOperation::MakeDirectory, StrL("."), pluginDir,
                            {}, true));
    utassert(ShellFixtureFs(FsOperation::MakeDirectory, StrL("."), brokenDir,
                            {}, true));
    utassert(ShellFixtureFs(FsOperation::MakeDirectory, StrL("."), dataDir, {},
                            true));
    const Str fixtureManifest = StrL(
        "{\"id\":\"com.example.plugin\",\"name\":\"Plugin\",\"version\":\"1.0."
        "0\",\"shell-version\":\"0.1.0\",\"entry\":\"main.js\","
        "\"capabilities\":{\"fs\":{\"read\":[\"${pluginDir}\"]},\"storage\":"
        "true}}");
    utassert(ShellFixtureFs(FsOperation::Write, pluginDir,
                            Str(kShellManifestFile), fixtureManifest));
    utassert(ShellFixtureFs(
        FsOperation::Write, pluginDir, StrL("main.js"),
        StrL("import { View, div } from 'gpui'; globalThis.pluginExecuted = "
             "true; export default class Plugin extends View { render() { "
             "return div().child('plugin'); } }")));
    utassert(ShellFixtureFs(FsOperation::Write, brokenDir,
                            Str(kShellManifestFile), StrL("{broken")));

    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    PluginManager manager(container);
    manager.DataHome(dataDir);
    const Vec<PluginDiscovery>& discovered = manager.Discover();
    utassert(discovered.len == 2);
    int good = 0, broken = 0;
    for (int i = 0; i < discovered.len; i++) {
        good += discovered[i].manifest != nullptr;
        broken += discovered[i].error.s != nullptr;
    }
    utassert(good == 1 && broken == 1);
    bool approved = false;
    utassert(!manager.Load(runtime, StrL("com.example.plugin"), AuthorizePlugin,
                           &approved, &window, &app, &error));
    utassert(StrContains(error.message, StrL("not approved")) &&
             runtime->LiveTasks() == 0);
    ShellErrorClear(&error);
    approved = true;
    utassert(manager.Load(runtime, StrL("com.example.plugin"), AuthorizePlugin,
                          &approved, &window, &app, &error));
    const Plugin* loaded = manager.Loaded(StrL("com.example.plugin"));
    utassert(loaded && loaded->view.IsValid() && loaded->policy &&
             PolicyCapabilities(loaded->policy).HasReadAccess() &&
             PolicyCapabilities(loaded->policy).HasStorage() &&
             StrContains(loaded->dataDirectory, StrL("gpui-shell")));
    utassert(manager.Unload(StrL("com.example.plugin"), &app));
    utassert(!manager.Loaded(StrL("com.example.plugin")) &&
             !manager.Unload(StrL("com.example.plugin"), &app));

    runtime->Release();
    AppGlobalClear(&app);
    utassert(ShellFixtureFs(FsOperation::RemoveFile, pluginDir,
                            Str(kShellManifestFile)));
    utassert(
        ShellFixtureFs(FsOperation::RemoveFile, pluginDir, StrL("main.js")));
    utassert(ShellFixtureFs(FsOperation::RemoveFile, brokenDir,
                            Str(kShellManifestFile)));
    utassert(
        ShellFixtureFs(FsOperation::RemoveDirectory, container, StrL("mail")));
    utassert(ShellFixtureFs(FsOperation::RemoveDirectory, container,
                            StrL("broken")));
#if GPUI_OS_WINDOWS
    utassert(RemoveDirectoryA(container.s) != 0);
#else
    utassert(rmdir(container.s) == 0);
#endif
    ShellFixtureFs(FsOperation::RemoveDirectory, dataDir,
                   StrL("gpui-shell/plugins/com.example.plugin"));
    ShellFixtureFs(FsOperation::RemoveDirectory, dataDir,
                   StrL("gpui-shell/plugins"));
    ShellFixtureFs(FsOperation::RemoveDirectory, dataDir, StrL("gpui-shell"));
    ShellFixtureFs(FsOperation::RemoveDirectory, StrL("."), dataDir);
    ShellErrorClear(&error);
}

// ── templates ────────────────────────────────────────────────────────────
// Ported from crates/shell/src/tests/template.rs. `template(build)` is a
// description recorded once and filled per call: the body runs a single time
// with a sentinel in each parameter position, and every call after that grafts
// the structure and writes its arguments into the slots.

// Renders one source and answers its debug tree, or an empty Str plus the
// runtime error the render reported.
static Str ShellTemplateTree(Arena* into, Str source, Str* failure) {
    App app;
    Window window;
    window.app = &app;
    ShellError error = {};
    Str tree = {};
    if (failure) *failure = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    if (runtime) {
        ViewType* type =
            runtime->LoadSource(StrL("template-test.js"), source, &error);
        ViewObject* object =
            type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
                 : nullptr;
        RenderSnapshot* snapshot =
            object ? runtime->BuildSnapshot(object, &window, &app, {}, nullptr,
                                            &error)
                   : nullptr;
        if (snapshot) {
            tree = snapshot->DebugTree(into);
            delete snapshot;
        } else if (failure) {
            *failure = StrDup(into, error.message);
        }
        ViewObjectRelease(object);
        ViewTypeRelease(type);
        runtime->Release();
    }
    ShellErrorClear(&error);
    AppGlobalClear(&app);
    return tree;
}

static bool ShellTemplateRefuses(Str source, Str expected) {
    Arena* arena = ArenaNew();
    Str failure = {};
    Str tree = ShellTemplateTree(arena, source, &failure);
    bool refused = tree.len == 0 && StrFind(failure, expected) >= 0;
    ArenaDelete(arena);
    return refused;
}

static void ShellTemplatesRecordOnceAndFillPerCall() {
    // A filled template must be indistinguishable from the chain it replaces.
    Str inlineSource = StrL(
        "import { View, div } from 'gpui';\n"
        "import { v_flex, h_flex } from 'gpui-base';\n"
        "export default class Board extends View {\n"
        "  render() {\n"
        "    const rows = [['AAPL', '230.42'], ['MSFT', '410.08']];\n"
        "    return v_flex().gap(4).children(rows.map(([symbol, price]) =>\n"
        "      h_flex().gap(6).px(6)\n"
        "        .child(div().w(80).child(symbol))\n"
        "        .child(div().w(80).child(price))));\n"
        "  }\n"
        "}\n");
    Str templatedSource = StrL(
        "import { View, div } from 'gpui';\n"
        "import { v_flex, h_flex } from 'gpui-base';\n"
        "const template = globalThis.__template;\n"
        "const Row = template((symbol, price) =>\n"
        "  h_flex().gap(6).px(6)\n"
        "    .child(div().w(80).child(symbol))\n"
        "    .child(div().w(80).child(price)));\n"
        "export default class Board extends View {\n"
        "  render() {\n"
        "    const rows = [['AAPL', '230.42'], ['MSFT', '410.08']];\n"
        "    return v_flex().gap(4).children(rows.map(([symbol, price]) =>\n"
        "      Row(symbol, price)));\n"
        "  }\n"
        "}\n");
    Arena* arena = ArenaNew();
    Str failure = {};
    Str inlineTree = ShellTemplateTree(arena, inlineSource, &failure);
    utassert(inlineTree.len > 0 && failure.len == 0);
    Str templatedTree = ShellTemplateTree(arena, templatedSource, &failure);
    utassert(templatedTree.len > 0 && failure.len == 0);
    utassert(StrEq(inlineTree, templatedTree));

    // A style argument and a handler are slots too, and each call writes its
    // own.
    Str slots = ShellTemplateTree(
        arena,
        StrL("import { View } from 'gpui';\n"
             "import { v_flex, Button } from 'gpui-base';\n"
             "const template = globalThis.__template;\n"
             "const Row = template((color, label, onPick) =>\n"
             "  Button.new('pick').bg(color).on_click(onPick).child(label));\n"
             "export default class Board extends View {\n"
             "  render() {\n"
             "    return v_flex()\n"
             "      .child(Row('#f8f8f8', 'one', () => 1))\n"
             "      .child(Row('#2563eb', 'two', () => 2));\n"
             "  }\n"
             "}\n"),
        &failure);
    utassert(failure.len == 0);
    utassert(StrFind(slots, StrL(".bg(\"#f8f8f8\")")) >= 0);
    utassert(StrFind(slots, StrL(".bg(\"#2563eb\")")) >= 0);
    utassert(StrFind(slots, StrL("text \"one\"")) >= 0);
    utassert(StrFind(slots, StrL("text \"two\"")) >= 0);
    ArenaDelete(arena);

    // Two calls must not share a handler, and a second render must not reuse
    // the first render's. A callback belongs to the snapshot that registered
    // it and is retired with it, so a template holding one would hand a
    // retired id to every call that followed.
    {
        App app;
        Window window;
        window.app = &app;
        ShellError error = {};
        ShellRuntime* runtime = ShellRuntime::New(&app, &error);
        utassert(runtime != nullptr);
        ViewType* type =
            runtime
                ? runtime->LoadSource(
                      StrL("handlers.js"),
                      StrL("import { View } from 'gpui';\n"
                           "import { v_flex, Button } from 'gpui-base';\n"
                           "const template = globalThis.__template;\n"
                           "const Row = template((label, onPick) =>\n"
                           "  Button.new('pick').on_click(onPick)"
                           ".child(label));\n"
                           "export default class Board extends View {\n"
                           "  render() {\n"
                           "    return v_flex().child(Row('one', () => 1))\n"
                           "      .child(Row('two', () => 2));\n"
                           "  }\n"
                           "}\n"),
                      &error)
                : nullptr;
        ViewObject* object =
            type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
                 : nullptr;
        utassert(object != nullptr && !error.IsSet());
        shell::CallbackId seen[4] = {0, 0, 0, 0};
        int count = 0;
        RenderSnapshot* held[2] = {nullptr, nullptr};
        for (int pass = 0; pass < 2 && object; pass++) {
            held[pass] = runtime->BuildSnapshot(object, &window, &app, {},
                                                nullptr, &error);
            utassert(held[pass] != nullptr);
            if (!held[pass]) break;
            const SpecArena* specs = held[pass]->Specs();
            for (SpecId id = 0; id < (SpecId)specs->Len(); id++) {
                const SpecNode* node = specs->Node(id);
                if (!node) continue;
                for (const SpecOp& op : node->ops) {
                    if (op.kind == SpecOpKind::Callback && count < 4) {
                        seen[count++] = op.callback;
                    }
                }
            }
        }
        utassert(count == 4);
        for (int i = 0; i < count; i++) {
            utassert(seen[i] != 0 && seen[i] != UINT64_MAX);
            for (int j = i + 1; j < count; j++) utassert(seen[i] != seen[j]);
        }
        delete held[0];
        delete held[1];
        ViewObjectRelease(object);
        ViewTypeRelease(type);
        if (runtime) runtime->Release();
        ShellErrorClear(&error);
        AppGlobalClear(&app);
    }

    // The things a template cannot hold, refused where they were written
    // rather than baked in.
    utassert(ShellTemplateRefuses(
        StrL("import { View, div } from 'gpui';\n"
             "const template = globalThis.__template;\n"
             "const Row = template((price) => div().child(`$${price}`));\n"
             "export default class Board extends View {\n"
             "  render() { return Row('230.42'); }\n"
             "}\n"),
        StrL("passed to a builder call but not computed on")));
    utassert(ShellTemplateRefuses(
        StrL("import { View } from 'gpui';\n"
             "import { Button } from 'gpui-base';\n"
             "const template = globalThis.__template;\n"
             "const Row = template((label) =>\n"
             "  Button.new('pick').on_click(() => 1).child(label));\n"
             "export default class Board extends View {\n"
             "  render() { return Row('one'); }\n"
             "}\n"),
        StrL("Take the handler as a parameter")));
    utassert(ShellTemplateRefuses(
        StrL("import { View, div } from 'gpui';\n"
             "const template = globalThis.__template;\n"
             "const Row = template((symbol, unused) => div().child(symbol));\n"
             "export default class Board extends View {\n"
             "  render() { return Row('AAPL', 'ignored'); }\n"
             "}\n"),
        StrL("template argument 1 is never used")));
    utassert(ShellTemplateRefuses(
        StrL("import { View, div } from 'gpui';\n"
             "const template = globalThis.__template;\n"
             "const Cell = template((value) => div().child(value));\n"
             "const Row = template((value) => div().child(Cell(value)));\n"
             "export default class Board extends View {\n"
             "  render() { return Row('AAPL'); }\n"
             "}\n"),
        StrL("template body cannot")));
    utassert(ShellTemplateRefuses(
        StrL("import { View } from 'gpui';\n"
             "import { Checkbox } from 'gpui-base';\n"
             "const template = globalThis.__template;\n"
             "const Row = template((flag) =>\n"
             "  Checkbox.new('pick').disabled(flag));\n"
             "export default class Board extends View {\n"
             "  render() { return Row(true); }\n"
             "}\n"),
        StrL("text children, style arguments and handlers")));
    utassert(
        ShellTemplateRefuses(StrL("import { View, div } from 'gpui';\n"
                                  "const template = globalThis.__template;\n"
                                  "const Row = template((symbol, price) =>\n"
                                  "  div().child(symbol).child(price));\n"
                                  "export default class Board extends View {\n"
                                  "  render() { return Row('AAPL'); }\n"
                                  "}\n"),
                             StrL("takes 2 argument(s) and was given 1")));

    // A state style survives being grafted twice: the interior of a grafted
    // subtree arrives already claimed, and only its root is free to attach.
    {
        Arena* a = ArenaNew();
        Str reused = ShellTemplateTree(
            a,
            StrL("import { View, div } from 'gpui';\n"
                 "import { v_flex } from 'gpui-base';\n"
                 "const template = globalThis.__template;\n"
                 "const Row = template((label) =>\n"
                 "  div().hover((s) => s.bg('#111111')).child(label));\n"
                 "export default class Board extends View {\n"
                 "  render() {\n"
                 "    return v_flex().child(Row('one')).child(Row('two'));\n"
                 "  }\n"
                 "}\n"),
            &failure);
        utassert(failure.len == 0 && reused.len > 0);
        utassert(StrFind(reused, StrL("text \"one\"")) >= 0);
        utassert(StrFind(reused, StrL("text \"two\"")) >= 0);
        ArenaDelete(a);
    }

    // A body that throws leaves the render it interrupted intact.
    {
        Arena* a = ArenaNew();
        Str after = ShellTemplateTree(
            a,
            StrL("import { View } from 'gpui';\n"
                 "import { v_flex } from 'gpui-base';\n"
                 "const template = globalThis.__template;\n"
                 "const Bad = template((value) => {\n"
                 "  throw new Error('no ' + typeof value);\n"
                 "});\n"
                 "export default class Board extends View {\n"
                 "  render() {\n"
                 "    let caught = false;\n"
                 "    try { Bad('x'); } catch (error) { caught = true; }\n"
                 "    return v_flex().child(caught ? 'recovered' : 'lost');\n"
                 "  }\n"
                 "}\n"),
            &failure);
        utassert(failure.len == 0);
        utassert(StrFind(after, StrL("text \"recovered\"")) >= 0);
        ArenaDelete(a);
    }
}

// A rebuild that produced the shape it replaced is counted, and one that did
// not is counted separately. Nothing acts on the answer: it is the measurement
// a template cache rests on.
static void ShellStructureFingerprintsCountRepeatedShapes() {
    App app;
    Window window;
    window.app = &app;
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    if (!runtime) return;
    ViewType* type = runtime->LoadSource(
        StrL("structure.js"),
        StrL("import { View } from 'gpui';\n"
             "import { v_flex } from 'gpui-base';\n"
             "globalThis.shellRows = 1;\n"
             "globalThis.shellPrice = 'one';\n"
             "export default class Board extends View {\n"
             "  render() {\n"
             "    let root = v_flex();\n"
             "    for (let i = 0; i < globalThis.shellRows; i += 1)\n"
             "      root = root.child(globalThis.shellPrice);\n"
             "    return root;\n"
             "  }\n"
             "}\n"),
        &error);
    ViewObject* object =
        type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
             : nullptr;
    utassert(object != nullptr && !error.IsSet());

    RenderSnapshot* first =
        object
            ? runtime->BuildSnapshot(object, &window, &app, {}, nullptr, &error)
            : nullptr;
    utassert(first != nullptr);
    // The same shape with a different value in it.
    runtime
        ->Eval(StrL("globalThis.shellPrice = 'two'"), StrL("edit.js"), &error);
    RenderSnapshot* second =
        first
            ? runtime->BuildSnapshot(object, &window, &app, {}, nullptr, &error)
            : nullptr;
    utassert(second != nullptr);
    // A row added: a different shape.
    runtime->Eval(StrL("globalThis.shellRows = 2"), StrL("grow.js"), &error);
    RenderSnapshot* third =
        second
            ? runtime->BuildSnapshot(object, &window, &app, {}, nullptr, &error)
            : nullptr;
    utassert(third != nullptr);
    if (first && second && third) {
        utassert(first->Structure() == second->Structure());
        utassert(second->Structure() != third->Structure());
        // A first build has no predecessor and is not a data point.
        runtime->RecordStructure(first->Structure() == second->Structure());
        runtime->RecordStructure(second->Structure() == third->Structure());
        RuntimeMetrics reading = runtime->ReadMetrics();
        utassert(reading.structureRepeats == 1);
        utassert(reading.structureChanges == 1);
        double rate = 0;
        utassert(reading.StructureRepeatRate(&rate) && rate == 0.5);
        RuntimeMetrics empty = {};
        utassert(!empty.StructureRepeatRate(&rate));
        RuntimeMetrics delta = reading.Since(reading);
        utassert(delta.structureRepeats == 0 && delta.structureChanges == 0);
    }
    delete third;
    delete second;
    delete first;
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

// A script states the one type size it has an opinion about and keeps the rest
// of the scale, and cx.theme() reports what it got. Ported from the typography
// half of crates/shell/src/tests/render.rs.
static void ScriptThemesCarryATypeScale() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    if (!runtime) return;
#define SHELL_TEST_COLORS                                                  \
    "const colors = { background:'#010203', foreground:'#fafafa', "        \
    "surface:'#111111', surface_foreground:'#f0f0f0', primary:'#222222', " \
    "primary_foreground:'#eeeeee', secondary:'#333333', "                  \
    "secondary_foreground:'#dddddd', muted:'#444444', "                    \
    "muted_foreground:'#cccccc', accent:'#555555', "                       \
    "accent_foreground:'#bbbbbb', destructive:'#666666', "                 \
    "destructive_foreground:'#aaaaaa', border:'#777777', "                 \
    "input:'#888888', ring:'#999999' };\n"                                 \
    "const scale = { colors, spacing:{xxs:1,xs:2,sm:3,md:13,lg:21,xl:34,"  \
    "xxl:55}, radius:{none:0,sm:2,md:9,lg:12,xl:18,full:999} };\n"
    ViewType* type = runtime->LoadSource(
        StrL("typography.js"),
        StrL("import { div, View } from 'gpui';\n"
             "import { set_theme } from 'gpui-base';\n" SHELL_TEST_COLORS
             "export default class Typed extends View {\n"
             "  init() {\n"
             "    set_theme({ appearance:'dark', tokens: { ...scale,\n"
             "      typography: { sans:'Iosevka', md: { size: 12 } } } });\n"
             "  }\n"
             "  render(cx) {\n"
             "    const t = cx.theme();\n"
             "    if (t.typography.sans !== 'Iosevka') throw new "
             "Error('family');\n"
             "    if (t.typography.md.size !== 12) throw new Error('size');\n"
             // Everything left out keeps the token it would have replaced.
             "    if (t.typography.md.line_height !== 24) throw new "
             "Error('lh');\n"
             "    if (t.typography.lg.size !== 18) throw new Error('lg');\n"
             "    return div().text_size(t.typography.md.size);\n"
             "  }\n"
             "}\n"),
        &error);
    ViewObject* object =
        type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
             : nullptr;
    utassert(object != nullptr && !error.IsSet());
    Arena* output = ArenaNew();
    Str spec = object ? runtime->RenderToSpec(output, object, &window, &app, {},
                                              nullptr, &error)
                      : Str{};
    utassert(!error.IsSet() && StrContains(spec, StrL(".text_size(12)")));
    const BaseTheme* theme = BaseThemeGlobal(&app);
    utassert(theme && theme->tokens.typography.md.size == 12 &&
             theme->tokens.typography.md.lineHeight == 24 &&
             StrEq(theme->tokens.typography.sans, StrL("Iosevka")));
    ViewObjectRelease(object);
    ViewTypeRelease(type);

    // A size or a line height of zero is text that cannot be read, and it
    // would be applied silently. The spacing and radius scales allow zero
    // because a gap of zero is a real answer.
    type = runtime->LoadSource(
        StrL("bad-typography.js"),
        StrL("import { div, View } from 'gpui';\n"
             "import { set_theme } from 'gpui-base';\n" SHELL_TEST_COLORS
             "export default class Bad extends View {\n"
             "  init() {\n"
             "    set_theme({ appearance:'light', tokens: { ...scale,\n"
             "      typography: { md: { size: 0 } } } });\n"
             "  }\n"
             "  render() { return div(); }\n"
             "}\n"),
        &error);
    object = type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
                  : nullptr;
    utassert(object == nullptr && error.IsSet());
    utassert(StrContains(error.message, StrL("md.size")) &&
             StrContains(error.message, StrL("above zero")));
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    ArenaDelete(output);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}
#undef SHELL_TEST_COLORS

// A palette change reaches a view that nothing notified, and a palette that did
// not change costs the view nothing. Ported from the theme half of
// crates/shell/src/tests/{render,snapshot}.rs.
static void ScriptViewsRebuildOnlyWhenThePaletteMoves() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    if (!runtime) return;
    ViewType* type = runtime->LoadSource(
        StrL("theme-view.js"),
        StrL("import { div, View } from 'gpui';\n"
             "globalThis.themeRenders = 0;\n"
             "export default class Themed extends View {\n"
             "  render(cx) {\n"
             "    globalThis.themeRenders += 1;\n"
             "    return div().bg(cx.theme().colors.background);\n"
             "  }\n"
             "}\n"),
        &error);
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    utassert(EntityRender(&app, &window, frame, view.id) != nullptr);
    utassert(runtime->Eval(
        StrL("if (globalThis.themeRenders !== 1) throw new Error('first')"),
        StrL("theme-check.js"), &error));
    // A clean frame replays the published snapshot without entering the VM.
    frame->Reset();
    utassert(EntityRender(&app, &window, frame, view.id) != nullptr);
    utassert(runtime->Eval(
        StrL("if (globalThis.themeRenders !== 1) throw new Error('reused')"),
        StrL("theme-reuse.js"), &error));
    // A new palette does reach it, with nothing having notified the view.
    BaseTheme base = BaseTheme::Global(&app);
    base.tokens.colors.background = Rgba8(1, 2, 3, 255);
    BaseThemeSet(&app, base);
    frame->Reset();
    utassert(EntityRender(&app, &window, frame, view.id) != nullptr);
    utassert(runtime->Eval(
        StrL("if (globalThis.themeRenders !== 2) throw new Error('palette')"),
        StrL("theme-move.js"), &error));
    frame->Reset();
    utassert(EntityRender(&app, &window, frame, view.id) != nullptr);
    utassert(runtime->Eval(
        StrL("if (globalThis.themeRenders !== 2) throw new Error('settled')"),
        StrL("theme-settle.js"), &error));
    utassert(!error.IsSet());
    EntityDrop(&app, view.id);
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

// A dialog's content is a function over somebody else's state, so the root
// rebuilds it whenever the root itself draws. Ported from
// `a_dialog_rebuilds_from_the_state_it_closes_over`.
static void ScriptOverlaysRebuildFromTheStateTheyCloseOver() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    if (!runtime) return;
    ViewType* type = runtime->LoadSource(
        StrL("overlay-state.js"),
        StrL("import { div, View } from 'gpui';\n"
             "globalThis.overlayLabel = 'first';\n"
             "globalThis.overlayRenders = 0;\n"
             "export default class Host extends View {\n"
             "  init(props, cx) {\n"
             "    window.open_dialog(() => {\n"
             "      globalThis.overlayRenders += 1;\n"
             "      return div().child(globalThis.overlayLabel);\n"
             "    });\n"
             "  }\n"
             "  render() { return div().child('host'); }\n"
             "}\n"),
        &error);
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Entity<ShellRoot> root = ShellRoot::New(&app, view.id);
    window.root = root.id;
    Arena* frame = ArenaNew();
    utassert(EntityRender(&app, &window, frame, root.id) != nullptr);
    utassert(!error.IsSet());
    utassert(runtime->Eval(
        StrL("if (globalThis.overlayRenders < 1) throw new Error('opened')"),
        StrL("overlay-open.js"), &error));
    // The state the dialog closes over moves, and nothing notifies the dialog:
    // the root rebuilds it whenever the root itself draws.
    utassert(runtime->Eval(StrL("globalThis.overlayLabel = 'second'; "
                                "globalThis.overlayRenders = 0"),
                           StrL("overlay-edit.js"), &error));
    frame->Reset();
    utassert(EntityRender(&app, &window, frame, root.id) != nullptr);
    utassert(runtime->Eval(
        StrL("if (globalThis.overlayRenders !== 1) throw new Error('rebuilt')"),
        StrL("overlay-check.js"), &error));
    utassert(!error.IsSet());
    EntityDrop(&app, root.id);
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

// The performance HUD is the root's: the script says whether and where, and
// what it renders can neither move the HUD nor rebuild it. Ported from
// crates/shell/src/engine/quickjs/overlay.rs's own tests.
static void ScriptsSwitchOnARootOwnedPerformanceHud() {
    App app;
    Window window;
    window.app = &app;
    VecAppend(app.windows, &window);
    component::Init(&app);
    ExecInit();
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    if (!runtime) return;
    ViewType* type = runtime->LoadSource(
        StrL("hud.js"),
        StrL(
            "import { div, View } from 'gpui';\n"
            "import { show_fps_monitor, hide_fps_monitor, fps_monitor_visible }"
            " from 'gpui-fps';\n"
            "globalThis.hudShow = () => show_fps_monitor(\n"
            "  { anchor: 'bottom_left', frame_budget: 8.33 });\n"
            "globalThis.hudHide = () => hide_fps_monitor();\n"
            "globalThis.hudVisible = () => fps_monitor_visible();\n"
            "export default class Host extends View {\n"
            "  render() { return div().child('host'); }\n"
            "}\n"),
        &error);
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Entity<ShellRoot> root = ShellRoot::New(&app, view.id);
    window.root = root.id;
    Arena* frame = ArenaNew();
    utassert(EntityRender(&app, &window, frame, root.id) != nullptr);

    Ctx cx = {};
    cx.app = &app;
    cx.win = &window;
    cx.a = frame;
    cx.self = root.id;
    utassert(!ShellRootFpsMonitorVisible(&cx));
    FpsHudRequest request = {};
    request.anchor = FpsAnchor::BottomLeft;
    request.continuous = false;
    request.hasFrameBudget = true;
    request.frameBudget = 1.f / 120.f;
    utassert(ShellRootShowFpsMonitor(&cx, request));
    utassert(ShellRootFpsMonitorVisible(&cx));
    ShellRoot* state = root.Get(&app);
    utassert(state && state->fpsHud.anchor == FpsAnchor::BottomLeft &&
             state->fpsHud.hasFrameBudget && !state->fpsHud.continuous);
    // Up means drawn: the root's own layer carries the HUD, not the script's.
    frame->Reset();
    window.animFrame = false;
    utassert(EntityRender(&app, &window, frame, root.id) != nullptr);
    utassert(!window.animFrame);
    utassert(ShellRootHideFpsMonitor(&cx));
    utassert(!ShellRootFpsMonitorVisible(&cx));
    // Nothing was up to take down.
    utassert(!ShellRootHideFpsMonitor(&cx));

    utassert(ShellRootShowFpsMonitor(&cx, FpsHudRequest{}));
    frame->Reset();
    window.animFrame = false;
    utassert(EntityRender(&app, &window, frame, root.id) != nullptr);
    utassert(window.animFrame);
    utassert(ShellRootHideFpsMonitor(&cx));

    // Every anchor a script can name resolves, and one it cannot is refused
    // rather than falling back to a corner — which is what lets the binding
    // list the valid ones back at the author.
    FpsAnchor named = FpsAnchor::TopRight;
    utassert(!FpsAnchorFromName(StrL("middle"), &named));
    int anchors = 0;
    SeqStrings all = FpsAnchorNames();
    for (Str name = SeqStrFirst(all); name.len > 0; name = SeqStrNext(name)) {
        utassert(FpsAnchorFromName(name, &named));
        anchors++;
    }
    utassert(anchors == 8);
    utassert(!ShellRootFpsMonitorVisible(&cx));

    // The monitor the HUD drew through samples resources on the executor, and
    // its completion reaches back for the App that spawned it. Both die with
    // this frame, so the job is drained and the monitor dropped while they are
    // still here rather than left for whichever suite drains next.
    ExecWaitIdle(5000);
    auto* monitor = KeyedState<Entity<FpsMonitor>>(
        &cx, (uint32_t)HashClickId(StrL("gpui-fps-monitor")));
    utassert(monitor && monitor->IsValid());
    if (monitor && monitor->IsValid()) {
        EntityDrop(&app, monitor->id);
        *monitor = {};
    }
    EntityDrop(&app, root.id);
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

// A right press on a virtual list row reports the row's key and the press
// itself, with local_position measured from the row's own box. Ported from
// the on_item_secondary_click half of crates/shell/src/tests/render.rs.
static void VirtualListRowsReportASecondaryPress() {
    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    if (!runtime) return;
    ViewType* type = runtime->LoadSource(
        StrL("virtual-secondary.js"),
        StrL("import { View, div } from 'gpui';\n"
             "import { v_virtual_list } from 'gpui-base';\n"
             "globalThis.pressKey = ''; globalThis.pressButton = '';\n"
             "globalThis.pressLocalX = -1; globalThis.pressShift = false;\n"
             "export default class Main extends View {\n"
             "  render(cx) { return v_virtual_list('rows', 4, 24,\n"
             "    index => 'row-' + index,\n"
             "    range => { const out = [];\n"
             "      for (let i = range.start; i < range.end; i++)\n"
             "        out.push(div().child('row ' + i));\n"
             "      return out; })\n"
             "    .on_item_secondary_click((key, event) => {\n"
             "      globalThis.pressKey = key;\n"
             "      globalThis.pressButton = event.button;\n"
             "      globalThis.pressLocalX = event.local_position.x;\n"
             "      globalThis.pressShift = event.modifiers.shift;\n"
             "    }); }\n"
             "}\n"),
        &error);
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    ViewTypeRelease(type);
    Arena* frame = ArenaNew();
    El* root =
        view.IsValid() ? EntityRender(&app, &window, frame, view.id) : nullptr;
    utassert(root != nullptr && !error.IsSet());
    El* firstRow = root && root->first ? root->first->first : nullptr;
    utassert(firstRow && firstRow->onMouseDown.IsValid());
    if (firstRow && firstRow->onMouseDown.IsValid()) {
        // A left press on the same row reports nothing: the row already
        // reports its click through on_item_click.
        MouseDownEvent left = {};
        left.button = MouseButton::Left;
        ListenerCall(&app, &window, firstRow->onMouseDown, &left);
        utassert(runtime->Eval(
            StrL("if (globalThis.pressKey !== '') throw new Error('left')"),
            StrL("press-left.js"), &error));
        MouseDownEvent press = {};
        press.button = MouseButton::Right;
        press.x = 30;
        press.y = 12;
        press.el = Bounds{10, 4, 100, 24};
        press.modifiers.shift = true;
        ListenerCall(&app, &window, firstRow->onMouseDown, &press);
    }
    utassert(runtime->Eval(
        StrL(
            "if (globalThis.pressKey !== 'row-0') throw new Error('key');\n"
            "if (globalThis.pressButton !== 'right') throw new "
            "Error('button');\n"
            "if (globalThis.pressLocalX !== 20) throw new Error('local');\n"
            "if (globalThis.pressShift !== true) throw new Error('modifiers')"),
        StrL("press-check.js"), &error));
    utassert(!error.IsSet());
    EntityDrop(&app, view.id);
    ArenaDelete(frame);
    runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

void TestShellCore() {
    TestSuite("shell_core");
    BridgedValuesMatchJavaScriptConversions();
    RuntimeMetricsSeparateScriptNativeAndFrames();
    CapabilitiesAreDenyFirstAndScoped();
    FilesystemGrantsReturnRootRelativeAuthority();
    PoliciesFreezeCapabilityGrants();
    ScopeGenerationsExpireAdoptAndRefuseReentry();
    SpecElementsAreSingleUseValues();
    SpecsAndSnapshotsDumpWithoutEnteringTheVm();
    ThemeTokenNamesAndValuesComeFromTheTheme();
    RenderContextExposesFrozenGenerationBoundTheme();
    ScriptThemesAndOpenUrlsFollowHostScopeRules();
    RuntimeLoadsRendersAndRetiresCallbacks();
    RuntimeAbortsFailedSnapshotTransactions();
    ShellTemplatesRecordOnceAndFillPerCall();
    ShellStructureFingerprintsCountRepeatedShapes();
    RuntimeLoadsOnlyModulesInsideTheApplicationRoot();
    ShellSourceWatchReloadsAtomically();
    ShellHostModulesBridgePlainDataAndPromises();
    ShellTypeDeclarationsMatchRuntimeAndRefreshImportDirectories();
    PublishedSnapshotsMaterializeToNativeElements();
    ShellMaterializesStateTemplatesInputsAndPaths();
    ShellRootHostsDialogsSheetsAndToasts();
    ScriptViewsReuseSnapshotsUntilNotified();
    RetainedScriptStateSurvivesFramesAndDispatchesEvents();
    NestedScriptViewsRetainUpdateRollbackAndRelease();
    VirtualListsRenderOneVisibleBatch();
    VirtualListRowsReportASecondaryPress();
    ScriptThemesCarryATypeScale();
    ScriptViewsRebuildOnlyWhenThePaletteMoves();
    ScriptOverlaysRebuildFromTheStateTheyCloseOver();
    ScriptsSwitchOnARootOwnedPerformanceHud();
    ShellSandboxWithholdsCompilersAndSharedPrototypeWrites();
    ShellSchedulerResumesPromisesInTaskScope();
    ShellStorageAndAuthorityFreeModulesWork();
    ShellStorageWritesRevisionsInOrderAndFlushes();
    ShellProcessRunIsBoundedAndPromiseBased();
    ShellFilesystemUsesGrantedHandleRelativePaths();
    ShellAssetsStayInsideTheApplicationRoot();
    ShellCryptoAndCompressionMatchStandardRuntime();
    ShellFetchFollowsOnlyLocationRedirectStatuses();
    ShellFetchRedirectsRewriteMethodAndBody();
    ShellFetchOriginComparesSchemeHostAndEffectivePort();
    ShellFetchRedirectsKeepCredentialsOnTheirOrigin();
    ShellFetchGrantsBindMethodSchemePortAndPath();
    ShellFetchProhibitsTheHeadersTheClientOwns();
    ShellFetchChecksEveryGetTargetBeforeContact();
    ShellAccessibilityRolesMirrorUpstream();
    ShellDockPanelsPersistAndChromeRunsInLayoutScope();
    ShellPluginManifestsDiscoverAuthorizeAndUnload();
}
