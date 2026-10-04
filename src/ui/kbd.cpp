#include "ui/kbd.h"

#include <string.h>

namespace gpui {

namespace component {

// The separator between the parts: macOS runs them together, everything else
// joins with a plus.
static const char* KbdSeparator() {
    return base::PlatSecondaryIsCommand() ? "" : "+";
}

static void KbdAppend(char* out, int cap, int* len, const char* part) {
    for (const char* p = part; *p && *len + 1 < cap; p++) {
        out[(*len)++] = *p;
    }
}

static void KbdAppendSep(char* out, int cap, int* len) {
    if (*len > 0) {
        KbdAppend(out, cap, len, KbdSeparator());
    }
}

// The name a key goes by on this platform, or null when it has none and is
// simply capitalised.
static const char* KbdKeyName(Str key) {
    struct Named {
        const char* key;
        const char* mac;
        const char* other;
    };
    // The table from `Kbd::format`, in the order it writes it.
    static const Named kNamed[] = {
        {"ctrl", "\u2303", "Ctrl"},
        {"alt", "\u2325", "Alt"},
        {"shift", "\u21e7", "Shift"},
        {"cmd", "\u2318", "Win"},
        {"space", "Space", nullptr},
        {"backspace", "\u232b", "Backspace"},
        {"delete", "\u232b", "Delete"},
        {"escape", "\u238b", "Esc"},
        {"enter", "\u23ce", "Enter"},
        {"pagedown", "Page Down", "Page Down"},
        {"pageup", "Page Up", "Page Up"},
        {"left", "\u2190", "Left"},
        {"right", "\u2192", "Right"},
        {"up", "\u2191", "Up"},
        {"down", "\u2193", "Down"},
    };
    for (size_t i = 0; i < sizeof(kNamed) / sizeof(kNamed[0]); i++) {
        if (!base::StrEq(key, kNamed[i].key)) {
            continue;
        }
        // A key macOS names and nothing else does — Space — is left to the
        // capitalising path, which spells it the same way.
        return base::PlatSecondaryIsCommand() ? kNamed[i].mac : kNamed[i].other;
    }
    return nullptr;
}

int KbdFormat(Keystroke stroke, char* out, int cap) {
    int len = 0;
    if (cap <= 0) {
        return 0;
    }
    // The modifier order is the platform's: ⌃⌥⇧⌘ on macOS, and
    // Ctrl+Alt+Shift+Win everywhere else.
    const bool mac = base::PlatSecondaryIsCommand();
    if (stroke.ctrl) {
        KbdAppendSep(out, cap, &len);
        KbdAppend(out, cap, &len, mac ? "\u2303" : "Ctrl");
    }
    if (stroke.alt) {
        KbdAppendSep(out, cap, &len);
        KbdAppend(out, cap, &len, mac ? "\u2325" : "Alt");
    }
    if (stroke.shift) {
        KbdAppendSep(out, cap, &len);
        KbdAppend(out, cap, &len, mac ? "\u21e7" : "Shift");
    }
    if (stroke.platform) {
        KbdAppendSep(out, cap, &len);
        KbdAppend(out, cap, &len, mac ? "\u2318" : "Win");
    }

    KbdAppendSep(out, cap, &len);
    const char* named = KbdKeyName(stroke.key);
    if (named) {
        KbdAppend(out, cap, &len, named);
    } else if (stroke.key.len == 1) {
        // A single character is upper-cased.
        char c = stroke.key.s[0];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if (len + 1 < cap) {
            out[len++] = c;
        }
    } else {
        // Anything else keeps its spelling with the first letter raised.
        for (int i = 0; i < stroke.key.len && len + 1 < cap; i++) {
            char c = stroke.key.s[i];
            if (i == 0 && c >= 'a' && c <= 'z') {
                c = (char)(c - 32);
            }
            out[len++] = c;
        }
    }
    out[len] = 0;
    return len;
}

static bool KeystrokePartIs(Str part, const char* name) {
    int n = (int)strlen(name);
    if (len(part) != n) return false;
    for (int i = 0; i < n; i++) {
        char c = part.s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != name[i]) return false;
    }
    return true;
}

bool KeystrokeParse(Arena* a, Str source, Keystroke* out) {
    // The `-`-separated parts, as `source.split('-')` gives them.
    Str parts[32];
    int n = 0;
    int start = 0;
    for (int i = 0; i <= len(source); i++) {
        if (i < len(source) && source.s[i] != '-') continue;
        if (n == 32) return false;
        parts[n++] = Str(source.s + start, i - start);
        start = i + 1;
    }
    Keystroke k;
    bool function = false;
    bool hasKey = false;
    Str key = {};
    bool lower = true;
    for (int i = 0; i < n; i++) {
        Str part = parts[i];
        if (KeystrokePartIs(part, "ctrl")) {
            k.ctrl = true;
            continue;
        }
        if (KeystrokePartIs(part, "alt")) {
            k.alt = true;
            continue;
        }
        if (KeystrokePartIs(part, "shift")) {
            k.shift = true;
            continue;
        }
        if (KeystrokePartIs(part, "fn")) {
            function = true;
            continue;
        }
        if (KeystrokePartIs(part, "secondary")) {
            if (base::PlatSecondaryIsCommand()) {
                k.platform = true;
            } else {
                k.ctrl = true;
            }
            continue;
        }
        if (KeystrokePartIs(part, "cmd") || KeystrokePartIs(part, "super") ||
            KeystrokePartIs(part, "win")) {
            k.platform = true;
            continue;
        }
        if (i + 1 < n) {
            Str next = parts[i + 1];
            if (len(next) == 0 && len(source) > 0 &&
                source.s[len(source) - 1] == '-') {
                key = StrL("-");
                hasKey = true;
                lower = false;
                break;
            }
            if (len(next) > 1 && next.s[0] == '>') {
                // `key->key_char`: the key as written, and the character
                // only test events use, which this Keystroke has no room for.
                key = part;
                hasKey = true;
                lower = false;
                i++;
                continue;
            }
            return false;
        }
        if (len(part) == 1 && part.s[0] >= 'A' && part.s[0] <= 'Z') {
            k.shift = true;
        }
        key = part;
        hasKey = true;
        lower = true;
    }
    if (!hasKey) {
        // A spec of nothing but modifiers names the first of them.
        if (k.shift) {
            k.shift = false;
            key = StrL("shift");
        } else if (k.ctrl) {
            k.ctrl = false;
            key = StrL("control");
        } else if (k.alt) {
            k.alt = false;
            key = StrL("alt");
        } else if (k.platform) {
            k.platform = false;
            key = StrL("platform");
        } else if (function) {
            key = StrL("function");
        } else {
            return false;
        }
        lower = false;
    }
    char* copy = (char*)Alloc(a, len(key) + 1);
    for (int i = 0; i < len(key); i++) {
        char c = key.s[i];
        if (lower && c >= 'A' && c <= 'Z') c = (char)(c + 32);
        copy[i] = c;
    }
    copy[len(key)] = 0;
    k.key = Str(copy, len(key));
    *out = k;
    return true;
}

TempStr KeystrokeParseErrorTemp(Str source) {
    return fmt(
        "Invalid keystroke \"%s\". Expected a sequence of modifiers "
        "(`ctrl`, `alt`, `shift`, `fn`, `cmd`, `super`, or `win`) "
        "followed by a key, separated by `-`.",
        source);
}

Str KbdFormatStr(Ctx* cx, Keystroke stroke) {
    TempStr buf = AllocStrTemp(63);
    int n = KbdFormat(stroke, buf.s, len(buf) + 1);
    return StrDup(cx->a, Str(buf.s, n));
}

Kbd* Kbd::New(Ctx* cx, Str stroke) {
    Arena* a = cx->a;
    Kbd* k = ArenaNew<Kbd>(a);
    k->a = a;
    k->cx = cx;
    k->stroke = stroke;
    return k;
}

Kbd* Kbd::New(Ctx* cx, Keystroke stroke) {
    return New(cx, KbdFormatStr(cx, stroke));
}

Kbd* Kbd::Appearance(bool v) {
    appearance = v;
    return this;
}

Kbd* Kbd::Outline() {
    outline = true;
    return this;
}

El* Kbd::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    if (!appearance) {
        return TextEl(a, stroke)->Font(12)->Fg(th.mutedFg);
    }
    // The plain chip is a muted wash with no border; outline swaps to the
    // window background inside one. px_1 / py_0p5 / min_w_5 / radius half.
    El* e = Div(a)
                ->PadX(Rems(cx, 0.25f))
                ->PadY(Rems(cx, 0.125f))
                ->MinW(Rems(cx, 1.25f))
                ->ItemsCenter()
                ->JustifyCenter()
                ->Radius(th.radius * 0.5f)
                ->Bg(th.tokens.muted);
    if (outline) {
        e->Bg(th.tokens.background)->Border(1, th.border);
    }
    e->Child(TextEl(a, stroke)->Font(12)->LineHeight(1.f)->Fg(th.mutedFg));
    return e;
}

