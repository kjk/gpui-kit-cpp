#include "shell/filesystem.h"

namespace gpui::shell {

void FsResult::Free() {
    StrFree(bytes);
    bytes = {};
    for (int i = 0; i < entries.len; i++) StrFree(entries[i].name);
    VecReset(entries);
    exists = false;
    hasStats = false;
    isFile = false;
    isDirectory = false;
    isSymbolicLink = false;
    size = 0;
    mode = 0;
    mtimeMs = 0;
    atimeMs = 0;
    birthtimeMs = 0;
}

} // namespace gpui::shell
