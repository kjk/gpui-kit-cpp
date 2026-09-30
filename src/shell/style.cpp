// crates/shell/src/style.rs, and the gpui style helpers it reflects:
// gpui-macros styles.rs (the spacing, corner and border ramps, cursors,
// shadows, overflow, position, visibility), gpui styled.rs (display, flex,
// alignment, text) and gpui-base styled.rs (h_flex, v_flex, font weights,
// debug colours).

#include "shell/style.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace gpui::shell::style {

// ─── lengths ─────────────────────────────────────────────────────────────

enum class LenKind : uint8_t {
    Px,
    // relative(f), f a fraction of the parent.
    Frac,
    Auto,
};

struct Len {
    LenKind kind = LenKind::Px;
    float v = 0;
};

static Len Px(float v) {
    return Len{LenKind::Px, v};
}

// rems(r) at GPUI's default 16px rem, which is the only rem this tree has.
static constexpr float kRem = 16.f;

// ─── families ────────────────────────────────────────────────────────────

// What follows a family's `_`: the tailwind spacing ramp (styles.rs
// box_style_suffixes), the corner ramp (corner_suffixes) or the border ramp
// (border_suffixes).
enum class Suffix : uint8_t {
    Box,
    Corner,
    Border,
};

// The argument type GPUI gives the family's own parametric method: `Length`
// allows "auto", `DefiniteLength` does not, `AbsoluteLength` also refuses a
// percentage.
enum class Arg : uint8_t {
    Length,
    Definite,
    Absolute,
};

enum class Fam : uint8_t {
    W,
    H,
    Size,
    MinW,
    MinH,
    MinSize,
    MaxW,
    MaxH,
    MaxSize,
    Gap,
    GapX,
    GapY,
    P,
    Pt,
    Pb,
    Px,
    Py,
    Pl,
    Pr,
    M,
    Mt,
    Mb,
    My,
    Mx,
    Ml,
    Mr,
    Inset,
    Top,
    Bottom,
    Left,
    Right,
    Rounded,
    RoundedT,
    RoundedB,
    RoundedL,
    RoundedR,
    RoundedTl,
    RoundedTr,
    RoundedBl,
    RoundedBr,
    Border,
    BorderT,
    BorderB,
    BorderL,
    BorderR,
    BorderX,
    BorderY,
};

struct Family {
    const char* name;
    Fam fam;
    Suffix suffix;
    Arg arg;
};

// Every prefix is also the name of the family's parametric method, which is
// why one table serves both halves.
static const Family kFamilies[] = {
    {"w", Fam::W, Suffix::Box, Arg::Length},
    {"h", Fam::H, Suffix::Box, Arg::Length},
    {"size", Fam::Size, Suffix::Box, Arg::Length},
    {"min_w", Fam::MinW, Suffix::Box, Arg::Length},
    {"min_h", Fam::MinH, Suffix::Box, Arg::Length},
    {"min_size", Fam::MinSize, Suffix::Box, Arg::Length},
    {"max_w", Fam::MaxW, Suffix::Box, Arg::Length},
    {"max_h", Fam::MaxH, Suffix::Box, Arg::Length},
    {"max_size", Fam::MaxSize, Suffix::Box, Arg::Length},
    {"gap", Fam::Gap, Suffix::Box, Arg::Definite},
    {"gap_x", Fam::GapX, Suffix::Box, Arg::Definite},
    {"gap_y", Fam::GapY, Suffix::Box, Arg::Definite},
    {"p", Fam::P, Suffix::Box, Arg::Definite},
    {"pt", Fam::Pt, Suffix::Box, Arg::Definite},
    {"pb", Fam::Pb, Suffix::Box, Arg::Definite},
    {"px", Fam::Px, Suffix::Box, Arg::Definite},
    {"py", Fam::Py, Suffix::Box, Arg::Definite},
    {"pl", Fam::Pl, Suffix::Box, Arg::Definite},
    {"pr", Fam::Pr, Suffix::Box, Arg::Definite},
    {"m", Fam::M, Suffix::Box, Arg::Length},
    {"mt", Fam::Mt, Suffix::Box, Arg::Length},
    {"mb", Fam::Mb, Suffix::Box, Arg::Length},
    {"my", Fam::My, Suffix::Box, Arg::Length},
    {"mx", Fam::Mx, Suffix::Box, Arg::Length},
    {"ml", Fam::Ml, Suffix::Box, Arg::Length},
    {"mr", Fam::Mr, Suffix::Box, Arg::Length},
    {"inset", Fam::Inset, Suffix::Box, Arg::Length},
    {"top", Fam::Top, Suffix::Box, Arg::Length},
    {"bottom", Fam::Bottom, Suffix::Box, Arg::Length},
    {"left", Fam::Left, Suffix::Box, Arg::Length},
    {"right", Fam::Right, Suffix::Box, Arg::Length},
    {"rounded", Fam::Rounded, Suffix::Corner, Arg::Absolute},
    {"rounded_t", Fam::RoundedT, Suffix::Corner, Arg::Absolute},
    {"rounded_b", Fam::RoundedB, Suffix::Corner, Arg::Absolute},
    {"rounded_l", Fam::RoundedL, Suffix::Corner, Arg::Absolute},
    {"rounded_r", Fam::RoundedR, Suffix::Corner, Arg::Absolute},
    {"rounded_tl", Fam::RoundedTl, Suffix::Corner, Arg::Absolute},
    {"rounded_tr", Fam::RoundedTr, Suffix::Corner, Arg::Absolute},
    {"rounded_bl", Fam::RoundedBl, Suffix::Corner, Arg::Absolute},
    {"rounded_br", Fam::RoundedBr, Suffix::Corner, Arg::Absolute},
    {"border", Fam::Border, Suffix::Border, Arg::Absolute},
    {"border_t", Fam::BorderT, Suffix::Border, Arg::Absolute},
    {"border_b", Fam::BorderB, Suffix::Border, Arg::Absolute},
    {"border_l", Fam::BorderL, Suffix::Border, Arg::Absolute},
    {"border_r", Fam::BorderR, Suffix::Border, Arg::Absolute},
    {"border_x", Fam::BorderX, Suffix::Border, Arg::Absolute},
    {"border_y", Fam::BorderY, Suffix::Border, Arg::Absolute},
};

static int CompareStr(Str a, Str b) {
    int n = len(a) < len(b) ? len(a) : len(b);
    int c = n > 0 ? memcmp(a.s, b.s, (size_t)n) : 0;
    if (c != 0) {
        return c;
    }
    return len(a) - len(b);
}

// A table of names, sorted once on first use so a lookup is a binary search
// rather than a scan: this runs for every style call of every frame.
template <typename T, int N>
struct SortedIndex {
    const T* items[N];

