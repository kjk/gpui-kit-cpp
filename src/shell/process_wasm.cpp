#include "shell/process.h"

namespace gpui::shell {

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

uint32_t ProcessStreamPid(const ProcessStream*) {
    return 0;
}

void ProcessStreamPoll(ProcessStream*, Vec<ProcessStreamEvent>*) {}

bool ProcessStreamWrite(ProcessStream*, Str, Str* error) {
    if (error)
        *error = StrDup(StrL("running a process is unavailable in a browser"));
    return false;
}

void ProcessStreamEnd(ProcessStream*) {}

void ProcessStreamKill(ProcessStream*) {}

void ProcessStreamFree(ProcessStream*) {}

} // namespace gpui::shell
