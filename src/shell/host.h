#ifndef GPUI_SHELL_HOST_H_
#define GPUI_SHELL_HOST_H_

#include "shell/component_registry.h"

// crates/shell/src/host.rs and host/: the command line the shipped host
// (gpui_shell/main.cpp) accepts, parsed without starting anything, plus the
// installed-application directory, the packaged application, `new`, `list`
// and `agents`. Rust's `parse_invocation` is what a wrapper binary tests; the
// C++ host has one name, gpui-shell, so there is no HostBrand.

namespace gpui::shell {

// host.rs `Invocation`, with --help and --version kept as kinds. Rust answers
// them as Print(text); the host prints that text.
enum class InvocationKind : uint8_t {
    Run,
    // No command: the application packaged beside the executable, or the help
    // when there is none.
    Bare,
    List,
    Check,
    Types,
    Agents,
    New,
    Help,
    Version,
};

// Which application a command line names. Left unresolved so parsing stays
// free of the home directory and of where the executable is.
enum class ApplicationKind : uint8_t {
    None,
    // A directory name under ~/.gpui-shell/apps.
    Installed,
    // A directory, or the entry file inside it, given as a path.
    Path,
    // Nothing named: the application packaged beside the executable.
    Bundled,
    // `run`, `check` or `types` with no application: the working directory.
    Current,
};

struct Invocation {
    InvocationKind kind = InvocationKind::Run;
    ApplicationKind application = ApplicationKind::None;
    // Borrowed from the arguments. The installed name, the path, or the
    // directory `new` creates. Empty when the command names none.
    Str directory;
    bool watch = false;
    bool development = false;
    bool printSpec = false;
};

// host.rs `parse`: the arguments after the program name. False with the
// exact sentence for stderr in `error` (owned, StrFree) when the command line
// cannot be acted on.
bool ShellParseInvocation(const char* const* arguments, int count,
                          Invocation* out, Str* error);

// The help text, including the version line. `out` is appended to.
void ShellFormatHelp(StrBuilder* out);

// ~/.gpui-shell, or GPUI_SHELL_HOME when that variable is set. A relative
// shell home is made absolute against the working directory, the way
// `std::path::absolute` does for the applications directory. Owned.
Str ShellHome();
// The same, with the environment supplied so a test does not read the
// process. An empty shell-home variable means unset.
Str ShellHomeFrom(Str shellHome, Str home, Str userProfile);
// <shell home>/apps. Owned.
Str ShellApplicationsDirectory();

// The application packaged beside the executable: ../Resources/app, then
// app. False when neither holds main.js or gpui-shell.json.
bool ShellBundledApplication(Str* directory);

// The directory a parsed application runs in. Owned. False with the sentence
// in `error` when an installed name is missing or nothing is packaged.
bool ShellResolveApplication(const Invocation& invocation, Str* directory,
                             Str* error);

// `list`. Prints to stdout and returns 0, or 1 when nothing is installed.
int ShellListApplications();

// `new`. `created` is the canonical directory, owned. False with the sentence
// in `error` when the directory exists and is not empty, or a write fails.
bool ShellCreateApplication(Str directory,
                            const FrozenComponentRegistry* components,
                            Str* created, Str* error);

// `agents` with no application: the instructions, owned.
Str ShellAgentsText(const FrozenComponentRegistry* components);
// `agents <application>`. `written` is the path when the file changed, empty
// when it was already current. False when a hand-written file is left alone
// or the directory cannot be written; the sentence is in `error`.
bool ShellWriteAgents(Str directory, const FrozenComponentRegistry* components,
                      Str* written, Str* error);

// host.rs, where the host opens its window: through the catalog's opener
// when it registered one (ComponentWindowOpener), otherwise an ordinary
// window whose root view is what `build` answers — the ShellRoot a script
// application mounts in. `build` runs once, with the window already open.
// Null when no window could be opened.
Window* ShellOpenWindow(App* app, const FrozenComponentRegistry* components,
                        const ComponentWindowOptions& options,
                        ComponentWindowBuild build, void* data);

} // namespace gpui::shell

#endif // GPUI_SHELL_HOST_H_