    explicit SortedIndex(const T (&table)[N]) {
        for (int i = 0; i < N; i++) {
            items[i] = &table[i];
        }
        qsort(items, N, sizeof(items[0]), [](const void* a, const void* b) {
            const T* x = *(const T* const*)a;
            const T* y = *(const T* const*)b;
            return strcmp(x->name, y->name);
        });
    }

    const T* Find(Str name) const {
        int lo = 0;
        int hi = N - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            int c = CompareStr(Str(items[mid]->name), name);
            if (c == 0) {
                return items[mid];
            }
            if (c < 0) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return nullptr;
    }
};

static const Family* FindFamily(Str name) {
    static const SortedIndex<Family, dimof(kFamilies)> index(kFamilies);
    return index.Find(name);
}

// ─── suffix grammars ─────────────────────────────────────────────────────

// A run of decimal digits, as an integer; false for anything else.
static bool Digits(Str text, int* out) {
    if (len(text) <= 0 || len(text) > 4) {
        return false;
    }
    int n = 0;
    for (int i = 0; i < len(text); i++) {
        char c = text.s[i];
        if (c < '0' || c > '9') {
            return false;
        }
        n = n * 10 + (c - '0');
    }
    *out = n;
    return true;
}

// box_style_suffixes: `0`, `0p5` … `3p5`, `4` … `12`, then 16 to 128 on the
// tailwind ramp — rems(n / 4) — plus `px`, `full`, `auto` and the thirteen
// fractions. `neg_` in front negates everything except `auto`.
static bool ParseBoxSuffix(Str text, bool autoAllowed, Len* out) {
    bool negative = false;
    if (StrStartsWith(text, "neg_")) {
        negative = true;
        text = Str(text.s + 4, len(text) - 4);
    }
    float sign = negative ? -1.f : 1.f;
    if (StrEq(text, StrL("auto"))) {
        if (negative || !autoAllowed) {
            return false;
        }
        *out = Len{LenKind::Auto, 0};
        return true;
    }
    if (StrEq(text, StrL("px"))) {
        *out = Px(sign * 1.f);
        return true;
    }
    if (StrEq(text, StrL("full"))) {
        *out = Len{LenKind::Frac, sign * 1.f};
        return true;
    }
    const char* under = (const char*)memchr(text.s, '_', (size_t)len(text));
    if (under) {
        static const uint8_t fractions[][2] = {
            {1, 2}, {1, 3}, {2, 3}, {1, 4}, {2, 4}, {3, 4},  {1, 5},
            {2, 5}, {3, 5}, {4, 5}, {1, 6}, {5, 6}, {1, 12},
        };
        int num = 0;
        int den = 0;
        int at = (int)(under - text.s);
        if (!Digits(Str(text.s, at), &num) ||
            !Digits(Str(under + 1, len(text) - at - 1), &den)) {
            return false;
        }
        for (const uint8_t* f : fractions) {
            if (f[0] == num && f[1] == den) {
                *out = Len{LenKind::Frac, sign * (float)num / (float)den};
                return true;
            }
        }
        return false;
    }
    // `NpN` is the half step: 0p5 … 3p5.
    bool half = false;
    if (StrEndsWith(text, "p5")) {
        half = true;
        text.len -= 2;
    }
    int n = 0;
    if (!Digits(text, &n)) {
        return false;
    }
    bool ok = false;
    if (half) {
        ok = n <= 3;
    } else if (n <= 12) {
        ok = true;
    } else {
        static const uint8_t ramp[] = {16, 20, 24, 32, 40,  48, 56,
                                       64, 72, 80, 96, 112, 128};
        for (uint8_t r : ramp) {
            ok = ok || r == n;
        }
    }
    if (!ok || (half && len(text) != 1)) {
        return false;
    }
    // A leading zero spells a different method (`w_04` is not `w_4`).
    if (len(text) > 1 && text.s[0] == '0') {
        return false;
    }
    float steps = (float)n + (half ? 0.5f : 0.f);
    *out = Px(sign * steps * 0.25f * kRem);
    return true;
}

// corner_suffixes: none, xs, sm, md, lg, xl, 2xl, 3xl, full.
static bool ParseCornerSuffix(Str text, Len* out) {
    static const struct {
        const char* name;
        float px;
    } corners[] = {
        {"none", 0},           {"xs", 0.125f * kRem}, {"sm", 0.25f * kRem},
        {"md", 0.375f * kRem}, {"lg", 0.5f * kRem},   {"xl", 0.75f * kRem},
        {"2xl", 1.f * kRem},   {"3xl", 1.5f * kRem},  {"full", 9999.f},
    };
    for (const auto& c : corners) {
        if (StrEq(text, c.name)) {
            *out = Px(c.px);
            return true;
        }
    }
    return false;
}

// border_suffixes: 0 to 12, 16, 20, 24 and 32 pixels.
static bool ParseBorderSuffix(Str text, Len* out) {
    int n = 0;
    if (!Digits(text, &n) || (len(text) > 1 && text.s[0] == '0')) {
        return false;
    }
    if (n > 12 && n != 16 && n != 20 && n != 24 && n != 32) {
        return false;
    }
    *out = Px((float)n);
    return true;
}

// Splits `name` at each `_` in turn into a family prefix and a suffix the
// family's grammar accepts: `gap_x_2` tries `gap` + `x_2`, then `gap_x` + `2`.
static const Family* ParseFamily(Str name, Len* out) {
    for (int i = 1; i < len(name) - 1; i++) {
        if (name.s[i] != '_') {
            continue;
        }
        const Family* family = FindFamily(Str(name.s, i));
        if (!family) {
            continue;
        }
        Str suffix(name.s + i + 1, len(name) - i - 1);
        bool ok = false;
        switch (family->suffix) {
            case Suffix::Box:
                ok = ParseBoxSuffix(suffix, family->arg == Arg::Length, out);
                break;
            case Suffix::Corner:
                ok = ParseCornerSuffix(suffix, out);
                break;
            case Suffix::Border:
                ok = ParseBorderSuffix(suffix, out);
                break;
        }
        if (ok) {
            return family;
        }
    }
    return nullptr;
}

// ─── applying a family's length ──────────────────────────────────────────

// A size in pixels. CSS has no negative size, and this Style spells `auto`
// and `100%` as -1 and -2, so `w_neg_4` is held at zero rather than read as
// one of those.
static float SizePx(float v) {
    return v < 0 ? 0.f : v;
}

// An inset in pixels. -1 is kAuto here, so `top_neg_px()` — the one ramp
// value that lands on it — is nudged off it by a hair.
static float InsetPx(float v) {
    return v == kAuto ? -1.0001f : v;
}

