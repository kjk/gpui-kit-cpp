#ifndef GPUI_UI_I18N_H_
#define GPUI_UI_I18N_H_
/* The strings a component shows, in the language the application asked for —
   `rust_i18n` and the `crates/ui/locales/ui.yml` catalogue behind it.

   Rust writes `t!("Dialog.ok")` and a macro looks the key up in the locale
   `rust_i18n::set_locale` last named, falling back to the one the crate was
   built with. There is no macro here and no YAML at runtime: the catalogue is
   `locale_data.cpp`, generated from upstream's own file by
   `cmd/gen-locale-data.ts`, so the translations come from upstream rather
   than from a hand transcription, and a later checkin's wording lands by
   re-running the generator.

   What a caller gets back is a `Str` over a string literal, so it outlives
   the frame and needs no copy. A key nothing translates answers with the key
   itself, which is what rust_i18n does — a missing translation shows up on
   screen as `Dialog.ok` rather than as an empty box. */

#include "gpui/gpui.h"

namespace gpui {

namespace component {

// One key and what it says in each locale, in the order LocaleAt lists them.
// A null is a locale the catalogue has nothing for; the lookup falls back to
// English, which every key has.
struct LocaleRow {
    const char* key;
    const char* const* values;
};

// t!(key). The string this key has in the locale in force.
Str Tr(const char* key);

// t!(key, name = value, ...): Tr(key) with each `%{name}` replaced by its
// value. A placeholder no argument names is left as it is.
struct TrArg {
    const char* name;
    Str value;
};
TempStr TrFmtTemp(const char* key, const TrArg* args, int nArgs);

// rust_i18n::set_locale. Names one of the locales the catalogue carries;
// anything else is ignored and the locale stays as it was, which is what
// keeps a typo from blanking every label. Answers whether it took.
bool LocaleSet(Str name);
// rust_i18n::locale.
Str LocaleNow();

// The locales the catalogue has, in the order the generator wrote them:
// English first, since it is the fallback and the language upstream writes
// its keys in.
int LocaleCount();
Str LocaleAt(int i);
// Which one `name` is, or -1.
int LocaleIndex(Str name);

// The catalogue itself. The generator writes the rows sorted by key, which is
// what the lookup's binary search rests on, and gives every row an English
// value, which is what the fallback rests on; both are worth a test, and
// neither is visible from a lookup that answers correctly by accident.
int LocaleRowCount();
const LocaleRow* LocaleRowAt(int i);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_I18N_H_
