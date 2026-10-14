// crates/shell/src/host.rs: `parse`, the help text, and how the host opens
// its window.

#include "shell/host.h"

#include "shell/plugin.h"

namespace gpui::shell {

static bool IsApplicationName(Str argument) {
    if (!argument || StrEq(argument, StrL(".")) || StrEq(argument, StrL("..")))
        return false;
    if (argument.s[0] == '~') return false;
    for (int i = 0; i < len(argument); i++) {
        if (argument.s[i] == '/' || argument.s[i] == '\\') return false;
    }
    return true;
}

static bool TakeApplication(Invocation* out, Str argument, Str* error) {
    if (out->application != ApplicationKind::None) {
        *error =
            StrDup(fmt("unexpected argument `%s`; gpui-shell runs one "
                       "application",
                       argument));
        return false;
    }
    if (IsApplicationName(argument)) {
        out->application = ApplicationKind::Installed;
    } else {
        out->application = ApplicationKind::Path;
    }
    out->directory = argument;
    return true;
}

static const char* kHelpBody =
    "\n\nUsage: gpui-shell dev [<name> | --dir <directory>] [--dev]\n"
    "       gpui-shell run [<name> | --dir <directory>] [--watch] [--dev] "
    "[--packaged]\n"
    "       gpui-shell new <directory>\n"
    "       gpui-shell list\n"
    "       gpui-shell check [<name> | --dir <directory>] [--print-spec]\n"
    "       gpui-shell types [<name> | --dir <directory>]\n"
    "       gpui-shell agents [<name> | --dir <directory>]\n"
    "       gpui-shell bundle [<name> | --dir <directory>] [--platform "
    "<platform>]\n"
    "              [--icon <png>] [--out <directory>] [--download-url "
    "<url>]\n"
    "\n"
    "Arguments:\n"
    "  <name>       An installed application: the directory of that name in\n"
    "               %s. An argument containing a path\n"
    "               separator is a path instead, as if given with --dir.\n"
    "\n"
    "`run`, `dev`, `check`, `types` and `bundle` with no application use the\n"
    "current directory. With no arguments at all, gpui-shell runs the "
    "application\n"
    "packaged beside its executable (`app` beside the executable, or "
    "`../Resources/app`\n"
    "inside a macOS bundle), and prints this help when there is none.\n"
    "\n"
    "Commands:\n"
    "  dev          Run an application while you edit it: reload it when its\n"
    "               sources change, and log at debug level.\n"
    "  run          Run one application in a window.\n"
    "  new          Create a blank application in a new directory, relative "
    "to\n"
    "               where the command runs: manifest, entry module, editor "
    "types\n"
    "               and AGENTS.md.\n"
    "  list         Print the installed applications — id, name, version and\n"
    "               requested capabilities — without running any.\n"
    "  agents       Print the development instructions a coding agent needs "
    "to write\n"
    "               an application for this runtime, or write them into the\n"
    "               application's AGENTS.md (only ever replacing one it "
    "generated).\n"
    "  types        Write gpui-kit.d.ts next to the application, so an editor "
    "— or a\n"
    "               model writing the code — sees the whole API and catches "
    "a\n"
    "               mistyped style method before it runs.\n"
    "  check        Load the application, build its description, and "
    "materialize its\n"
    "               eager native elements in a hidden window. Exit 0 on "
    "success or\n"
    "               1 on load, render, or registered-component "
    "materialization errors.\n"
    "               Does not check layout, paint, deferred slots, nested "
    "view renders,\n"
    "               or later interaction and asynchronous states.\n"
    "  bundle       Package the application as a desktop application. Needs\n"
    "               gpui-shell.json. macOS: an unsigned .app. Linux: a "
    ".tar.gz, and an\n"
    "               install.sh beside it that installs it. Both hold no "
    "runtime:\n"
    "               they use an installed gpui-shell %s or later, or\n"
    "               download that release. Windows: a portable .zip whose "
    ".exe is\n"
    "               gpui-shell %s itself.\n"
    "\n"
    "Options:\n"
    "  -d, --dir <directory>\n"
    "               The application root, or the main.js inside it.\n"
    "  --packaged   With run, run the directory as a packaged application, "
    "the way a\n"
    "               bundle's launcher does: its data stays under the "
    "manifest's id\n"
    "               wherever it is, and no editor files are written into "
    "it.\n"
    "  --watch      With run, reload the application when its sources "
    "change.\n"
    "               `dev` always does.\n"
    "  --dev        With run or dev, development mode: relax the sandbox, "
    "opening\n"
    "               eval and the built-in prototypes. Capabilities are "
    "unchanged.\n"
    "  --print-spec With check, also print the element description that was "
    "built.\n"
    "  --platform <platform>\n"
    "               With bundle, the platform: macos, linux or windows. "
    "Defaults to\n"
    "               the one this runs on.\n"
    "  --icon <png> With bundle, a square PNG icon, ideally 1024×1024.\n"
    "  --out <directory>\n"
    "               With bundle, where the bundle is written. Defaults to "
    "./dist.\n"
    "  --download-url <url>\n"
    "               With bundle for Linux, where the archive will be "
    "published, so\n"
    "               install.sh run on its own downloads it from there.\n"
    "  --help       Print this message and exit.\n"
    "  --version    Print the version and exit.\n";

void ShellFormatHelp(StrBuilder* out) {
    if (!out) return;
    out->Append(fmt("gpui-shell %s", Str(kShellVersion)));
    Str apps = ShellApplicationsDirectory();
    Str version = Str(kShellVersion);
    out->Append(fmt(kHelpBody, apps ? apps : Str{}, version, version));
    StrFree(apps);
}

// The value after a flag that takes one. A following flag is never the value.
static bool FlagValue(const char* const* arguments, int count, int* index,
                      const char* flag, const char* expects, Str* value,
                      Str* error) {
    if (*index + 1 >= count || !arguments[*index + 1] ||
        arguments[*index + 1][0] == '-') {
        *error = StrDup(fmt("`%s` expects %s", Str(flag), Str(expects)));
        return false;
    }
    (*index)++;
    *value = Str(arguments[*index]);
    return true;
}

static void NoteBundleFlag(const char** first, const char* flag) {
    if (!*first) *first = flag;
}

bool ShellParseInvocation(const char* const* arguments, int count,
                          Invocation* out, Str* error) {
    *out = {};
    StrFree(*error);
    *error = {};
    // Answered before anything else can fail. A caller who mistyped one flag
    // is exactly the caller who needs `--help` to still work.
    for (int i = 0; i < count; i++) {
        Str argument = Str(arguments[i]);
        if (StrEq(argument, StrL("--help")) || StrEq(argument, StrL("-h"))) {
            out->kind = InvocationKind::Help;
            return true;
        }
    }
    for (int i = 0; i < count; i++) {
        Str argument = Str(arguments[i]);
        if (StrEq(argument, StrL("--version")) || StrEq(argument, StrL("-V"))) {
            out->kind = InvocationKind::Version;
            return true;
        }
    }
    bool command = false;
    bool dev = false;
    const char* bundleFlag = nullptr;
    for (int i = 0; i < count; i++) {
        Str argument = Str(arguments[i]);
        bool bareCommand =
            !command && out->application == ApplicationKind::None;
        if (bareCommand && StrEq(argument, StrL("new"))) {
            out->kind = InvocationKind::New;
            command = true;
        } else if (bareCommand && StrEq(argument, StrL("run"))) {
            out->kind = InvocationKind::Run;
            command = true;
        } else if (bareCommand && StrEq(argument, StrL("dev"))) {
            out->kind = InvocationKind::Run;
            dev = true;
            command = true;
        } else if (bareCommand && StrEq(argument, StrL("check"))) {
            out->kind = InvocationKind::Check;
            command = true;
        } else if (bareCommand && StrEq(argument, StrL("types"))) {
            out->kind = InvocationKind::Types;
            command = true;
        } else if (bareCommand && StrEq(argument, StrL("list"))) {
            out->kind = InvocationKind::List;
            command = true;
        } else if (bareCommand && StrEq(argument, StrL("agents"))) {
            out->kind = InvocationKind::Agents;
            command = true;
        } else if (bareCommand && StrEq(argument, StrL("bundle"))) {
            out->kind = InvocationKind::Bundle;
            command = true;
        } else if (StrEq(argument, StrL("--platform"))) {
            Str value;
            if (!FlagValue(arguments, count, &i, "--platform",
                           "macos, linux or windows", &value, error))
                return false;
            if (!BundlePlatformParse(value, &out->platform)) {
                *error =
                    StrDup(fmt("unknown platform `%s`; `--platform` is "
                               "macos, linux or windows",
                               value));
                return false;
            }
            out->platformSet = true;
            NoteBundleFlag(&bundleFlag, "--platform");
        } else if (StrEq(argument, StrL("--icon"))) {
            if (!FlagValue(arguments, count, &i, "--icon", "a PNG image",
                           &out->icon, error))
                return false;
            NoteBundleFlag(&bundleFlag, "--icon");
        } else if (StrEq(argument, StrL("--download-url"))) {
            Str value;
            if (!FlagValue(arguments, count, &i, "--download-url", "a URL",
                           &value, error))
                return false;
            if (!StrStartsWith(value, StrL("https://")) &&
                !StrStartsWith(value, StrL("http://"))) {
                *error =
                    StrDup(fmt("`--download-url` expects an http(s) URL, "
                               "not `%s`",
                               value));
                return false;
            }
            out->downloadUrl = value;
            NoteBundleFlag(&bundleFlag, "--download-url");
        } else if (StrEq(argument, StrL("--out"))) {
            if (!FlagValue(arguments, count, &i, "--out", "a directory",
                           &out->out, error))
                return false;
            NoteBundleFlag(&bundleFlag, "--out");
        } else if (StrEq(argument, StrL("--dir")) ||
                   StrEq(argument, StrL("-d"))) {
            if (i + 1 >= count || !arguments[i + 1] ||
                arguments[i + 1][0] == '-') {
                *error =
                    StrDup(StrL("`--dir` expects an application directory"));
                return false;
            }
            i++;
            if (!TakeApplication(out, Str(arguments[i]), error)) return false;
            // --dir is a path even when the text would read as a name.
            out->application = ApplicationKind::Path;
        } else if (StrEq(argument, StrL("--watch"))) {
            out->watch = true;
        } else if (StrEq(argument, StrL("--packaged"))) {
            out->packaged = true;
        } else if (StrEq(argument, StrL("--dev"))) {
            out->development = true;
        } else if (StrEq(argument, StrL("--print-spec"))) {
            out->printSpec = true;
        } else if (len(argument) > 0 && argument.s[0] == '-') {
            *error = StrDup(fmt("unknown flag `%s`", argument));
            return false;
        } else if (!TakeApplication(out, argument, error)) {
            return false;
        }
    }
    if (bundleFlag && out->kind != InvocationKind::Bundle) {
        *error = StrDup(fmt("`%s` is only for `bundle`", Str(bundleFlag)));
        return false;
    }
    // No command word is allowed to carry --watch and --dev: that line is
    // the packaged application. Any other command has to be run or dev.
    bool watchOk = !command || dev || out->kind == InvocationKind::Run;
    if ((out->watch || out->development) && !watchOk) {
        *error = StrDup(fmt("`%s` is only for `run` and `dev`",
                            out->watch ? StrL("--watch") : StrL("--dev")));
        return false;
    }
    if (out->packaged &&
        !(command && !dev && out->kind == InvocationKind::Run)) {
        *error = StrDup(StrL("`--packaged` is only for `run`"));
        return false;
    }
    if (out->kind == InvocationKind::Bundle) {
        if (out->application == ApplicationKind::None)
            out->application = ApplicationKind::Current;
        return true;
    }
    if (out->kind == InvocationKind::List) {
        if (out->application != ApplicationKind::None) {
            *error = StrDup(StrL("`list` takes no application"));
            return false;
        }
        return true;
    }
    if (out->kind == InvocationKind::Agents) return true;
    if (out->kind == InvocationKind::New) {
        if (out->application == ApplicationKind::None) {
            *error = StrDup(StrL("`new` expects the directory to create"));
            return false;
        }
        // A bare name is a new directory here, the way `cargo new` reads it,
        // not an installed application.
        out->application = ApplicationKind::Path;
        return true;
    }
    // `kind` defaults to Run, so this branch is only an explicit command.
    // `dev` is that run, reloading, with the debug log. The sandbox stays as
    // shipped unless `--dev` asked otherwise.
    if (command && (out->kind == InvocationKind::Types ||
                    out->kind == InvocationKind::Check ||
                    out->kind == InvocationKind::Run)) {
        if (dev) {
            out->watch = true;
            out->verbose = true;
            out->packaged = false;
        }
        if (out->application == ApplicationKind::None)
            out->application = ApplicationKind::Current;
        return true;
    }
    // No command. Nothing named is the packaged application. A named
    // application still needs `run`.
    if (out->application == ApplicationKind::None) {
        out->kind = InvocationKind::Bare;
        out->application = ApplicationKind::Bundled;
        return true;
    }
    TempStr shown = out->application == ApplicationKind::Installed
                        ? StrDupTemp(out->directory)
                        : fmt("-d %s", out->directory);
    *error = StrDup(
        fmt("an application runs with `gpui-shell run %s`", Str(shown.s)));
    return false;
}

Window* ShellOpenWindow(App* app, const FrozenComponentRegistry* components,
                        const ComponentWindowOptions& options,
                        ComponentWindowBuild build, void* data) {
    if (!app || !build) return nullptr;
    // A catalog whose components require a particular window root opens the
    // window itself. Everything after this only wants the window, so it is
    // the same pointer either way.
    ComponentWindowOpener open =
        components ? components->WindowOpener() : nullptr;
    if (open) return open(app, options, build, data);
    Window* window = WindowOpen(app, options.title, options.dipW, options.dipH,
                                options.opts);
    if (!window) return nullptr;
    window->root = build(window, app, data);
    AppInvalidate(window);
    return window;
}

} // namespace gpui::shell
