#include "gpui.h"

#include <stdio.h>
#include <string.h>

using namespace gpui;
using namespace gpui::shell;

static void Print(Str value, FILE* file = stdout) {
    if (value) fwrite(value.s, 1, (size_t)len(value), file);
}

static void PrintVersion() {
    printf("gpui-shell %s\n", kShellVersion);
}

static void PrintHelp() {
    StrBuilder help;
    ShellFormatHelp(&help);
    Str text = help.TakeStr();
    Print(text);
    if (text && (len(text) == 0 || text.s[len(text) - 1] != '\n'))
        fputc('\n', stdout);
    StrFree(text);
}

// cmd-q on macOS, alt-f4 elsewhere. Installed before the script's own
// bindings, so a script that binds the same chord still wins.
static void QuitShell(Window* win, ActionEvent*) {
    if (win && win->app) AppQuitAll(win->app);
}

static void InstallQuit() {
    KeyBinding binding;
    binding.stroke = PlatSecondaryIsCommand() ? "cmd-q" : "alt-f4";
    binding.action = ActionOf(StrL("gpui_shell::Quit"));
    KeymapBind(&binding, 1);
    AppOnAction(binding.action, &QuitShell);
}

static Window* gActivateWindow = nullptr;
static void ActivatePosted() {
    Window* window = gActivateWindow;
    gActivateWindow = nullptr;
    if (window) AppActivate(window);
}

static TempStr JoinPathTemp(Str left, Str right) {
    bool separator =
        left && left.s[len(left) - 1] != '/' && left.s[len(left) - 1] != '\\';
    int n = len(left) + (separator ? 1 : 0) + len(right);
    if (!left || !right || n >= kMaxPath) return {};
    if (!separator) return fmt("%s%s", left, right);
    return fmt("%s%c%s", left, GPUI_OS_WINDOWS ? '\\' : '/', right);
}

static bool ResolveRoot(Str input, bool requireEntry, Str* root, Str* entry,
                        ShellError* error) {
    *root = {};
    *entry = {};
    if (!input || len(input) >= kMaxPath) {
        ShellErrorSet(error, StrL("application path is empty or too long"));
        return false;
    }
    TempStr candidate = StrDupTemp(input);
    if (PlatFileExists(candidate.s)) {
        int slash = len(input) - 1;
        while (slash >= 0 && candidate.s[slash] != '/' &&
               candidate.s[slash] != '\\')
            slash--;
        if (slash < 0)
            candidate = StrDupTemp(StrL("."));
        else if (slash == 0)
            candidate.s[1] = 0;
        else
            candidate.s[slash] = 0;
    }
    if (!PlatDirExists(candidate.s)) {
        ShellErrorSet(error, fmt("`%s` does not exist", input));
        return false;
    }
    TempStr canonical = AllocStrTemp(kMaxPath - 1);
    if (!PlatCanonicalPath(candidate.s, canonical.s, len(canonical) + 1)) {
        ShellErrorSet(error, fmt("cannot read `%s`", candidate));
        return false;
    }
    Str resolved(canonical.s);
    PluginManifest manifest;
    TempStr manifestPath = JoinPathTemp(resolved, Str(kShellManifestFile));
    bool hasManifest = manifestPath && PlatFileExists(manifestPath.s);
    Str selected = StrL("main.js");
    if (hasManifest && requireEntry) {
        if (!PluginManifestRead(resolved, &manifest, error)) return false;
        selected = manifest.entry;
    }
    TempStr entryPath = JoinPathTemp(resolved, selected);
    if (requireEntry && (!entryPath || !PlatFileExists(entryPath.s))) {
        ShellErrorSet(error,
                      fmt("no `%s` in %s\n\nAn application directory must "
                          "contain %s, which default-exports a view class.",
                          selected, resolved, selected));
        return false;
    }
    StrDup2(resolved, selected, *root, *entry);
    return root->s && entry->s;
}

