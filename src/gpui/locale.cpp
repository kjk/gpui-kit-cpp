#include "gpui/platform.h"

namespace gpui {

// sys_locale::get_locale: get_locales().next()
TempStr PlatPreferredLocaleTemp() {
    Str all = PlatPreferredLocalesTemp();
    for (int i = 0; i < len(all); i++) {
        if (all.s[i] == '\n') {
            return Str(all.s, i);
        }
    }
    return all;
}

void PlatLocaleListAdd(StrBuilder* out, Str locale) {
    if (len(locale) == 0) {
        return;
    }
    int start = 0;
    for (int i = 0; i <= len(out); i++) {
        if (i < len(out) && out->els[i] != '\n') {
            continue;
        }
        if (StrEq(Str(out->els + start, i - start), locale)) {
            return;
        }
        start = i + 1;
    }
    if (len(out) > 0) {
        out->AppendChar('\n');
    }
    out->Append(locale);
}

} // namespace gpui
