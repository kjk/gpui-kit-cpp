#include "shell/storage.h"

#if GPUI_OS_WINDOWS

#include <windows.h>

namespace gpui::shell {

bool StorageReplaceFile(Str temporary, Str path, Str* error) {
    WCHAR* source = ToCWstrTemp(temporary);
    int sourceLen = (int)wcslen(source);
    WCHAR* sourceCopy = AllocArray<WCHAR>(sourceLen + 1);
    if (sourceCopy)
        memcpy(sourceCopy, source, (size_t)(sourceLen + 1) * sizeof(WCHAR));
    WCHAR* destination = ToCWstrTemp(path);
    bool ok = sourceCopy && MoveFileExW(sourceCopy, destination,
                                        MOVEFILE_REPLACE_EXISTING |
                                            MOVEFILE_WRITE_THROUGH) != 0;
    DWORD code = ok ? 0 : GetLastError();
    Free(nullptr, sourceCopy);
    if (!ok && error) {
        StrFree(*error);
        *error = StrDup(
            fmt("cannot atomically replace storage file `%s`: Windows error %u",
                path, code));
    }
    return ok;
}

} // namespace gpui::shell
#endif