static Policy* LocalPolicy(Str root, bool installed, ShellError* error) {
    PluginManifest manifest;
    TempStr manifestPath = JoinPathTemp(root, Str(kShellManifestFile));
    bool hasManifest = manifestPath && PlatFileExists(manifestPath.s);
    if (hasManifest && !PluginManifestRead(root, &manifest, error))
        return nullptr;

    Arena* scratch = ArenaNew();
    // An installed or packaged application keeps its data under the manifest
    // id. A directory named with -d stays keyed by its path.
    Str id = installed && hasManifest && manifest.id
                 ? StrDup(manifest.id)
                 : ShellBundleIdForPath(root);
    Str data = ShellAppDataDirectory(id, scratch, error);
    if (!data) {
        StrFree(id);
        ArenaDelete(scratch);
        return nullptr;
    }
    Str home = ShellDataHome();
    StrBuilder relative;
    relative.Append(StrL("gpui-shell/apps/"));
    relative.Append(id);
    Str relativePath = relative.TakeStr();
    FsResult result;
    Str fsError;
    if (!FsRun(FsOperation::MakeDirectory, home, relativePath, {}, true,
               &result, &fsError)) {
        fprintf(stderr, "gpui-shell: storage directory is unavailable: ");
        Print(fsError, stderr);
        fputc('\n', stderr);
    }
    result.Free();
    StrFree(fsError);

    Capabilities capabilities =
        hasManifest ? manifest.Grant(root, data) : Capabilities();
    if (!hasManifest) capabilities.Storage(true);
    capabilities.AddReadRoot(root).AddReadRoot(data).AddWriteRoot(data).Exit(
        true);
    Policy* policy = PolicyNew(capabilities);
    // The same name namespaces the application's dock panels. Storage and a
    // persisted layout are the two things that have to survive a restart under
    // a name rather than a path, so they take the name from one place.
    PolicySetApplication(policy, id);
    if (capabilities.HasStorage()) {
        TempStr store = JoinPathTemp(data, StrL("store.json"));
        if (store) {
            Str storageError;
            if (!PolicySetStoragePath(policy, store, &storageError)) {
                fprintf(stderr, "gpui-shell: storage is unavailable: ");
                Print(storageError, stderr);
                fputc('\n', stderr);
            }
            StrFree(storageError);
        }
    }
    StrFree(relativePath);
    StrFree(home);
    StrFree(id);
    ArenaDelete(scratch);
    return policy;
}

static bool RefreshTypes(Str root, Policy* policy, bool reportFailure) {
    HostModules* modules = PolicyHostModules(policy);
    ShellError error = {};
    int written = 0;
    bool ok = ShellWriteTypeDeclarations(root, modules, &written, &error,
                                         component_shell::Components());
    if (!ok && reportFailure) {
        fprintf(stderr, "gpui-shell: ");
        Print(error.message, stderr);
        fputc('\n', stderr);
    }
    ShellErrorClear(&error);
    HostModulesRelease(modules);
    return ok;
}

static int Check(Str root, Str entry, bool printSpec, Policy* policy) {
    (void)entry;
    App app;
    Window window;
    window.app = &app;
    ShellInitWithComponents(&app, component_shell::Components());
    ShellError error = {};
    ShellRuntime* runtime =
        ShellRuntime::New(&app, &error, component_shell::Components());
    Arena* arena = ArenaNew();
    Str spec = runtime ? ShellCheckApplication(arena, runtime, root, &window,
                                               &app, policy, &error)
                       : Str{};
    int status = error.IsSet() ? 1 : 0;
    if (status) {
        Print(error.message, stderr);
        fprintf(stderr, "\n\ncheck failed: ");
        Print(root, stderr);
        fputc('\n', stderr);
    } else {
        if (printSpec) {
            Print(spec);
            fputc('\n', stdout);
        }
        printf("check passed: ");
        Print(root);
        fputc('\n', stdout);
    }
    EntityDropAll(&app);
    if (runtime) runtime->Release();
    AppGlobalClear(&app);
    ArenaDelete(arena);
    ShellErrorClear(&error);
    return status;
}

static void OnExitRequest(const ShellExitRequest& request, Ctx* cx) {
    if (!cx || !cx->app) return;
    cx->app->exitCode = request.code;
    AppQuitAll(cx->app);
}

