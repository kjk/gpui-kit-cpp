#include "shell/value.h"

#include "shell/spec.h"
#include "shell/theme_tokens.h"

#include <math.h>

namespace gpui::shell {

Bridged Bridged::Nil() {
    return {};
}

Bridged Bridged::Bool(bool value) {
    Bridged out;
    out.kind = BridgedKind::Bool;
    out.boolean = value;
    return out;
}

Bridged Bridged::Number(double value) {
    Bridged out;
    out.kind = BridgedKind::Number;
    out.number = value;
    return out;
}

Bridged Bridged::String(Str value) {
    Bridged out;
    out.kind = BridgedKind::String;
    out.string = value;
    return out;
}

Bridged Bridged::Background(const BackgroundSpec* spec) {
    Bridged out;
    out.kind = BridgedKind::Background;
    out.background = spec;
    return out;
}

Bridged Bridged::Shadows(const ShadowSpec* specs, int count) {
    Bridged out;
    out.kind = BridgedKind::Shadows;
    out.shadows = specs;
    out.shadowCount = count;
    return out;
}

Str BridgedDescribe(Arena* arena, const Bridged& value) {
    switch (value.kind) {
        case BridgedKind::Nil:
            return StrDup(arena, StrL("nil"));
        case BridgedKind::Bool:
            return StrDup(arena,
                          fmt("boolean (%s)",
                              value.boolean ? StrL("true") : StrL("false")));
        case BridgedKind::Number:
            return StrDup(arena, fmt("number (%g)", value.number));
        case BridgedKind::String:
            return StrDup(arena, fmt("string (\"%s\")", value.string));
        case BridgedKind::Background:
            return StrDup(arena, StrL("a Background"));
        case BridgedKind::Shadows:
            return StrDup(arena, StrL("a shadow list"));
    }
    return {};
}

static void Expected(ShellError* error, const char* expected,
                     const Bridged& value) {
    if (!error) {
        return;
    }
    Arena* arena = GetTempArena();
    Str got = BridgedDescribe(arena, value);
    ShellErrorSet(error, fmt("expected %s, got %s", Str(expected), got));
}

bool BridgedAsF32(const Bridged& value, float* out, ShellError* error) {
    if (value.kind != BridgedKind::Number) {
        Expected(error, "a number", value);
        return false;
    }
    if (out) {
        *out = (float)value.number;
    }
    return true;
}

bool BridgedAsString(const Bridged& value, Str* out, ShellError* error) {
    if (value.kind != BridgedKind::String) {
        Expected(error, "a string", value);
        return false;
    }
    if (out) {
        *out = value.string;
    }
    return true;
}

bool BridgedAsPixels(const Bridged& value, float* out, ShellError* error) {
    return BridgedAsF32(value, out, error);
}

static int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool ParseHex(Str value, Hsla* out) {
    if (!out || (len(value) != 3 && len(value) != 6 && len(value) != 8)) {
        return false;
    }
    uint32_t rgba = 0;
    if (len(value) == 3) {
        int r = HexDigit(value.s[0]);
        int g = HexDigit(value.s[1]);
        int b = HexDigit(value.s[2]);
        if (r < 0 || g < 0 || b < 0) return false;
        rgba = (uint32_t)(r * 17) << 24 | (uint32_t)(g * 17) << 16 |
               (uint32_t)(b * 17) << 8 | 0xffu;
    } else {
        for (int i = 0; i < len(value); i++) {
            int digit = HexDigit(value.s[i]);
            if (digit < 0) return false;
            rgba = (rgba << 4) | (uint32_t)digit;
        }
        if (len(value) == 6) rgba = (rgba << 8) | 0xffu;
    }
    Rgba color = {(uint8_t)(rgba >> 24), (uint8_t)(rgba >> 16),
                  (uint8_t)(rgba >> 8), (uint8_t)rgba};
    *out = HslaFromRgba(color);
    return true;
}

bool BridgedAsColor(const Bridged& value, Hsla* out, ShellError* error) {
    Str text;
    if (!BridgedAsString(value, &text, error)) {
        return false;
    }
    if (len(text) > 0 && text.s[0] == '#') {
        if (ParseHex(Str(text.s + 1, len(text) - 1), out)) {
            return true;
        }
        ShellErrorSet(error, fmt("`%s` is not a valid color literal (expected "
                                 "#rgb, #rrggbb or #rrggbbaa)",
                                 text));
        return false;
    }
    if (ThemeTokenColor(text, out)) {
        return true;
    }
    ShellErrorSet(error,
                  fmt("unknown color token `%s`; expected one of: background, "
                      "foreground, surface, surface_foreground, primary, "
                      "primary_foreground, secondary, secondary_foreground, "
                      "muted, muted_foreground, accent, accent_foreground, "
                      "destructive, destructive_foreground, border, input, "
                      "ring — or a #rrggbb literal",
                      text));
    return false;
}

static bool ColorLiteral(Str text, Hsla* out, ShellError* error) {
    return BridgedAsColor(Bridged::String(text), out, error);
}

bool BackgroundSpecResolve(const BackgroundSpec& spec, ::gpui::Background* out,
                           ShellError* error) {
    if (!out) return false;
    Hsla first = {};
    Str literal = spec.kind == BackgroundKind::LinearGradient ? spec.fromColor
                                                              : spec.color;
    if (!ColorLiteral(literal, &first, error)) return false;
    ::gpui::Background result = HslaToRgba(first);
    if (spec.kind == BackgroundKind::LinearGradient) {
        Hsla second = {};
        if (!ColorLiteral(spec.toColor, &second, error)) return false;
        result = BackgroundLinear(
            spec.angle, ColorStopAt(HslaToRgba(first), spec.fromPosition),
            ColorStopAt(HslaToRgba(second), spec.toPosition));
    }
    // PatternSlash and Checkerboard have no brush on an element fill. They
    // keep their colour, which is the same fallback paint_path already used.
    *out = BackgroundOpacity(result, spec.opacity);
    return true;
}

bool BridgedAsBackground(const Bridged& value, ::gpui::Background* out,
                         ShellError* error) {
    if (value.kind == BridgedKind::Background) {
        if (!value.background) {
            ShellErrorSet(error, StrL("expected a color or a Background"));
            return false;
        }
        return BackgroundSpecResolve(*value.background, out, error);
    }
    if (value.kind == BridgedKind::String) {
        Hsla color = {};
        if (!BridgedAsColor(value, &color, error)) return false;
        if (out) *out = HslaToRgba(color);
        return true;
    }
    if (error) {
        Arena* arena = GetTempArena();
        Str got = BridgedDescribe(arena, value);
        ShellErrorSet(error,
                      fmt("expected a color or a Background, got %s", got));
    }
    return false;
}

bool BridgedAsShadows(const Bridged& value, ::gpui::BoxShadow* out, int cap,
                      int* count, ShellError* error) {
    if (value.kind != BridgedKind::Shadows) {
        if (error) {
            Arena* arena = GetTempArena();
            Str got = BridgedDescribe(arena, value);
            ShellErrorSet(error,
                          fmt("expected an array of shadows, got %s", got));
        }
        return false;
    }
    int n = value.shadowCount;
    if (n < 0) n = 0;
    if (count) *count = n;
    if (n == 0) return true;
    if (!out || cap < n || !value.shadows) {
        ShellErrorSet(error, StrL("shadow list is too large"));
        return false;
    }
    // CSS paints the first shadow on top. GPUI paints the last on top, so
    // the list is stored reversed. Errors still name the script's index.
    for (int i = 0; i < n; i++) {
        int from = n - 1 - i;
        const ShadowSpec& spec = value.shadows[from];
        Hsla color = {};
        ShellError colorError;
        if (!ColorLiteral(spec.color, &color, &colorError)) {
            ShellErrorSet(error,
                          fmt("shadow %d: %s", from, colorError.message));
            ShellErrorClear(&colorError);
            return false;
        }
        ::gpui::BoxShadow shadow = {};
        shadow.x = spec.x;
        shadow.y = spec.y;
        shadow.blur = spec.blur / 2.f;
        shadow.spread = spec.spread;
        shadow.color = HslaToRgba(color);
        shadow.inset = spec.inset;
        out[i] = shadow;
    }
    return true;
}

bool BridgedIsTruthy(const Bridged& value) {
    switch (value.kind) {
        case BridgedKind::Nil:
            return false;
        case BridgedKind::Bool:
            return value.boolean;
        case BridgedKind::Number:
            return value.number != 0.0 && !isnan(value.number);
        case BridgedKind::String:
            return len(value.string) != 0;
        case BridgedKind::Background:
        case BridgedKind::Shadows:
            return true;
    }
    return false;
}

bool BridgedArg(const Bridged* args, int count, int index, Str method,
                Bridged* out, ShellError* error) {
    if (!args || index < 0 || index >= count) {
        ShellErrorSet(error, fmt("`%s` expects at least %d argument(s), got %d",
                                 method, index + 1, count));
        return false;
    }
    if (out) {
        *out = args[index];
    }
    return true;
}

} // namespace gpui::shell
