#include "gpui/platform.h"

#include <stdlib.h>

namespace gpui {

TempStr PlatPreferredLocaleTemp() {
    const char* languages = getenv("LANGUAGE");
    if (languages) {
        while (*languages == ':') languages++;
        if (*languages) {
            const char* end = strchr(languages, ':');
            return StrDupTemp(end ? Str(languages, (int)(end - languages))
                                  : Str(languages));
        }
    }
    const char* vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* name : vars) {
        const char* value = getenv(name);
        if (value && *value) return StrDupTemp(Str(value));
    }
    return {};
}

} // namespace gpui
