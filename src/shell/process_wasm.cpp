#include "shell/process.h"

namespace gpui::shell {

// Never made: ProcessStreamStart refuses in a browser. The shared
// accessors in process.cpp see only null.
struct ProcessStream : ProcessStreamCore {};

bool ProcessRunBounded(Str command, const Str*, int, ProcessCancellation*,
                       ProcessOutput*, Str* error, const ProcessOptions*) {
    if (error)
        *error =
            StrDup(fmt("running `%s` is unavailable in a browser", command));
    return false;
}

bool ProcessStreamStart(Str command, const Str*, int,
                        const ProcessSpawnOptions*, ProcessStream** out,
                        Str* error) {
    if (out) *out = nullptr;
    if (error)
        *error =
            StrDup(fmt("running `%s` is unavailable in a browser", command));
    return false;
}

ProcessStreamCore* ProcessStreamCoreOf(ProcessStream* stream) {
    return stream;
}

void ProcessStreamKill(ProcessStream*) {}

void ProcessStreamFree(ProcessStream*) {}

} // namespace gpui::shell
