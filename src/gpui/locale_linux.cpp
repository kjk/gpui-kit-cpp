#include "gpui/platform.h"

#include <stdlib.h>
#include <string.h>

namespace gpui {

// sys-locale's unix.rs: $LANGUAGE's colon-separated list, then $LC_ALL,
// $LC_MESSAGES and $LANG
TempStr PlatPreferredLocalesTemp() {
    StrBuilder out(GetTempArena());
    const char* languages = getenv("LANGUAGE");
    while (languages && *languages) {
        const char* end = strchr(languages, ':');
        int n = end ? (int)(end - languages) : (int)strlen(languages);
        PlatLocaleListAdd(&out, Str(languages, n));
        languages = end ? end + 1 : nullptr;
    }
    const char* vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* name : vars) {
        const char* value = getenv(name);
        if (value && *value) {
            PlatLocaleListAdd(&out, Str(value));
        }
    }
    return out.TakeStr();
}

} // namespace gpui
