#include "ui/kbd.h"

namespace gpui {

namespace component {

// The separator between the parts: macOS runs them together, everything else
// joins with a plus.
#if GPUI_OS_MAC
static const char* kKbdSeparator = "";
#else
static const char* kKbdSeparator = "+";
#endif

static void KbdAppend(char* out, int cap, int* len, const char* part) {
    for (const char* p = part; *p && *len + 1 < cap; p++) {
        out[(*len)++] = *p;
    }
}

static void KbdAppendSep(char* out, int cap, int* len) {
    if (*len > 0) {
        KbdAppend(out, cap, len, kKbdSeparator);
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
#if GPUI_OS_MAC
        return kNamed[i].mac;
#else
        // A key macOS names and nothing else does — Space — is left to the
        // capitalising path, which spells it the same way.
        return kNamed[i].other;
#endif
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
    if (stroke.ctrl) {
        KbdAppendSep(out, cap, &len);
#if GPUI_OS_MAC
        KbdAppend(out, cap, &len, "\u2303");
#else
        KbdAppend(out, cap, &len, "Ctrl");
#endif
    }
    if (stroke.alt) {
        KbdAppendSep(out, cap, &len);
#if GPUI_OS_MAC
        KbdAppend(out, cap, &len, "\u2325");
#else
        KbdAppend(out, cap, &len, "Alt");
#endif
    }
    if (stroke.shift) {
        KbdAppendSep(out, cap, &len);
#if GPUI_OS_MAC
        KbdAppend(out, cap, &len, "\u21e7");
#else
        KbdAppend(out, cap, &len, "Shift");
#endif
    }
    if (stroke.platform) {
        KbdAppendSep(out, cap, &len);
#if GPUI_OS_MAC
        KbdAppend(out, cap, &len, "\u2318");
#else
        KbdAppend(out, cap, &len, "Win");
#endif
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
                ->PadX(4)
                ->PadY(2)
                ->MinW(20)
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
