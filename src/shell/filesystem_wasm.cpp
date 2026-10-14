#include "shell/filesystem.h"

namespace gpui::shell {

static bool Unavailable(Str root, Str relative, Str* error) {
    if (error)
        *error = StrDup(
            fmt("filesystem mutation `%s/%s` is unavailable in a browser", root,
                relative));
    return false;
}

bool FsRun(FsOperation, Str root, Str relative, Str, bool, FsResult*,
           Str* error) {
    return Unavailable(root, relative, error);
}

bool FsRename(Str fromRoot, Str fromRelative, Str, Str, Str* error) {
    return Unavailable(fromRoot, fromRelative, error);
}

} // namespace gpui::shell
