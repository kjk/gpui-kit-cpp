#include "ui/avatar.h"

namespace gpui {

namespace component {

Avatar* Avatar::New(Ctx* cx) {
    Arena* a = cx->a;
    Avatar* v = ArenaNew<Avatar>(a);
    v->a = a;
    v->cx = cx;
    return v;
}

TempStr AvatarInitialsTemp(Str name) {
    TempStr out = AllocStrTemp(2);
    // The first letter of each of the first two words.
    int n = 0;
    bool atWord = true;
    for (int i = 0; i < len(name) && n < 2; i++) {
        char c = name.s[i];
        if (c == ' ') {
            atWord = true;
            continue;
        }
        if (atWord) {
            out.s[n++] = c;
            atWord = false;
        }
    }
    // One word only: its first two letters instead.
    if (n == 1) {
        n = 0;
        for (int i = 0; i < len(name) && n < 2; i++) {
            out.s[n++] = name.s[i];
        }
    }
    for (int i = 0; i < n; i++) {
        if (out.s[i] >= 'a' && out.s[i] <= 'z') {
            out.s[i] = (char)(out.s[i] - 'a' + 'A');
        }
    }
    out.s[n] = 0;
    out.len = n;
    return out;
}

Avatar* Avatar::Name(Str s) {
    initials = StrDup(a, AvatarInitialsTemp(s));
    return this;
}

Avatar* Avatar::Initials(Str s) {
    initials = s;
    return this;
}
Avatar* Avatar::Bg(Background c) {
    bg = c;
    hasBg = true;
    return this;
}
Avatar* Avatar::Size(float v) {
    size = v;
    return this;
}
// crates/ui/src/avatar/mod.rs: avatar_size + avatar_text_size. Avatars do not
// use the generic control heights.
float AvatarSizePx(UiSize s) {
    switch (s) {
        case UiSize::XSmall:
            return 16;
        case UiSize::Small:
            return 24;
        case UiSize::Large:
            return 80;
        default:
            return 48;
    }
}

static float AvatarTextPx(UiSize s) {
    switch (s) {
        case UiSize::XSmall:
            return 10.4f; // rems(0.65)
        case UiSize::Small:
            return 12; // text_xs
        case UiSize::Large:
            return 30; // text_3xl
        default:
            return 14; // text_sm
    }
}

Avatar* Avatar::WithSize(UiSize s) {
    size = AvatarSizePx(s);
    textPx = AvatarTextPx(s);
    textSemibold = s == UiSize::Large;
    return this;
}
Avatar* Avatar::Radius(float v) {
    radius = v;
    return this;
}
Avatar* Avatar::Border(float w, Rgba c) {
    borderW = w;
    borderC = c;
    hasBorderC = true;
    return this;
}
Avatar* Avatar::Placeholder(IconName n) {
    placeholder = n;
    return this;
}

struct AvatarIdentityColors {
    Rgba background = {};
    Rgba foreground = {};
    Rgba border = {};
};

// rustc-hash 2's multiply_mix: the two halves of the 128-bit product folded
// together, written with 32-bit halves so it needs no 128-bit type.
static uint64_t FxMultiplyMix(uint64_t x, uint64_t y) {
    uint64_t xl = (uint32_t)x, xh = x >> 32, yl = (uint32_t)y, yh = y >> 32;
    uint64_t ll = xl * yl, lh = xl * yh, hl = xh * yl, hh = xh * yh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    uint64_t lo = (mid << 32) | (uint32_t)ll;
    uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    return lo ^ hi;
}

static uint64_t FxLoad(const uint8_t* p, int n) {
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

// rustc-hash 2's hash_bytes.
static uint64_t FxHashBytes(const uint8_t* b, int n) {
    uint64_t s0 = 0x243f6a8885a308d3ull, s1 = 0x13198a2e03707344ull;
    if (n <= 16) {
        if (n >= 8) {
            s0 ^= FxLoad(b, 8);
            s1 ^= FxLoad(b + n - 8, 8);
        } else if (n >= 4) {
            s0 ^= FxLoad(b, 4);
            s1 ^= FxLoad(b + n - 4, 4);
        } else if (n > 0) {
            s0 ^= b[0];
            s1 ^= ((uint64_t)b[n - 1] << 8) | b[n / 2];
        }
    } else {
        int at = 0;
        while (n - 1 - at >= 16) {
            uint64_t x = FxLoad(b + at, 8), y = FxLoad(b + at + 8, 8);
            uint64_t t = FxMultiplyMix(s0 ^ x, 0xa4093822299f31d0ull ^ y);
            s0 = s1;
            s1 = t;
            at += 16;
        }
        s0 ^= FxLoad(b + n - 16, 8);
        s1 ^= FxLoad(b + n - 8, 8);
    }
    return FxMultiplyMix(s0, s1) ^ (uint64_t)n;
}

// gpui::hash(&SharedString): FxBuildHasher over the string, which Hash
// writes as its bytes and then 0xff.
static uint64_t AvatarNameHash(Str s) {
    const uint64_t k = 0xf1357aea2e62a9c5ull;
    uint64_t h = 0;
    h = (h + FxHashBytes((const uint8_t*)s.s, len(s))) * k;
    h = (h + 0xff) * k;
    return (h << 26) | (h >> 38);
}

// Twelve evenly spaced OkLCH hues keep perceived brightness constant, the
// one for a name picked by gpui::hash of its initials, as Rust does.
static AvatarIdentityColors AvatarIdentity(const Theme& th, Str initials) {
    uint64_t h = AvatarNameHash(initials);
    float deg = (float)((h % 12) * 30);
    AvatarIdentityColors out;
    if (th.mode == ThemeMode::Dark) {
        out.background = ThemeOklch(0.30f, 0.05f, deg);
        out.foreground = ThemeOklch(0.82f, 0.11f, deg);
        out.border = ThemeOklch(0.36f, 0.06f, deg);
    } else {
        out.background = ThemeOklch(0.97f, 0.032f, deg);
        out.foreground = ThemeOklch(0.50f, 0.145f, deg);
        out.border = ThemeOklch(0.89f, 0.05f, deg);
    }
    return out;
}

Avatar* Avatar::Src(Str url) {
    src = url;
    return this;
}

El* Avatar::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // `rounded_full_style`: as round as the box goes, unless the theme squares
    // its corners — an avatar that stayed a circle in a square-cornered UI is
    // exactly what Theme::radius_full was added for.
    float r = radius >= 0 ? radius : (th.radiusFull > 0 ? size * 0.5f : 0.f);
    // GPUI's border sits inside the box, so the fallback fills what is left
    // of it; drawn edge to edge it would paint over the ring.
    float inset = borderW > 0 ? borderW : 0;
    float innerSize = size - inset * 2;
    bool named = initials.s && len(initials) > 0;
    Background fill = th.tokens.secondary;
    Rgba text = th.mutedFg;
    Rgba identityBorder = th.border;
    if (hasBg) {
        fill = bg;
        text = th.foreground;
    } else if (named) {
        AvatarIdentityColors identity = AvatarIdentity(th, initials);
        fill = identity.background;
        text = identity.foreground;
        identityBorder = identity.border;
    }
    float txt = textPx > 0 ? textPx : size * 0.35f;
    El* inner = named ? TextEl(a, initials)->Font(txt)->Fg(text)
                      : IconEl(a, placeholder, size * 0.6f)->Fg(text);
    if (named && textSemibold) inner->Semibold();
    El* fb = AvatarFallback::New(cx)
                 ->W(innerSize)
                 ->H(innerSize)
                 ->ItemsCenter()
                 ->JustifyCenter()
                 ->Bg(fill)
                 ->Radius(r - inset)
                 ->Child(inner);
    // The base is opaque (bg tokens.secondary) and the fallback tint sits on
    // top, so overlapping group avatars do not show through each other.
    gpui::Avatar* base = gpui::Avatar::New(cx)->Size(size)->Fallback(fb);
    if (src.s && len(src) > 0) {
        // AvatarImage::new(src).size_full().rounded_full(): the picture takes
        // the whole of the base and the fallback is not drawn at all.
        base->Image(
            AvatarImage::New(cx)
                ->W(innerSize)
                ->H(innerSize)
                ->Radius(r - inset)
                ->Child(ImageEl(a, src)->W(innerSize)->H(innerSize)->Radius(
                    r - inset)));
    }
    El* el = base->IntoEl()->Radius(r)->Bg(th.tokens.secondary);
    Rgba bd = hasBorderC ? borderC
                         : (named && !(src.s && len(src) > 0) ? identityBorder
                                                              : th.border);
    if (borderW > 0) {
        el->Pad(inset)->Border(borderW, bd);
    }
    return el;
}

AvatarGroup* AvatarGroup::New(Ctx* cx) {
    AvatarGroup* g = ArenaNew<AvatarGroup>(cx->a);
    g->a = cx->a;
    g->cx = cx;
    return g;
}
AvatarGroup* AvatarGroup::Child(Avatar* av) {
    if (av) {
        avatars.Append(a, av);
    }
    return this;
}
AvatarGroup* AvatarGroup::WithSize(UiSize s) {
    size = s;
    return this;
}
AvatarGroup* AvatarGroup::Limit(int v) {
    limit = v;
    return this;
}
AvatarGroup* AvatarGroup::Ellipsis() {
    ellipsis = true;
    return this;
}

El* AvatarGroup::IntoEl() {
    float sz = AvatarSizePx(size);
    // item_ml = -avatar_size * 0.3, so each avatar past the first overlaps
    // the one before it by that much; the ⋯ chip sits ml_1 past the last.
    float step = sz - sz * 0.3f;
    int shown = avatars.len < limit ? avatars.len : limit;
    bool more = ellipsis && avatars.len > limit;
    float chipLeft = (float)shown * step + 4;
    float w =
        more ? chipLeft + sz : sz + (shown > 0 ? (float)(shown - 1) * step : 0);
    El* box = Div(a)->H(sz)->W(w);
    // flex_row_reverse: the row is built right to left, so the leftmost
    // avatar is the last child and paints over its neighbour. Absolute
    // placement gets the same stack without a reversed row or a margin.
    if (more) {
        box->Child(Avatar::New(cx)
                       // Avatar::name("⋯"): a name, so the chip is
                       // tinted and lettered like any other fallback.
                       ->Initials(StrL("⋯"))
                       ->WithSize(size)
                       ->IntoEl()
                       ->Absolute()
                       ->Left(chipLeft));
    }
    for (int i = shown - 1; i >= 0; i--) {
        box->Child(avatars[i]->WithSize(size)->IntoEl()->Absolute()->Left(
            (float)i * step));
    }
    return box;
}

} // namespace component
} // namespace gpui
