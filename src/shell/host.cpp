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
    "\nUsage: gpui-shell run [<name> | --dir <directory>] [--watch] [--dev]\n"
    "       gpui-shell new <directory>\n"
    "       gpui-shell list\n"
    "       gpui-shell check [<name> | --dir <directory>] [--print-spec]\n"
    "       gpui-shell types [<name> | --dir <directory>]\n"
    "       gpui-shell agents [<name> | --dir <directory>]\n"
    "\n"
    "Arguments:\n"
    "  <name>       An installed application: the directory of that name in\n"
    "               %s. An argument containing a path\n"
    "               separator is a path instead, as if given with --dir.\n"
    "\n"
    "`run`, `check` and `types` with no application use the current "
    "directory.\n"
    "With no arguments at all, gpui-shell runs the application packaged "
    "beside its\n"
    "executable (`app` beside the executable, or `../Resources/app` inside a "
    "macOS bundle), and prints this help when there is none.\n"
    "\n"
    "Commands:\n"
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
    "\n"
    "Options:\n"
    "  -d, --dir <directory>\n"
    "               The application root, or the main.js inside it.\n"
    "  --watch      Reload the application when its sources change.\n"
    "  --dev        Development mode: implies --watch, and relaxes the "
    "sandbox.\n"
    "  --print-spec With check, also print the element description that was "
    "built.\n"
    "  --help       Print this message and exit.\n"
    "  --version    Print the version and exit.\n";

void ShellFormatHelp(StrBuilder* out) {
    if (!out) return;
    out->Append(fmt("gpui-shell %s", Str(kShellVersion)));
    Str apps = ShellApplicationsDirectory();
    out->Append(fmt(kHelpBody, apps ? apps : Str{}));
    StrFree(apps);
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
        } else if (StrEq(argument, StrL("--dev"))) {
            out->development = true;
            out->watch = true;
        } else if (StrEq(argument, StrL("--print-spec"))) {
            out->printSpec = true;
        } else if (len(argument) > 0 && argument.s[0] == '-') {
            *error = StrDup(fmt("unknown flag `%s`", argument));
            return false;
        } else if (!TakeApplication(out, argument, error)) {
            return false;
        }
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
    // No command word is the packaged application, or an error when a name
    // was given without `run`.
    if (command && (out->kind == InvocationKind::Types ||
                    out->kind == InvocationKind::Check ||
                    out->kind == InvocationKind::Run)) {
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
