#include "shell/filesystem.h"

#include "shell/error.h"

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

bool FsOperationWrites(FsOperation operation) {
    return operation == FsOperation::Write ||
           operation == FsOperation::RemoveFile ||
           operation == FsOperation::RemoveDirectory ||
           operation == FsOperation::MakeDirectory;
}

bool FsSetLinkTarget(FsResult* result, Str target, Str* error) {
    result->bytes = StrDup(target);
    if (!result->bytes.s && len(target) != 0) {
        ErrorOutSet(error, StrL("allocating a symlink target failed"));
        return false;
    }
    return true;
}

} // namespace gpui::shell
