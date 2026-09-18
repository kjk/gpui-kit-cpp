#include "gpui.h"

#include <stdio.h>
#include <string.h>

using namespace gpui;
using namespace gpui::shell;

enum class CommandKind : uint8_t {
    Run,
    Check,
    Types,
    Help,
    Version,
};

struct Invocation {
    CommandKind kind = CommandKind::Run;
    Str directory;
    bool watch = false;
    bool development = false;
    bool printSpec = false;
};

static void Print(Str value, FILE* file = stdout) {
    if (value) fwrite(value.s, 1, (size_t)len(value), file);
}

static void PrintVersion() {
    printf("gpui-shell %s\n", kShellVersion);
}

static void PrintHelp() {
    PrintVersion();
    printf(
        "\nUsage: gpui-shell <directory> [options]\n"
        "       gpui-shell check <directory> [--print-spec]\n"
        "       gpui-shell types <directory>\n\n"
        "Arguments:\n"
        "  <directory>  The application root, or the main.js inside it.\n\n"
        "Commands:\n"
        "  types        Write gpui.d.ts beside scripts that import Shell "
        "modules, and\n"
        "               link the manifest's Git dependencies into "
        "node_modules.\n"
        "  check        Load and render once without opening a window.\n\n"
        "Options:\n"
        "  --watch      Reload when application sources change.\n"
        "  --dev        Enable development mode and imply --watch.\n"
        "  --print-spec With check, print the rendered element description.\n"
        "  --help       Print this message and exit.\n"
        "  --version    Print the version and exit.\n");
}

static bool Parse(int argc, char** argv, Invocation* out, Str* error) {
    *out = {};
    StrFree(*error);
    *error = {};
    for (int i = 1; i < argc; i++) {
        Str argument = Str(argv[i]);
        if (StrEq(argument, StrL("--help")) || StrEq(argument, StrL("-h"))) {
            out->kind = CommandKind::Help;
            return true;
        }
        if (StrEq(argument, StrL("--version")) || StrEq(argument, StrL("-V"))) {
            out->kind = CommandKind::Version;
            return true;
        }
    }
    bool command = false;
    for (int i = 1; i < argc; i++) {
        Str argument = Str(argv[i]);
        if (!command && !out->directory && StrEq(argument, StrL("check"))) {
            out->kind = CommandKind::Check;
            command = true;
        } else if (!command && !out->directory &&
                   StrEq(argument, StrL("types"))) {
            out->kind = CommandKind::Types;
            command = true;
        } else if (StrEq(argument, StrL("--watch"))) {
            out->watch = true;
        } else if (StrEq(argument, StrL("--dev"))) {
            out->development = true;
            out->watch = true;
        } else if (StrEq(argument, StrL("--print-spec"))) {
            out->printSpec = true;
        } else if (argument.s[0] == '-') {
            *error = StrDup(fmt("unknown flag `%s`", argument));
            return false;
        } else if (!out->directory) {
            out->directory = argument;
        } else {
            *error =
                StrDup(fmt("unexpected argument `%s`; gpui-shell runs one "
                           "application directory",
                           argument));
            return false;
        }
    }
    if (!out->directory) {
        *error = StrDup(StrL("expected an application directory"));
        return false;
    }
    return true;
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

static Policy* LocalPolicy(Str root, ShellError* error) {
    PluginManifest manifest;
    TempStr manifestPath = JoinPathTemp(root, Str(kShellManifestFile));
    bool hasManifest = manifestPath && PlatFileExists(manifestPath.s);
    if (hasManifest && !PluginManifestRead(root, &manifest, error))
        return nullptr;

    Arena* scratch = ArenaNew();
    Str id = ShellBundleIdForPath(root);
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
    bool ok = ShellWriteTypeDeclarations(root, modules, &written, &error);
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
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
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

static int Run(Str root, Str entry, const Invocation& invocation,
               Policy* policy) {
    ShellSetDevelopmentMode(invocation.development);
    AppAssets assets(root);
    assets.Install();
    App* app = AppNew();
    if (!app) {
        fprintf(stderr, "gpui-shell: could not initialize the platform\n");
        ShellSetDevelopmentMode(false);
        return 1;
    }
    component::Init(app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(app, &error);
    ViewType* type =
        runtime ? runtime->LoadApp(root, entry, policy, &error) : nullptr;
    if (!type) {
        fprintf(stderr, "gpui-shell: ");
        Print(error.message, stderr);
        fputc('\n', stderr);
        if (runtime) runtime->Release();
        AppFree(app);
        ShellErrorClear(&error);
        ShellSetDevelopmentMode(false);
        return 1;
    }
    Str title = WindowTitle(root);
    Window* window = WindowOpen(app, title, 880, 720, WinOpts{});
    StrFree(title);
    if (!window) {
        fprintf(stderr, "gpui-shell: could not open a window\n");
        ViewTypeRelease(type);
        runtime->Release();
        AppFree(app);
        ShellSetDevelopmentMode(false);
        return 1;
    }
    Entity<ScriptView> view = ScriptView::New(app, runtime, type, policy);
    ViewTypeRelease(type);
    Entity<ShellRoot> shellRoot = ShellRoot::New(app, view.id);
    window->root = shellRoot.id;
    AppInvalidate(window);
    RefreshTypes(root, policy, false);
    if (invocation.watch) {
        ShellWatcher::Start(runtime, view, root, entry, window, app, &error);
        if (error.IsSet()) {
            fprintf(stderr, "gpui-shell: --watch is inactive: ");
            Print(error.message, stderr);
            fputc('\n', stderr);
            ShellErrorClear(&error);
        }
    }
    ShellOnExitRequest(OnExitRequest);
    runtime->Release();
    int status = AppRun(app);
    ShellOnExitRequest(nullptr);
    AppFree(app);
    ShellErrorClear(&error);
    ShellSetDevelopmentMode(false);
    return status;
}

int GpuiMain(int argc, char** argv) {
    Invocation invocation;
    Str parseError;
    if (!Parse(argc, argv, &invocation, &parseError)) {
        fprintf(stderr, "gpui-shell: ");
        Print(parseError, stderr);
        fprintf(stderr,
                "\nTry `gpui-shell --help` for the accepted arguments.\n");
        StrFree(parseError);
        return 2;
    }
    if (invocation.kind == CommandKind::Help) {
        PrintHelp();
        return 0;
    }
    if (invocation.kind == CommandKind::Version) {
        PrintVersion();
        return 0;
    }

    ShellError error = {};
    Str root;
    Str entry;
    if (!ResolveRoot(invocation.directory,
                     invocation.kind != CommandKind::Types, &root, &entry,
                     &error)) {
        fprintf(stderr, "gpui-shell: ");
        Print(error.message, stderr);
        fputc('\n', stderr);
        ShellErrorClear(&error);
        return 1;
    }
    if (invocation.kind == CommandKind::Types) {
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

    Policy* policy = LocalPolicy(root, &error);
    if (!policy) {
        fprintf(stderr, "gpui-shell: ");
        Print(error.message, stderr);
        fputc('\n', stderr);
        StrFree(root);
        ShellErrorClear(&error);
        return 1;
    }

    int status = 0;
    if (invocation.kind == CommandKind::Check) {
        RefreshTypes(root, policy, false);
        status = Check(root, entry, invocation.printSpec, policy);
    } else {
        status = Run(root, entry, invocation, policy);
    }
    PolicyRelease(policy);
    StrFree(root);
    ShellErrorClear(&error);
    return status;
}
