#include "ui/i18n.h"

namespace gpui {

namespace component {

// The generated catalogue: locale_data.cpp.
extern const char* const kLocaleNames[];
extern const int kLocaleCount;
extern const LocaleRow kLocaleRows[];
extern const int kLocaleRowCount;

// The locale in force, as an index into kLocaleNames. Process-wide, the way
// the theme and the scrollbar mode are: Rust keeps it in a thread-local
// inside rust_i18n, and an App here is not a container for globals.
static int gLocale = 0;

int LocaleCount() {
    return kLocaleCount;
}

Str LocaleAt(int i) {
    if (i < 0 || i >= kLocaleCount) {
        return {};
    }
    return Str(kLocaleNames[i]);
}

int LocaleIndex(Str name) {
    for (int i = 0; i < kLocaleCount; i++) {
        if (base::StrEq(name, kLocaleNames[i])) {
            return i;
        }
    }
    return -1;
}

bool LocaleSet(Str name) {
    int ix = LocaleIndex(name);
    if (ix < 0) {
        return false;
    }
    gLocale = ix;
    return true;
}

Str LocaleNow() {
    return Str(kLocaleNames[gLocale]);
}

int LocaleRowCount() {
    return kLocaleRowCount;
}

const LocaleRow* LocaleRowAt(int i) {
    if (i < 0 || i >= kLocaleRowCount) {
        return nullptr;
    }
    return &kLocaleRows[i];
}

// The rows are written sorted by key, so this is a binary search rather than
// a walk: a frame asks for a handful of them and the table is small, but the
// order costs the generator nothing.
static const LocaleRow* FindRow(Str key) {
    int lo = 0;
    int hi = kLocaleRowCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = StrCmp(Str(kLocaleRows[mid].key), key);
        if (cmp == 0) {
            return &kLocaleRows[mid];
        }
        if (cmp < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return nullptr;
}

Str Tr(const char* key) {
    if (!key || !key[0]) {
        return {};
    }
    const LocaleRow* row = FindRow(Str(key));
    if (!row) {
        // rust_i18n answers with the key path, which is what makes a missing
        // one obvious on screen instead of blank.
        return Str(key);
    }
    const char* v = row->values[gLocale];
    if (!v) {
        // Not translated into this locale. English is the fallback and every
        // key has one, since that is the language the catalogue is written
        // in — `fallback` in rust_i18n's own configuration.
        v = row->values[0];
    }
    return v ? Str(v) : Str(key);
}

TempStr TrFmtTemp(const char* key, const TrArg* args, int nArgs) {
    Str pattern = Tr(key);
    StrBuilder out(GetTempArena());
    int i = 0;
    while (i < len(pattern)) {
        bool replaced = false;
        if (pattern.s[i] == '%' && i + 1 < len(pattern) &&
            pattern.s[i + 1] == '{') {
            Str rest = Str(pattern.s + i + 2, len(pattern) - i - 2);
            for (int a = 0; a < nArgs && !replaced; a++) {
                Str name = Str(args[a].name);
                if (StrStartsWith(rest, name) && len(rest) > len(name) &&
                    rest.s[len(name)] == '}') {
                    out.Append(args[a].value);
                    i += 2 + len(name) + 1;
                    replaced = true;
                }
            }
        }
        if (!replaced) {
            out.AppendChar(pattern.s[i]);
            i++;
        }
    }
    return Str(out.els, len(out));
}

} // namespace component
} // namespace gpui
