#include "ui/label.h"

namespace gpui {

namespace component {

HighlightsMatch HighlightsMatch::Prefix(Str text) {
    HighlightsMatch out;
    out.kind = HighlightsMatchKind::Prefix;
    out.text = text;
    return out;
}

HighlightsMatch HighlightsMatch::Full(Str text) {
    HighlightsMatch out;
    out.text = text;
    return out;
}

HighlightsMatch HighlightsMatch::From(Str text) {
    return Full(text);
}

Str HighlightsMatch::AsStr() const {
    return text;
}

bool HighlightsMatch::IsPrefix() const {
    return kind == HighlightsMatchKind::Prefix;
}

Label* Label::New(Ctx* cx, Str text) {
    Arena* a = cx->a;
    Label* l = ArenaNew<Label>(a);
    l->a = a;
    l->cx = cx;
    l->text = text;
    return l;
}

Label* Label::Secondary(Str s) {
    secondary = s;
    hasSecondary = true;
    return this;
}

Label* Label::Masked(bool v) {
    masked = v;
    return this;
}

Label* Label::Semibold() {
    semibold = true;
    return this;
}

Label* Label::Font(float px) {
    font = px;
    return this;
}

Label* Label::Highlights(HighlightsMatch matched) {
    highlight = matched;
    hasHighlight = true;
    return this;
}

Label* Label::Highlights(Str matched, bool prefix) {
    return Highlights(prefix ? HighlightsMatch::Prefix(matched)
                             : HighlightsMatch::Full(matched));
}

Label* Label::TextCenter() {
    align = 1;
    return this;
}

Label* Label::TextRight() {
    align = 2;
    return this;
}

Label* Label::LineHeight(float mult) {
    lineHeight = mult;
    return this;
}

Str Label::FullText() const {
    if (!hasSecondary) {
        return text;
    }
    int n = len(text) + 1 + len(secondary);
    char* out = (char*)Alloc(a, n);
    if (!out) {
        return {};
    }
    if (len(text) > 0) {
        memcpy(out, text.s, len(text));
    }
    out[len(text)] = ' ';
    if (len(secondary) > 0) {
        memcpy(out + len(text) + 1, secondary.s, len(secondary));
    }
    return Str(out, n);
}

// Common simple-case mappings keep the comparison portable without pulling
// a Unicode database into the UI layer. Equal CJK and other uncased
// codepoints compare directly. Rust's to_lowercase also has a small set of
// expanding mappings; those remain an explicit dependency-free limitation
// because their lowered offsets do not map one-to-one to original StyledText
// byte ranges either.
static uint32_t LabelLower(uint32_t c) {
    if (c >= 'A' && c <= 'Z') {
        return c + 0x20;
    }
    if ((c >= 0x00c0 && c <= 0x00d6) || (c >= 0x00d8 && c <= 0x00de)) {
        return c + 0x20;
    }
    // Latin Extended-A is predominantly alternating upper/lower pairs.
    if (c >= 0x0100 && c <= 0x012f && (c & 1) == 0) {
        return c + 1;
    }
    if (c >= 0x0132 && c <= 0x0137 && (c & 1) == 0) {
        return c + 1;
    }
    if (c >= 0x014a && c <= 0x0177 && (c & 1) == 0) {
        return c + 1;
    }
    if (c >= 0x0391 && c <= 0x03a1) {
        return c + 0x20;
    }
    if (c >= 0x03a3 && c <= 0x03ab) {
        return c + 0x20;
    }
    if (c >= 0x0410 && c <= 0x042f) {
        return c + 0x20;
    }
    if (c >= 0x0400 && c <= 0x040f) {
        return c + 0x50;
    }
    if (c >= 0x0531 && c <= 0x0556) {
        return c + 0x30;
    }
    return c;
}

// Compare equally-sized UTF-8 slices under the simple case mapping above.
// Every mapping retained here has the same encoded width as its source, so
// the byte offsets handed to TextSpan remain offsets in the original string.
static bool LabelEqI(Str left, Str right) {
    if (len(left) != len(right)) {
        return false;
    }
    int li = 0;
    int ri = 0;
    while (li < len(left) && ri < len(right)) {
        uint32_t lc = 0;
        uint32_t rc = 0;
        int ln = Utf8At(left, li, &lc);
        int rn = Utf8At(right, ri, &rc);
        if (ln <= 0 || rn <= 0 || LabelLower(lc) != LabelLower(rc)) {
            return false;
        }
        li += ln;
        ri += rn;
    }
    return li == len(left) && ri == len(right);
}

static int LabelHighlightRanges(const Label* label, Str full, int totalLength,
                                Selection* out, int capacity) {
    int count = 0;
    auto append = [&](int start, int end) {
        if (out && count < capacity) {
            out[count] = {start, end};
        }
        count++;
    };
    if (label->hasSecondary) {
        append(0, len(label->text));
        append(len(label->text), totalLength);
    }
    if (!label->hasHighlight || len(label->highlight.text) == 0) {
        return count;
    }

    Str needle = label->highlight.AsStr();
    if (!full.s || len(needle) > len(full)) {
        return count;
    }
    if (label->highlight.IsPrefix()) {
        if (LabelEqI(Str(full.s, len(needle)), needle)) {
            append(0, len(needle));
        }
        return count;
    }

    int search = 0;
    while (search + len(needle) <= len(full)) {
        int match = -1;
        for (int at = search; at + len(needle) <= len(full);) {
            if (LabelEqI(Str(full.s + at, len(needle)), needle)) {
                match = at;
                break;
            }
            uint32_t c = 0;
            int n = Utf8At(full, at, &c);
            at += n > 0 ? n : 1;
        }
        if (match < 0) {
            break;
        }
        append(match, match + len(needle));
        // Rust advances one byte from the match and then moves to the next
        // character boundary, which deliberately retains overlapping hits.
        search = match + 1;
        while (search < len(full) && ((uint8_t)full.s[search] & 0xc0) == 0x80) {
            search++;
        }
        if (search >= len(full)) {
            break;
        }
    }
    return count;
}

int Label::HighlightRanges(int totalLength, Selection* out,
                           int capacity) const {
    return LabelHighlightRanges(this, FullText(), totalLength, out, capacity);
}

static Str LabelMasked(Arena* a, Str text) {
    int chars = 0;
    for (int at = 0; at < len(text);) {
        uint32_t c = 0;
        int n = Utf8At(text, at, &c);
        at += n > 0 ? n : 1;
        chars++;
    }
    if (chars == 0) {
        return StrL("");
    }
    char* out = (char*)Alloc(a, chars * 3);
    static const char bullet[] = "\xe2\x80\xa2";
    for (int i = 0; i < chars; i++) {
        // U+2022 BULLET, the pinned MASKED string.
        char* dst = out + (size_t)i * 3;
        dst[0] = bullet[0];
        dst[1] = bullet[1];
        dst[2] = bullet[2];
    }
    return Str(out, chars * 3);
}

static int LabelSpans(Label* label, Str full, Str shown, const Theme& th,
                      TextSpan* spans, int capacity) {
    int maxRanges = len(full) + 2;
    Selection* ranges =
        (Selection*)Alloc(label->a, (int)sizeof(Selection) * maxRanges);
    int nRanges =
        LabelHighlightRanges(label, full, len(shown), ranges, maxRanges);
    int firstMatch = label->hasSecondary ? 2 : 0;
    int pointsCap = 3 + 2 * (nRanges - firstMatch);
    int* points = (int*)Alloc(label->a, (int)sizeof(int) * pointsCap);
    int nPoints = 0;
    points[nPoints++] = 0;
    points[nPoints++] = len(shown);
    if (label->hasSecondary) {
        points[nPoints++] = len(label->text);
    }
    for (int i = firstMatch; i < nRanges; i++) {
        int lo = std::max(0, std::min(len(shown), ranges[i].start));
        int hi = std::max(0, std::min(len(shown), ranges[i].end));
        points[nPoints++] = lo;
        points[nPoints++] = hi;
    }
    std::sort(points, points + nPoints);
    int unique = 0;
    for (int i = 0; i < nPoints; i++) {
        if (unique == 0 || points[i] != points[unique - 1]) {
            points[unique++] = points[i];
        }
    }

    int nSpans = 0;
    for (int i = 0; i + 1 < unique; i++) {
        int lo = points[i];
        int hi = points[i + 1];
        if (hi <= lo) {
            continue;
        }
        bool matched = false;
        for (int r = firstMatch; r < nRanges; r++) {
            if (ranges[r].start <= lo && ranges[r].end >= hi) {
                matched = true;
                break;
            }
        }
        bool muted = label->hasSecondary && lo >= len(label->text);
        if (!matched && !muted) {
            continue;
        }
        Rgba color = matched ? th.blue : th.mutedFg;
        if (nSpans > 0 && spans[nSpans - 1].hi == lo &&
            RgbaEq(spans[nSpans - 1].color, color)) {
            spans[nSpans - 1].hi = hi;
            continue;
        }
        if (nSpans >= capacity) {
            break;
        }
        spans[nSpans].lo = lo;
        spans[nSpans].hi = hi;
        spans[nSpans].color = color;
        spans[nSpans].bg = Rgba8(0, 0, 0, 0);
        nSpans++;
    }
    return nSpans;
}

El* Label::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    Str full = FullText();
    Str shown = masked ? LabelMasked(a, full) : full;

    int spanCap = len(shown) + 1;
    TextSpan* spans = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * spanCap);
    int nSpans = LabelSpans(this, full, shown, th, spans, spanCap);
    El* styled = TextEl(a, shown);
    if (nSpans > 0) {
        styled->Spans(spans, nSpans);
    }

    El* root = Div(a)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->W(kFill)
                   ->LineHeight(lineHeight)
                   ->Fg(th.foreground);
    if (font > 0) {
        root->Font(font);
    }
    if (semibold) {
        root->Semibold();
    }
    if (align == 1) {
        root->JustifyCenter();
    } else if (align == 2) {
        root->JustifyEnd();
    }
    return root->Child(styled);
}

} // namespace component
} // namespace gpui
