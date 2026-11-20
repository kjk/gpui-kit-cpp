#include "gpui/platform.h"

namespace gpui {

TempStr PlatPreferredLocalesTemp() {
    ULONG languages = 0;
    ULONG size = 0;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &languages, nullptr,
                                     &size) ||
        size == 0 || size > 0x7fffffff / sizeof(WCHAR)) {
        return {};
    }
    auto* names = (WCHAR*)Alloc(GetTempArena(), (int)(size * sizeof(WCHAR)));
    if (!names ||
        !GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &languages, names,
                                     &size) ||
        languages == 0) {
        return {};
    }
    // a double-NUL-terminated list
    StrBuilder out(GetTempArena());
    for (const WCHAR* name = names; *name; name += wcslen(name) + 1) {
        char locale[256] = {};
        int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, -1,
                                    locale, sizeof(locale), nullptr, nullptr);
        if (n > 1) {
            PlatLocaleListAdd(&out, Str(locale, n - 1));
        }
    }
    return out.TakeStr();
}

} // namespace gpui
