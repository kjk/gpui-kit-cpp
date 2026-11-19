#include "gpui/platform.h"

#include <sys/system_properties.h>

namespace gpui {

TempStr PlatPreferredLocaleTemp() {
    const char* keys[] = {"persist.sys.locale", "persist.sys.language",
                          "ro.product.locale", "ro.product.locale.language"};
    char locale[PROP_VALUE_MAX] = {};
    for (const char* key : keys) {
        int n = __system_property_get(key, locale);
        if (n > 0) return StrDupTemp(Str(locale, n));
    }
    return {};
}

} // namespace gpui
