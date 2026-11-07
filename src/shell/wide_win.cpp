#include "shell/wide.h"

#include <windows.h>

namespace gpui::shell {

wchar_t* WideDup(Str value) {
    if (!value.s || len(value) < 0) return nullptr;
    for (int i = 0; i < len(value); i++) {
        if (value.s[i] == 0) return nullptr;
    }
    int count = len(value) == 0
                    ? 0
                    : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          value.s, len(value), nullptr, 0);
    if (len(value) > 0 && count <= 0) return nullptr;
    WCHAR* result = AllocArray<WCHAR>(count + 1);
    if (!result) return nullptr;
    if (count > 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.s,
                                         len(value), result, count) != count) {
        Free(nullptr, result);
        return nullptr;
    }
    result[count] = 0;
    return result;
}

Str Utf8Dup(const wchar_t* value, int count) {
    int bytes = WideCharToMultiByte(CP_UTF8, 0, value, count, nullptr, 0,
                                    nullptr, nullptr);
    if (bytes <= 0) return {};
    char* result = AllocArray<char>(bytes + 1);
    if (!result) return {};
    if (WideCharToMultiByte(CP_UTF8, 0, value, count, result, bytes, nullptr,
                            nullptr) != bytes) {
        Free(nullptr, result);
        return {};
    }
    result[bytes] = 0;
    return Str(result, bytes);
}

} // namespace gpui::shell
