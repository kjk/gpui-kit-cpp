#include "shell/filesystem.h"

namespace gpui::shell {

bool FsRun(FsOperation, Str root, Str relative, Str, bool, FsResult*,
           Str* error) {
    if (error)
        *error = StrDup(
            fmt("filesystem mutation `%s/%s` is unavailable in a browser", root,
                relative));
    return false;
}

} // namespace gpui::shell