// Fractions this Style cannot hold for a field: `min_w_1_2`, `p_1_2`,
// `gap_full`, `m_1_3`. GPUI resolves them against the parent; this tree's
// Style carries those fields in pixels only, so the name is accepted and
// changes nothing. `full` of a min or max size is the exception, since kFill
// there already means relative(1.).

static void SetWidth(Style& s, Len l) {
    switch (l.kind) {
        case LenKind::Px:
            s.width = SizePx(l.v);
            s.widthFrac = 0;
            break;
        case LenKind::Frac:
            s.width = l.v == 1.f ? kFill : (l.v < 0 ? 0.f : kAuto);
            s.widthFrac = l.v > 0 && l.v != 1.f ? l.v : 0;
            break;
        case LenKind::Auto:
            s.width = kAuto;
            s.widthFrac = 0;
            break;
    }
}

static void SetHeight(Style& s, Len l) {
    switch (l.kind) {
        case LenKind::Px:
            s.height = SizePx(l.v);
            s.heightFrac = 0;
            break;
        case LenKind::Frac:
            s.height = l.v == 1.f ? kFill : (l.v < 0 ? 0.f : kAuto);
            s.heightFrac = l.v > 0 && l.v != 1.f ? l.v : 0;
            break;
        case LenKind::Auto:
            s.height = kAuto;
            s.heightFrac = 0;
            break;
    }
}

static void SetMin(float* field, Len l) {
    if (l.kind == LenKind::Px) {
        *field = SizePx(l.v);
    } else if (l.kind == LenKind::Auto) {
        *field = kAuto;
    } else if (l.v == 1.f) {
        *field = kFill;
    }
}

static void SetMaxW(Style& s, Len l) {
    if (l.kind == LenKind::Px) {
        s.maxW = SizePx(l.v);
        s.maxWFrac = 0;
    } else if (l.kind == LenKind::Auto) {
        s.maxW = 1e9f;
        s.maxWFrac = 0;
    } else if (l.v == 1.f) {
        s.maxW = kFill;
        s.maxWFrac = 0;
    } else if (l.v > 0) {
        s.maxW = 1e9f;
        s.maxWFrac = l.v;
    } else {
        s.maxW = 0;
        s.maxWFrac = 0;
    }
}

static void SetMaxH(Style& s, Len l) {
    if (l.kind == LenKind::Px) {
        s.maxH = SizePx(l.v);
    } else if (l.kind == LenKind::Auto) {
        s.maxH = 1e9f;
    } else if (l.v == 1.f) {
        s.maxH = kFill;
    }
}

static void SetMargin(Style& s, Len l, bool t, bool r, bool b, bool left) {
    if (l.kind == LenKind::Frac) {
        return;
    }
    bool automatic = l.kind == LenKind::Auto;
    float v = automatic ? 0.f : l.v;
    uint8_t bits =
        (uint8_t)((t ? kMarginAutoT : 0) | (r ? kMarginAutoR : 0) |
                  (b ? kMarginAutoB : 0) | (left ? kMarginAutoL : 0));
    if (t) s.margin.top = v;
    if (r) s.margin.right = v;
    if (b) s.margin.bottom = v;
    if (left) s.margin.left = v;
    if (automatic) {
        s.marginAuto |= bits;
    } else {
        s.marginAuto &= (uint8_t)~bits;
    }
}

static void SetPad(Style& s, Len l, bool t, bool r, bool b, bool left) {
    if (l.kind != LenKind::Px) {
        return;
    }
    if (t) s.pad.top = l.v;
    if (r) s.pad.right = l.v;
    if (b) s.pad.bottom = l.v;
    if (left) s.pad.left = l.v;
}

static void SetInset(float* px, float* rel, Len l) {
    switch (l.kind) {
        case LenKind::Px:
            *px = InsetPx(l.v);
            *rel = 0;
            break;
        case LenKind::Frac:
            *px = 0;
            *rel = l.v;
            break;
        case LenKind::Auto:
            *px = kAuto;
            *rel = 0;
            break;
    }
}

static void SetCorners(Style& s, float v, bool tl, bool tr, bool br, bool bl) {
    if (tl && tr && br && bl) {
        s.radius = v;
        s.corners = {v, v, v, v};
        s.hasCorners = false;
        return;
    }
    if (!s.hasCorners) {
        s.corners = {s.radius, s.radius, s.radius, s.radius};
    }
    if (tl) s.corners.tl = v;
    if (tr) s.corners.tr = v;
    if (br) s.corners.br = v;
    if (bl) s.corners.bl = v;
    s.hasCorners = true;
    // What El::Corners keeps in `radius` for the readers that know one number.
    s.radius = s.corners.tl > s.corners.tr ? s.corners.tl : s.corners.tr;
}

// The all-round `border` and the per-edge widths are held apart here and an
// edge draws the larger of the two, while GPUI has one width per edge. So
// setting all four clears the per-edge ones, and narrowing one edge below
// the all-round width first spreads that width onto the four edges.
static void SetBorderAll(Style& s, float v) {
    s.border = v;
    s.borderT = s.borderB = s.borderL = s.borderR = 0;
}

static void SetBorderEdges(Style& s, float v, bool t, bool r, bool b,
                           bool left) {
    if (s.border > v) {
        float all = s.border;
        s.borderT = s.borderT > all ? s.borderT : all;
        s.borderB = s.borderB > all ? s.borderB : all;
        s.borderL = s.borderL > all ? s.borderL : all;
        s.borderR = s.borderR > all ? s.borderR : all;
        s.border = 0;
    }
    if (t) s.borderT = v;
    if (r) s.borderR = v;
    if (b) s.borderB = v;
    if (left) s.borderL = v;
}

