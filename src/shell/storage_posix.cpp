#include "shell/storage.h"

#if !GPUI_OS_WINDOWS

#include <stdio.h>

namespace gpui::shell {

bool StorageReplaceFile(Str temporary, Str path, Str* error) {
    if (rename(temporary.s, path.s) == 0) return true;
    if (error) {
        StrFree(*error);
        *error =
            StrDup(fmt("cannot atomically replace storage file `%s`", path));
    }
    return false;
}

} // namespace gpui::shell
#endif
