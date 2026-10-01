#ifndef GPUI_SHELL_HOST_H_
#define GPUI_SHELL_HOST_H_

#include "shell/component_registry.h"

// crates/shell/src/host.rs: the command line the shipped host (gpui_shell/
// main.cpp) accepts, parsed without starting anything, so the host and its
// tests share one parser. Rust's `parse_invocation` is what a wrapper binary
// tests; the C++ host has one name, gpui-shell, so there is no HostBrand.

namespace gpui::shell {

// host.rs `Invocation`. Rust answers --help and --version as Print(text);
// here they are kinds, and the host prints the text.
enum class InvocationKind : uint8_t {
    Run,
    Check,
    Types,
    Help,
    Version,
};

struct Invocation {
    InvocationKind kind = InvocationKind::Run;
    // Borrowed from the arguments.
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