static void ApplyFamily(El* e, Fam fam, Len l) {
    Style& s = e->style;
    switch (fam) {
        case Fam::W:
            SetWidth(s, l);
            break;
        case Fam::H:
            SetHeight(s, l);
            break;
        case Fam::Size:
            SetWidth(s, l);
            SetHeight(s, l);
            break;
        case Fam::MinW:
            SetMin(&s.minW, l);
            break;
        case Fam::MinH:
            SetMin(&s.minH, l);
            break;
        case Fam::MinSize:
            SetMin(&s.minW, l);
            SetMin(&s.minH, l);
            break;
        case Fam::MaxW:
            SetMaxW(s, l);
            break;
        case Fam::MaxH:
            SetMaxH(s, l);
            break;
        case Fam::MaxSize:
            SetMaxW(s, l);
            SetMaxH(s, l);
            break;
        // gpui's gap.width is the gap between columns — gap_x.
        case Fam::Gap:
            if (l.kind == LenKind::Px) s.gapX = s.gapY = l.v;
            break;
        case Fam::GapX:
            if (l.kind == LenKind::Px) s.gapX = l.v;
            break;
        case Fam::GapY:
            if (l.kind == LenKind::Px) s.gapY = l.v;
            break;
        case Fam::P:
            SetPad(s, l, true, true, true, true);
            break;
        case Fam::Pt:
            SetPad(s, l, true, false, false, false);
            break;
        case Fam::Pb:
            SetPad(s, l, false, false, true, false);
            break;
        case Fam::Px:
            SetPad(s, l, false, true, false, true);
            break;
        case Fam::Py:
            SetPad(s, l, true, false, true, false);
            break;
        case Fam::Pl:
            SetPad(s, l, false, false, false, true);
            break;
        case Fam::Pr:
            SetPad(s, l, false, true, false, false);
            break;
        case Fam::M:
            SetMargin(s, l, true, true, true, true);
            break;
        case Fam::Mt:
            SetMargin(s, l, true, false, false, false);
            break;
        case Fam::Mb:
            SetMargin(s, l, false, false, true, false);
            break;
        case Fam::My:
            SetMargin(s, l, true, false, true, false);
            break;
        case Fam::Mx:
            SetMargin(s, l, false, true, false, true);
            break;
        case Fam::Ml:
            SetMargin(s, l, false, false, false, true);
            break;
        case Fam::Mr:
            SetMargin(s, l, false, true, false, false);
            break;
        case Fam::Inset:
            SetInset(&s.absTop, &s.absTopRel, l);
            SetInset(&s.absRight, &s.absRightRel, l);
            SetInset(&s.absBottom, &s.absBottomRel, l);
            SetInset(&s.absLeft, &s.absLeftRel, l);
            break;
        case Fam::Top:
            SetInset(&s.absTop, &s.absTopRel, l);
            break;
        case Fam::Bottom:
            SetInset(&s.absBottom, &s.absBottomRel, l);
            break;
        case Fam::Left:
            SetInset(&s.absLeft, &s.absLeftRel, l);
            break;
        case Fam::Right:
            SetInset(&s.absRight, &s.absRightRel, l);
            break;
        case Fam::Rounded:
            SetCorners(s, l.v, true, true, true, true);
            break;
        case Fam::RoundedT:
            SetCorners(s, l.v, true, true, false, false);
            break;
        case Fam::RoundedB:
            SetCorners(s, l.v, false, false, true, true);
            break;
        case Fam::RoundedL:
            SetCorners(s, l.v, true, false, false, true);
            break;
        case Fam::RoundedR:
            SetCorners(s, l.v, false, true, true, false);
            break;
        case Fam::RoundedTl:
            SetCorners(s, l.v, true, false, false, false);
            break;
        case Fam::RoundedTr:
            SetCorners(s, l.v, false, true, false, false);
            break;
        case Fam::RoundedBl:
            SetCorners(s, l.v, false, false, false, true);
            break;
        case Fam::RoundedBr:
            SetCorners(s, l.v, false, false, true, false);
            break;
        case Fam::Border:
            SetBorderAll(s, l.v);
            break;
        case Fam::BorderT:
            SetBorderEdges(s, l.v, true, false, false, false);
            break;
        case Fam::BorderB:
            SetBorderEdges(s, l.v, false, false, true, false);
            break;
        case Fam::BorderL:
            SetBorderEdges(s, l.v, false, false, false, true);
            break;
        case Fam::BorderR:
            SetBorderEdges(s, l.v, false, true, false, false);
            break;
        case Fam::BorderX:
            SetBorderEdges(s, l.v, false, true, false, true);
            break;
        case Fam::BorderY:
            SetBorderEdges(s, l.v, true, false, true, false);
            break;
    }
}

static uint32_t FamilyFields(Fam fam) {
    switch (fam) {
        case Fam::W:
            return StyleFieldWidth;
        case Fam::H:
            return StyleFieldHeight;
        case Fam::Size:
            return StyleFieldWidth | StyleFieldHeight;
        case Fam::Gap:
        case Fam::GapX:
        case Fam::GapY:
            return StyleFieldGap;
        case Fam::P:
        case Fam::Pt:
        case Fam::Pb:
        case Fam::Px:
        case Fam::Py:
        case Fam::Pl:
        case Fam::Pr:
            return StyleFieldPad;
        case Fam::M:
        case Fam::Mt:
        case Fam::Mb:
        case Fam::My:
        case Fam::Mx:
        case Fam::Ml:
        case Fam::Mr:
            return StyleFieldMargin;
        case Fam::Rounded:
        case Fam::RoundedT:
        case Fam::RoundedB:
        case Fam::RoundedL:
        case Fam::RoundedR:
        case Fam::RoundedTl:
        case Fam::RoundedTr:
        case Fam::RoundedBl:
        case Fam::RoundedBr:
            return StyleFieldRadius;
        case Fam::Border:
            return StyleFieldBorder;
        case Fam::BorderT:
            return StyleFieldBorderT;
        case Fam::BorderB:
            return StyleFieldBorderB;
        case Fam::BorderL:
            return StyleFieldBorderL;
        case Fam::BorderR:
            return StyleFieldBorderR;
        case Fam::BorderX:
            return StyleFieldBorderL | StyleFieldBorderR;
        case Fam::BorderY:
            return StyleFieldBorderT | StyleFieldBorderB;
        default:
            return 0;
    }
}

// ─── keywords ────────────────────────────────────────────────────────────

static void SetAlignContent(El* e, taffy::AlignContentKeyword keyword) {
    e->style.alignContent = (uint8_t)((uint8_t)keyword + 1);
}

static void SetAlignSelf(El* e, FlexAlign align) {
    e->style.alignSelf = align;
    e->style.hasAlignSelf = true;
}

static void SetShadows(El* e, const float (*rows)[5], int count) {
    BoxShadow shadows[2];
    for (int i = 0; i < count; i++) {
        // BoxShadow::new(x, y, hsla(0, 0, 0, alpha)), then blur and spread.
        shadows[i].x = rows[i][0];
        shadows[i].y = rows[i][1];
        shadows[i].blur = rows[i][2];
        shadows[i].spread = rows[i][3];
        shadows[i].color = RgbaHsla(0, 0, 0, rows[i][4]);
    }
    e->Shadows(shadows, count);
}

// gpui-base's debug colours: `border_1().border_color(hsl(..))` in a debug
// build and nothing at all in a release one, as `cfg!(debug_assertions)`.
// text_overflow: GPUI truncates the run to the width it is given (times any
// line clamp) before it wraps, so the run is one truncated line whatever
// white_space says.
static void TextEllipsis(El* e) {
    e->style.wrap = false;
    e->style.truncate = true;
    e->style.whiteSpaceSet = true;
}