static Str WindowTitle(Str root) {
    int start = len(root);
    while (start > 0 && root.s[start - 1] != '/' && root.s[start - 1] != '\\')
        start--;
    StrBuilder title;
    title.Append(Str(root.s + start, len(root) - start));
    title.Append(StrL(" — gpui-shell"));
    return title.TakeStr();
}

// failure_surface: the cwd prefix is noise when the failure is the
// application the command just started.
static Str DisplayMessage(Arena* a, Str message) {
    if (!message) return {};
    char cwd[kMaxPath];
    cwd[0] = 0;
    PlatGetCwd(cwd, kMaxPath);
    if (!cwd[0]) return StrDup(a, message);
    int n = (int)strlen(cwd);
    StrBuilder out;
    for (int i = 0; i < len(message);) {
        bool hit = i + n < len(message) &&
                   (message.s[i + n] == '/' || message.s[i + n] == '\\') &&
                   memcmp(message.s + i, cwd, (size_t)n) == 0;
        if (hit) {
            i += n + 1;
            continue;
        }
        out.AppendChar(message.s[i]);
        i++;
    }
    Str built = out.TakeStr();
    Str stable = StrDup(a, built);
    StrFree(built);
    return stable;
}

struct FailureCopied {
    bool on = false;
};

static uint32_t FailureCopiedKey() {
    return KeyedKey(HashClickId(StrL("shell-failure-copied")), 1);
}

// What the window shows when the application could not be loaded, or loaded
// but would not mount. host.rs mounts this in the ShellRoot so the window
// says why instead of closing.
struct LoadFailure {
    Str message;

    ~LoadFailure() { StrFree(message); }

    static void OnCopy(LoadFailure* self, Ctx* cx, const ClickEvent*) {
        StrBuilder payload;
        payload.Append(StrL("This application could not be loaded\n\n"));
        if (self && self->message) payload.Append(self->message);
        Str text = payload.TakeStr();
        ClipboardSetText(cx->win, text);
        StrFree(text);
        FailureCopied* copied =
            KeyedState<FailureCopied>(cx, FailureCopiedKey());
        if (copied) copied->on = true;
        Notify(cx);
    }

    static El* Render(LoadFailure* self, Ctx* cx) {
        Arena* a = cx->a;
        const Theme& th = ThemeNow(cx->app);
        FailureCopied* copied =
            KeyedState<FailureCopied>(cx, FailureCopiedKey());
        bool isCopied = copied && copied->on;
        Str detail = DisplayMessage(a, self ? self->message : Str{});
        El* copy =
            Div(a)
                ->W(Rems(cx, 1.625f))
                ->H(Rems(cx, 1.625f))
                ->Shrink0()
                ->ItemsCenter()
                ->JustifyCenter()
                ->Border(1, th.border)
                ->Click(HashClickId(StrL("shell-failure-copy")))
                ->OnClick(Listen(cx, &LoadFailure::OnCopy))
                ->Child(IconEl(a, isCopied ? IconName::Check : IconName::Copy,
                               Rems(cx, 0.875f))
                            ->Fg(isCopied ? th.mutedFg : th.foreground));
        El* body =
            Div(a)
                ->FlexCol()
                ->Gap(Rems(cx, 0.625f))
                ->Pad(Rems(cx, 1.25f))
                ->Child(TextEl(a, StrL("This application could not be loaded"))
                            ->Fg(th.foreground)
                            ->Font(Rems(cx, 0.875f))
                            ->LineHeight(1.4f))
                ->Child(Div(a)
                            ->MaxH(Rems(cx, 18.f))
                            ->ScrollY(0)
                            ->Child(TextEl(a, detail)
                                        ->Wrap()
                                        ->Fg(th.mutedFg)
                                        ->Font(Rems(cx, 0.75f))
                                        ->FontFamily(Str(PlatMonoFontName()))
                                        ->LineHeight(1.5f)))
                ->Child(
                    Div(a)
                        ->FlexRow()
                        ->ItemsCenter()
                        ->JustifyBetween()
                        ->Gap(Rems(cx, 1.f))
                        ->PadT(Rems(cx, 0.75f))
                        ->BorderT(1, th.border)
                        ->Child(TextEl(a, StrL("An application directory needs "
                                               "main.js, or the entry its "
                                               "gpui-shell.json names, "
                                               "default-exporting a class that "
                                               "extends View."))
                                    ->Wrap()
                                    ->Flex1()
                                    ->MinW(0)
                                    ->Fg(th.mutedFg)
                                    ->Font(Rems(cx, 0.75f))
                                    ->LineHeight(1.5f))
                        ->Child(copy));
        return Div(a)
            ->SizeFull()
            ->Flex()
            ->ItemsCenter()
            ->JustifyCenter()
            ->Bg(th.background)
            ->Pad(Rems(cx, 2.f))
            ->Child(Div(a)
                        ->FlexCol()
                        ->W(kFill)
                        ->MaxW(Rems(cx, 42.f))
                        ->Bg(th.popover)
                        ->Border(1, th.border)
                        ->Child(Div(a)->H(2)->W(kFill)->Bg(th.danger))
                        ->Child(body));
    }
};

