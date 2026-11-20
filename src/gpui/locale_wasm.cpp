#include "gpui/platform.h"

#include <emscripten/emscripten.h>

namespace gpui {

// navigator.languages, one a line
EM_JS(void, GpJsPreferredLocales, (char* out, int cap), {
    const nav = globalThis.navigator;
    const list =
        nav ? (nav.languages && nav.languages.length ? nav.languages
                                                     : [nav.language || ""])
            : [];
    const bytes = new TextEncoder().encode(list.join(String.fromCharCode(10)));
    const count = Math.min(bytes.length, cap - 1);
    HEAPU8.set(bytes.subarray(0, count), out);
    HEAPU8[out + count] = 0;
});

TempStr PlatPreferredLocalesTemp() {
    char locales[1024] = {};
    GpJsPreferredLocales(locales, sizeof(locales));
    return StrDupTemp(Str(locales));
}

} // namespace gpui