static void DebugBorder(El* e, float h, float s, float l) {
#if defined(DEBUG)
    SetBorderAll(e->style, 1);
    e->style.borderColor = RgbaHsla(h / 360.f, s / 100.f, l / 100.f, 1);
#else
    (void)e;
    (void)h;
    (void)s;
    (void)l;
#endif
}

struct Keyword {
    const char* name;
    void (*apply)(El* e);
    uint32_t fields;
};

// Every no-argument style method that is not a ramp. Where this tree's Style
// has no field for what the method sets, the entry says so and changes
// nothing.
static const Keyword kKeywords[] = {
    // Display.
    {"block", [](El* e) { e->style.display = Display::Block; }, 0},
    {"flex", [](El* e) { e->style.display = Display::Flex; }, 0},
    {"grid", [](El* e) { e->style.display = Display::Grid; }, 0},
    {"hidden", [](El* e) { e->style.display = Display::None; }, 0},
    // gpui-base StyledExt.
    {"h_flex",
     [](El* e) {
         e->style.display = Display::Flex;
         e->style.dir = FlexDir::Row;
         e->style.align = FlexAlign::Center;
     },
     0},
    {"v_flex",
     [](El* e) {
         e->style.display = Display::Flex;
         e->style.dir = FlexDir::Col;
     },
     0},
    // Visibility.
    {"visible", [](El* e) { e->style.invisible = false; }, 0},
    {"invisible", [](El* e) { e->style.invisible = true; }, 0},
    // Position.
    {"relative",
     [](El* e) {
         e->style.absolute = false;
         e->style.fixed = false;
     },
     0},
    {"absolute", [](El* e) { e->style.absolute = true; }, 0},
    // Overflow.
    {"overflow_hidden",
     [](El* e) {
         e->style.overflowX = Overflow::Hidden;
         e->style.overflowY = Overflow::Hidden;
     },
     0},
    {"overflow_x_hidden", [](El* e) { e->style.overflowX = Overflow::Hidden; },
     0},
    {"overflow_y_hidden", [](El* e) { e->style.overflowY = Overflow::Hidden; },
     0},
    // Flex direction and wrapping.
    {"flex_row", [](El* e) { e->style.dir = FlexDir::Row; }, 0},
    {"flex_col", [](El* e) { e->style.dir = FlexDir::Col; }, 0},
    {"flex_row_reverse", [](El* e) { e->style.dir = FlexDir::RowReverse; }, 0},
    {"flex_col_reverse", [](El* e) { e->style.dir = FlexDir::ColReverse; }, 0},
    {"flex_wrap",
     [](El* e) {
         e->style.flexWrap = true;
         e->style.flexWrapReverse = false;
     },
     0},
    {"flex_wrap_reverse",
     [](El* e) {
         e->style.flexWrap = true;
         e->style.flexWrapReverse = true;
     },
     0},
    {"flex_nowrap",
     [](El* e) {
         e->style.flexWrap = false;
         e->style.flexWrapReverse = false;
     },
     0},
    // Flex item sizing.
    {"flex_1",
     [](El* e) {
         e->style.flexGrow = 1;
         e->style.flexShrink = 1;
         e->style.flexBasis = 0;
         e->style.flexBasisFrac = 0;
         e->style.flexBasisPercent = true;
     },
     0},
    {"flex_auto",
     [](El* e) {
         e->style.flexGrow = 1;
         e->style.flexShrink = 1;
         e->style.flexBasis = kAuto;
         e->style.flexBasisFrac = 0;
         e->style.flexBasisPercent = false;
     },
     0},
    {"flex_initial",
     [](El* e) {
         e->style.flexGrow = 0;
         e->style.flexShrink = 1;
         e->style.flexBasis = kAuto;
         e->style.flexBasisFrac = 0;
         e->style.flexBasisPercent = false;
     },
     0},
    {"flex_none",
     [](El* e) {
         e->style.flexGrow = 0;
         e->style.flexShrink = 0;
         e->style.flexBasis = kAuto;
         e->style.flexBasisFrac = 0;
         e->style.flexBasisPercent = false;
     },
     0},
    {"flex_grow_0", [](El* e) { e->style.flexGrow = 0; }, 0},
    {"flex_grow_1", [](El* e) { e->style.flexGrow = 1; }, 0},
    {"flex_shrink_0", [](El* e) { e->style.flexShrink = 0; }, 0},
    {"flex_shrink_1", [](El* e) { e->style.flexShrink = 1; }, 0},
    // align_items. gpui's items_start is FlexStart; this Style's Start is
    // what the rest of the tree has always meant by it.
    {"items_start", [](El* e) { e->style.align = FlexAlign::Start; }, 0},
    {"items_end", [](El* e) { e->style.align = FlexAlign::End; }, 0},
    {"items_center", [](El* e) { e->style.align = FlexAlign::Center; }, 0},
    {"items_baseline", [](El* e) { e->style.align = FlexAlign::Baseline; }, 0},
    {"items_stretch", [](El* e) { e->style.align = FlexAlign::Stretch; }, 0},
    // align_self. FlexAlign has no flex-start/flex-end apart from start/end;
    // they differ only in a reversed direction.
    {"self_start", [](El* e) { SetAlignSelf(e, FlexAlign::Start); }, 0},
    {"self_end", [](El* e) { SetAlignSelf(e, FlexAlign::End); }, 0},
    {"self_flex_start", [](El* e) { SetAlignSelf(e, FlexAlign::Start); }, 0},
    {"self_flex_end", [](El* e) { SetAlignSelf(e, FlexAlign::End); }, 0},
    {"self_center", [](El* e) { SetAlignSelf(e, FlexAlign::Center); }, 0},
    {"self_baseline", [](El* e) { SetAlignSelf(e, FlexAlign::Baseline); }, 0},
    {"self_stretch", [](El* e) { SetAlignSelf(e, FlexAlign::Stretch); }, 0},
    // justify_content.
    {"justify_start", [](El* e) { e->style.justify = Justify::Start; }, 0},
    {"justify_end", [](El* e) { e->style.justify = Justify::End; }, 0},
    {"justify_center", [](El* e) { e->style.justify = Justify::Center; }, 0},
    {"justify_between", [](El* e) { e->style.justify = Justify::SpaceBetween; },
     0},
    {"justify_around", [](El* e) { e->style.justify = Justify::SpaceAround; },
     0},
    {"justify_evenly", [](El* e) { e->style.justify = Justify::SpaceEvenly; },
     0},
    // align_content. content_normal is gpui's `None`.
    {"content_normal", [](El* e) { e->style.alignContent = 0; }, 0},
    {"content_center",
     [](El* e) { SetAlignContent(e, taffy::AlignContentKeyword::Center); }, 0},
    {"content_start",
     [](El* e) { SetAlignContent(e, taffy::AlignContentKeyword::FlexStart); },
     0},
    {"content_end",
     [](El* e) { SetAlignContent(e, taffy::AlignContentKeyword::FlexEnd); }, 0},
    {"content_between",
     [](El* e) {
         SetAlignContent(e, taffy::AlignContentKeyword::SpaceBetween);
     },
     0},
    {"content_around",
     [](El* e) { SetAlignContent(e, taffy::AlignContentKeyword::SpaceAround); },
     0},
    {"content_evenly",
     [](El* e) { SetAlignContent(e, taffy::AlignContentKeyword::SpaceEvenly); },
     0},
    {"content_stretch",
     [](El* e) { SetAlignContent(e, taffy::AlignContentKeyword::Stretch); }, 0},
    // Grid placement. This Style carries no grid lines or spans, so these
    // are accepted and change nothing.
    {"col_start_auto", [](El*) {}, 0},
    {"col_end_auto", [](El*) {}, 0},
    {"col_span_full", [](El*) {}, 0},
    {"row_start_auto", [](El*) {}, 0},
    {"row_end_auto", [](El*) {}, 0},
    {"row_span_full", [](El*) {}, 0},
    // Box.
    {"aspect_square", [](El* e) { e->style.aspect = 1; }, 0},
    {"border_dashed", [](El* e) { e->style.borderDashed = true; }, 0},
    // Shadows: styles.rs box_shadow_style_methods, {x, y, blur, spread,
    // alpha} per BoxShadow.
    {"shadow_none", [](El* e) { e->Shadows(nullptr, 0); }, 0},
    {"shadow_2xs",
     [](El* e) {
         static const float rows[][5] = {{0, 1, 0, 0, 0.05f}};
         SetShadows(e, rows, 1);
     },
     0},
    {"shadow_xs",
     [](El* e) {
         static const float rows[][5] = {{0, 1, 2, 0, 0.05f}};
         SetShadows(e, rows, 1);
     },
     0},
    {"shadow_sm",
     [](El* e) {
         static const float rows[][5] = {{0, 1, 3, 0, 0.1f},
                                         {0, 1, 2, -1, 0.1f}};
         SetShadows(e, rows, 2);
     },
     0},
    {"shadow_md",
     [](El* e) {
         static const float rows[][5] = {{0, 4, 6, -1, 0.1f},
                                         {0, 2, 4, -2, 0.1f}};
         SetShadows(e, rows, 2);
     },
     0},
    {"shadow_lg",
     [](El* e) {
         static const float rows[][5] = {{0, 10, 15, -3, 0.1f},
                                         {0, 4, 6, -4, 0.1f}};
         SetShadows(e, rows, 2);
     },
     0},
    {"shadow_xl",
     [](El* e) {
         static const float rows[][5] = {{0, 20, 25, -5, 0.1f},
                                         {0, 8, 10, -6, 0.1f}};
         SetShadows(e, rows, 2);
     },
     0},
    {"shadow_2xl",
     [](El* e) {
         static const float rows[][5] = {{0, 25, 50, -12, 0.25f}};
         SetShadows(e, rows, 1);
     },
     0},
    // Cursors: styles.rs cursor_style_methods.
    {"cursor_default", [](El* e) { e->Cursor(CursorKind::Arrow); }, 0},
    {"cursor_pointer", [](El* e) { e->Cursor(CursorKind::Pointer); }, 0},
    {"cursor_text", [](El* e) { e->Cursor(CursorKind::IBeam); }, 0},
    {"cursor_move", [](El* e) { e->Cursor(CursorKind::ClosedHand); }, 0},
    {"cursor_not_allowed", [](El* e) { e->Cursor(CursorKind::NotAllowed); }, 0},
    {"cursor_context_menu", [](El* e) { e->Cursor(CursorKind::ContextMenu); },
     0},
    {"cursor_crosshair", [](El* e) { e->Cursor(CursorKind::Crosshair); }, 0},
    {"cursor_vertical_text",
     [](El* e) { e->Cursor(CursorKind::IBeamVertical); }, 0},
    {"cursor_alias", [](El* e) { e->Cursor(CursorKind::DragLink); }, 0},
    {"cursor_copy", [](El* e) { e->Cursor(CursorKind::DragCopy); }, 0},
    {"cursor_no_drop", [](El* e) { e->Cursor(CursorKind::NotAllowed); }, 0},
    {"cursor_grab", [](El* e) { e->Cursor(CursorKind::OpenHand); }, 0},
    {"cursor_grabbing", [](El* e) { e->Cursor(CursorKind::ClosedHand); }, 0},
    {"cursor_ew_resize", [](El* e) { e->Cursor(CursorKind::ResizeLeftRight); },
     0},
    {"cursor_ns_resize", [](El* e) { e->Cursor(CursorKind::ResizeUpDown); }, 0},
    {"cursor_nesw_resize",
     [](El* e) { e->Cursor(CursorKind::ResizeUpRightDownLeft); }, 0},
    {"cursor_nwse_resize",
     [](El* e) { e->Cursor(CursorKind::ResizeUpLeftDownRight); }, 0},
    {"cursor_col_resize", [](El* e) { e->Cursor(CursorKind::ColResize); }, 0},
    {"cursor_row_resize", [](El* e) { e->Cursor(CursorKind::RowResize); }, 0},
    {"cursor_n_resize", [](El* e) { e->Cursor(CursorKind::ResizeUp); }, 0},
    {"cursor_e_resize", [](El* e) { e->Cursor(CursorKind::ResizeRight); }, 0},
    {"cursor_s_resize", [](El* e) { e->Cursor(CursorKind::ResizeDown); }, 0},
    {"cursor_w_resize", [](El* e) { e->Cursor(CursorKind::ResizeLeft); }, 0},
    // Text size: styled.rs, in rems.
    {"text_xs", [](El* e) { e->style.fontSize = 0.75f * kRem; },
     StyleFieldFontSize},
    {"text_sm", [](El* e) { e->style.fontSize = 0.875f * kRem; },
     StyleFieldFontSize},
    {"text_base", [](El* e) { e->style.fontSize = 1.f * kRem; },
     StyleFieldFontSize},
    {"text_lg", [](El* e) { e->style.fontSize = 1.125f * kRem; },
     StyleFieldFontSize},
    {"text_xl", [](El* e) { e->style.fontSize = 1.25f * kRem; },
     StyleFieldFontSize},
    {"text_2xl", [](El* e) { e->style.fontSize = 1.5f * kRem; },
     StyleFieldFontSize},
    {"text_3xl", [](El* e) { e->style.fontSize = 1.875f * kRem; },
     StyleFieldFontSize},
    // Text alignment.
    {"text_left", [](El* e) { e->TextAlignment(TextAlign::Left); }, 0},
    {"text_center", [](El* e) { e->TextAlignment(TextAlign::Center); }, 0},
    {"text_right", [](El* e) { e->TextAlignment(TextAlign::Right); }, 0},
    // White space and overflow. This tree's truncation is one ellipsis at the
    // end of one line, so the start and middle ellipses truncate there too.
    {"whitespace_normal",
     [](El* e) {
         e->style.wrap = true;
         e->style.whiteSpaceSet = true;
     },
     0},
    {"whitespace_nowrap",
     [](El* e) {
         e->style.wrap = false;
         e->style.whiteSpaceSet = true;
     },
     0},
    {"text_ellipsis", TextEllipsis, 0},
    {"text_ellipsis_start", TextEllipsis, 0},
    {"text_ellipsis_middle", TextEllipsis, 0},
    {"truncate",
     [](El* e) {
         e->style.overflowX = Overflow::Hidden;
         e->style.overflowY = Overflow::Hidden;
         e->style.wrap = false;
         e->style.truncate = true;
         e->style.whiteSpaceSet = true;
     },
     0},
    // Font style and decoration. The underline here is a single straight
    // line of the text's own weight: a thickness or a wavy line changes
    // nothing, and a zero thickness is no line.
    {"italic", [](El* e) { e->style.italic = true; }, 0},
    {"not_italic", [](El* e) { e->style.italic = false; }, 0},
    {"underline", [](El* e) { e->style.underline = true; }, 0},
    {"line_through", [](El* e) { e->style.strike = true; }, 0},
    {"text_decoration_none", [](El* e) { e->style.underline = false; }, 0},
    {"text_decoration_solid", [](El*) {}, 0},
    {"text_decoration_wavy", [](El*) {}, 0},
    {"text_decoration_0", [](El* e) { e->style.underline = false; }, 0},
    {"text_decoration_1", [](El* e) { e->style.underline = true; }, 0},
    {"text_decoration_2", [](El* e) { e->style.underline = true; }, 0},
    {"text_decoration_4", [](El* e) { e->style.underline = true; }, 0},
    {"text_decoration_8", [](El* e) { e->style.underline = true; }, 0},
    // Font weight: gpui-base's font_weight! helpers (EXTRA_NULLARY).
    {"font_thin", [](El* e) { e->Weight(FontWeight::Thin); }, 0},
    {"font_extralight", [](El* e) { e->Weight(FontWeight::ExtraLight); }, 0},
    {"font_light", [](El* e) { e->Weight(FontWeight::Light); }, 0},
    {"font_normal", [](El* e) { e->Weight(FontWeight::Normal); }, 0},
    {"font_medium", [](El* e) { e->Weight(FontWeight::Medium); }, 0},
    {"font_semibold", [](El* e) { e->Weight(FontWeight::Semibold); }, 0},
    {"font_bold", [](El* e) { e->Weight(FontWeight::Bold); }, 0},
    {"font_extrabold", [](El* e) { e->Weight(FontWeight::ExtraBold); }, 0},
    {"font_black", [](El* e) { e->Weight(FontWeight::Black); }, 0},
    // Debugging. gpui's debug borders are painted by its own debug build's
    // renderer, which this tree does not have.
    {"debug", [](El*) {}, 0},
    {"debug_below", [](El*) {}, 0},
    {"debug_red", [](El* e) { DebugBorder(e, 0.f, 72.2f, 50.6f); }, 0},
    {"debug_blue", [](El* e) { DebugBorder(e, 217.2f, 91.2f, 59.8f); }, 0},
    {"debug_yellow", [](El* e) { DebugBorder(e, 47.9f, 95.8f, 53.1f); }, 0},
    {"debug_green", [](El* e) { DebugBorder(e, 142.1f, 70.6f, 45.3f); }, 0},
    {"debug_pink", [](El* e) { DebugBorder(e, 330.4f, 81.2f, 60.4f); }, 0},
};

