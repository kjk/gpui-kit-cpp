// crates/shell/src/host.rs `parse`

#include "shell/host.h"

namespace gpui::shell {

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
        if (!command && !out->directory && StrEq(argument, StrL("check"))) {
            out->kind = InvocationKind::Check;
            command = true;
        } else if (!command && !out->directory &&
                   StrEq(argument, StrL("types"))) {
            out->kind = InvocationKind::Types;
            command = true;
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

} // namespace gpui::shell
