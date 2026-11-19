#include "gpui/platform.h"

#include <emscripten/emscripten.h>

namespace gpui {

EM_JS(void, GpJsPreferredLocale, (char* out, int cap), {
    const nav = globalThis.navigator;
    const locale = nav ? (nav.languages || [])[0] || nav.language || "" : "";
    const bytes = new TextEncoder().encode(locale);
    const count = Math.min(bytes.length, cap - 1);
    HEAPU8.set(bytes.subarray(0, count), out);
    HEAPU8[out + count] = 0;
});

TempStr PlatPreferredLocaleTemp() {
    char locale[256] = {};
    GpJsPreferredLocale(locale, sizeof(locale));
    return StrDupTemp(Str(locale));
}

} // namespace gpui