static const Keyword* FindKeyword(Str name) {
    static const SortedIndex<Keyword, dimof(kKeywords)> index(kKeywords);
    return index.Find(name);
}

// ─── parametric styles ───────────────────────────────────────────────────

// PARAM_STYLES, less the ramp families (whose prefixes are their parametric
// names too).
struct ParamName {
    const char* name;
};

static const ParamName kOtherParamNames[] = {
    {"flex_grow"},   {"flex_shrink"}, {"flex_basis"}, {"bg"},
    {"text_color"},  {"text_bg"},     {"text_size"},  {"font_family"},
    {"font_weight"}, {"line_height"}, {"opacity"},    {"border_color"},
};

static bool IsOtherParam(Str name) {
    static const SortedIndex<ParamName, dimof(kOtherParamNames)> index(
        kOtherParamNames);
    return index.Find(name) != nullptr;
}

static bool ParseNumber(Str text, float* out) {
    text = StrTrimAscii(text);
    if (len(text) <= 0 || len(text) >= 64) {
        return false;
    }
    char buf[64];
    memcpy(buf, text.s, (size_t)len(text));
    buf[len(text)] = 0;
    char* end = nullptr;
    double number = strtod(buf, &end);
    if (!end || end == buf || *end || !isfinite(number)) {
        return false;
    }
    *out = (float)number;
    return true;
}

