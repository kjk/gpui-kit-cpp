#include "shell/process.h"

namespace gpui::shell {

bool ProcessRunBounded(Str command, const Str*, int, ProcessCancellation*,
                       ProcessOutput*, Str* error, const ProcessOptions*) {
    if (error)
        *error =
            StrDup(fmt("running `%s` is unavailable in a browser", command));
    return false;
}

} // namespace gpui::shell