static EntityId FailureRoot(App* app, Str message) {
    Entity<LoadFailure> failure = EntityNew<LoadFailure>(app);
    if (LoadFailure* state = failure.Get(app)) state->message = StrDup(message);
    return ShellRoot::New(app, failure.id).id;
}

// The window builder host.rs hands the catalog's opener: mounts the loaded
// application in the window that just opened and answers the ShellRoot it
// lives in.
struct MountRequest {
    ShellRuntime* runtime = nullptr;
    LoadedApplication* application = nullptr;
    // Set when the application never loaded. The window still opens.
    Str loadError;
    Entity<ScriptView> view = {};
};

static EntityId BuildShellRoot(Window* window, App* app, void* data) {
    MountRequest* request = (MountRequest*)data;
    if (!request->application) return FailureRoot(app, request->loadError);
    ShellError error = {};
    request->view = request->runtime->MountApplication(request->application,
                                                       window, app, &error);
    if (!request->view.IsValid()) {
        fprintf(stderr, "gpui-shell: ");
        Print(error.message, stderr);
        fputc('\n', stderr);
        Str message = error.message;
        ShellErrorClear(&error);
        return FailureRoot(app, message);
    }
    ShellErrorClear(&error);
    return ShellRoot::New(app, request->view.id).id;
}

static int Run(Str root, Str entry, const Invocation& invocation,
               Policy* policy, Str earlyError) {
    ShellSetDevelopmentMode(invocation.development);
    AppAssets assets(root);
    assets.Install();
    App* app = AppNew();
    if (!app) {
        fprintf(stderr, "gpui-shell: could not initialize the platform\n");
        ShellSetDevelopmentMode(false);
        return 1;
    }
    const FrozenComponentRegistry* components = component_shell::Components();
    ShellInitWithComponents(app, components);
    InstallQuit();
    // load_application loads under the default policy, as Rust's does, so
    // the application's grants -- its manifest's capabilities, its name and
    // its store -- become the default before anything loads.
    if (policy) PolicySetDefault(policy);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(app, &error, components);
    LoadedApplication* application = nullptr;
    if (!earlyError && runtime)
        application = runtime->LoadApplication(root, entry, &error);
    Str loadError = {};
    if (earlyError)
        loadError = StrDup(earlyError);
    else if (!application)
        loadError = StrDup(error.IsSet() ? error.message
                                         : StrL("failed to start the script "
                                                "runtime"));
    if (loadError) {
        fprintf(stderr, "gpui-shell: ");
        Print(loadError, stderr);
        fputc('\n', stderr);
    }
    Str title = WindowTitle(root);
    ComponentWindowOptions options;
    options.title = title;
    if (!earlyError) {
        PluginManifest manifest;
        ShellError windowError = {};
        TempStr manifestPath = JoinPathTemp(root, Str(kShellManifestFile));
        if (manifestPath && PlatFileExists(manifestPath.s) &&
            PluginManifestRead(root, &manifest, &windowError) &&
            manifest.hasWindow) {
            options.dipW = (int)manifest.windowW;
            options.dipH = (int)manifest.windowH;
        }
        ShellErrorClear(&windowError);
    }
    MountRequest request;
    request.runtime = runtime;
    request.application = application;
    request.loadError = loadError;
    // A catalog whose components require a particular window root opens the
    // window itself; see ComponentWindowOpener.
    Window* window =
        ShellOpenWindow(app, components, options, &BuildShellRoot, &request);
    StrFree(title);
    LoadedApplicationFree(application);
    if (!window) {
        fprintf(stderr, "gpui-shell: could not open a window\n");
        if (runtime) runtime->Release();
        StrFree(loadError);
        AppFree(app);
        ShellErrorClear(&error);
        ShellSetDevelopmentMode(false);
        return 1;
    }
    ShellErrorClear(&error);
    if (policy) RefreshTypes(root, policy, false);
    if (invocation.watch) {
        if (request.view.IsValid()) {
            ShellWatcher::Start(runtime, request.view, root, entry, window, app,
                                &error);
        } else {
            // A failed load never produced a view to reload into.
            fprintf(stderr,
                    "--watch is inactive: the application did not load, so "
                    "there is no view to reload into. Fix the declared entry "
                    "and start gpui-shell again.\n");
        }
        if (error.IsSet()) {
            fprintf(stderr, "gpui-shell: --watch is inactive: ");
            Print(error.message, stderr);
            fputc('\n', stderr);
            ShellErrorClear(&error);
        }
    }
    gActivateWindow = window;
    ExecPost(MkFunc0Void(&ActivatePosted));
    ShellOnExitRequest(OnExitRequest);
    if (runtime) runtime->Release();
    StrFree(loadError);
    int status = AppRun(app);
    ShellOnExitRequest(nullptr);
    AppFree(app);
    ShellErrorClear(&error);
    ShellSetDevelopmentMode(false);
    return status;
}