// parse_length: a bare number is pixels; a string says "auto", "50%",
// "1rem" or "12px".
static bool ParseLength(const Bridged& value, Str method, Len* out,
                        ShellError* error) {
    if (value.kind == BridgedKind::String) {
        Str text = StrTrimAscii(value.string);
        if (StrEq(text, StrL("auto"))) {
            *out = Len{LenKind::Auto, 0};
            return true;
        }
        Str number = text;
        float scale = 0;
        LenKind kind = LenKind::Px;
        if (StrEndsWith(text, "%")) {
            number.len -= 1;
            kind = LenKind::Frac;
            scale = 0.01f;
        } else if (StrEndsWith(text, "rem")) {
            number.len -= 3;
            scale = kRem;
        } else if (StrEndsWith(text, "px")) {
            number.len -= 2;
            scale = 1;
        } else {
            ShellErrorSet(error,
                          fmt("`%s` expects a length: a number of pixels, or a "
                              "string like \"50%%\", \"12px\", \"1rem\" or "
                              "\"auto\"; got \"%s\"",
                              method, text));
            return false;
        }
        float v = 0;
        if (!ParseNumber(number, &v)) {
            ShellErrorSet(error, fmt("`%s` could not read a number in the "
                                     "length \"%s\"",
                                     method, text));
            return false;
        }
        *out = Len{kind, v * scale};
        return true;
    }
    float v = 0;
    if (!BridgedAsPixels(value, &v, error)) {
        return false;
    }
    *out = Px(v);
    return true;
}