static bool KeystrokeFromChord(const KeyChord& c, Keystroke* out) {
    Str key = KeyName(c.vk);
    if (!out || !key.s) {
        return false;
    }
    Keystroke k;
    k.ctrl = c.ctrl;
    k.alt = c.alt;
    k.shift = c.shift;
    k.platform = c.platform;
    k.key = key;
    *out = k;
    return true;
}

bool KeystrokeForAction(uint32_t action, const char* context, Keystroke* out) {
    KeyChord c = {};
    uint32_t ctx = context ? KeyContextOf(Str(context)) : 0;
    if (!KeymapBindingForAction(action, context ? &ctx : nullptr,
                                context ? 1 : 0, &c)) {
        return false;
    }
    return KeystrokeFromChord(c, out);
}

bool KeystrokeForActionAtFocus(Ctx* cx, uint32_t action, FocusHandle focus,
                               Keystroke* out) {
    KeyChord c = {};
    return cx && WindowBindingForActionAtFocus(cx->win, action, focus, &c) &&
           KeystrokeFromChord(c, out);
}

Kbd* Kbd::ForAction(Ctx* cx, uint32_t action, const char* context) {
    Keystroke k;
    if (!KeystrokeForAction(action, context, &k)) {
        return nullptr;
    }
    return Kbd::New(cx, k);
}

Kbd* Kbd::ForActionAtFocus(Ctx* cx, uint32_t action, FocusHandle focus) {
    Keystroke k;
    if (!KeystrokeForActionAtFocus(cx, action, focus, &k)) {
        return nullptr;
    }
    return Kbd::New(cx, k);
}

} // namespace component
} // namespace gpui
