#include "gpui/platform.h"

namespace gpui {

TempStr PlatPreferredLocaleTemp() {
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
    char locale[256] = {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, names, -1,
                                locale, sizeof(locale), nullptr, nullptr);
    return n > 1 ? StrDupTemp(Str(locale, n - 1)) : Str{};
}

} // namespace gpui