// A failure before the application loads. A check reports it the way it
// reports a failed load, so its caller finds `check failed:` either way
// (host.rs CheckOutcome::report).
static void ReportSetupFailure(const Invocation& invocation, Str message) {
    if (invocation.kind == InvocationKind::Check) {
        Print(message, stderr);
        fprintf(stderr, "\n\ncheck failed: ");
        Print(invocation.directory, stderr);
        fputc('\n', stderr);
        return;
    }
    fprintf(stderr, "gpui-shell: ");
    Print(message, stderr);
    fputc('\n', stderr);
}

int GpuiMain(int argc, char** argv) {
    Invocation invocation;
    Str parseError;
    if (!ShellParseInvocation(argv + 1, argc - 1, &invocation, &parseError)) {
        fprintf(stderr, "gpui-shell: ");
        Print(parseError, stderr);
        fprintf(stderr,
                "\nTry `gpui-shell --help` for the accepted arguments.\n");
        StrFree(parseError);
        return 2;
    }
    if (invocation.kind == InvocationKind::Help) {
        PrintHelp();
        return 0;
    }
    if (invocation.kind == InvocationKind::Version) {
        PrintVersion();
        return 0;
    }
    if (invocation.kind == InvocationKind::List) return ShellListApplications();
    if (invocation.kind == InvocationKind::Agents &&
        invocation.application == ApplicationKind::None) {
        const FrozenComponentRegistry* components =
            component_shell::Components();
        Str text = ShellAgentsText(components);
        Print(text);
        if (text && (len(text) == 0 || text.s[len(text) - 1] != '\n'))
            fputc('\n', stdout);
        StrFree(text);
        return 0;
    }
    if (invocation.kind == InvocationKind::New) {
        const FrozenComponentRegistry* components =
            component_shell::Components();
        Str created;
        Str createError;
        if (!ShellCreateApplication(invocation.directory, components, &created,
                                    &createError)) {
            fprintf(stderr, "gpui-shell: ");
            Print(createError, stderr);
            fputc('\n', stderr);
            StrFree(createError);
            return 1;
        }
        printf("created ");
        Print(created);
        printf("\n\n");
        printf("  gpui-shell run -d %s --watch   # run it, reloading on save\n",
               invocation.directory.s ? invocation.directory.s : "");
        printf(
            "  gpui-shell check -d %s         # check it after every change\n",
            invocation.directory.s ? invocation.directory.s : "");
        printf("\n");
        Str apps = ShellApplicationsDirectory();
        printf("To install it, copy or link it into %s.\n",
               apps.s ? apps.s : "");
        StrFree(apps);
        StrFree(created);
        return 0;
    }
    if (invocation.kind == InvocationKind::Bare &&
        !ShellBundledApplication(nullptr)) {
        PrintHelp();
        return 0;
    }

    ShellError error = {};
    Str resolved;
    if (!ShellResolveApplication(invocation, &resolved, &error.message)) {
        if (invocation.kind == InvocationKind::Check) {
            ReportSetupFailure(invocation, error.message);
        } else {
            fprintf(stderr, "gpui-shell: ");
            Print(error.message, stderr);
            fputc('\n', stderr);
        }
        ShellErrorClear(&error);
        return 1;
    }
    // Resolve hands back the directory. The entry name still comes from the
    // manifest, which ResolveRoot reads.
    Str root;
    Str entry;
    if (!ResolveRoot(resolved, invocation.kind != InvocationKind::Types, &root,
                     &entry, &error)) {
        ReportSetupFailure(invocation, error.message);
        StrFree(resolved);
        ShellErrorClear(&error);
        return 1;
    }
    StrFree(resolved);
    if (invocation.kind == InvocationKind::Agents) {
        const FrozenComponentRegistry* components =
            component_shell::Components();
        Str written;
        Str agentsError;
        if (!ShellWriteAgents(root, components, &written, &agentsError)) {
            fprintf(stderr, "gpui-shell: ");
            Print(agentsError, stderr);
            fputc('\n', stderr);
            StrFree(agentsError);
            StrFree(root);
            return 1;
        }
        if (written) {
            printf("wrote ");
            Print(written);
            fputc('\n', stdout);
            StrFree(written);
        } else {
            printf("%s%cAGENTS.md is up to date\n", root.s,
                   root.s[len(root) - 1] == '/' || root.s[len(root) - 1] == '\\'
                       ? root.s[len(root) - 1]
                       : (GPUI_OS_WINDOWS ? '\\' : '/'));
        }
        StrFree(root);
        return 0;
    }
    if (invocation.kind == InvocationKind::Types) {
        Policy* policy = PolicyDefault();
        int status = RefreshTypes(root, policy, true) ? 0 : 1;
        // The declarations describe the runtime; the manifest's Git
        // dependencies are the rest of what the editor has to resolve, and a
        // command that wrote half of it and said nothing would leave the
        // import underlined with no explanation.
        Str linkError = {};
        if (status == 0 &&
            !ShellWriteDependencyLinks(root, nullptr, &linkError)) {
            fprintf(stderr, "gpui-shell: ");
            Print(linkError, stderr);
            fputc('\n', stderr);
            status = 1;
        }
        StrFree(linkError);
        PolicyRelease(policy);
        StrFree(root);
        ShellErrorClear(&error);
        return status;
    }

    bool installed = invocation.application == ApplicationKind::Installed ||
                     invocation.application == ApplicationKind::Bundled;
    PluginManifest manifest;
    ShellError manifestError = {};
    TempStr manifestPath = JoinPathTemp(root, Str(kShellManifestFile));
    bool hasManifest = manifestPath && PlatFileExists(manifestPath.s);
    bool manifestOk =
        !hasManifest || PluginManifestRead(root, &manifest, &manifestError);
    if (invocation.kind == InvocationKind::Check && !manifestOk) {
        ReportSetupFailure(invocation, manifestError.message);
        StrFree(root);
        ShellErrorClear(&manifestError);
        ShellErrorClear(&error);
        return 1;
    }

    Policy* policy = nullptr;
    if (manifestOk) {
        policy = LocalPolicy(root, installed, &error);
        if (!policy) {
            ReportSetupFailure(invocation, error.message);
            StrFree(root);
            ShellErrorClear(&error);
            ShellErrorClear(&manifestError);
            return 1;
        }
    }

    int status = 0;
    if (invocation.kind == InvocationKind::Check) {
        RefreshTypes(root, policy, false);
        status = Check(root, entry, invocation.printSpec, policy);
    } else {
        status = Run(root, entry, invocation, policy,
                     manifestOk ? Str{} : manifestError.message);
    }
    if (policy) PolicyRelease(policy);
    StrFree(root);
    ShellErrorClear(&manifestError);
    ShellErrorClear(&error);
    return status;
}