static bool NarrowLength(Len l, Arg arg, Str method, ShellError* error) {
    if (arg == Arg::Definite && l.kind == LenKind::Auto) {
        ShellErrorSet(error, fmt("`%s` cannot be \"auto\"; it expects a "
                                 "definite length such as 12 or \"50%%\"",
                                 method));
        return false;
    }
    if (arg == Arg::Absolute && l.kind != LenKind::Px) {
        ShellErrorSet(error, fmt("`%s` expects an absolute length such as 8 or "
                                 "\"0.5rem\"; percentages and \"auto\" are not "
                                 "allowed here",
                                 method));
        return false;
    }
    return true;
}

static bool Color(const Bridged& value, Rgba* out, ShellError* error) {
    Hsla color = {};
    if (!BridgedAsColor(value, &color, error)) {
        return false;
    }
    *out = HslaToRgba(color);
    return true;
}

static bool ApplyOtherParam(El* e, Str name, const Bridged& value,
                            ShellError* error) {
    Style& s = e->style;
    float number = 0;
    Rgba color = {};
    Len l = {};
    if (StrEq(name, StrL("flex_grow"))) {
        if (!BridgedAsF32(value, &number, error)) return false;
        s.flexGrow = number;
    } else if (StrEq(name, StrL("flex_shrink"))) {
        if (!BridgedAsF32(value, &number, error)) return false;
        s.flexShrink = number;
    } else if (StrEq(name, StrL("flex_basis"))) {
        if (!ParseLength(value, name, &l, error)) return false;
        s.flexBasis = l.kind == LenKind::Px ? l.v : kAuto;
        s.flexBasisFrac = l.kind == LenKind::Frac ? l.v : 0;
        s.flexBasisPercent = l.kind == LenKind::Frac;
    } else if (StrEq(name, StrL("bg"))) {
        if (!Color(value, &color, error)) return false;
        e->Bg(color);
    } else if (StrEq(name, StrL("text_color"))) {
        if (!Color(value, &color, error)) return false;
        e->Fg(color);
    } else if (StrEq(name, StrL("text_bg"))) {
        // The text style's background_color: this tree paints no background
        // behind a text run, so the colour is checked and goes nowhere.
        if (!Color(value, &color, error)) return false;
    } else if (StrEq(name, StrL("text_size"))) {
        if (!ParseLength(value, name, &l, error) ||
            !NarrowLength(l, Arg::Absolute, name, error))
            return false;
        s.fontSize = l.v;
    } else if (StrEq(name, StrL("font_family"))) {
        Str family;
        if (!BridgedAsString(value, &family, error)) return false;
        // The text system here has the UI face and one monospace face.
        if (StrEq(family, StrL("monospace"))) e->Mono();
    } else if (StrEq(name, StrL("font_weight"))) {
        if (!BridgedAsF32(value, &number, error)) return false;
        if (!isfinite(number) || number < 100.f || number > 900.f) {
            ShellErrorSet(error, fmt("`%s` expects a finite number between "
                                     "100 and 900; got %g",
                                     name, (double)number));
            return false;
        }
        e->Weight((FontWeight)(int)number);
    } else if (StrEq(name, StrL("line_height"))) {
        // A bare number is a multiplier, not pixels.
        if (value.kind == BridgedKind::Number) {
            s.lineHeight = (float)value.number;
        } else {
            if (!ParseLength(value, name, &l, error) ||
                !NarrowLength(l, Arg::Definite, name, error))
                return false;
            if (l.kind == LenKind::Frac) {
                s.lineHeight = l.v;
            } else {
                // A length in pixels, held as the multiple of the font size
                // it is, since that is all this Style keeps.
                s.lineHeight = l.v / (s.fontSize > 0 ? s.fontSize : kRem);
            }
        }
    } else if (StrEq(name, StrL("opacity"))) {
        if (!BridgedAsF32(value, &number, error)) return false;
        e->Opacity(number);
    } else if (StrEq(name, StrL("border_color"))) {
        if (!Color(value, &color, error)) return false;
        s.borderColor = color;
    } else {
        return false;
    }
    return true;
}

} // namespace gpui::shell::style

namespace gpui::shell {

bool IsParamStyleName(Str name) {
    return style::FindFamily(name) != nullptr || style::IsOtherParam(name);
}

bool IsNullaryStyleName(Str name) {
    if (style::FindKeyword(name)) {
        return true;
    }
    style::Len l;
    return style::ParseFamily(name, &l) != nullptr;
}

bool ApplyNullaryStyle(El* element, Str name) {
    if (const style::Keyword* keyword = style::FindKeyword(name)) {
        if (element) {
            keyword->apply(element);
        }
        return true;
    }
    style::Len l;
    const style::Family* family = style::ParseFamily(name, &l);
    if (!family) {
        return false;
    }
    if (element) {
        style::ApplyFamily(element, family->fam, l);
    }
    return true;
}

bool ApplyParamStyle(El* element, const SpecOp& op, ShellError* error) {
    const style::Family* family = style::FindFamily(op.name);
    if (!family && !style::IsOtherParam(op.name)) {
        return false;
    }
    Bridged value;
    if (!BridgedArg(op.args, op.argCount, 0, op.name, &value, error)) {
        return false;
    }
    if (!family) {
        return style::ApplyOtherParam(element, op.name, value, error);
    }
    style::Len l;
    if (!style::ParseLength(value, op.name, &l, error) ||
        !style::NarrowLength(l, family->arg, op.name, error)) {
        return false;
    }
    style::ApplyFamily(element, family->fam, l);
    return true;
}

uint32_t StyleFieldsOf(Str name) {
    if (StrEq(name, StrL("bg"))) return StyleFieldBg;
    if (StrEq(name, StrL("text_color"))) return StyleFieldColor;
    if (StrEq(name, StrL("border_color"))) return StyleFieldBorderColor;
    if (StrEq(name, StrL("opacity"))) return StyleFieldOpacity;
    if (StrEq(name, StrL("text_size"))) return StyleFieldFontSize;
    if (const style::Family* family = style::FindFamily(name)) {
        return style::FamilyFields(family->fam);
    }
    if (const style::Keyword* keyword = style::FindKeyword(name)) {
        return keyword->fields;
    }
    style::Len l;
    if (const style::Family* family = style::ParseFamily(name, &l)) {
        return style::FamilyFields(family->fam);
    }
    return 0;
}

} // namespace gpui::shell
