/* Port of crates/base/src/input/ — the text field and the editing engine
   behind it. Rust splits the module across input/input/mod.rs,
   input/textarea/mod.rs and input/base/{state,movement,selection,mode,
   mask_pattern,rope_ext,change,undo_manager}.rs; the directory is one file
   here. blink_cursor.rs stays in gpui/gpui.cpp beside the window timers it
   needs. */

#include "base/input.h"
#include "base/element_ext.h"
#include "base/global_state.h"
#include "base/number_input.h"
#include "base/text_boundary.h"
#include "base/theme.h"

namespace gpui {

InputEditorStyle InputEditorStyleResolve(const InputEditorStyle& projected,
                                         const SemanticThemeTokens& tokens) {
    InputEditorStyle out = projected;
    const ColorTokens& colors = tokens.colors;
    if (out.foreground.a == 0) out.foreground = colors.foreground;
    if (out.mutedForeground.a == 0) {
        out.mutedForeground = colors.mutedForeground;
    }
    if (out.background.a == 0) out.background = colors.surface;
    if (out.border.a == 0) out.border = colors.border;
    if (out.selection.a == 0) {
        out.selection = RgbaOpacity(colors.accent, 0.4f);
    }
    if (out.caret.a == 0) out.caret = out.foreground;
    return out;
}

El* InputBase::New(Ctx* cx, Str id, bool interactive, AccessibilityRole role) {
    Arena* a = cx->a;
    return (interactive ? Div(a)->PathId(id) : Div(a)->Id(id))
        ->Role(role)
        ->AriaDisabled(!interactive);
}

El* InputBase::New(Ctx* cx, Str id, const InputPresentation& presentation,
                   const InputStyles& styles) {
    El* element = New(cx, id, presentation.IsEditable());
    element->TrackFocus(presentation.focus);
    styles.Apply(&element->style,
                 FocusHandleIsFocused(cx->win, presentation.focus),
                 presentation.disabled);
    return element;
}

// The washes one row carries: the search matches that fall inside it, and the
// colours a document colour provider found there — element.rs paints both as
// a quad behind the glyphs, the match from `layout_search_matches` and the
// colour from `layout_document_colors`. Both sets are in document order, so a
// walk over the rows carries on where the last one left off — `*at` is where
// that is for the matches, and the colours are walked from the front, there
// being a handful of them on a screen.
static El* RowMatchWashes(Arena* a, El* el, const InputEditorStyle& style,
                          const InputState* state, int start, int len,
                          int* at) {
    int nColors = 0;
    if (state) {
        for (int i = 0; i < state->documentColors.len; i++) {
            const DocumentColor& dc = state->documentColors[i];
            if (dc.range.end > start && dc.range.start < start + len) {
                nColors++;
            }
        }
    }
    if (style.nMatches <= 0 && nColors == 0) {
        return el;
    }
    if (style.nMatches <= 0) {
        auto* w = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * nColors);
        int n = 0;
        for (int i = 0; i < state->documentColors.len && w; i++) {
            const DocumentColor& dc = state->documentColors[i];
            int lo = dc.range.start - start;
            int hi = dc.range.end - start;
            if (lo < 0) {
                lo = 0;
            }
            if (hi > len) {
                hi = len;
            }
            if (hi <= lo) {
                continue;
            }
            w[n].lo = lo;
            w[n].hi = hi;
            w[n].bg = dc.color;
            n++;
        }
        return n > 0 ? el->Washes(w, n) : el;
    }
    while (*at < style.nMatches && style.matches[*at].end <= start) {
        (*at)++;
    }
    int first = *at, count = 0;
    while (first + count < style.nMatches && style.matches[first + count]
                                                     .start < start + len) {
        count++;
    }
    if (count <= 0) {
        return el;
    }
    auto* w = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * (count + nColors));
    int n = 0;
    for (int k = 0; k < count && w; k++) {
        int ix = first + k;
        int lo = style.matches[ix].start - start;
        int hi = style.matches[ix].end - start;
        if (lo < 0) {
            lo = 0;
        }
        if (hi > len) {
            hi = len;
        }
        if (hi <= lo) {
            continue;
        }
        w[n].lo = lo;
        w[n].hi = hi;
        w[n].bg =
            ix == style.currentMatch ? style.currentMatchBg : style.matchBg;
        n++;
    }
    for (int i = 0; i < nColors && w; i++) {
        const DocumentColor& dc = state->documentColors[i];
        int lo = dc.range.start - start;
        int hi = dc.range.end - start;
        if (lo < 0) {
            lo = 0;
        }
        if (hi > len) {
            hi = len;
        }
        if (hi <= lo) {
            continue;
        }
        w[n].lo = lo;
        w[n].hi = hi;
        w[n].bg = dc.color;
        n++;
    }
    return n > 0 ? el->Washes(w, n) : el;
}

// Input::LINE_HEIGHT is 1.25rem — 20 px at the 16 px root, whatever the text
// size is, rather than the phi box every other line of text gets.
static const float kInputLineH = 20.f;
// element.rs RIGHT_MARGIN: what the text keeps clear on its right, which a
// soft-wrapping editor wraps short of and an inline token is measured within.
static const float kEditorRightMargin = 10.f;

static float DisplayLineH(const InputState* s, int row, float lineH);
static float DisplayRowDocY(const InputState* s, int row, float lineH);
static void Notify(App* app, Window* win);

// ─── soft wrap (display_map/text_wrapper.rs) ──────────────────────────────
//
// TextWrapper over the live document. Rust keeps a SumTree of LineItems and
// re-wraps the lines an edit touched; the map here is flat and re-wraps the
// whole document when anything it depends on moved, which is a sum of cached
// character widths per byte.

static int WrapEncodeUtf8(uint32_t c, char* out) {
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xC0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000) {
        out[0] = (char)(0xE0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

struct WrapMeasure {
    InputWrapMap* map = nullptr;
    PaintCtx* ctx = nullptr;
};

// The advance of `bytes` on their own, in the map's font: where a caret after
// them stands, which counts a trailing space the way a width would not.
static bool WrapAdvance(const WrapMeasure* wm, Str bytes, float* out) {
    const InputWrapMap* m = wm->map;
    float x = 0, y = 0, h = 0;
    if (!wm->ctx || !TextPointAt(wm->ctx, bytes, m->fontSize, 0, false,
                                 len(bytes), &x, &y, &h, m->fontWord)) {
        return false;
    }
    *out = x;
    return true;
}

// LineWrapper::width_for_char.
static float WrapCharWidthOf(void* user, uint32_t c) {
    WrapMeasure* wm = (WrapMeasure*)user;
    InputWrapMap* m = wm->map;
    if (m->charFont != m->fontSize || m->charWord != m->fontWord) {
        m->charFont = m->fontSize;
        m->charWord = m->fontWord;
        for (float& w : m->asciiWidths) {
            w = -1;
        }
        for (uint32_t& k : m->otherChars) {
            k = 0;
        }
    }
    int slot = -1;
    if (c < 128) {
        if (m->asciiWidths[c] >= 0) {
            return m->asciiWidths[c];
        }
    } else {
        const int kSlots = (int)dimof(m->otherChars);
        int at = (int)((c * 2654435761u) >> 24) % kSlots;
        for (int probe = 0; probe < kSlots; probe++) {
            int i = (at + probe) % kSlots;
            if (m->otherChars[i] == c) {
                return m->otherWidths[i];
            }
            if (m->otherChars[i] == 0) {
                slot = i;
                break;
            }
        }
    }
    char buf[4];
    int n = WrapEncodeUtf8(c, buf);
    float w = 0;
    if (!WrapAdvance(wm, Str(buf, n), &w)) {
        // Nothing to shape against: an estimate, not kept, so the next
        // measure with a context replaces it.
        return m->fontSize * 0.6f;
    }
    if (c < 128) {
        m->asciiWidths[c] = w;
    } else if (slot >= 0) {
        m->otherChars[slot] = c;
        m->otherWidths[slot] = w;
    }
    return w;
}

// One line's fragments for the wrapper: the text between its inline tokens,
// and each token as an element of its measured width.
struct WrapLineUser {
    WrapMeasure* measure = nullptr;
    float width = 0;
    int lineStart = 0;
    const InlineTokenSpan* spans = nullptr;
    const float* widths = nullptr;
    int nSpans = 0;
};

static float WrapTokenWidth(WrapLineUser* u, int i) {
    if (u->widths) {
        return u->widths[i];
    }
    // Not measured: the label's own width, which is what a chip with no
    // renderer draws.
    Str label = u->spans[i].token.label;
    if (len(label) == 0) {
        label = u->spans[i].token.text;
    }
    float w = 0;
    for (int at = 0; at < len(label);) {
        uint32_t c = 0;
        int n = Utf8At(label, at, &c);
        w += WrapCharWidthOf(u->measure, c);
        at += n > 0 ? n : 1;
    }
    return w > 1 ? w : 1;
}

static void WrapLineFragments(void* user, Str slice, int base,
                              Vec<WrapBoundary>* out) {
    WrapLineUser* u = (WrapLineUser*)user;
    if (u->nSpans == 0) {
        LineFragment f = LineFragment::Text(slice);
        LineWrapperWrapLine(&f, 1, u->width, &WrapCharWidthOf, u->measure, out);
        return;
    }
    Vec<LineFragment> frags;
    int from = u->lineStart + base;
    int end = from + len(slice);
    int at = from;
    for (int i = 0; i < u->nSpans; i++) {
        const InlineTokenSpan& span = u->spans[i];
        if (span.end <= from || span.start >= end) {
            continue;
        }
        int s0 = span.start < from ? from : span.start;
        if (s0 > at) {
            VecAppend(frags,
                      LineFragment::Text(Str(slice.s + (at - from), s0 - at)));
        }
        int e0 = span.end > end ? end : span.end;
        VecAppend(frags, LineFragment::Element(WrapTokenWidth(u, i), e0 - s0));
        at = e0;
    }
    if (at < end) {
        VecAppend(frags,
                  LineFragment::Text(Str(slice.s + (at - from), end - at)));
    }
    LineWrapperWrapLine(frags.els, len(frags), u->width, &WrapCharWidthOf,
                        u->measure, out);
}

// One logical line's rows, as byte offsets into it, and the continuation
// rows' indent: the body of TextWrapper::_update for a row.
static void WrapOneLine(InputState* s, WrapMeasure* wm, int line,
                        Vec<int>* rows, float* indentOut) {
    InputWrapMap* m = &s->wrap;
    const Vec<int>& lineStarts = InputLineStarts(s);
    Str text = InputValue(s);
    int nLines = len(lineStarts);
    int start = lineStarts[line];
    int end = line + 1 < nLines ? lineStarts[line + 1] - 1 : len(text);
    Str str = Str(text.s + start, end - start);
    WrapLineUser u;
    u.measure = wm;
    u.width = m->width;
    u.lineStart = start;
    const Vec<InlineTokenSpan>* spans =
        InputTokensVisible(s) ? InputTokens(s) : nullptr;
    int nSpans = spans ? len(*spans) : 0;
    if (nSpans > 0) {
        // The first span that ends inside or after the line, and how many
        // start before its end.
        int lo = 0, hi = nSpans;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if ((*spans)[mid].end <= start) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        int count = 0;
        while (lo + count < nSpans && (*spans)[lo + count].start < end) {
            count++;
        }
        if (count > 0) {
            const float* widths =
                len(m->tokenWidths) == nSpans ? m->tokenWidths.els : nullptr;
            u.spans = spans->els + lo;
            u.widths = widths ? widths + lo : nullptr;
            u.nSpans = count;
        }
    }
    WrappingIndent indent =
        m->wrappingIndent ? WrappingIndent::Same : WrappingIndent::None;
    int indentChars = 0;
    VecClear(*rows);
    TextWrapperWrapItem(str, m->width > 0, indent, &WrapLineFragments, &u, rows,
                        &indentChars);
    *indentOut = 0;
    // Use the first visual row's indentation width for the rows after it:
    // x_for_index of the indent's byte length in that row.
    if (indentChars > 0 && len(*rows) > 1) {
        int bytes = 0;
        for (int k = 0; k < indentChars && bytes < len(str); k++) {
            uint32_t c = 0;
            int n = Utf8At(str, bytes, &c);
            bytes += n > 0 ? n : 1;
        }
        float x = 0;
        if (!WrapAdvance(wm, Str(str.s, bytes), &x)) {
            x = m->spaceWidth * (float)indentChars;
        }
        *indentOut = x;
    }
}

// The rows are the current document's now: no edit is waiting on them.
static void WrapMapCaughtUp(InputState* s) {
    InputWrapMap* m = &s->wrap;
    m->docVersion = s->docVersion;
    m->hasEdit = false;
    m->editWhole = false;
    m->valid = true;
}

static void WrapMapRebuild(InputState* s, PaintCtx* ctx) {
    InputWrapMap* m = &s->wrap;
    VecClear(m->lines);
    VecClear(m->starts);
    VecClear(m->dirtyLines);
    m->totalRows = 0;
    WrapMapCaughtUp(s);
    int nLines = len(InputLineStarts(s));
    WrapMeasure wm;
    wm.map = m;
    wm.ctx = ctx;
    m->spaceWidth = WrapCharWidthOf(&wm, ' ');
    VecReserve(m->lines, nLines);
    Vec<int> rows;
    for (int line = 0; line < nLines; line++) {
        InputWrapLine item;
        WrapOneLine(s, &wm, line, &rows, &item.indent);
        item.firstStart = len(m->starts);
        item.nRows = len(rows);
        item.rowsAbove = m->totalRows;
        VecAppendN(m->starts, rows.els, len(rows));
        m->totalRows += item.nRows;
        VecAppend(m->lines, item);
    }
}

// TextWrapper::_update's splice: the map's lines [first, first + oldCount)
// become the current document's lines [first, first + newCount), wrapped
// afresh, and every line after them keeps its rows. Only where each line's
// rows sit in `starts` and how many rows are above it move along.
static void WrapMapReplaceLines(InputState* s, WrapMeasure* wm, int first,
                                int oldCount, int newCount) {
    InputWrapMap* m = &s->wrap;
    int nOld = len(m->lines);
    int sFrom = first < nOld ? m->lines[first].firstStart : len(m->starts);
    int sTo = first + oldCount < nOld ? m->lines[first + oldCount].firstStart
                                      : len(m->starts);
    Vec<InputWrapLine> items;
    Vec<int> starts;
    Vec<int> rows;
    VecReserve(items, newCount);
    for (int i = 0; i < newCount; i++) {
        InputWrapLine item;
        WrapOneLine(s, wm, first + i, &rows, &item.indent);
        item.nRows = len(rows);
        VecAppendN(starts, rows.els, len(rows));
        VecAppend(items, item);
    }
    VecRemoveAtN(m->lines, first, oldCount);
    if (newCount > 0) {
        if (InputWrapLine* at = VecInsertSpace(m->lines, first, newCount)) {
            memcpy((void*)at, (const void*)items.els,
                   sizeof(InputWrapLine) * (size_t)newCount);
        }
    }
    VecRemoveAtN(m->starts, sFrom, sTo - sFrom);
    if (len(starts) > 0) {
        if (int* at = VecInsertSpace(m->starts, sFrom, len(starts))) {
            memcpy(at, starts.els, sizeof(int) * (size_t)len(starts));
        }
    }
    // A sum tree carries these as summaries; a flat list walks the lines
    // after the splice once, which is integer adds and nothing measured.
    int firstStart = sFrom;
    int above = 0;
    if (first > 0) {
        above = m->lines[first - 1].rowsAbove + m->lines[first - 1].nRows;
    }
    for (int i = first; i < len(m->lines); i++) {
        InputWrapLine& item = m->lines[i];
        item.firstStart = firstStart;
        item.rowsAbove = above;
        firstStart += item.nRows;
        above += item.nRows;
    }
    m->totalRows = above;
}

// The line holding byte `offset` of the current document.
static int WrapLineOfOffset(const Vec<int>& lineStarts, int offset) {
    int lo = 0, hi = len(lineStarts) - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (lineStarts[mid] <= offset) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

// Bring a map wrapped at its own width and font up to the current document:
// the lines the edit envelope covers, then the lines whose chips measured
// another width. A map that cannot say what changed is wrapped whole.
static void WrapMapCatchUp(InputState* s, PaintCtx* ctx) {
    InputWrapMap* m = &s->wrap;
    bool edited = m->docVersion != s->docVersion;
    if (!edited && len(m->dirtyLines) == 0) {
        return;
    }
    if (edited && (!m->hasEdit || m->editWhole)) {
        WrapMapRebuild(s, ctx);
        return;
    }
    WrapMeasure wm;
    wm.map = m;
    wm.ctx = ctx;
    const Vec<int>& lineStarts = InputLineStarts(s);
    int nNew = len(lineStarts);
    int nOld = len(m->lines);
    if (edited) {
        // The lines after the one the envelope ends on are the same lines
        // in both documents, so the old line it ended on is as far from the
        // old end as the new one is from the new end.
        int first = WrapLineOfOffset(lineStarts, m->editStart);
        int lastNew = WrapLineOfOffset(lineStarts, m->editNewEnd);
        int lastOld = nOld - (nNew - lastNew);
        if (lastOld < first || lastNew < first || lastOld >= nOld) {
            WrapMapRebuild(s, ctx);
            return;
        }
        WrapMapReplaceLines(s, &wm, first, lastOld - first + 1,
                            lastNew - first + 1);
        WrapMapCaughtUp(s);
    }
    for (int i = 0; i < len(m->dirtyLines); i++) {
        int line = m->dirtyLines[i];
        if (line >= 0 && line < len(m->lines)) {
            WrapMapReplaceLines(s, &wm, line, 1, 1);
        }
    }
    VecClear(m->dirtyLines);
}

void InputUpdateWrapMap(InputState* s, PaintCtx* ctx, float width,
                        float fontSize, uint16_t fontWord) {
    if (!s) {
        return;
    }
    InputWrapMap* m = &s->wrap;
    if (width < 0) {
        width = 0;
    }
    // set_wrap_width, set_font and set_wrapping_indent re-wrap every line;
    // an edit or a chip re-measured re-wraps the lines it touched.
    bool same = m->valid && m->width == width && m->fontSize == fontSize &&
                m->fontWord == fontWord &&
                m->wrappingIndent == s->wrappingIndent;
    if (same) {
        WrapMapCatchUp(s, ctx);
        return;
    }
    m->width = width;
    m->fontSize = fontSize;
    m->fontWord = fontWord;
    m->wrappingIndent = s->wrappingIndent;
    WrapMapRebuild(s, ctx);
}

// The map as of the current document. An edit since the element last built
// it re-wraps the lines it touched, at the width and font the map last had.
static const InputWrapMap* WrapMapOf(const InputState* s, PaintCtx* ctx) {
    if (!s || !s->softWrap || !InputIsMultiLine(s)) {
        return nullptr;
    }
    const InputWrapMap* m = &s->wrap;
    if (!m->valid || m->width <= 0) {
        return nullptr;
    }
    InputState* ms = const_cast<InputState*>(s);
    if (m->wrappingIndent != s->wrappingIndent) {
        ms->wrap.wrappingIndent = s->wrappingIndent;
        WrapMapRebuild(ms, ctx);
    } else {
        WrapMapCatchUp(ms, ctx);
    }
    return m;
}

int InputWrapRows(const InputState* s, int line, const int** starts,
                  float* indent) {
    static const int kZero = 0;
    const InputWrapMap* m = WrapMapOf(s, nullptr);
    if (!m || line < 0 || line >= len(m->lines)) {
        if (starts) {
            *starts = &kZero;
        }
        if (indent) {
            *indent = 0;
        }
        return 1;
    }
    const InputWrapLine& item = m->lines[line];
    if (starts) {
        *starts = m->starts.els + item.firstStart;
    }
    if (indent) {
        *indent = item.indent;
    }
    return item.nRows;
}

void InputSetWrappingIndent(InputState* s, App* app, Window* win,
                            WrappingIndent indent) {
    if (!s) {
        return;
    }
    s->wrappingIndent = indent == WrappingIndent::Same ? 1 : 0;
    Notify(app, win);
}

// The visual row of a line holding `local`: the last row starting at or
// before it, or, at a soft-wrap boundary with the line-end affinity, the
// row that boundary ends.
static int WrapRowOfOffset(const int* starts, int nRows, int local,
                           bool lineEndAffinity) {
    int k = nRows - 1;
    while (k > 0 && starts[k] > local) {
        k--;
    }
    if (lineEndAffinity && k > 0 && starts[k] == local) {
        k--;
    }
    return k;
}

// layout_cursors, for the cursors other than the active one: the part of each
// selection that falls inside this row and each caret that lands on it, as
// offsets into the row. A caret on a soft-wrap boundary opens the next row
// unless this is its line's last. The lists live in the frame arena.
static void RowExtraCursors(Arena* a, El* el, const InputState* state,
                            const InputEditorStyle& style, int start, int len,
                            bool caret, bool lastRow) {
    int n = state->extraCursors.len;
    auto* sels = (Selection*)Alloc(a, n * (int)sizeof(Selection));
    auto* carets = (int*)Alloc(a, n * (int)sizeof(int));
    if (!sels || !carets) {
        return;
    }
    int nSels = 0;
    int nCarets = 0;
    for (int i = 0; i < n; i++) {
        const CursorSelection& c = state->extraCursors[i];
        int lo = c.range.start - start;
        int hi = c.range.end - start;
        if (lo < 0) {
            lo = 0;
        }
        if (hi > len) {
            hi = len;
        }
        if (!c.IsEmpty() && lo < hi) {
            sels[nSels++] = Selection{lo, hi};
        }
        int cur = c.Cursor();
        if (caret && cur >= start && cur <= start + len &&
            (lastRow || cur < start + len)) {
            carets[nCarets++] = cur - start;
        }
    }
    if (nSels > 0) {
        if (el->selLo < 0) {
            // No active selection on this row set the wash colour.
            el->SelRange(-1, -1, style.selection);
        }
        el->ExtraSelRanges(sels, nSels);
    }
    if (nCarets > 0) {
        el->ExtraCarets(carets, nCarets, style.caret);
    }
}

// One bullet per character, not per byte, for a masked field.
static Str MaskedRun(Arena* a, Str text) {
    int chars = 0;
    for (int i = 0; i < len(text); i++) {
        if (((unsigned char)text.s[i] & 0xc0) != 0x80) {
            chars++;
        }
    }
    char* dots = (char*)Alloc(a, chars * 3 + 1);
    int n = 0;
    for (int i = 0; i < chars; i++) {
        memcpy(dots + n, "\xE2\x80\xA2", 3); // U+2022 BULLET
        n += 3;
    }
    dots[n] = 0;
    return Str(dots, n);
}

// A byte offset into the text, in the bullets that stand in for it: three
// bytes per character, so the caret and the selection land between bullets.
static int MaskedOffset(Str text, int off) {
    int chars = 0;
    for (int i = 0; i < off && i < len(text); i++) {
        if (((unsigned char)text.s[i] & 0xc0) != 0x80) {
            chars++;
        }
    }
    return chars * 3;
}

struct TokenClick {
    InputState* state = nullptr;
    App* app = nullptr;
    Window* win = nullptr;
    int start = 0;
    int end = 0;
};

static void OnTokenChipClick(TokenClick* p) {
    if (!p || !p->state) {
        return;
    }
    InputSetSelectedRange(p->state, p->app, p->win, p->start, p->end);
    InlineTokenStore* store = p->state->tokens;
    if (!store || !store->click || p->state->disabled) {
        return;
    }
    InlineTokenClickEvent ev = {};
    ev.span.start = p->start;
    ev.span.end = p->end;
    if (const Vec<InlineTokenSpan>* spans = InputTokens(p->state)) {
        for (int i = 0; i < spans->len; i++) {
            if ((*spans)[i].start == p->start) {
                ev.span = (*spans)[i];
                break;
            }
        }
    }
    Ctx cx = {};
    cx.app = p->app;
    cx.win = p->win;
    if (p->win) {
        cx.a = p->win->frameArena;
    }
    store->click(&ev, &cx, store->clickUser);
}

static El* TokenChip(Ctx* cx, InputState* state, const InlineTokenSpan& span,
                     const Selection& sel, float lineH,
                     const InputEditorStyle& style, float font) {
    Arena* a = cx->a;
    InlineTokenContext ctx = {};
    ctx.span = span;
    ctx.selected = sel.start < span.end && span.start < sel.end;
    ctx.disabled = state->disabled;
    ctx.readonly = state->readonly;
    ctx.lineHeight = lineH > 0 ? lineH : kInputLineH;
    // token_context's width: the text column, the bounds less RIGHT_MARGIN
    // (a wrapping editor's is its wrap width). Not the run the field last
    // bound, which in a token row is whichever text piece painted last.
    float avail = state->wrap.width > 0 && InputIsMultiLine(state)
                      ? state->wrap.width
                      : state->viewW - kEditorRightMargin;
    ctx.availableWidth = avail > 1 ? avail : kFill;
    El* chip = nullptr;
    InlineTokenStore* store = state->tokens;
    if (store && store->renderer) {
        chip = store->renderer(cx, &ctx, store->rendererUser);
    }
    if (!chip) {
        chip = Div(a)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->H(ctx.lineHeight)
                   ->Child(TextEl(a, span.token.label));
    }
    chip->Shrink0();
    // The chip is drawn inside the editor, under the editor's text style:
    // what it names no size or colour of itself takes the editor's.
    if (chip->style.fontSize <= 0) {
        chip->Font(font);
    }
    if (!chip->style.hasColor) {
        chip->Fg(style.foreground);
    }
    if (state->disabled) {
        return chip;
    }
    TokenClick* click = ArenaNew<TokenClick>(a);
    click->state = state;
    click->app = cx->app;
    click->win = cx->win;
    click->start = span.start;
    click->end = span.end;
    chip->OnClick(MkFunc0(&OnTokenChipClick, click))->StopClick();
    return chip;
}

// measure_tokens: every inline token's chip laid out on its own at the line
// height, which is the width the wrapper reserves for it (at most the wrap
// width, at least a pixel). Rust keys its cache on the font, the width, the
// line height and the document revision and measures again when one moved;
// a new measure re-wraps the document.
static void MeasureTokenWidths(Ctx* cx, InputState* state,
                               const InputEditorStyle& style, float font,
                               float lineH, float width) {
    InputWrapMap* m = &state->wrap;
    const Vec<InlineTokenSpan>* spans =
        InputTokensVisible(state) ? InputTokens(state) : nullptr;
    int n = spans ? len(*spans) : 0;
    if (n == 0) {
        if (len(m->tokenWidths) > 0) {
            VecClear(m->tokenWidths);
            m->valid = false;
        }
        return;
    }
    if (!cx->win) {
        return;
    }
    uint32_t fontBits = 0, widthBits = 0, lineBits = 0;
    memcpy(&fontBits, &font, sizeof(fontBits));
    memcpy(&widthBits, &width, sizeof(widthBits));
    memcpy(&lineBits, &lineH, sizeof(lineBits));
    uint64_t key = state->docVersion * 1000003u;
    key = (key ^ fontBits) * 1000003u;
    key = (key ^ widthBits) * 1000003u;
    key = (key ^ lineBits) * 1000003u;
    key ^= (uint64_t)n;
    // Everything when the key moved; otherwise the chips on the lines in
    // view, which is where a renderer drawing at another width shows.
    bool all = key != m->tokenKey || len(m->tokenWidths) != n;
    Selection visible = {0, 0};
    if (!all) {
        float viewH = state->viewH > 0 ? state->viewH : 600.f;
        float top = state->scrollY - viewH;
        float bottom = state->scrollY + 2 * viewH;
        int lines = InputLinesLen(state);
        float at = 0;
        visible = {-1, len(InputValue(state))};
        for (int i = 0; i < lines; i++) {
            float h = DisplayLineH(state, i, lineH);
            if (visible.start < 0 && at + h > top) {
                visible.start = InputLineStartOffset(state, i);
            }
            if (at > bottom) {
                visible.end = InputLineStartOffset(state, i);
                break;
            }
            at += h;
        }
        if (visible.start < 0) {
            visible.start = 0;
        }
    }
    // set_inline_metrics: only the lines whose chips came out another width
    // wrap again. When the chips are not the ones measured last -- one was
    // added or removed -- every chip's line does, which is a line per token.
    Vec<float> was;
    bool sameChips = len(m->tokenWidths) == n;
    bool wraps = state->softWrap && InputIsMultiLine(state);
    if (!sameChips && m->docVersion == state->docVersion) {
        // A token went or came with no edit to say where -- an association
        // undone, say -- so the line that lost a chip is not known.
        m->valid = false;
    }
    if (all) {
        if (sameChips) {
            VecAppendVec(was, m->tokenWidths);
        }
        VecClear(m->tokenWidths);
        if (VecAppendBlanks(m->tokenWidths, n)) {
            for (int i = 0; i < n; i++) {
                m->tokenWidths[i] = 0;
            }
        }
    }
    Selection none = {};
    for (int i = 0; i < n && i < len(m->tokenWidths); i++) {
        const InlineTokenSpan& span = (*spans)[i];
        if (!all && (span.end < visible.start || span.start > visible.end)) {
            continue;
        }
        El* chip = TokenChip(cx, state, span, none, lineH, style, font);
        Size size = MeasureEl(&cx->win->paint, chip, font);
        float w = size.w;
        if (width > 0 && w > width) {
            w = width;
        }
        w = w > 1 ? w : 1.f;
        float before = all ? (sameChips ? was[i] : -1.f) : m->tokenWidths[i];
        if (before != w && wraps) {
            int line = WrapLineOfOffset(InputLineStarts(state), span.start);
            if (len(m->dirtyLines) == 0 ||
                m->dirtyLines[len(m->dirtyLines) - 1] != line) {
                VecAppend(m->dirtyLines, line);
            }
        }
        m->tokenWidths[i] = w;
    }
    m->tokenKey = key;
}

static bool LineHasVisibleTokens(const InputState* state, int start, int end) {
    if (!InputTokensVisible(state)) {
        return false;
    }
    const Vec<InlineTokenSpan>* spans = InputTokens(state);
    if (!spans) {
        return false;
    }
    for (int i = 0; i < spans->len; i++) {
        if ((*spans)[i].start < end && start < (*spans)[i].end) {
            return true;
        }
    }
    return false;
}

// The face a row is drawn in: Editor::font_family's family, over the mono
// flag a code editor sets, the way `.font_family(..)` refines the text
// style the rows inherit.
static void InputFace(El* e, const InputEditorStyle& style) {
    if (style.mono) {
        e->Mono();
    }
    if (style.fontFamily) {
        e->FontFamilyId(style.fontFamily);
    }
}

static uint16_t InputFontWord(const InputEditorStyle& style) {
    return (uint16_t)((style.mono ? kFontMono : 0) |
                      FontFamilyBits(style.fontFamily));
}

// One text run between a row's chips: the whole gap, shaped as one fragment
// the way Rust shapes it. Where the row breaks is the wrap map's business.
static void EmitTokenTextRun(El* row, Arena* a, const InputEditorStyle& style,
                             float font, float lineMult, Str slice,
                             int docStart, const Selection& sel, bool caret,
                             int cursor) {
    if (!row || len(slice) == 0) {
        return;
    }
    El* piece = TextEl(a, slice)
                    ->Font(font)
                    ->LineHeight(lineMult)
                    ->Fg(style.foreground)
                    // The wrap map already fit the row to the column; a run
                    // that shrank would slide under the chip beside it.
                    ->Shrink0();
    // Not bound to the field: a run beside a chip is not where the line
    // starts, so it must not be the box the field measures from. The row it
    // is in takes the press.
    InputFace(piece, style);
    int lo = sel.start - docStart;
    int hi = sel.end - docStart;
    if (lo < 0) {
        lo = 0;
    }
    if (hi > len(slice)) {
        hi = len(slice);
    }
    if (!sel.IsEmpty() && lo < hi) {
        piece->SelRange(lo, hi, style.selection);
    }
    if (caret && cursor >= docStart && cursor <= docStart + len(slice)) {
        piece->Caret(cursor - docStart, style.caret);
    }
    row->Child(piece);
}

// Split a document range into text runs and chips. Tokens cannot contain
// newlines, and a wrap never breaks inside one, so a visual row is a
// complete set of pieces.
static void AppendTokenPieces(El* row, Ctx* cx, InputState* state,
                              const InputEditorStyle& style, float font,
                              float lineMult, float lineH, Str run, int start,
                              const Selection& sel, bool caret, int cursor) {
    if (!row) {
        return;
    }
    const Vec<InlineTokenSpan>* spans = InputTokens(state);
    int end = start + len(run);
    int at = start;
    if (spans) {
        for (int i = 0; i < spans->len; i++) {
            const InlineTokenSpan& span = (*spans)[i];
            if (span.end <= start) {
                continue;
            }
            if (span.start >= end) {
                break;
            }
            if (span.start > at) {
                EmitTokenTextRun(row, cx->a, style, font, lineMult,
                                 Str(run.s + (at - start), span.start - at), at,
                                 sel, caret, cursor);
            }
            row->Child(TokenChip(cx, state, span, sel, lineH, style, font));
            at = span.end;
        }
    }
    if (at < end) {
        EmitTokenTextRun(row, cx->a, style, font, lineMult,
                         Str(run.s + (at - start), end - at), at, sel, caret,
                         cursor);
    }
}

El* Input::New(Ctx* cx, InputState* state) {
    return New(cx, state, InputEditorStyle{});
}

El* Input::New(Ctx* cx, InputState* state, const InputEditorStyle& projected) {
    Arena* a = cx->a;
    if (!state) {
        return TextEl(a, Str{});
    }
    BaseTheme theme = base_theme::Theme::Global(cx->app);
    InputEditorStyle resolved =
        InputEditorStyleResolve(projected, theme.tokens);
    const InputEditorStyle& style = resolved;
    float font = style.fontSize > 0 ? style.fontSize : 12.f;
    float lineMult = kInputLineH / font;
    state->lastLineH = kInputLineH;
    state->lastFontWord = InputFontWord(style);
    Str text = InputValue(state);
    bool masked = style.mask || state->masked;
    // show_cursor: focused, not disabled, and this half of the blink is the
    // lit one.
    bool caret =
        state->focused && !state->disabled && BlinkVisible(cx, state->blink);

    // The row fills its field, so a press to the right of the text still
    // lands on the editor — Rust's InputElement takes the whole content box.
    // A single line fills the frame and sits at its vertical center, so the
    // frame needs no layout of its own to hold it (state.rs `h_full()` and
    // `flex().items_center()`). The minimum is the line: Rust's TextElement
    // always asks for one, where an empty row here would collapse.
    El* row = Div(a)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->H(kFill)
                  ->MinH(kInputLineH)
                  ->Flex1()
                  ->BindInput(state);
    if (style.align == 1) {
        row->W(kFill)->JustifyCenter();
    } else if (style.align == 2) {
        row->W(kFill)->JustifyEnd();
    }

    if (len(text) == 0) {
        // The cue takes the muted color and the caret sits at the left edge of
        // the row, so the placeholder is not pushed aside by it.
        if (caret) {
            row->Caret(0, style.caret);
        }
        Str cue = state->placeholder;
        return row->Child(TextEl(a, cue)->Font(font)->LineHeight(lineMult)->Fg(
            style.mutedForeground));
    }

    Str run = masked ? MaskedRun(a, text) : text;
    int cursor = InputCursor(state);
    Selection sel = state->selectedRange;
    Selection mark = {};
    bool marking = InputMarkedRange(state, &mark);
    if (marking) {
        // InputElement puts the caret at the end of the marked range and
        // shows no selection inside it: what the input method has staged is
        // one run being composed, not text the user has picked out.
        cursor = mark.end;
        sel = SelectionAt(mark.end);
    }
    if (masked) {
        cursor = MaskedOffset(text, cursor);
        sel.start = MaskedOffset(text, sel.start);
        sel.end = MaskedOffset(text, sel.end);
        mark.start = MaskedOffset(text, mark.start);
        mark.end = MaskedOffset(text, mark.end);
    }
    // The token row's own box is the field's lastBounds, where the line
    // starts: the hit test and the caret walk its text runs and chips from
    // there, since no one run's box says where the line is.
    state->chipLine = false;
    if (InputTokensVisible(state) && !masked) {
        // The chips' widths, which the hit test and the caret step over.
        // One line does not wrap, so nothing caps them.
        MeasureTokenWidths(cx, state, style, font, kInputLineH, 0);
        state->chipLine = true;
        state->lastFont = font;
        El* line = Div(a)->FlexRow()->ItemsCenter()->Shrink0()->BoundsOut(
            &state->lastBounds);
        AppendTokenPieces(line, cx, state, style, font, lineMult, kInputLineH,
                          run, 0, sel, caret, cursor);
        return row->Child(line);
    }
    El* el = TextEl(a, run)
                 ->Font(font)
                 ->LineHeight(lineMult)
                 ->Fg(style.foreground)
                 ->BindInput(state);
    // A single-line field is one row, so the whole document is its slice.
    // A masked one is not searched: what it holds is not what it shows.
    if (!masked) {
        int matchAt = 0;
        RowMatchWashes(a, el, style, state, 0, len(run), &matchAt);
    }
    if (!sel.IsEmpty()) {
        el->SelRange(sel.start, sel.end, style.selection);
    }
    if (marking) {
        el->MarkRange(mark.start, mark.end);
    }
    if (caret) {
        el->Caret(cursor, style.caret);
    }
    return row->Child(el);
}

// element.rs FOLD_ICON_WIDTH / FOLD_ICON_HITBOX_WIDTH.
static const float kFoldIcon = 14.f;
static const float kFoldIconHitbox = 18.f;
// element.rs LINE_NUMBER_RIGHT_MARGIN.
static const float kLineNumberRightMargin = 6.f;

// Whether the pointer is over the gutter, which is what decides if the
// chevrons show. Rust inserts one hitbox over the whole line-number column
// (fold icons included) at the editor's visible height; the column is the
// same x on every row, so last frame's gutter strip locates it, and the
// editor's clip is the vertical extent.
static bool GutterHovered(const InputState* s, Window* win) {
    if (!win || s->gutterBox.w <= 0) {
        return false;
    }
    float x = win->mouseX;
    if (x < s->gutterBox.x || x >= s->gutterBox.x + s->gutterBox.w) {
        return false;
    }
    const Bounds& clip = s->inputBounds.h > 0 ? s->inputBounds : s->contentBox;
    if (clip.h <= 0) {
        return false;
    }
    return win->mouseY >= clip.y && win->mouseY < clip.y + clip.h;
}

// One cell of the fold gutter: a chevron when the line opens a fold, and an
// empty box of the same width when it does not, so the text column starts at
// the same place on every row.
static El* FoldChevron(Arena* a, InputState* state,
                       const InputEditorStyle& style, int row, int caretRow,
                       float lineH, bool gutterHover) {
    // size(FOLD_ICON_HITBOX_WIDTH, line_height): the height is spelled out
    // because a wrapped row's band is items-start rather than stretch, so
    // a cell with no chevron in it would otherwise measure as nothing and
    // a press could not land on it.
    El* cell =
        Div(a)->W(kFoldIconHitbox)->H(lineH)->ItemsCenter()->JustifyCenter();
    if (!FoldMapIsCandidate(&state->folds, row)) {
        return cell;
    }
    bool folded = FoldMapIsFolded(&state->folds, row);
    // The box a press is matched against. Every candidate gets one, whether
    // or not its chevron is drawn: layout_fold_icons prepaints an icon for
    // each one and only paint_fold_icons skips the drawing, so a click lands
    // on a chevron that the same click is what makes visible. Reserved before
    // the rows were built, so appending here cannot move a box already handed
    // out.
    if (state->foldIcons.len < state->foldIcons.cap) {
        FoldIconBox slot;
        slot.line = row;
        VecAppend(state->foldIcons, slot);
        cell->BoundsOut(&state->foldIcons[state->foldIcons.len - 1].bounds);
    }
    // paint_fold_icons: hovered, on the caret's row, or closed. A closed fold
    // always shows, because nothing else on the row says its text is hidden.
    if (!gutterHover && !folded && row != caretRow) {
        return cell;
    }
    // crates/ui Input's fold_icon_renderer: a ghost xsmall button, 14px,
    // whose hover fill is what makes the chevron a target rather than a
    // glyph. PathClick gives it a hover id so the wash lands and so moving
    // onto the icon invalidates the frame.
    El* icon = Div(a)
                   ->W(kFoldIcon)
                   ->H(kFoldIcon)
                   ->Radius(4)
                   ->ItemsCenter()
                   ->JustifyCenter()
                   ->PathClick(StrDup(a, fmt("fold-%d", row)))
                   ->HoverBg(RgbaOpacity(style.mutedForeground, 0.25f))
                   ->Cursor(CursorKind::Pointer)
                   ->Child(IconEl(a,
                                  folded ? IconName::ChevronRight
                                         : IconName::ChevronDown,
                                  kFoldIcon)
                               ->Fg(style.mutedForeground));
    return cell->Child(icon);
}

// element.rs compose_decoration_collections, kept flat. Lived beside the
// highlighter facade until the highlighter became an installed seam; the
// element composes the decoration layers itself now, which is where Rust
// does it.
int InputComposeSpans(TextSpan* spans, int n, const TextSpan* decs, int nDecs,
                      int cap, TextSpan* tmp) {
    // The decorations win over the spans they overlap: every span is cut
    // back to what the decorations leave it, and the decorations go in
    // whole. Both lists are in order, and so is the result, built into the
    // caller's scratch and copied back.
    int m = 0;
    int i = 0;
    for (int d = 0; d < nDecs && m < cap; d++) {
        const TextSpan& dec = decs[d];
        for (; i < n && m < cap; i++) {
            TextSpan sp = spans[i];
            if (sp.hi <= dec.lo) {
                tmp[m++] = sp;
                continue;
            }
            if (sp.lo >= dec.hi) {
                break;
            }
            // The part before the decoration survives; the part after it is
            // put back for the next decoration to look at.
            if (sp.lo < dec.lo && m < cap) {
                TextSpan head = sp;
                head.hi = dec.lo;
                tmp[m++] = head;
            }
            if (sp.hi > dec.hi) {
                spans[i].lo = dec.hi;
                break;
            }
        }
        if (m < cap) {
            tmp[m++] = dec;
        }
    }
    for (; i < n && m < cap; i++) {
        tmp[m++] = spans[i];
    }
    for (int k = 0; k < m; k++) {
        spans[k] = tmp[k];
    }
    return m;
}

// element.rs layout_range_decorations and its paint. Rust builds one path per
// decoration in prepaint from the shaped lines and paints the fills, then the
// frames, below the selection. The rows here are elements the flex column
// lays out, so the geometry is built when the column paints, once per frame,
// from where every visual row's run landed, and the column paints every path
// before any row paints its own: after the active line, before the
// selection and the text, which is Rust's order.
struct RangeDecorationRow {
    El* text = nullptr; // the visual row's shaped run (a token row's box)
    int start = 0;      // the buffer offset of the row's first byte
    int len = 0;        // the row's length
    int lineStart = 0;  // the logical line it belongs to
    int lineLen = 0;    // that line's length, without its newline
    bool first = true;  // the line's first visual row
    bool last = true;   // the line's last visual row
};

struct RangeDecorationPath {
    Point* points = nullptr;
    int n = 0;
    Rgba color = {};
    float stroke = 0; // 0 fills
};

struct RangeDecorationPaint {
    const RangeDecoration** decorations = nullptr;
    int nDecorations = 0;
    RangeDecorationRow* rows = nullptr;
    int nRows = 0;
    Rgba foreground = {};
    float lineH = 0;
    Arena* arena = nullptr;
    bool built = false;
    RangeDecorationPath* paths = nullptr;
    int nPaths = 0;
};

// layout_range_corners: a buffer range through the visible (non-folded)
// visual rows. Half-open intersections give soft-wrap ends their trailing
// affinity and keep a range on a later row from painting an earlier row's
// first glyph. A range that runs over a line's end gives the line's last
// visual row a one-space newline cell; a wrap boundary gets none.
static int RangeDecorationCorners(PaintCtx* ctx, const RangeDecorationPaint* p,
                                  Selection range, Vec<RangeCorners>* out) {
    int before = len(*out);
    for (int r = 0; r < p->nRows; r++) {
        const RangeDecorationRow& row = p->rows[r];
        if (!row.text || row.text->kind != ElKind::Text) {
            continue;
        }
        int rowEnd = row.start + row.len;
        int lineEnd = row.lineStart + row.lineLen;
        int startIx = std::max(range.start, row.start);
        int endIx = std::min(range.end, rowEnd);
        bool newline =
            row.last && range.start <= lineEnd && range.end > lineEnd;
        bool hasText = startIx < endIx;
        if (!hasText && !newline) {
            continue;
        }
        float left = row.text->x;
        float right = row.text->x;
        Bounds rect = {};
        if (hasText && ElTextRangeRects(ctx, row.text, startIx - row.start,
                                        endIx - row.start, &rect, 1) == 0) {
            // Nothing shaped there (a lone '\r'): only a newline cell, if
            // the range has one, is left to draw.
            if (!newline) {
                continue;
            }
            hasText = false;
        }
        if (hasText) {
            left = rect.x;
            right = rect.x + rect.w;
        } else if (row.len > 0) {
            // The newline cell alone, where the row's text ends.
            Bounds whole = {};
            if (ElTextRangeRects(ctx, row.text, 0, row.len, &whole, 1) > 0) {
                left = right = whole.x + whole.w;
            }
        }
        if (newline) {
            right += ElTextSpaceWidth(ctx, row.text);
        }
        float top = row.text->y;
        RangeCorners c;
        c.topLeft = {left, top};
        c.topRight = {right, top};
        c.bottomLeft = {left, top + p->lineH};
        c.bottomRight = {right, top + p->lineH};
        VecAppend(*out, c);
    }
    return len(*out) - before;
}

// What InputLastRangeCorners and its siblings read: every visual row the
// element built, whether or not a decoration lies over it, and the
// decorations' own paint when there was one.
struct InputPaintedRows {
    RangeDecorationPaint geometry;
    RangeDecorationPaint* decorations = nullptr;
    // What the column painted the active line from.
    const struct EditorUnderlay* underlay = nullptr;
};

static const InputPaintedRows* LastPaintedRows(const InputState* s,
                                               const Window* win) {
    // The frame counter moves on as a frame ends, so the rows are the last
    // finished frame's exactly when it is one past the frame they were
    // built in.
    if (!s || !win || !s->paintedRows ||
        s->paintedRowsFrame + 1 != win->frameSeq) {
        return nullptr;
    }
    return s->paintedRows;
}

int InputLastPaintedRows(const InputState* s, const Window* win, Selection* out,
                         int cap) {
    const InputPaintedRows* pr = LastPaintedRows(s, win);
    if (!pr) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < pr->geometry.nRows; i++) {
        const RangeDecorationRow& row = pr->geometry.rows[i];
        if (!row.first) {
            continue;
        }
        if (n < cap && out) {
            out[n] = Selection{row.lineStart, row.lineStart + row.lineLen};
        }
        n++;
    }
    return n;
}

int InputLastVisualRows(const InputState* s, Window* win,
                        InputPaintedVisualRow* out, int cap) {
    const InputPaintedRows* pr = LastPaintedRows(s, win);
    if (!pr) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < pr->geometry.nRows; i++) {
        const RangeDecorationRow& row = pr->geometry.rows[i];
        if (n < cap && out && row.text) {
            InputPaintedVisualRow& v = out[n];
            v.start = row.start;
            v.len = row.len;
            v.lineStart = row.lineStart;
            v.lineLen = row.lineLen;
            v.lastOfLine = row.last;
            v.text = row.text->Bounds();
            v.width = 0;
            Bounds whole = {};
            if (row.len > 0 && row.text->kind == ElKind::Text &&
                ElTextRangeRects(&win->paint, row.text, 0, row.len, &whole, 1) >
                    0) {
                v.width = whole.x + whole.w - row.text->x;
            }
        }
        n++;
    }
    return n;
}

int InputLastRangeCorners(const InputState* s, Window* win, Selection range,
                          Vec<RangeCorners>* out) {
    const InputPaintedRows* pr = LastPaintedRows(s, win);
    if (!pr || !out) {
        return 0;
    }
    return RangeDecorationCorners(&win->paint, &pr->geometry, range, out);
}

bool InputLastRangeDecorationPaths(const InputState* s, const Window* win,
                                   int* fills, int* frames) {
    int nFills = 0, nFrames = 0;
    const InputPaintedRows* pr = LastPaintedRows(s, win);
    const RangeDecorationPaint* p = pr ? pr->decorations : nullptr;
    for (int i = 0; p && i < p->nPaths; i++) {
        if (p->paths[i].stroke > 0) {
            nFrames++;
        } else {
            nFills++;
        }
    }
    if (fills) {
        *fills = nFills;
    }
    if (frames) {
        *frames = nFrames;
    }
    return pr != nullptr;
}

static void BuildRangeDecorationPaths(PaintCtx* ctx, RangeDecorationPaint* p,
                                      Bounds contentMask) {
    p->built = true;
    p->paths = (RangeDecorationPath*)Alloc(
        p->arena, (int)sizeof(RangeDecorationPath) * p->nDecorations);
    if (!p->paths) {
        return;
    }
    float scale = ctx->dpi > 0 ? ctx->dpi / 96.f : 1.f;
    // Fills first, then frames: Rust collects the two separately and paints
    // every fill before any frame.
    for (int pass = 0; pass < 2; pass++) {
        RangeDecorationStyle want = pass == 0 ? RangeDecorationStyle::Fill
                                              : RangeDecorationStyle::Frame;
        for (int i = 0; i < p->nDecorations; i++) {
            const RangeDecoration& d = *p->decorations[i];
            if (d.style != want) {
                continue;
            }
            Vec<RangeCorners> corners;
            if (RangeDecorationCorners(ctx, p, d.range, &corners) == 0) {
                continue;
            }
            RangeDecorationPath path;
            Vec<Point> points;
            if (want == RangeDecorationStyle::Fill) {
                path.color =
                    d.hasColor ? d.color : RgbaOpacity(p->foreground, 0.12f);
                FrameOutlinePoints(corners.els, len(corners), &points);
            } else {
                path.color = d.hasColor ? d.color : p->foreground;
                PadFrameCorners(corners.els, len(corners), 1.f);
                FrameOutlinePoints(corners.els, len(corners), &points);
                path.stroke =
                    SnapFrameOutline(points.els, len(points), 1.f, scale);
                ClampFrameToContentMask(points.els, len(points), path.stroke,
                                        contentMask);
            }
            path.n = len(points);
            path.points =
                (Point*)Alloc(p->arena, (int)sizeof(Point) * (path.n + 1));
            if (!path.points || path.n == 0) {
                continue;
            }
            memcpy(path.points, points.els, sizeof(Point) * (size_t)path.n);
            p->paths[p->nPaths++] = path;
        }
    }
}

// What the editor's column paints before its rows: the active line's wash and
// then every range decoration, each path once for the whole editor. It also
// keeps what the column was built from, for RewrapEditorColumn to build it
// again.
struct EditorUnderlay {
    InputState* state = nullptr;
    RangeDecorationPaint* decorations = nullptr;
    El* activeLine = nullptr; // the caret line's band, gutter and all
    Rgba activeColor = {};
    float activeBleedL = 0;
    Ctx cx = {};
    InputEditorStyle projected = {};
    bool lineNumbers = false;
};

// element.rs prepaint: wrap_width comes off the bounds layout has just given
// the editor, and the lines are wrapped to it in the frame that lays them
// out. The rows here are elements, built before layout, so they are wrapped
// to the column the last frame laid out. When this frame's column came out
// another width -- the window was resized, or this is the field's first
// frame -- the column is built again at prepaint, at the width it now has,
// and its rows laid out inside its box. The box itself is the one layout
// gave it: a column whose height moved asks for one more frame so that what
// holds it can follow, which is the frame Rust's auto-grow takes as well
// (request_layout sizes from mode.rows(), last frame's wrap).
static void RewrapEditorColumn(PaintCtx* ctx, El* e, void* user) {
    EditorUnderlay* u = (EditorUnderlay*)user;
    InputState* s = u ? u->state : nullptr;
    if (!s || !s->softWrap || e->w <= 0 || e->w == s->wrap.measuredWidth ||
        LayoutInScratchPass()) {
        return;
    }
    s->contentBox = e->Bounds();
    Ctx cx = u->cx;
    El* fresh = Textarea::New(&cx, s, u->projected, u->lineNumbers);
    if (!fresh || !fresh->first) {
        return;
    }
    float was = e->h;
    e->first = fresh->first;
    e->last = fresh->last;
    e->customPaint = fresh->customPaint;
    e->customUser = fresh->customUser;
    IdsCollectChildren(e);
    LayoutEl(ctx, e, e->x, e->y, e->w, 0, e->laidFont, e->style.color);
    if (e->h != was && ctx->window) {
        AppInvalidate(ctx->window);
    }
}

static void PaintEditorUnderlay(PaintCtx* ctx, El* e, void* user) {
    EditorUnderlay* u = (EditorUnderlay*)user;
    if (!u) {
        return;
    }
    // RewrapEditorColumn has wrapped the rows to this column already, unless
    // the column was laid out inside a measure, which it leaves alone; the
    // frame after wraps to it then.
    InputState* s = u->state;
    if (s && s->softWrap && e->w > 0 && e->w != s->wrap.measuredWidth &&
        ctx->window) {
        AppInvalidate(ctx->window);
    }
    if (u->activeLine && u->activeColor.a != 0) {
        const El* band = u->activeLine;
        FillRound(ctx, band->x - u->activeBleedL, band->y,
                  band->w + u->activeBleedL, band->h, 0,
                  PaintFade(ctx, u->activeColor));
    }
    RangeDecorationPaint* p = u->decorations;
    if (!p) {
        return;
    }
    if (!p->built) {
        Bounds mask = ctx->hasHitMask ? ctx->hitMask : e->Bounds();
        BuildRangeDecorationPaths(ctx, p, mask);
    }
    for (int i = 0; i < p->nPaths; i++) {
        const RangeDecorationPath& path = p->paths[i];
        Path* shape = PathNew(ctx, true);
        if (!shape) {
            continue;
        }
        PathMoveTo(shape, path.points[0].x, path.points[0].y);
        for (int k = 1; k < path.n; k++) {
            PathLineTo(shape, path.points[k].x, path.points[k].y);
        }
        PathClose(shape);
        Rgba c = PaintFade(ctx, path.color);
        if (path.stroke > 0) {
            PathStroke(ctx, shape, path.stroke, c);
        } else {
            PathFill(ctx, shape, c);
        }
        PathFree(shape);
    }
}

bool InputLastActiveLine(const InputState* s, const Window* win, Bounds* out) {
    const InputPaintedRows* pr = LastPaintedRows(s, win);
    const EditorUnderlay* u = pr ? pr->underlay : nullptr;
    if (!u || !u->activeLine || u->activeColor.a == 0) {
        return false;
    }
    if (out) {
        const El* band = u->activeLine;
        *out = {band->x - u->activeBleedL, band->y, band->w + u->activeBleedL,
                band->h};
    }
    return true;
}

El* Textarea::New(Ctx* cx, InputState* state) {
    return New(cx, state, InputEditorStyle{});
}

// The multi-line editor. Rust lays every visible row out through the display
// map; without one, each logical line is its own run and the selection is
// clipped to it — which is the same picture as long as nothing soft-wraps.
El* Textarea::New(Ctx* cx, InputState* state, const InputEditorStyle& projected,
                  bool lineNumbers) {
    Arena* a = cx->a;
    if (!state) {
        return TextEl(a, Str{});
    }
    BaseTheme theme = base_theme::Theme::Global(cx->app);
    InputEditorStyle resolved =
        InputEditorStyleResolve(projected, theme.tokens);
    const InputEditorStyle& style = resolved;
    float font = style.fontSize > 0 ? style.fontSize : 12.f;
    // EDITOR_LINE_HEIGHT: a code editor takes its rows from its own font, so
    // a smaller or larger one keeps its leading in proportion. Every other
    // field keeps Input::LINE_HEIGHT — 1.25rem whatever the text size is —
    // which is what Rust sets on the input rather than on the editor. At the
    // theme's 13px monospace the two are the same 20.
    float lineH =
        state->kind == InputKind::Editor ? roundf(font * 1.5f) : kInputLineH;
    float lineMult = lineH / font;
    state->lastLineH = lineH;
    state->lastFontWord = InputFontWord(style);
    Str text = InputValue(state);
    bool caret =
        state->focused && !state->disabled && BlinkVisible(cx, state->blink);
    int cursor = InputCursor(state);
    Selection sel = state->selectedRange;

    El* col = Div(a)->FlexCol()->W(kFill)->BindInput(state);
    col->BoundsOut(&state->contentBox);
    if (len(text) == 0) {
        if (caret) {
            col->Caret(0, style.caret);
        }
        El* ph = TextEl(a, state->placeholder)
                     ->Font(font)
                     ->LineHeight(lineMult)
                     ->Fg(style.mutedForeground);
        InputFace(ph, style);
        return col->Child(ph);
    }

    int rows = InputLinesLen(state);
    // The scrolled height, which is what scroll_to clamps against: one line
    // height per display row.
    state->contentH = (float)rows * lineH;
    if (LayoutModeIsFolding(state->mode)) {
        FoldMapRebuild(&state->folds, rows);
        state->contentH = (float)FoldMapDisplayRowCount(&state->folds) * lineH;
    }
    float numW = 0;
    // The numbers are shaped at the editor's own text size and family, as
    // layout_line_numbers shapes them with the text style.
    uint16_t numWeight = InputFontWord(style);
    if (lineNumbers) {
        // layout_line_numbers: the width of line_number_len "+" shaped in
        // that font -- three for a small document, then the line count's
        // own digits up to seven.
        char plus[8] = {};
        int digits = InputLineNumberLen(rows);
        for (int i = 0; i < digits && i < 7; i++) {
            plus[i] = '+';
        }
        numW = MeasureText(cx->win ? &cx->win->paint : nullptr,
                           Str(plus, digits), font, 0, false, numWeight)
                   .w;
        if (numW <= 0) {
            numW = 7.f * (float)digits;
        }
    }
    // The fold gutter. Rust widens the line-number column by the hitbox and
    // lays the icons into the space it made; the column here is a flex row,
    // so the icons get a cell of their own that is the same width.
    bool folding = lineNumbers && LayoutModeIsFolding(state->mode);
    float foldW = folding ? kFoldIconHitbox : 0.f;
    if (folding) {
        FoldMapRebuild(&state->folds, rows);
    }
    // soft_wrap: wrap_width is the bounds less the line numbers and
    // RIGHT_MARGIN. Rust has the bounds in prepaint, the frame it wraps in;
    // the rows here are built before layout, so the bounds are the column
    // the last frame laid out, and a column that came out a different width
    // is built again at prepaint (RewrapEditorColumn). Until the first frame
    // has one, this build wraps nothing and that one does.
    bool wrap = false;
    {
        float colW = state->contentBox.w;
        float gutterW =
            lineNumbers ? numW + kLineNumberRightMargin + foldW : 0.f;
        float textW = 0;
        if (colW > 0) {
            textW = colW - gutterW - kEditorRightMargin;
            if (textW < 1) {
                textW = 1;
            }
        }
        // The chips are measured whether or not the text wraps: the hit
        // test and the caret walk them as fragments of their own width.
        MeasureTokenWidths(cx, state, style, font, lineH, textW);
        if (state->softWrap) {
            InputUpdateWrapMap(state, cx->win ? &cx->win->paint : nullptr,
                               textW, font, InputFontWord(style));
            state->wrap.measuredWidth = colW;
            wrap = WrapMapOf(state, cx->win ? &cx->win->paint : nullptr) !=
                   nullptr;
            if (wrap) {
                state->contentH = DisplayRowDocY(state, rows, lineH);
            }
        }
    }
    // The chevrons are only on screen while the gutter is hovered, on the
    // caret's own row, or over a fold that is closed — a column of them on
    // every candidate line would read as noise. This is last frame's boxes,
    // which is one frame stale and is what Rust's hitbox is too.
    bool gutterHover = folding && GutterHovered(state, cx->win);
    VecClear(state->foldIcons);
    if (folding) {
        VecReserve(state->foldIcons, state->folds.candidates.len);
    }
    // The row the caret is on, which is the one the active-line wash covers
    // and the one the fold gutter keeps a chevron showing on. A caret inside
    // a closed fold reads as the fold's own line: display_map maps a folded
    // buffer position to column 0 of the nearest visible display row, so the
    // caret sits at the head of the line the fold collapsed into rather than
    // vanishing with the text it is in.
    int caretRow = -1;
    if (style.activeLine.a != 0 || folding || lineNumbers) {
        caretRow = InputOffsetToPoint(state, cursor).row;
        caretRow = FoldMapNearestVisibleLine(&state->folds, caretRow);
    }
    bool caretFolded =
        folding && caret &&
        FoldMapLineHidden(&state->folds, InputOffsetToPoint(state, cursor).row);
    // layout_indent_guides' last_indents: an empty line carries the guides
    // of the line built above it, so a blank line inside a block does not
    // break the block's guides.
    bool indentGuides = style.indentGuide.a != 0 && style.indentWidth > 0;
    int lastIndent = 0;
    // Only the rows the box can show are built. Rust lays out the display
    // rows in the visible range and nothing else; without that, a document of
    // ten thousand lines is ten thousand elements a frame, which is more than
    // the frame arena holds and more than any of it is worth. The rows that
    // are skipped are stood in for by a spacer at each end, the way the list
    // and the table do it, so the scrolled height and the scrollbar are the
    // ones the whole document has.
    //
    // Without soft wrap every row is `lineH` and the range is
    // arithmetic. With it a row is as tall as its own text, so the range
    // comes off last frame's boxes — one frame stale, which is what the fold
    // gutter's hitbox already is — and a row with no box yet is estimated at
    // one line, which the next frame corrects.
    //
    // viewH is the clip box, not the content column. BindInput can record
    // the inner column's laid-out height (the whole document) or nothing
    // yet on the first frame of a file; either would build every line.
    // Cap at the window so a mistaken content height still virtualizes.
    float vh = state->viewH;
    float vhCap = 600.f;
    if (cx->win && cx->win->paint.viewH > 0) {
        vhCap = cx->win->paint.viewH;
    }
    if (vh <= 0 || vh > vhCap) {
        vh = vhCap;
    }
    int firstRow = 0;
    int endRow = rows;
    float padTop = 0;
    float padBottom = 0;
    if (rows > 1) {
        // Two rows of slack at each end, so a scroll of a few pixels does not
        // uncover an empty band before the next frame fills it.
        const int kSlack = 2;
        float top = state->scrollY;
        float bottom = top + vh;
        if (!wrap) {
            int first = (int)(top / lineH) - kSlack;
            int end = (int)(bottom / lineH) + 1 + kSlack;
            firstRow = first < 0 ? 0 : (first > rows ? rows : first);
            endRow = end < firstRow ? firstRow : (end > rows ? rows : end);
            padTop = (float)firstRow * lineH;
            padBottom = (float)(rows - endRow) * lineH;
        } else {
            // Heights only: a box's window y is last-painted and goes stale
            // the moment that row leaves the viewport. Subtracting it from
            // contentBox.y (which embeds this frame's scrollY) made firstRow
            // stick at the old band, so the viewport was empty (white) until
            // a click's scroll_to jumped back there.
            float at = 0;
            int first = -1;
            int end = rows;
            for (int i = 0; i < rows; i++) {
                float h = DisplayLineH(state, i, lineH);
                if (first < 0 && at + h > top) {
                    first = i;
                }
                if (at > bottom) {
                    end = i;
                    break;
                }
                at += h;
            }
            firstRow = first < 0 ? 0 : first;
            endRow = end < firstRow ? firstRow : end;
            firstRow = firstRow > kSlack ? firstRow - kSlack : 0;
            endRow = endRow + kSlack > rows ? rows : endRow + kSlack;
            padTop = DisplayRowDocY(state, firstRow, lineH);
            padBottom = DisplayRowDocY(state, rows, lineH) -
                        DisplayRowDocY(state, endRow, lineH);
            if (padBottom < 0) {
                padBottom = 0;
            }
        }
        // A wrap walk over empty boxes, or a viewH that is still the full
        // document, can ask for every row. Cap at a viewport band.
        int maxRows = (int)(vh / lineH) + 1 + 2 * kSlack;
        if (maxRows < 8) {
            maxRows = 8;
        }
        if (endRow - firstRow > maxRows) {
            endRow = firstRow + maxRows;
            if (endRow > rows) {
                endRow = rows;
            }
            padBottom = DisplayRowDocY(state, rows, lineH) -
                        DisplayRowDocY(state, endRow, lineH);
            if (padBottom < 0) {
                padBottom = 0;
            }
        }
    }
    // empty_bottom_height: extra scrollable space past the last line, so
    // the caret can sit in the upper half of a code editor. Ghost lines
    // are overlaid here rather than stacked, so they do not share this.
    float emptyBottom =
        InputEmptyBottomHeight(state->mode.kind == LayoutModeKind::CodeEditor,
                               state->scrollBeyondLastLine, vh, lineH);
    state->contentH += emptyBottom;
    padBottom += emptyBottom;
    if (padTop > 0) {
        col->Child(Div(a)->W(kFill)->Shrink0()->H(padTop));
    }

    // Which diagnostic the pointer is over, for the popover the component
    // draws. Rust works it out in the element's own mouse handling; the
    // pointer is the window's here, and this is the one place that has the
    // boxes to answer against.
    // What the document names in colour, asked for again when it changed.
    InputLspUpdate(state);
    // set_disabled / set_readonly put the menus and any suggestion away. The
    // flags are plain fields here, so the frame that first sees the field
    // uneditable does it.
    if (!InputIsEditable(state)) {
        InputHideContextMenu(state);
        if (InputHasInlineCompletion(state) || !state->inlineCompletion.asked) {
            InputClearInlineCompletion(state);
        }
    }
    // The debounce in front of an inline suggestion. A frame is the clock, so
    // one has to keep coming while it runs.
    if (InputUpdateInlineCompletion(state, state->completion.open)) {
        WindowRequestAnimationFrame(cx->win);
    }
    if ((state->diagnostics.len > 0 || state->hoverProvider ||
         state->definitionProvider) &&
        cx->win) {
        float mx = cx->win->mouseX;
        float my = cx->win->mouseY;
        bool inside = state->inputBounds.Contains({mx, my});
        bool overPopover = state->popoverBounds.Contains({mx, my});
        if (!overPopover) {
            state->hoverDiagnostic = -1;
        }
        int at =
            inside ? InputIndexForPosition(state, &cx->win->paint, mx, my) : -1;
        // handle_mouse_move: with the shortcut modifier down the pointer is
        // asking what a symbol is defined as; without it, it is asking what
        // the symbol *is*, which is the hover popover below. The two are
        // exclusive, and a pointer outside the field clears both.
        // Alt is the multi-cursor modifier, so alt+secondary asks nothing.
        bool secondary = inside && !state->selecting &&
                         cx->win->mouseModifiers.Secondary() &&
                         !cx->win->mouseModifiers.alt;
        if (secondary && state->definitionProvider) {
            InputHoverDefinition(state, at);
        } else {
            InputClearHoverDefinition(state);
        }
        if (inside) {
            for (int d = 0; d < state->diagnostics.len; d++) {
                const Diagnostic& dg = state->diagnostics[d];
                if (at >= dg.range.start && at < dg.range.end) {
                    state->hoverDiagnostic = d;
                    state->hoverDiagnosticX = mx;
                    state->hoverDiagnosticY = my;
                    break;
                }
            }
        }
        // What the pointer is resting on, for the hover popover —
        // handle_hover_popover. The provider is asked once per word: while
        // the pointer stays inside the word it was asked about, what it said
        // stands. A diagnostic under the pointer wins, and so does a drag.
        if (overPopover) {
            // The source keeps the union of trigger and popover live. The
            // popover's own outside listener clears it on a press elsewhere.
        } else if (!state->hoverProvider || !inside || state->selecting ||
                   state->hoverDiagnostic >= 0 || secondary) {
            state->hoverText = Str{};
            state->hoverRange = Selection{};
            state->hoverAsked = true;
        } else if (at < state->hoverRange.start ||
                   at >= state->hoverRange.end || state->hoverRange.IsEmpty()) {
            Str doc = InputValue(state);
            int a0 = at, b0 = at;
            if (!TextWordRangeAt(doc, at, &a0, &b0)) {
                a0 = b0 = at;
            }
            Selection word = {a0, b0};
            // `should_delay = hover_popover.is_none()`: with nothing showing,
            // the pointer has to rest on the word for 150 ms before the
            // provider is asked; with a popover already up, moving from word
            // to word answers at once. A frame is the clock, so a wait asks
            // for the next one.
            bool showing = state->hoverText.len > 0;
            bool pending = !state->hoverAsked &&
                           state->hoverPending.start == word.start &&
                           state->hoverPending.end == word.end;
            if (!showing && !pending) {
                state->hoverPending = word;
                state->hoverAsked = false;
                state->hoverDueAt = TimeNow() + 0.150;
                state->hoverText = Str{};
                state->hoverRange = Selection{};
                WindowRequestAnimationFrame(cx->win);
            } else if (!showing && TimeNow() < state->hoverDueAt) {
                WindowRequestAnimationFrame(cx->win);
            } else {
                state->hoverAsked = true;
                state->hoverRange = word;
                state->hoverText =
                    a0 < b0 ? state->hoverProvider(state->hoverData, doc, at)
                            : Str{};
                state->hoverX = mx;
                state->hoverY = my;
            }
        }
    }

    // The document's runs, sliced per row below, and the search matches
    // beside them.
    int spanAt = 0;
    int matchAt = 0;
    // Each row's start and end are lookups in the line index rather than
    // scans of the document — RopeSliceLine, three scans per row, was 13% of
    // a scroll frame in the editor example.
    const Vec<int>& lineStarts = InputLineStarts(state);
    // The style runs the rows slice out of. With a highlighter installed the
    // element asks it for the visible byte range only — element.rs groups
    // the visible lines and calls styles() per group — and lays the
    // decoration collection the caller projected (style.spans: semantic
    // tokens, document colours, caller runs) over what it answered. Without
    // one, style.spans is the whole collection, as it always was. Range
    // queries are what free the highlighter from any whole-document span
    // cap: a document of any size styles the band on screen.
    const TextSpan* docSpans = style.spans;
    int nDocSpans = style.nSpans;
    if (state->highlighter.styles && firstRow < endRow &&
        firstRow < len(lineStarts)) {
        Selection vis = {lineStarts[firstRow], endRow < len(lineStarts)
                                                   ? lineStarts[endRow]
                                                   : len(text)};
        TextSpan* hl = nullptr;
        int nHl = state->highlighter
                      .Styles(vis, &style.highlightStyles, a, &hl);
        // decoration_layers: the editor's own text decorations go over what
        // the caller passed, as Rust composes the extras' layers last.
        int nOwn = InputDecorationSpans(state, nullptr, 0);
        TextSpan* own = nullptr;
        if (nOwn > 0) {
            own = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * nOwn);
            nOwn = own ? InputDecorationSpans(state, own, nOwn) : 0;
        }
        if (style.nSpans > 0 || nOwn > 0) {
            // Compose in the arena: every decoration adds at most itself and
            // one split, so the bound is exact.
            int cap = nHl + 2 * style.nSpans + 2 * nOwn;
            auto* buf = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * cap);
            auto* tmp = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * cap);
            if (buf && tmp) {
                if (nHl > 0) {
                    memcpy(buf, hl, (size_t)nHl * sizeof(TextSpan));
                }
                nDocSpans = nHl;
                if (style.nSpans > 0) {
                    nDocSpans = InputComposeSpans(buf, nDocSpans, style.spans,
                                                  style.nSpans, cap, tmp);
                }
                if (nOwn > 0) {
                    nDocSpans =
                        InputComposeSpans(buf, nDocSpans, own, nOwn, cap, tmp);
                }
                docSpans = buf;
            }
        } else {
            docSpans = hl;
            nDocSpans = nHl;
        }
    }
    // layout_range_decorations: query separate buffer spans across folds
    // instead of scanning annotations in the hidden text between the first
    // and last visible offsets. Each visible line's span takes its newline.
    RangeDecorationPaint* rangePaint = nullptr;
    if (state->rangeDecorations && firstRow < endRow) {
        Vec<Selection> spans;
        for (int row = firstRow; row < endRow && row < len(lineStarts); row++) {
            if (folding && FoldMapLineHidden(&state->folds, row)) {
                continue;
            }
            int offset = lineStarts[row];
            int lineEnd =
                row + 1 < len(lineStarts) ? lineStarts[row + 1] - 1 : len(text);
            int end = std::min(lineEnd + 1, len(text));
            if (len(spans) > 0 && spans[len(spans) - 1].end == offset) {
                spans[len(spans) - 1].end = end;
            } else if (offset < end) {
                VecAppend(spans, Selection{offset, end});
            }
        }
        int n = InputRangeDecorations(state, spans.els, len(spans), nullptr, 0);
        if (n > 0) {
            rangePaint = ArenaNew<RangeDecorationPaint>(a);
            rangePaint->decorations = (const RangeDecoration**)Alloc(
                a, (int)sizeof(const RangeDecoration*) * n);
            rangePaint->nDecorations = InputRangeDecorations(
                state, spans.els, len(spans), rangePaint->decorations, n);
            rangePaint->foreground = style.foreground;
            rangePaint->lineH = lineH;
            rangePaint->arena = a;
        }
    }
    // Every visual row the lines in the band wrap to: each is one shaped
    // run, the way element.rs shapes `wrapped_lines` one ShapedLine apiece.
    int visualRows = 0;
    for (int row = firstRow; row < endRow; row++) {
        visualRows += InputWrapRows(state, row, nullptr, nullptr);
    }
    InputPaintedRows* painted = ArenaNew<InputPaintedRows>(a);
    state->paintedRows = painted;
    state->paintedRowsFrame = cx->win ? cx->win->frameSeq : 0;
    if (painted) {
        painted->decorations = rangePaint;
        painted->geometry.lineH = lineH;
        painted->geometry.arena = a;
        if (visualRows > 0) {
            painted->geometry.rows = (RangeDecorationRow*)Alloc(
                a, (int)sizeof(RangeDecorationRow) * visualRows);
        }
        if (rangePaint) {
            rangePaint->rows = painted->geometry.rows;
        }
    }
    // What the column paints under its rows, once for the whole editor: the
    // active line, then every decoration path, element.rs's paint order.
    EditorUnderlay* underlay = ArenaNew<EditorUnderlay>(a);
    if (underlay) {
        underlay->state = state;
        underlay->decorations = rangePaint;
        underlay->activeColor = style.activeLine;
        underlay->activeBleedL = style.activeLineBleedL;
        underlay->cx = *cx;
        underlay->projected = projected;
        underlay->lineNumbers = lineNumbers;
        col->customPaint = &PaintEditorUnderlay;
        col->customUser = underlay;
        if (state->softWrap) {
            col->prePaint = &RewrapEditorColumn;
        }
        if (painted) {
            painted->underlay = underlay;
        }
    }
    for (int row = firstRow; row < endRow; row++) {
        int lineStart = lineStarts[row];
        int lineEnd =
            row + 1 < len(lineStarts) ? lineStarts[row + 1] - 1 : len(text);
        Str lineText = Str(text.s + lineStart, lineEnd - lineStart);
        // A line inside a closed fold is not built at all, which is what
        // makes the rows below it move up.
        if (folding && FoldMapLineHidden(&state->folds, row)) {
            continue;
        }
        // The spans and matches are in document order and the walk carries on
        // where the last row left off, so a range that does not start at the
        // top has to skip what came before it.
        if (row == firstRow) {
            while (spanAt < nDocSpans && docSpans[spanAt].hi <= lineStart) {
                spanAt++;
            }
            // layout_search_matches: partition_point over the sorted,
            // non-overlapping matches, so a document with thousands of them
            // does not walk every one above the viewport each frame.
            int lo = matchAt, hi = style.nMatches;
            while (lo < hi) {
                int mid = lo + (hi - lo) / 2;
                if (style.matches[mid].end <= lineStart) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            matchAt = lo;
        }
        const int* wrapStarts = nullptr;
        float wrapIndent = 0;
        int nVis = InputWrapRows(state, row, &wrapStarts, &wrapIndent);
        // A wrapped line's rows stack in a column of their own, each a whole
        // line height, the ones after the first shifted by the wrap indent.
        El* lineEl = nVis > 1 ? Div(a)->FlexCol()->W(kFill) : nullptr;
        El* single = nullptr;
        for (int vis = 0; vis < nVis; vis++) {
            int segLo = wrapStarts[vis];
            int segHi = vis + 1 < nVis ? wrapStarts[vis + 1] : len(lineText);
            int start = lineStart + segLo;
            Str line = Str(lineText.s + segLo, segHi - segLo);
            bool firstSeg = vis == 0;
            bool lastSeg = vis == nVis - 1;
            bool tokenLine =
                LineHasVisibleTokens(state, start, start + len(line));
            El* el = nullptr;
            RangeDecorationRow* paintedRow = nullptr;
            if (painted && painted->geometry.rows &&
                painted->geometry.nRows < visualRows) {
                paintedRow = &painted->geometry.rows[painted->geometry.nRows++];
                paintedRow->start = start;
                paintedRow->len = len(line);
                paintedRow->lineStart = lineStart;
                paintedRow->lineLen = len(lineText);
                paintedRow->first = firstSeg;
                paintedRow->last = lastSeg;
            }
            if (tokenLine) {
                // Overlay chips instead of the raw token text, matching
                // Input::New: the row's text runs and its atomic chips, one
                // visual row of what the wrap map broke the line into.
                // Its runs are not bound to the field, so none of them says
                // what size the text is drawn at.
                state->lastFont = font;
                el = Div(a)->FlexRow()->ItemsCenter()->H(lineH);
                AppendTokenPieces(el, cx, state, style, font, lineMult, lineH,
                                  line, start, sel, caret, cursor);
            } else {
                el = TextEl(a, line)->Font(font)->LineHeight(lineMult)->Fg(
                    style.foreground);
                InputFace(el, style);
            }
            if (paintedRow) {
                paintedRow->text = el;
            }
            // element.rs MAX_HIGHLIGHT_LINE_LENGTH: a line longer than this --
            // minified output, generated code -- draws in the default style
            // rather than styled, which is Rust's guard against laying spans
            // over an enormous run. The cursor is not advanced here; the next
            // row's catch-up loop above skips whatever this one left behind.
            const int kMaxHighlightLineLen = 10000;
            // Highlight, diagnostics and selection belong to the shaped
            // TextEl. A token row is already split into runs and chips.
            if (!tokenLine && nDocSpans > 0 &&
                len(lineText) <= kMaxHighlightLineLen) {
                while (spanAt < nDocSpans && docSpans[spanAt].hi <= start) {
                    spanAt++;
                }
                int first = spanAt;
                int count = 0;
                while (first + count < nDocSpans &&
                       docSpans[first + count].lo < start + len(line)) {
                    count++;
                }
                if (count > 0) {
                    auto* rowSpans =
                        (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * count);
                    int nRowSpans = 0;
                    for (int k = 0; k < count; k++) {
                        const TextSpan& sp = docSpans[first + k];
                        int lo = sp.lo - start;
                        int hi = sp.hi - start;
                        if (lo < 0) {
                            lo = 0;
                        }
                        if (hi > len(line)) {
                            hi = len(line);
                        }
                        if (hi <= lo) {
                            continue;
                        }
                        rowSpans[nRowSpans] = sp;
                        rowSpans[nRowSpans].lo = lo;
                        rowSpans[nRowSpans].hi = hi;
                        nRowSpans++;
                    }
                    if (nRowSpans > 0) {
                        el->Spans(rowSpans, nRowSpans);
                    }
                }
            }
            // element.rs composes the diagnostic styles over the rest: a wavy
            // underline in the severity's colour, which is a run of its own
            // rather than a recolouring of the glyphs.
            if (!tokenLine && state->diagnostics.len > 0) {
                int nDiag = 0;
                for (int d = 0; d < state->diagnostics.len; d++) {
                    const Diagnostic& dg = state->diagnostics[d];
                    if (dg.range.end <= start ||
                        dg.range.start >= start + len(line)) {
                        continue;
                    }
                    nDiag++;
                }
                if (nDiag > 0) {
                    auto* runs =
                        (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * nDiag);
                    int n = 0;
                    for (int d = 0; d < state->diagnostics.len && runs; d++) {
                        const Diagnostic& dg = state->diagnostics[d];
                        int lo = dg.range.start - start;
                        int hi = dg.range.end - start;
                        if (lo < 0) {
                            lo = 0;
                        }
                        if (hi > len(line)) {
                            hi = len(line);
                        }
                        if (hi <= lo) {
                            continue;
                        }
                        Rgba c = style.diagnostics.info;
                        if (dg.severity == DiagnosticSeverity::Error) {
                            c = style.diagnostics.error;
                        } else if (dg.severity == DiagnosticSeverity::Warning) {
                            c = style.diagnostics.warning;
                        } else if (dg.severity == DiagnosticSeverity::Hint) {
                            c = style.diagnostics.hint;
                        }
                        if (c.a == 0) {
                            continue;
                        }
                        runs[n].lo = lo;
                        runs[n].hi = hi;
                        runs[n].color = c;
                        runs[n].bg = Rgba{0, 0, 0, 0};
                        runs[n].underline = true;
                        runs[n].wavy = true;
                        n++;
                    }
                    if (n > 0) {
                        el->Underlines(runs, n);
                    }
                }
            }
            // hover_definition_style: the symbol a secondary-hover found is
            // underlined in the link colour, one hairline and not a wavy one.
            // Rust pushes it as another highlight style over the row; here it
            // is one more underline run, which is the same list the
            // diagnostics use.
            if (!tokenLine && state->hoverDef.locations.len > 0 &&
                style.linkText.a != 0) {
                Selection sym = state->hoverDef.symbolRange;
                int lo = sym.start - start;
                int hi = sym.end - start;
                if (lo < 0) {
                    lo = 0;
                }
                if (hi > len(line)) {
                    hi = len(line);
                }
                if (hi > lo) {
                    auto* run = (TextSpan*)Alloc(a, (int)sizeof(TextSpan));
                    if (run) {
                        run->lo = lo;
                        run->hi = hi;
                        run->color = style.linkText;
                        run->bg = Rgba{0, 0, 0, 0};
                        run->underline = true;
                        run->wavy = false;
                        el->Underlines(run, 1);
                    }
                    // Where it landed, for the hand cursor: measured on the
                    // row the symbol starts in.
                    if (sym.start >= start || firstSeg) {
                        el->RangeOut(lo, hi, &state->hoverDef.bounds);
                    }
                }
            }
            // input/popovers::Popover::trigger_bounds: use the exact shaped
            // range, not the pointer that happened to ask for it, on the row
            // it starts in. Diagnostic and hover popovers are mutually
            // exclusive, as they are in the source.
            Selection popoverRange = state->hoverRange;
            if (state->hoverDiagnostic >= 0 &&
                state->hoverDiagnostic < state->diagnostics.len) {
                popoverRange = state->diagnostics[state->hoverDiagnostic].range;
            }
            int popoverLo = popoverRange.start - start;
            int popoverHi = popoverRange.end - start;
            bool popoverHere = popoverRange.start >= start || firstSeg;
            if (popoverLo < 0) popoverLo = 0;
            if (popoverHi > len(line)) popoverHi = len(line);
            if (!tokenLine && popoverHere && popoverHi > popoverLo) {
                if (state->popoverTriggerRange.start != popoverRange.start ||
                    state->popoverTriggerRange.end != popoverRange.end) {
                    state->popoverTriggerRange = popoverRange;
                    state->popoverTriggerBounds = {};
                    WindowRequestAnimationFrame(cx->win);
                }
                el->RangeOut(popoverLo, popoverHi,
                             &state->popoverTriggerBounds);
            }
            if (!tokenLine) {
                RowMatchWashes(a, el, style, state, start, len(line), &matchAt);
            }
            // The first line's first row is the one the state measures
            // against; every row below it is a whole lastLineH further down.
            if (row == 0 && firstSeg) {
                el->BindInput(state);
                if (tokenLine) {
                    // A token row draws no run of its own to measure from;
                    // its box starts where the text column does.
                    el->BoundsOut(&state->lastBounds);
                }
            }
            int lo = sel.start - start;
            int hi = sel.end - start;
            if (lo < 0) {
                lo = 0;
            }
            if (hi > len(line)) {
                hi = len(line);
            }
            if (!tokenLine && !sel.IsEmpty() && lo < hi) {
                el->SelRange(lo, hi, style.selection);
            }
            // The caret on a soft-wrap boundary stands at the end of the row
            // the boundary closes with the line-end affinity, and at the
            // start of the row it opens without.
            bool caretHere = cursor >= start && cursor <= start + len(line);
            if (caretHere && cursor == start + len(line) && !lastSeg &&
                !state->cursorLineEndAffinity) {
                caretHere = false;
            }
            if (caretHere && cursor == start && !firstSeg &&
                state->cursorLineEndAffinity) {
                caretHere = false;
            }
            if (!tokenLine && caretFolded) {
                if (row == caretRow && firstSeg) {
                    el->Caret(0, style.caret);
                }
            } else if (!tokenLine && caret && caretHere) {
                el->Caret(cursor - start, style.caret, 2,
                          state->cursorLineEndAffinity);
                // Where it lands is the anchor a completion menu hangs off.
                el->CaretOut(&state->caretWinX, &state->caretWinY);
            } else if (!tokenLine && !caretFolded && caretHere) {
                // cursor_bounds is laid out whether or not the caret shows --
                // unfocused, or the dark half of the blink -- so
                // cursor_layout() follows the selection all the same.
                // Measured, not drawn.
                el->Caret(cursor - start, Rgba{0, 0, 0, 0}, 2,
                          state->cursorLineEndAffinity);
                el->CaretOut(&state->caretWinX, &state->caretWinY);
            }
            if (!tokenLine && state->extraCursors.len > 0) {
                RowExtraCursors(a, el, state, style, start, len(line), caret,
                                lastSeg);
            }
            // indent_guides: a hairline every tab stop of the line's leading
            // whitespace (a tab counts as a whole stop), drawn by the line's
            // first run behind its text at the width its font gives
            // `indentWidth` spaces -- Rust's measure_indent_width, not a
            // guessed column.
            if (indentGuides && firstSeg) {
                int indent = lastIndent;
                if (len(lineText) > 0) {
                    indent = TabSize{style.indentWidth}.IndentCount(lineText);
                    lastIndent = indent;
                }
                if (!tokenLine) {
                    el->IndentGuides(style.indentGuide, indent,
                                     style.indentWidth);
                }
            }
            // show_whitespaces: the row's own run paints a mark over each
            // space and tab, measured against the glyphs it shaped (the same
            // x_for_index Rust uses). editor_invisible is not in this tree's
            // highlight theme; Rust falls back to muted_foreground without it.
            if (!tokenLine && state->showWhitespaces) {
                el->Whitespaces(style.mutedForeground);
            }
            if (!lineEl) {
                single = el;
            } else if (vis > 0 && wrapIndent > 0) {
                lineEl->Child(
                    Div(a)->FlexRow()->H(lineH)->PadL(wrapIndent)->Child(el));
            } else {
                lineEl->Child(el);
            }
        }
        El* el = lineEl ? lineEl : single;
        if (!el) {
            continue;
        }
        if (!lineNumbers) {
            col->Child(el);
            continue;
        }
        // LINE_NUMBER_RIGHT_MARGIN: what separates the numbers from the text.
        // A wrapped line's band is as tall as its rows, and its number sits
        // on the first of them.
        El* band = Div(a)
                       ->FlexRow()
                       ->W(kFill)
                       ->Gap(kLineNumberRightMargin)
                       ->H((float)nVis * lineH)
                       ->ItemsStart();
        // active_line: the wash under the line the caret is on, gutter and
        // all, and the editor's padding left of the gutter. The column
        // paints it under every row, before the decorations.
        if (row == caretRow && style.activeLine.a != 0 && underlay) {
            underlay->activeLine = band;
        }
        // The caret's line is numbered in the foreground, the rest muted.
        El* num =
            TextEl(a, StrDup(a, fmt("%d", InputDisplayedLineNumber(row + 1))))
                ->Font(font)
                ->LineHeight(lineMult)
                ->Fg(row == caretRow ? style.foreground
                                     : style.mutedForeground);
        InputFace(num, style);
        El* numCell = Div(a)->W(numW)->JustifyEnd()->Child(num);
        if (folding) {
            // The line-number column and the fold icons are one hit strip,
            // the way Rust's line_number_hitbox covers both. PathClick on
            // every row so entering the gutter from the text changes hover
            // id and rebuilds -- otherwise the editor already owns hover and
            // a move over the numbers would not show the chevrons.
            // The fold hitbox starts where the numbers end: Rust widens
            // the column by FOLD_ICON_HITBOX_WIDTH and puts the icon in it.
            El* gutter = Div(a)->FlexRow()->ItemsCenter()->H(lineH)->PathClick(
                StrDup(a, fmt("gutter-%d", row)));
            gutter->Child(numCell);
            gutter->Child(FoldChevron(a, state, style, row, caretRow, lineH,
                                      gutterHover));
            if (row == firstRow) {
                gutter->BoundsOut(&state->gutterBox);
            }
            band->Child(gutter);
        } else {
            if (row == 0) {
                numCell->BoundsOut(&state->gutterBox);
            }
            band->Child(numCell);
        }
        if (wrap) {
            // flex_1: the text column is what the gutter leaves, the width
            // the rows were wrapped to.
            el->Flex1();
        }
        band->Child(el);
        col->Child(band);
    }
    if (rangePaint && painted) {
        rangePaint->nRows = painted->geometry.nRows;
    }
    if (padBottom > 0) {
        col->Child(Div(a)->W(kFill)->Shrink0()->H(padBottom));
    }

    // layout_inline_completion: the suggestion in front of the caret, in the
    // muted foreground at half opacity. Rust shapes the first line to sit
    // after the cursor and shifts the rows below down to make room for the
    // rest; the rows here are a virtualized flex column whose heights the
    // layout owns, so every line is drawn *over* what is under it — each on
    // its own background, which is what Rust paints under its first line for
    // the same reason.
    if (state->focused && InputHasInlineCompletion(state) &&
        state->contentBox.h > 0) {
        Rgba ghostFg = RgbaOpacity(style.mutedForeground, 0.5f);
        // Where the text column starts, for the lines after the first: the
        // gutter and the fold strip are not part of it.
        float textLeft = 0;
        if (lineNumbers) {
            textLeft = numW + kLineNumberRightMargin + (folding ? foldW : 0.f);
        }
        // Where the caret was last painted, in the column's own coordinates.
        float gx = state->caretWinX - state->contentBox.x;
        float gy = state->caretWinY - state->contentBox.y - lineH;
        Str rest = state->inlineCompletion.text;
        for (int line = 0; len(rest) > 0 || line == 0; line++) {
            int nl = -1;
            for (int i = 0; i < len(rest); i++) {
                if (rest.s[i] == '\n') {
                    nl = i;
                    break;
                }
            }
            Str one = nl >= 0 ? Str(rest.s, nl) : rest;
            rest = nl >= 0 ? Str(rest.s + nl + 1, len(rest) - nl - 1) : Str{};
            if (len(one) > 0) {
                El* ghost = Div(a)
                                ->Absolute()
                                ->Left(line == 0 ? gx : textLeft)
                                ->Top(gy + (float)line * lineH)
                                ->H(lineH)
                                ->Bg(style.background);
                El* run = TextEl(a, one)->Font(font)->LineHeight(lineMult)->Fg(
                    ghostFg);
                InputFace(run, style);
                col->Child(ghost->Child(run));
            }
            if (nl < 0) {
                break;
            }
        }
    }
    return col;
}

El* Editor::New(Ctx* cx, InputState* state) {
    return New(cx, state, InputEditorStyle{});
}

El* Editor::New(Ctx* cx, InputState* state, const InputEditorStyle& style) {
    // EditorMode is TextareaMode plus the language features this tree does not
    // have; what is left of it that we do render is the line number gutter.
    return Textarea::New(cx, state, style, true);
}

/* Port of crates/base/src/input/base — state.rs, movement.rs, selection.rs and
   mode.rs. blink_cursor.rs is in Gpui.cpp beside the window timers it needs,
   rope_ext.rs is Rope.cpp, mask_pattern.rs is MaskPattern.cpp, and change.rs +
   undo_manager.rs are UndoManager.cpp.

   Rust's engine is `InputBaseState<M>`, generic over a mode marker so that a
   method which makes no sense for a single-line field does not exist on it.
   There is no such thing to bound on here, so the marker is a runtime
   `InputKind` and those methods return early — `InputMoveVertical` on an
   `InputKind::Input` is the compile error Rust would have raised.

   What is not ported is one thing: a language *server*. Every seam in
   `input/editor/lsp` is here — completion, hover, code actions, document
   colours, semantic tokens, definitions — but there is no JSON-RPC and no
   child process behind them, so a provider is a function pointer an
   application fills. Code folding, the search session and the display map
   came over in later passes; vertical movement walks display rows, and
   start_of_line / end_of_line take the wrapped row first and the logical line
   on a second press, the way Rust gates it on soft wrap in a code editor. */

// ─── the document ─────────────────────────────────────────────────────────
//
// Rust holds it in a `ropey::Rope`. Here it is a flat UTF-8 buffer, kept
// NUL-terminated past `len` so a `const char*` reader still works; the
// terminator is not counted in the length.

Str InputValue(const InputState* s) {
    if (!s || len(s->text) <= 0) {
        return {};
    }
    return Str(s->text.els, len(s->text));
}

const char* InputCStr(const InputState* s) {
    return s && s->text.els ? s->text.els : "";
}

// ─── the line index ───────────────────────────────────────────────────────
//
// ropey keeps a tree and answers line_to_byte_idx / byte_to_line_idx in
// O(log n); the flat buffer's equivalent is one Vec of line-start offsets,
// rebuilt in a single memchr pass when the document moved. Filling a lazy
// cache is a read as far as every caller is concerned, which is what the
// const_cast below says.

static void LineStartsEnsure(InputState* s) {
    if (s->lineStartsValid && s->lineStartsVersion == s->docVersion) {
        return;
    }
    VecClear(s->lineStarts);
    VecAppend(s->lineStarts, 0);
    Str t = InputValue(s);
    int at = 0;
    while (at < len(t)) {
        const char* nl =
            (const char*)memchr(t.s + at, '\n', (size_t)(len(t) - at));
        if (!nl) {
            break;
        }
        at = (int)(nl - t.s) + 1;
        VecAppend(s->lineStarts, at);
    }
    s->lineStartsValid = true;
    s->lineStartsVersion = s->docVersion;
}

const Vec<int>& InputLineStarts(const InputState* s) {
    LineStartsEnsure(const_cast<InputState*>(s));
    return s->lineStarts;
}

int InputLinesLen(const InputState* s) {
    return InputLineStarts(s).len;
}

int InputLineStartOffset(const InputState* s, int row) {
    const Vec<int>& starts = InputLineStarts(s);
    if (row <= 0) {
        return 0;
    }
    if (row >= len(starts)) {
        return len(s->text);
    }
    return starts[row];
}

Str InputSliceLine(const InputState* s, int row) {
    const Vec<int>& starts = InputLineStarts(s);
    if (row < 0 || row >= len(starts)) {
        return {};
    }
    int a = starts[row];
    int b = row + 1 < len(starts) ? starts[row + 1] - 1 : len(s->text);
    return Str(s->text.els + a, b - a);
}

RopePoint InputOffsetToPoint(const InputState* s, int offset) {
    const Vec<int>& starts = InputLineStarts(s);
    offset = RopeClipOffset(InputValue(s), offset, Bias::Left);
    // The last line whose start is at or before the offset.
    int lo = 0;
    int hi = len(starts) - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (starts[mid] <= offset) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    RopePoint p = {};
    p.row = lo;
    p.column = offset - starts[lo];
    return p;
}

// The one clipboard read a Paste has in flight, and what it was asked to
// replace. A browser has one user activation at a time, so one slot is enough.
static struct {
    InputState* state = nullptr;
    InputPasteTarget target;
} gPendingPaste;

InputState::~InputState() {
    // A Paste still waiting on the clipboard must not land in freed memory;
    // Rust's weak handle simply fails to upgrade.
    if (gPendingPaste.state == this) {
        gPendingPaste.state = nullptr;
    }
    InputDecorationsFree(this);
    if (contextMenuDrop && contextMenuData) {
        contextMenuDrop(contextMenuData);
    }
    InputSyntaxCacheFree(this);
    // A field removed from the tree while it had the keyboard: the window
    // still points at it, and nothing would ever render it again to say
    // otherwise. Rust drops that registration the next time it is read; here
    // the field takes it with it, which also keeps the pointer from dangling.
    if (focusWin) {
        if (focusWin->input == this) {
            focusWin->input = nullptr;
        }
        if (focusWin->prevInput == this) {
            focusWin->prevInput = nullptr;
        }
    }
    StrFree(placeholder);
    MaskPatternFree(&maskPattern);
    VecReset(autoClosed);
    InlineTokenStoreFree(tokens);
    tokens = nullptr;
    if (highlighter.drop) {
        highlighter.drop(highlighter.data);
    }
}

static void TextReserve(InputState* s, int want) {
    VecReserve(s->text, want + 1);
}

// The wrap map's edit envelope, grown by one splice of [a, b) of the current
// text into `insLen` bytes. Before the envelope's start the two documents
// agree byte for byte, and past its new end they differ by a constant shift,
// so the merge is exact however many splices come before the rows are
// wrapped again.
static void WrapMapNoteEdit(InputWrapMap* m, int a, int b, int insLen) {
    if (m->editWhole) {
        return;
    }
    if (!m->hasEdit) {
        m->hasEdit = true;
        m->editStart = a;
        m->editOldEnd = b;
        m->editNewEnd = a + insLen;
        return;
    }
    if (a < m->editStart) {
        m->editStart = a;
    }
    if (b > m->editNewEnd) {
        m->editOldEnd += b - m->editNewEnd;
        m->editNewEnd = b;
    }
    m->editNewEnd += insLen - (b - a);
}

// How many edits the highlighter log keeps before it gives up and asks for a
// whole-document update. A frame drives the highlighter, so this is how many
// splices one frame's input can make -- a multi-cursor keystroke is one per
// cursor -- before the batch costs more to hand over than a re-scan.
static const int kMaxHighlightEdits = 64;
// And how many bytes of reconstructed text a batch may take: past it, a
// large document edited at many cursors is handed over whole instead.
static const int64_t kMaxHighlightBatchBytes = 64ll << 20;

static void HighlightLogClear(InputState* s) {
    VecClear(s->highlightEdits);
    VecClear(s->highlightRemoved);
    s->highlightWhole = false;
}

static void HighlightLogWhole(InputState* s) {
    HighlightLogClear(s);
    s->highlightWhole = true;
}

// on_text_changed's envelope for one splice of [a, b) into `insLen` bytes,
// called before the splice so the bytes it removes can be kept. Only while
// a highlighter is installed: nothing else reads the log.
static void HighlightLogEdit(InputState* s, int a, int b, int insLen) {
    if (!s->highlighter.update || s->highlightWhole) {
        return;
    }
    if (len(s->highlightEdits) >= kMaxHighlightEdits) {
        HighlightLogWhole(s);
        return;
    }
    InputHighlightEdit e;
    e.edit = InputEdit{a, b, a + insLen};
    e.removedAt = len(s->highlightRemoved);
    e.removedLen = b - a;
    if (e.removedLen > 0) {
        VecAppendN(s->highlightRemoved, s->text.els + a, e.removedLen);
    }
    VecAppend(s->highlightEdits, e);
}

void InputDriveHighlighter(InputState* s, bool folding) {
    if (!s) {
        return;
    }
    Str text = InputValue(s);
    int n = len(s->highlightEdits);
    bool tooBig =
        (int64_t)(n - 1) * (int64_t)len(text) > kMaxHighlightBatchBytes;
    if (s->highlightWhole || n == 0 || (n > 1 && tooBig)) {
        // update(None): the text as a whole, the first time a highlighter
        // sees it or when the log could not say what moved.
        InputEdit whole = {};
        whole.oldEndByte = -1;
        whole.newEndByte = len(text);
        s->highlighter.Update(&whole, text, folding);
    } else if (n == 1) {
        s->highlighter.Update(&s->highlightEdits[0].edit, text, folding);
    } else {
        // The text after each edit, from the last back: the one after the
        // last is the document, and undoing edit k -- its inserted bytes
        // back to the ones it removed -- is the text after edit k - 1.
        Arena* a = GetTempArena();
        auto* batch =
            (InputEditWithText*)Alloc(a, (int)sizeof(InputEditWithText) * n);
        if (batch) {
            batch[n - 1].edit = s->highlightEdits[n - 1].edit;
            batch[n - 1].text = text;
            for (int k = n - 1; k > 0; k--) {
                const InputHighlightEdit& e = s->highlightEdits[k];
                Str after = batch[k].text;
                int ins = e.edit.newEndByte - e.edit.startByte;
                int outLen = len(after) - ins + e.removedLen;
                char* buf = (char*)Alloc(a, outLen + 1);
                if (!buf) {
                    batch = nullptr;
                    break;
                }
                memcpy(buf, after.s, (size_t)e.edit.startByte);
                memcpy(buf + e.edit.startByte,
                       s->highlightRemoved.els + e.removedAt,
                       (size_t)e.removedLen);
                memcpy(buf + e.edit.startByte + e.removedLen,
                       after.s + e.edit.newEndByte,
                       (size_t)(len(after) - e.edit.newEndByte));
                buf[outLen] = 0;
                batch[k - 1].edit = s->highlightEdits[k - 1].edit;
                batch[k - 1].text = Str(buf, outLen);
            }
        }
        if (batch) {
            s->highlighter.UpdateBatch(batch, n, folding);
        } else {
            InputEdit whole = {};
            whole.oldEndByte = -1;
            whole.newEndByte = len(text);
            s->highlighter.Update(&whole, text, folding);
        }
    }
    InputSkipHighlighterEdits(s);
}

void InputSkipHighlighterEdits(InputState* s) {
    if (!s) {
        return;
    }
    HighlightLogClear(s);
    s->hasPendingEdit = false;
}

// Rope::replace, over the flat buffer.
static void TextSplice(InputState* s, int a, int b, Str ins) {
    int n = len(s->text);
    if (a < 0) {
        a = 0;
    }
    if (a > n) {
        a = n;
    }
    if (b > n) {
        b = n;
    }
    if (b < a) {
        b = a;
    }
    int insLen = len(ins) > 0 ? len(ins) : 0;
    int out = n - (b - a) + insLen;
    TextReserve(s, out);
    if (!s->text.els) {
        return;
    }
    HighlightLogEdit(s, a, b, insLen);
    memmove(s->text.els + a + insLen, s->text.els + b, (size_t)(n - b));
    if (insLen > 0) {
        memcpy(s->text.els + a, ins.s, (size_t)insLen);
    }
    s->text.len = out;
    s->text.els[out] = 0;
    s->docVersion++;
    WrapMapNoteEdit(&s->wrap, a, b, insLen);
    s->hasPendingEdit = true;
}

static void TextSet(InputState* s, Str v) {
    int n = len(v) > 0 ? len(v) : 0;
    TextReserve(s, n);
    if (!s->text.els) {
        return;
    }
    if (n > 0) {
        memmove(s->text.els, v.s, (size_t)n);
    }
    s->text.len = n;
    s->text.els[n] = 0;
    s->docVersion++;
    s->wrap.editWhole = true;
    HighlightLogWhole(s);
    s->hasPendingEdit = true;
}

// ─── mode ─────────────────────────────────────────────────────────────────

void LayoutModeSetRows(LayoutMode* m, int rows) {
    if (m->kind == LayoutModeKind::AutoGrow) {
        int lo = m->minRows > 0 ? m->minRows : 1;
        int hi = m->maxRows > 0 ? m->maxRows : rows;
        m->rows = rows < lo ? lo : (rows > hi ? hi : rows);
        return;
    }
    m->rows = rows;
}

void TextareaSetAutoGrow(InputState* s, int minRows, int maxRows) {
    // LayoutMode::auto_grow(min_rows, max_rows.max(min_rows)).
    s->mode.kind = LayoutModeKind::AutoGrow;
    s->mode.rows = minRows;
    s->mode.minRows = minRows;
    s->mode.maxRows = maxRows > minRows ? maxRows : minRows;
}

void TextareaSetRows(InputState* s, int rows) {
    s->mode.rows = rows;
    if (s->mode.kind == LayoutModeKind::AutoGrow) s->mode.maxRows = rows;
}

int LayoutModeRows(const LayoutMode& m) {
    return m.rows > 1 ? m.rows : 1; // "At least 1 row be return."
}

int LayoutModeMinRows(const LayoutMode& m) {
    if (m.kind != LayoutModeKind::AutoGrow) {
        return 1;
    }
    return m.minRows > 1 ? m.minRows : 1;
}

bool LayoutModeIsFolding(const LayoutMode& m) {
    return m.kind == LayoutModeKind::CodeEditor && m.folding;
}

int InputFoldIconAt(const InputState* s, float x, float y) {
    if (!s) {
        return -1;
    }
    for (int i = 0; i < s->foldIcons.len; i++) {
        const Bounds& b = s->foldIcons[i].bounds;
        if (b.w <= 0 || b.h <= 0) {
            continue;
        }
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
            return s->foldIcons[i].line;
        }
    }
    return -1;
}

void InputToggleFold(InputState* s, App* app, Window* win, int line) {
    if (!s || !LayoutModeIsFolding(s->mode)) {
        return;
    }
    FoldMapToggle(&s->folds, line);
    AppInvalidate(win);
    (void)app;
}

bool InputUnfoldAt(InputState* s, App* app, Window* win, RopePoint position) {
    if (!s || !LayoutModeIsFolding(s->mode)) {
        return false;
    }
    // position_to_offset then offset_to_point: the row of the position once
    // it has been clipped to the document, which is what a column past the
    // end of a line or a row past the last line resolve to.
    int offset = RopePointToOffset(InputValue(s), position);
    int line = InputOffsetToPoint(s, offset).row;
    // A fold hides start_line + 1 ..= end_line - 1, so a line is hidden
    // exactly when some folded range strictly contains it. The start lines
    // are gathered first: opening a fold takes it out of `folded`, which is
    // the list being walked.
    const Vec<FoldRange>& folded = s->folds.folded;
    int* covering =
        (int*)Alloc(GetTempArena(), (int)sizeof(int) * (len(folded) + 1));
    int nCovering = 0;
    for (int i = 0; i < len(folded); i++) {
        if (line > folded[i].startLine && line < folded[i].endLine) {
            covering[nCovering++] = folded[i].startLine;
        }
    }
    if (nCovering == 0) {
        return false;
    }
    for (int i = 0; i < nCovering; i++) {
        FoldMapSetFolded(&s->folds, covering[i], false);
    }
    AppInvalidate(win);
    (void)app;
    return true;
}

void InputSetFoldCandidates(InputState* s, const FoldRange* ranges, int n) {
    if (!s || !LayoutModeIsFolding(s->mode)) {
        return;
    }
    FoldMapSetCandidates(&s->folds, ranges, n);
}

// ─── fold map (display_map/fold_map.rs) ───────────────────────────────────
//
// The projection that hides folded lines. Rust folds wrap rows; the rows here
// are logical lines, so this maps line <-> display row. See the header for
// why the two differ.

// The index of the range starting at `line`, or -1.
static int FoldFindAt(const Vec<FoldRange>& v, int line) {
    for (int i = 0; i < len(v); i++) {
        if (v[i].startLine == line) {
            return i;
        }
    }
    return -1;
}

static void FoldRemoveAt(Vec<FoldRange>* v, int ix) {
    for (int i = ix; i + 1 < v->len; i++) {
        (*v)[i] = (*v)[i + 1];
    }
    v->len--;
}

// Sorted by startLine. An insertion sort: a document's candidate list is
// short and arrives nearly sorted, since the scanner walks it in order.
static void FoldSort(Vec<FoldRange>* v) {
    for (int i = 1; i < v->len; i++) {
        FoldRange cur = (*v)[i];
        int j = i - 1;
        for (; j >= 0 && (*v)[j].startLine > cur.startLine; j--) {
            (*v)[j + 1] = (*v)[j];
        }
        (*v)[j + 1] = cur;
    }
}

// dedup_by_key(start_line): of ranges sharing a start line, the first wins.
// Rust's tree walk emits the outermost node first, so the first is the widest
// fold at that line, which is the one worth offering.
static void FoldDedup(Vec<FoldRange>* v) {
    int out = 0;
    for (int i = 0; i < v->len; i++) {
        if (out > 0 && (*v)[out - 1].startLine == (*v)[i].startLine) {
            continue;
        }
        (*v)[out++] = (*v)[i];
    }
    v->len = out;
}

void FoldMapSetCandidates(FoldMap* m, const FoldRange* ranges, int n) {
    if (!m) {
        return;
    }
    VecClear(m->candidates);
    for (int i = 0; i < n; i++) {
        if (ranges[i].startLine <= ranges[i].endLine) {
            VecAppend(m->candidates, ranges[i]);
        }
    }
    FoldSort(&m->candidates);
    FoldDedup(&m->candidates);
    // A fold whose candidate is gone has nothing left to describe it.
    for (int i = m->folded.len - 1; i >= 0; i--) {
        if (FoldFindAt(m->candidates, m->folded[i].startLine) < 0) {
            FoldRemoveAt(&m->folded, i);
            m->needsRebuild = true;
        }
    }
}

void FoldMapSetFolded(FoldMap* m, int startLine, bool folded) {
    if (!m) {
        return;
    }
    if (folded) {
        int ix = FoldFindAt(m->candidates, startLine);
        if (ix < 0 || FoldFindAt(m->folded, startLine) >= 0) {
            return;
        }
        VecAppend(m->folded, m->candidates[ix]);
        FoldSort(&m->folded);
        m->needsRebuild = true;
        return;
    }
    int ix = FoldFindAt(m->folded, startLine);
    if (ix >= 0) {
        FoldRemoveAt(&m->folded, ix);
        m->needsRebuild = true;
    }
}

void FoldMapToggle(FoldMap* m, int startLine) {
    FoldMapSetFolded(m, startLine, !FoldMapIsFolded(m, startLine));
}

bool FoldMapIsFolded(const FoldMap* m, int startLine) {
    return m && FoldFindAt(m->folded, startLine) >= 0;
}

bool FoldMapIsCandidate(const FoldMap* m, int startLine) {
    return m && FoldFindAt(m->candidates, startLine) >= 0;
}

void FoldMapClearFolds(FoldMap* m) {
    if (m && m->folded.len > 0) {
        VecClear(m->folded);
        m->needsRebuild = true;
    }
}

// A range that overlaps the edited lines describes text that is no longer
// there; one below the edit keeps its shape and only moves.
static void FoldShiftForEdit(Vec<FoldRange>* v, int editStartLine,
                             int editEndLine, int lineDelta) {
    for (int i = v->len - 1; i >= 0; i--) {
        const FoldRange& r = (*v)[i];
        if (r.startLine <= editEndLine && r.endLine >= editStartLine) {
            FoldRemoveAt(v, i);
        }
    }
    if (lineDelta == 0) {
        return;
    }
    for (int i = 0; i < v->len; i++) {
        FoldRange& r = (*v)[i];
        if (r.startLine > editEndLine) {
            r.startLine = r.startLine + lineDelta;
            r.endLine = r.endLine + lineDelta;
            if (r.startLine < 0) {
                r.startLine = 0;
            }
            if (r.endLine < 0) {
                r.endLine = 0;
            }
        }
    }
}

void FoldMapAdjustForEdit(FoldMap* m, int editStartLine, int editEndLine,
                          int lineDelta) {
    if (!m || (m->folded.len == 0 && m->candidates.len == 0)) {
        return;
    }
    FoldShiftForEdit(&m->folded, editStartLine, editEndLine, lineDelta);
    FoldShiftForEdit(&m->candidates, editStartLine, editEndLine, lineDelta);
    m->needsRebuild = true;
}

void FoldMapRebuild(FoldMap* m, int lineCount) {
    if (!m) {
        return;
    }
    if (lineCount < 0) {
        lineCount = 0;
    }
    if (!m->needsRebuild && lineCount == m->cachedLineCount) {
        return;
    }
    m->needsRebuild = false;
    // With nothing folded the projection is the identity and there are no
    // runs; every reader below answers from `cachedLineCount` in that case.
    if (m->folded.len == 0) {
        m->cachedLineCount = lineCount;
        VecClear(m->hidden);
        m->totalHidden = 0;
        return;
    }
    // Which lines a closed fold hides: the ones *between* its ends. Both the
    // line the fold starts on and the one it ends on stay on screen, so a
    // folded block reads as its opening line and its closing brace.
    int n = m->folded.len;
    auto* ranges =
        (Selection*)Alloc(GetTempArena(), n * (int)sizeof(Selection));
    if (!ranges) {
        return;
    }
    for (int i = 0; i < n; i++) {
        ranges[i] = Selection{m->folded[i].startLine + 1, m->folded[i].endLine};
    }
    FoldMapSetHiddenRows(m, lineCount, ranges, n);
}

// set_hidden_rows: install the projection for `lineCount` lines with
// `ranges` hidden, merging overlapping and adjacent ranges.
void FoldMapSetHiddenRows(FoldMap* m, int lineCount, Selection* ranges, int n) {
    // Sorted by start; the folds arrive that way already, so this is a walk.
    for (int i = 1; i < n; i++) {
        Selection r = ranges[i];
        int j = i;
        while (j > 0 && ranges[j - 1].start > r.start) {
            ranges[j] = ranges[j - 1];
            j--;
        }
        ranges[j] = r;
    }
    m->cachedLineCount = lineCount;
    VecClear(m->hidden);
    m->totalHidden = 0;
    for (int i = 0; i < n; i++) {
        int start = ranges[i].start < 0 ? 0 : ranges[i].start;
        int end = ranges[i].end < lineCount ? ranges[i].end : lineCount;
        if (end <= start) {
            continue;
        }
        if (m->hidden.len > 0) {
            FoldHiddenRows& last = m->hidden[m->hidden.len - 1];
            if (start <= last.end) {
                if (end > last.end) {
                    m->totalHidden += end - last.end;
                    last.end = end;
                }
                continue;
            }
        }
        FoldHiddenRows run;
        run.start = start;
        run.end = end;
        run.hiddenBefore = m->totalHidden;
        VecAppend(m->hidden, run);
        m->totalHidden += end - start;
    }
}

// partition_point(|run| run.end <= line): the first run that ends after it.
static int FoldRunAfter(const FoldMap* m, int line) {
    int lo = 0, hi = m->hidden.len;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (m->hidden[mid].end <= line) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// hidden_before: how many lines the runs before `index` hide.
static int FoldHiddenBefore(const FoldMap* m, int index) {
    return index < m->hidden.len ? m->hidden[index].hiddenBefore
                                 : m->totalHidden;
}

int FoldMapDisplayRowCount(const FoldMap* m) {
    if (!m || m->folded.len == 0) {
        return m ? m->cachedLineCount : 0;
    }
    return m->cachedLineCount - m->totalHidden;
}

int FoldMapDisplayRow(const FoldMap* m, int line) {
    if (!m || m->folded.len == 0) {
        return (m && line >= 0 && line < m->cachedLineCount) ? line : -1;
    }
    if (line < 0 || line >= m->cachedLineCount) {
        return -1;
    }
    int ix = FoldRunAfter(m, line);
    if (ix < m->hidden.len && m->hidden[ix].start <= line) {
        return -1;
    }
    return line - FoldHiddenBefore(m, ix);
}

int FoldMapLineAt(const FoldMap* m, int displayRow) {
    if (!m || m->folded.len == 0) {
        return (m && displayRow >= 0 && displayRow < m->cachedLineCount)
                   ? displayRow
                   : -1;
    }
    if (displayRow < 0 || displayRow >= FoldMapDisplayRowCount(m)) {
        return -1;
    }
    // partition_point(|run| run.display_start() <= display_row)
    int lo = 0, hi = m->hidden.len;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        const FoldHiddenRows& run = m->hidden[mid];
        if (run.start - run.hiddenBefore <= displayRow) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return displayRow + FoldHiddenBefore(m, lo);
}

bool FoldMapLineHidden(const FoldMap* m, int line) {
    return m && m->folded.len > 0 && FoldMapDisplayRow(m, line) < 0;
}

int FoldMapNearestVisibleLine(const FoldMap* m, int line) {
    if (!FoldMapLineHidden(m, line)) {
        return line;
    }
    if (line < 0) {
        return 0;
    }
    // Hidden means something above it is folded, so the line the fold starts
    // on is both visible and the row the hidden text now reads as: the last
    // visible line before the run, or before the end for a line past it.
    int at = line;
    if (at >= m->cachedLineCount) {
        at = m->cachedLineCount - 1;
        if (!FoldMapLineHidden(m, at)) {
            return at < 0 ? 0 : at;
        }
    }
    int ix = FoldRunAfter(m, at);
    int before = ix < m->hidden.len ? m->hidden[ix].start - 1 : at;
    return before < 0 ? 0 : before;
}

bool InputIsMultiLine(const InputState* s) {
    // kind.rs MULTI_LINE. The kind decides this, not the layout: an
    // auto-growing textarea capped at one row is still multi-line.
    return s->kind != InputKind::Input;
}

bool InputIsSingleLine(const InputState* s) {
    return !InputIsMultiLine(s);
}

// is_copyable: whether the selection may leave the field. A masked one may
// not — what it shows is not what it holds, and the clipboard would get what
// it holds.
bool InputIsCopyable(const InputState* s) {
    return s && !s->selectedRange.IsEmpty() && !s->masked;
}

bool InputIsEditable(const InputState* s) {
    return !s->disabled && !s->readonly;
}

// ─── cursor and selection ─────────────────────────────────────────────────

int InputCursor(const InputState* s) {
    return s->selectionReversed ? s->selectedRange.start : s->selectedRange.end;
}

// RopeExt::offset_to_position: the row, and the column counted in
// characters rather than bytes.
RopePoint InputCursorPosition(const InputState* s) {
    RopePoint p = InputOffsetToPoint(s, InputCursor(s));
    Str line = InputSliceLine(s, p.row);
    int n = p.column < len(line) ? p.column : len(line);
    int chars = 0;
    for (int i = 0; i < n; i++) {
        if (((uint8_t)line.s[i] & 0xC0) != 0x80) {
            chars++;
        }
    }
    p.column = chars;
    return p;
}

Str InputSelectedValue(const InputState* s) {
    Str t = InputValue(s);
    Selection r = s->selectedRange;
    if (r.IsEmpty() || r.start < 0 || r.end > len(t)) {
        return {};
    }
    return Str(t.s + r.start, r.end - r.start);
}

Str InputUnmaskValue(Arena* a, const InputState* s) {
    return MaskUnapply(a, s->maskPattern, InputValue(s));
}

static int InputCursorBoundary(const InputState* s, int offset, Bias bias) {
    Str t = InputValue(s);
    offset = RopeClipOffset(t, offset, bias);
    offset = InputTokenBoundary(s, offset, bias);
    if (offset > 0 && offset < len(t) && t.s[offset - 1] == '\r' &&
        t.s[offset] == '\n') {
        return bias == Bias::Left ? offset - 1 : offset + 1;
    }
    return offset;
}

int InputPreviousBoundary(const InputState* s, int offset) {
    return InputCursorBoundary(s, offset > 0 ? offset - 1 : 0, Bias::Left);
}

int InputNextBoundary(const InputState* s, int offset) {
    return InputCursorBoundary(s, offset + 1, Bias::Right);
}

// The visual row the caret is on, as a range of the logical line holding it:
// `display_map.line(row).wrapped_lines[wrap_point.local_row]`, the wrap
// point taken with the caret's affinity. False when the field does not wrap
// as a code editor, which is what leaves Home and End on the logical line.
static bool WrappedRowOfCaret(const InputState* s, Window* win, int line,
                              int rel, int* outLo, int* outHi) {
    // `soft_wrap && is_code_editor()`: a plain textarea keeps the logical
    // line even when it wraps, which is what Rust gates this on.
    if (s->kind != InputKind::Editor ||
        !WrapMapOf(s, win ? &win->paint : nullptr)) {
        return false;
    }
    const int* starts = nullptr;
    int nRows = InputWrapRows(s, line, &starts, nullptr);
    int k = WrapRowOfOffset(starts, nRows, rel, s->cursorLineEndAffinity);
    *outLo = starts[k];
    *outHi = k + 1 < nRows ? starts[k + 1] : len(InputSliceLine(s, line));
    return true;
}

int InputStartOfLine(const InputState* s, Window* win) {
    if (InputIsSingleLine(s)) {
        return 0;
    }
    Str t = InputValue(s);
    int cursor = InputCursor(s);
    int row = RopeOffsetToPoint(t, cursor).row;
    int start = RopeLineStartOffset(t, row);
    // The first press goes to the visual row's start; a second one, with the
    // caret already there, carries on to the logical line's.
    int lo = 0, hi = 0;
    if (WrappedRowOfCaret(s, win, row, cursor - start, &lo, &hi) &&
        cursor != start + lo) {
        return start + lo;
    }
    return start;
}

int InputEndOfLine(const InputState* s, Window* win) {
    Str t = InputValue(s);
    if (InputIsSingleLine(s)) {
        return len(t);
    }
    int cursor = InputCursor(s);
    int row = RopeOffsetToPoint(t, cursor).row;
    int start = RopeLineStartOffset(t, row);
    int lo = 0, hi = 0;
    if (WrappedRowOfCaret(s, win, row, cursor - start, &lo, &hi) &&
        cursor != start + hi) {
        return start + hi;
    }
    return RopeLineEndOffset(t, row);
}

// previous_start_of_word / next_end_of_word. Rust asks
// unicode-segmentation for the word bounds and takes the nearest one whose
// text is not all whitespace; the same answer falls out of walking the
// character classes text_boundary.rs already sorts characters into.
int InputPreviousStartOfWordAt(const InputState* s, int offset) {
    if (s->masked) {
        // Every character shows as the same bullet, so there are no word
        // boundaries on screen to move or delete by: the word is the whole
        // of it.
        return 0;
    }
    Str t = InputValue(s);
    int off = RopeClipOffset(t, offset, Bias::Left);
    while (off > 0) {
        int prev = Utf8Prev(t, off);
        uint32_t c = 0;
        Utf8At(t, prev, &c);
        CharKind k = CharKindOf(c);
        if (k != CharKind::Whitespace && k != CharKind::Newline) {
            break;
        }
        off = prev;
    }
    if (off <= 0) {
        return 0;
    }
    uint32_t first = 0;
    Utf8At(t, Utf8Prev(t, off), &first);
    CharKind kind = CharKindOf(first);
    while (off > 0) {
        int prev = Utf8Prev(t, off);
        uint32_t c = 0;
        Utf8At(t, prev, &c);
        if (CharKindOf(c) != kind) {
            break;
        }
        off = prev;
    }
    return InputTokenBoundary(s, off, Bias::Left);
}

int InputPreviousStartOfWord(const InputState* s) {
    return InputPreviousStartOfWordAt(s, s->selectedRange.start);
}

int InputNextEndOfWordAt(const InputState* s, int offset) {
    Str t = InputValue(s);
    if (s->masked) {
        // See InputPreviousStartOfWord.
        return len(t);
    }
    int off = RopeClipOffset(t, offset, Bias::Left);
    while (off < len(t)) {
        uint32_t c = 0;
        int n = Utf8At(t, off, &c);
        CharKind k = CharKindOf(c);
        if (k != CharKind::Whitespace && k != CharKind::Newline) {
            break;
        }
        off += n;
    }
    if (off >= len(t)) {
        return len(t);
    }
    uint32_t first = 0;
    Utf8At(t, off, &first);
    CharKind kind = CharKindOf(first);
    while (off < len(t)) {
        uint32_t c = 0;
        int n = Utf8At(t, off, &c);
        if (CharKindOf(c) != kind) {
            break;
        }
        off += n;
    }
    return InputTokenBoundary(s, off, Bias::Right);
}

int InputNextEndOfWord(const InputState* s) {
    return InputNextEndOfWordAt(s, InputCursor(s));
}

static void Notify(App* app, Window* win) {
    if (win) {
        AppInvalidate(win);
    }
    (void)app;
}

static void Emit(InputState* s, App* app, Window* win, InputEvent ev) {
    if (!s->onChange.IsValid() || !s->emitEvents) {
        return;
    }
    ListenerCall(app, win, s->onChange, &ev);
}

// pause_blink_cursor: solid while the user is doing something, so the caret
// never blinks out under their hands.
static void PauseBlink(InputState* s, App* app, Window* win) {
    if (win) {
        BlinkPause(app, win, &s->blink);
    }
}

// update_preferred_column. Rust remembers the measured x as well and falls
// back to the column; without a display map there is only the column.
static void UpdatePreferredColumn(InputState* s) {
    s->preferredColumn = RopeOffsetToPoint(InputValue(s), InputCursor(s))
                             .column;
    // The x a display-row walk aims at only survives the walk, so any other
    // move drops it. MoveVertical puts its own back afterwards.
    s->preferredX = -1;
}

// RIGHT_MARGIN: how much of the run stays visible past the caret when the
// field scrolls sideways to reach it.
static const float kInputRightMargin = 5.f;

// BOTTOM_MARGIN_ROWS: the default trailing space and the default
// cursor-surrounding clearance, in line-heights.
static const int kBottomMarginRows = 3;

// element.rs MIN_LINE_NUMBER_DIGITS / MAX_LINE_NUMBER_DIGITS /
// MAX_DISPLAYED_LINE_NUMBER.
const int kMinLineNumberDigits = 3;
const int kMaxLineNumberDigits = 7;
const int kMaxDisplayedLineNumber = 9999999;

int InputLineNumberLen(int totalLines) {
    int digits = 1;
    for (int n = totalLines < 1 ? 1 : totalLines; n >= 10; n /= 10) {
        digits++;
    }
    return digits < kMinLineNumberDigits   ? kMinLineNumberDigits
           : digits > kMaxLineNumberDigits ? kMaxLineNumberDigits
                                           : digits;
}

int InputDisplayedLineNumber(int number) {
    return number > kMaxDisplayedLineNumber ? kMaxDisplayedLineNumber : number;
}

float InputEmptyBottomHeight(bool isCodeEditor, int overrideRows,
                             float viewportH, float lineH) {
    if (!isCodeEditor) {
        return 0;
    }
    if (overrideRows >= 0) {
        return (float)overrideRows * lineH;
    }
    float half = viewportH * 0.5f;
    float floor = (float)kBottomMarginRows * lineH;
    return half > floor ? half : floor;
}

float InputCursorSurroundingPadding(bool isAutoGrow, int overrideLines,
                                    int visibleLines, float lineH) {
    if (isAutoGrow) {
        return lineH;
    }
    float raw;
    if (overrideLines >= 0) {
        raw = (float)overrideLines * lineH;
    } else if (visibleLines < kBottomMarginRows * 8) {
        raw = lineH;
    } else {
        raw = (float)kBottomMarginRows * lineH;
    }
    float half = (float)visibleLines * lineH * 0.5f;
    return raw < half ? raw : half;
}

void InputScrollToCaret(InputState* s, float caretX, float caretY,
                        InputMoveDir dir) {
    // scroll_to: a directed move keeps the surrounding lines, a plain reveal
    // keeps one line's clearance.
    InputScrollToCaretWithPadding(s, caretX, caretY, dir,
                                  dir != InputMoveDir::None
                                      ? InputScrollPadding::SurroundingLines
                                      : InputScrollPadding::Minimal);
}

void InputScrollToCaretWithPadding(InputState* s, float caretX, float caretY,
                                   InputMoveDir dir,
                                   InputScrollPadding padding) {
    if (!s) {
        return;
    }
    float wasY = s->scrollY;
    float lineH = s->lastLineH > 0 ? s->lastLineH : kInputLineH;

    // Sideways: the caret keeps a margin from either edge of the box. A
    // negative x is "leave it where it is" — see InputScrollToOffset.
    if (s->viewW > 0 && caretX >= 0) {
        if (caretX - kInputRightMargin < s->scrollX) {
            s->scrollX = caretX - kInputRightMargin;
        } else if (caretX + kInputRightMargin > s->scrollX + s->viewW) {
            s->scrollX = caretX + kInputRightMargin - s->viewW;
        }
        float mostX = s->contentW - s->viewW;
        if (mostX < 0) {
            mostX = 0;
        }
        if (s->scrollX > mostX) {
            s->scrollX = mostX;
        }
        if (s->scrollX < 0) {
            s->scrollX = 0;
        }
    }

    // Down the page: the caret's whole line has to be inside the box, with a
    // line's clearance at whichever edge it came in from. A code editor
    // walking with Up/Down uses cursor_surrounding_lines instead, the way
    // scroll_to and layout_cursor share one helper in Rust.
    if (s->viewH > 0) {
        bool surrounding = padding == InputScrollPadding::SurroundingLines &&
                           s->mode.kind == LayoutModeKind::CodeEditor;
        if (surrounding) {
            int visible = lineH > 0 ? (int)(s->viewH / lineH) : 0;
            float edge = InputCursorSurroundingPadding(
                false, s->cursorSurroundingLines, visible, lineH);
            if (caretY - edge + lineH < s->scrollY) {
                s->scrollY = caretY - edge + lineH;
            } else if (caretY + edge > s->scrollY + s->viewH) {
                s->scrollY = caretY + edge - s->viewH;
            }
        } else if (caretY - lineH < s->scrollY) {
            s->scrollY = caretY - lineH;
        } else if (caretY + lineH + lineH > s->scrollY + s->viewH) {
            s->scrollY = caretY + lineH + lineH - s->viewH;
        }
        // A move that went up is never answered by scrolling down.
        if ((dir == InputMoveDir::Up && s->scrollY > wasY) ||
            (dir == InputMoveDir::Down && s->scrollY < wasY)) {
            s->scrollY = wasY;
        }
        float mostY = s->contentH - s->viewH;
        if (mostY < 0) {
            mostY = 0;
        }
        if (s->scrollY > mostY) {
            s->scrollY = mostY;
        }
        if (s->scrollY < 0) {
            s->scrollY = 0;
        }
    }
}

void InputScrollToCursor(InputState* s, InputMoveDir dir) {
    if (!s) {
        return;
    }
    float lineH = s->lastLineH > 0 ? s->lastLineH : kInputLineH;
    int row = RopeOffsetToPoint(InputValue(s), InputCursor(s)).row;
    // A caret inside a closed fold reads as the fold's own line, which is
    // where the frame draws it.
    row = FoldMapNearestVisibleLine(&s->folds, row);
    // Where that row actually starts: a wrapping editor's rows are uneven, so
    // the arithmetic only holds when nothing wrapped.
    float caretY = DisplayRowDocY(s, row, lineH);
    InputScrollToCaret(s, s->caretX, caretY, dir);
}

// blink_cursor.rs CURSOR_WIDTH: a whole pixel off the Mac, so the caret is
// never blurred.
#if GPUI_OS_MAC
static const float kInputCursorWidth = 1.5f;
#else
static const float kInputCursorWidth = 2.f;
#endif

bool InputUpdateScrollOffset(InputState* s, App* app, Window* win,
                             const Point* offset) {
    (void)app;
    if (!s) {
        return false;
    }
    // Rust keeps the offset negative-down on its ScrollHandle; the state here
    // keeps it positive, so each range is Rust's negated.
    Point want = offset ? *offset : Point{s->scrollX, s->scrollY};
    // A right- or centre-aligned run keeps a caret's width spare on the
    // right, which the left edge does not need.
    float safeX = s->align == 0 ? 0.f : kInputCursorWidth;
    float mostY = s->contentH - s->viewH;
    float mostX = s->contentW - s->viewW + safeX;
    if (mostY < 0) {
        mostY = 0;
    }
    if (mostX < safeX) {
        mostX = safeX;
    }
    float y = 0;
    if (!InputIsSingleLine(s)) {
        y = want.y < 0 ? 0 : (want.y > mostY ? mostY : want.y);
    }
    float x = want.x < 0 ? 0 : (want.x > mostX ? mostX : want.x);
    if (x == s->scrollX && y == s->scrollY) {
        return false;
    }
    s->scrollX = x;
    s->scrollY = y;
    Notify(app, win);
    return true;
}

bool InputOnScrollWheel(InputState* s, App* app, Window* win, float dx,
                        float dy, TouchPhase phase) {
    if (!s) {
        return false;
    }
    float oldX = s->scrollX;
    float oldY = s->scrollY;
    Point want = {oldX - dx, oldY - dy};
    InputUpdateScrollOffset(s, app, win, &want);
    bool moved = s->scrollX != oldX || s->scrollY != oldY;
    // diagnostic_popover.take(): a popover over text that may have moved is
    // put away, and that is a change even when the clamp kept the offset.
    if (s->hoverDiagnostic >= 0) {
        s->hoverDiagnostic = -1;
        Notify(app, win);
    }
    // The handles follow the text; the menu would sit over whatever scrolls
    // underneath it, so it steps aside until the finger lifts.
    InputEditMenuOnScroll(s, app, win, phase);
    return moved;
}

void InputScrollToOffset(InputState* s, int offset, InputMoveDir dir) {
    InputScrollToOffsetWithPadding(s, offset, dir,
                                   dir != InputMoveDir::None
                                       ? InputScrollPadding::SurroundingLines
                                       : InputScrollPadding::Minimal);
}

void InputScrollToOffsetWithPadding(InputState* s, int offset, InputMoveDir dir,
                                    InputScrollPadding padding) {
    if (!s) {
        return;
    }
    float lineH = s->lastLineH > 0 ? s->lastLineH : kInputLineH;
    int row = RopeOffsetToPoint(InputValue(s), offset).row;
    row = FoldMapNearestVisibleLine(&s->folds, row);
    float y = DisplayRowDocY(s, row, lineH);
    InputScrollToCaretWithPadding(s, -1, y, dir, padding);
}

static void InputScrollToSearchOffset(InputState* s, Window* win, int offset) {
    if (!s) {
        return;
    }
    float lineH = s->lastLineH > 0 ? s->lastLineH : kInputLineH;
    int row = RopeOffsetToPoint(InputValue(s), offset).row;
    row = FoldMapNearestVisibleLine(&s->folds, row);
    float y = DisplayRowDocY(s, row, lineH);
    if (WrapMapOf(s, win ? &win->paint : nullptr)) {
        // The display row the offset is on: line_and_position_for_offset
        // answers whole rows.
        int lineStart = RopeLineStartOffset(InputValue(s), row);
        const int* starts = nullptr;
        int nRows = InputWrapRows(s, row, &starts, nullptr);
        int k = WrapRowOfOffset(starts, nRows, std::max(0, offset - lineStart),
                                false);
        y += (float)k * lineH;
    }
    InputScrollToCaretWithPadding(s, -1, y, InputMoveDir::None,
                                  InputScrollPadding::SurroundingLines);
}

void InputMoveToWithAffinity(InputState* s, App* app, Window* win, int offset,
                             bool lineEndAffinity) {
    UndoBreakCoalescing(&s->undo);
    offset = InputCursorBoundary(s, offset, Bias::Left);
    s->cursorLineEndAffinity = lineEndAffinity;
    InputRemoveExtraCursors(s);
    s->selectedRange = SelectionAt(offset);
    s->hasSelectedWordRange = false;
    PauseBlink(s, app, win);
    UpdatePreferredColumn(s);
    // scroll_to: the caret takes the view with it.
    InputScrollToCursor(s, InputMoveDir::None);
    Notify(app, win);
}

void InputMoveTo(InputState* s, App* app, Window* win, int offset) {
    InputMoveToWithAffinity(s, app, win, offset, false);
}

void InputSelectToWithAffinity(InputState* s, App* app, Window* win, int offset,
                               bool lineEndAffinity) {
    offset = InputCursorBoundary(s, offset, Bias::Left);
    s->cursorLineEndAffinity = lineEndAffinity;
    if (s->selectionReversed) {
        s->selectedRange.start = offset;
    } else {
        s->selectedRange.end = offset;
    }
    if (s->selectedRange.end < s->selectedRange.start) {
        s->selectionReversed = !s->selectionReversed;
        Selection flipped = {s->selectedRange.end, s->selectedRange.start};
        s->selectedRange = flipped;
    }
    // A double click's word stays whole: dragging out of it may only grow the
    // selection, never eat back into the word it started from.
    if (s->hasSelectedWordRange) {
        if (s->selectedRange.start > s->selectedWordRange.start) {
            s->selectedRange.start = s->selectedWordRange.start;
        }
        if (s->selectedRange.end < s->selectedWordRange.end) {
            s->selectedRange.end = s->selectedWordRange.end;
        }
    }
    if (s->selectedRange.IsEmpty()) {
        UpdatePreferredColumn(s);
    }
    Notify(app, win);
}

void InputSelectTo(InputState* s, App* app, Window* win, int offset) {
    InputSelectToWithAffinity(s, app, win, offset, false);
}

void InputSelectAll(InputState* s, App* app, Window* win) {
    UndoBreakCoalescing(&s->undo);
    InputRemoveExtraCursors(s);
    s->cursorLineEndAffinity = false;
    s->selectedRange = Selection{0, len(InputValue(s))};
    s->selectionReversed = false;
    s->hasSelectedWordRange = false;
    Notify(app, win);
}

void InputUnselect(InputState* s, App* app, Window* win) {
    UndoBreakCoalescing(&s->undo);
    int offset = InputCursor(s);
    s->cursorLineEndAffinity = false;
    s->selectedRange = SelectionAt(offset);
    s->hasSelectedWordRange = false;
    Notify(app, win);
}

void InputSetSelectedRange(InputState* s, App* app, Window* win, int a, int b) {
    Str t = InputValue(s);
    s->cursorLineEndAffinity = false;
    InputNormalizeTokenRange(s, &a, &b);
    // A non-empty range grows out to character boundaries; an empty one stays
    // empty and clips to the boundary before it.
    Bias endBias = a == b ? Bias::Left : Bias::Right;
    int start = RopeClipOffset(t, a, Bias::Left);
    int end = RopeClipOffset(t, b, endBias);
    InputMoveTo(s, app, win, start);
    s->selectionReversed = false;
    s->hasSelectedWordRange = false;
    InputSelectTo(s, app, win, end);
}

// selection.rs: what a double and a triple click take.
void InputSelectWord(InputState* s, App* app, Window* win, int offset) {
    int a = 0;
    int b = 0;
    // A masked value renders as one unbroken run of mask characters, so it
    // has no word boundaries to select by: take all of it, rather than let
    // the selection highlight reveal where the words are.
    if (s->masked) {
        b = len(InputValue(s));
    } else if (!TextWordRangeAt(InputValue(s), offset, &a, &b)) {
        return;
    }
    UndoBreakCoalescing(&s->undo);
    InputRemoveExtraCursors(s);
    s->cursorLineEndAffinity = false;
    s->selectedRange = Selection{a, b};
    s->selectionReversed = false;
    s->selectedWordRange = s->selectedRange;
    s->hasSelectedWordRange = true;
    Notify(app, win);
}

void InputSelectLine(InputState* s, App* app, Window* win, int offset) {
    int a = 0;
    int b = 0;
    TextLineRangeAt(InputValue(s), offset, &a, &b);
    UndoBreakCoalescing(&s->undo);
    InputRemoveExtraCursors(s);
    s->cursorLineEndAffinity = false;
    s->selectedRange = Selection{a, b};
    s->selectionReversed = false;
    s->hasSelectedWordRange = false;
    Notify(app, win);
}

// ─── multiple cursors ─────────────────────────────────────────────────────
//
// selections.rs and cursor.rs Selections. The active cursor is the field's
// `selectedRange` and its neighbours; the rest are `extraCursors`. Anything
// that reads "the" cursor reads the active one, so a per-cursor evaluation
// stands each of the others in for it in turn (WithCursor) — Rust's `*_at`
// variants of the boundary helpers are the same code with the offset passed.

int InputCursorCount(const InputState* s) {
    return 1 + s->extraCursors.len;
}

void InputRemoveExtraCursors(InputState* s) {
    s->extraCursors.len = 0;
}

static CursorSelection ActiveCursor(const InputState* s) {
    CursorSelection c;
    c.range = s->selectedRange;
    c.reversed = s->selectionReversed;
    c.preferredColumn = s->preferredColumn;
    c.preferredX = s->preferredX;
    return c;
}

static void SetActiveCursor(InputState* s, const CursorSelection& c) {
    s->selectedRange = c.range;
    s->selectionReversed = c.reversed;
    s->preferredColumn = c.preferredColumn;
    s->preferredX = c.preferredX;
}

// Every cursor, the active one first, in the temp arena.
static CursorSelection* AllCursors(Arena* a, const InputState* s, int* n) {
    *n = InputCursorCount(s);
    auto* out = (CursorSelection*)Alloc(a, *n * (int)sizeof(CursorSelection));
    out[0] = ActiveCursor(s);
    for (int i = 0; i < s->extraCursors.len; i++) {
        out[i + 1] = s->extraCursors[i];
    }
    return out;
}

// replace_all: `sels[0]` becomes the active cursor.
static void SetAllCursors(InputState* s, const CursorSelection* sels, int n) {
    if (n <= 0) {
        return;
    }
    SetActiveCursor(s, sels[0]);
    s->extraCursors.len = 0;
    for (int i = 1; i < n; i++) {
        VecAppend(s->extraCursors, sels[i]);
    }
}

// Evaluate `f` with `c` standing in as the active cursor. Only the active
// cursor carries a line-end affinity (line_end_affinity_for).
template <class F>
static auto WithCursor(InputState* s, const CursorSelection& c, bool active,
                       F f) {
    CursorSelection saved = ActiveCursor(s);
    bool affinity = s->cursorLineEndAffinity;
    SetActiveCursor(s, c);
    if (!active) {
        s->cursorLineEndAffinity = false;
    }
    auto r = f();
    SetActiveCursor(s, saved);
    s->cursorLineEndAffinity = affinity;
    return r;
}

static bool HasCursorAt(const InputState* s, int offset) {
    if (InputCursor(s) == offset) {
        return true;
    }
    for (int i = 0; i < s->extraCursors.len; i++) {
        if (s->extraCursors[i].Cursor() == offset) {
            return true;
        }
    }
    return false;
}

void InputMergeOverlappingCursors(InputState* s) {
    if (s->extraCursors.len == 0) {
        return;
    }
    Arena* a = GetTempArena();
    int n = 0;
    CursorSelection* all = AllCursors(a, s, &n);
    // In start order; a stable sort keeps the active one ahead of a cursor
    // that starts where it does, the way Rust's sort_by_key does.
    auto* order = (int*)Alloc(a, n * (int)sizeof(int));
    for (int i = 0; i < n; i++) {
        int j = i;
        while (j > 0 && all[order[j - 1]].range.start > all[i].range.start) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
    auto* merged = (CursorSelection*)Alloc(a, n * (int)sizeof(CursorSelection));
    int m = 0;
    int activeAt = -1;
    for (int k = 0; k < n; k++) {
        const CursorSelection& c = all[order[k]];
        bool isActive = order[k] == 0;
        if (m > 0 && c.range.start <= merged[m - 1].range.end) {
            // Overlapping or adjacent: extend the last one.
            CursorSelection& last = merged[m - 1];
            bool didMerge = c.range.start != last.range.start ||
                            c.range.end != last.range.end;
            if (c.range.end > last.range.end) {
                last.range.end = c.range.end;
            }
            if (isActive) {
                activeAt = m - 1;
                last.reversed = c.reversed;
            }
            // Reset the column anchor on a real merge.
            if (didMerge) {
                last.preferredColumn = -1;
                last.preferredX = -1;
            }
            continue;
        }
        if (isActive) {
            activeAt = m;
        }
        merged[m++] = c;
    }
    // Re-front the active selection so it stays at index 0.
    if (activeAt > 0) {
        CursorSelection t = merged[0];
        merged[0] = merged[activeAt];
        merged[activeAt] = t;
    }
    SetAllCursors(s, merged, m);
}

void InputAddCursorAt(InputState* s, App* app, Window* win, int offset) {
    if (!InputIsMultiLine(s)) {
        return;
    }
    Arena* a = GetTempArena();
    int n = 0;
    CursorSelection* all = AllCursors(a, s, &n);
    for (int i = 0; i < n; i++) {
        if (all[i].range.Contains(offset)) {
            return;
        }
        if (all[i].IsEmpty() && all[i].Cursor() == offset) {
            return;
        }
    }
    UndoBreakCoalescing(&s->undo);
    CursorSelection c;
    c.range = SelectionAt(offset);
    VecAppend(s->extraCursors, c);
    Notify(app, win);
}

// The soft-wrapped rows of one line as byte offsets into it — the
// `wrapped_lines` of text_wrapper.rs, where row k is [starts[k], starts[k+1])
// and the last row runs to the end of the line.
static int WrappedRowStarts(const InputState* s, int line, const int** out) {
    return InputWrapRows(s, line, out, nullptr);
}

// offset_to_wrap_display_point, without affinity: the row of the line the
// offset falls in and its byte column within that row. An offset on a soft
// wrap boundary opens the next row.
struct WrapPoint {
    int line = 0;
    int row = 0;
    int column = 0;
};

static void WrapPointAt(const InputState* s, Str t, int offset,
                        WrapPoint* out) {
    RopePoint p = RopeOffsetToPoint(t, offset);
    const int* starts = nullptr;
    int rows = WrappedRowStarts(s, p.row, &starts);
    int local = offset - RopeLineStartOffset(t, p.row);
    int k = WrapRowOfOffset(starts, rows, local, false);
    out->line = p.row;
    out->row = k;
    out->column = local - starts[k];
}

// The paint context a display-row walk measures against, or null when the
// field is not soft-wrapping.
static PaintCtx* DisplayCtx(const InputState* s, Window* win) {
    if (!win || !WrapMapOf(s, &win->paint)) {
        return nullptr;
    }
    return &win->paint;
}

// build_columnar_selection over wrap display rows: one selection per visual
// row between the two offsets, at the same byte columns within each row,
// clipped to a short row (display_row_column_to_offset). Rows a closed fold
// hides are skipped. False when the run could not be measured.
static bool ColumnarRowsDisplay(const InputState* s, PaintCtx* ctx, Str t,
                                InputState::ColumnarPoint start,
                                InputState::ColumnarPoint end, Arena* a,
                                CursorSelection** outSels, int* outN) {
    if (start.offset > end.offset) {
        InputState::ColumnarPoint swap = start;
        start = end;
        end = swap;
    }
    WrapPoint ps, pe;
    (void)ctx;
    WrapPointAt(s, t, start.offset, &ps);
    WrapPointAt(s, t, end.offset, &pe);
    ps.column += start.columnsPastLineEnd;
    pe.column += end.columnsPastLineEnd;
    int col0 = ps.column <= pe.column ? ps.column : pe.column;
    int col1 = ps.column <= pe.column ? pe.column : ps.column;
    bool folding = LayoutModeIsFolding(s->mode);
    // The rows of every line spanned, measured once.
    int nLines = pe.line - ps.line + 1;
    auto* lineStarts = (const int**)Alloc(a, nLines * (int)sizeof(int*));
    int* lineRows = (int*)Alloc(a, nLines * (int)sizeof(int));
    int cap = 0;
    for (int i = 0; i < nLines; i++) {
        lineStarts[i] = nullptr;
        lineRows[i] = 0;
        if (folding && FoldMapLineHidden(&s->folds, ps.line + i)) {
            continue;
        }
        lineRows[i] = WrappedRowStarts(s, ps.line + i, &lineStarts[i]);
        if (lineRows[i] == 0) {
            return false;
        }
        cap += lineRows[i];
    }
    auto* sels = (CursorSelection*)Alloc(a, cap * (int)sizeof(CursorSelection));
    int m = 0;
    for (int i = 0; i < nLines; i++) {
        int rows = lineRows[i];
        if (rows == 0) {
            continue;
        }
        int line = ps.line + i;
        Str text = RopeSliceLine(t, line);
        int lineStart = RopeLineStartOffset(t, line);
        const int* starts = lineStarts[i];
        int kFrom = line == ps.line ? ps.row : 0;
        int kTo = line == pe.line ? pe.row : rows - 1;
        if (kTo > rows - 1) {
            kTo = rows - 1;
        }
        for (int k = kFrom; k <= kTo; k++) {
            int rs = starts[k];
            int re = k + 1 < rows ? starts[k + 1] : len(text);
            int a0 = lineStart + (rs + col0 < re ? rs + col0 : re);
            int a1 = lineStart + (rs + col1 < re ? rs + col1 : re);
            CursorSelection c;
            c.range = Selection{RopeClipOffset(t, a0, Bias::Left),
                                RopeClipOffset(t, a1, Bias::Left)};
            sels[m++] = c;
        }
    }
    *outSels = sels;
    *outN = m;
    return true;
}

void InputBuildColumnarSelection(InputState* s, App* app, Window* win,
                                 InputState::ColumnarPoint start,
                                 InputState::ColumnarPoint end) {
    if (!InputIsMultiLine(s)) {
        return;
    }
    UndoBreakCoalescing(&s->undo);
    Str t = InputValue(s);
    if (start.offset > end.offset) {
        InputState::ColumnarPoint swap = start;
        start = end;
        end = swap;
    }
    if (start.offset < 0) {
        start.offset = 0;
    }
    if (end.offset > len(t)) {
        end.offset = len(t);
    }
    Arena* a = GetTempArena();
    CursorSelection* sels = nullptr;
    int m = 0;
    PaintCtx* ctx = DisplayCtx(s, win);
    if (!ctx || !ColumnarRowsDisplay(s, ctx, t, start, end, a, &sels, &m)) {
        // Nothing laid out to measure against: document rows, which are the
        // wrap rows of a field that does not wrap.
        RopePoint ps = RopeOffsetToPoint(t, start.offset);
        RopePoint pe = RopeOffsetToPoint(t, end.offset);
        ps.column += start.columnsPastLineEnd;
        pe.column += end.columnsPastLineEnd;
        int col0 = ps.column <= pe.column ? ps.column : pe.column;
        int col1 = ps.column <= pe.column ? pe.column : ps.column;
        bool folding = LayoutModeIsFolding(s->mode);
        int rows = pe.row - ps.row + 1;
        sels = (CursorSelection*)Alloc(a, rows * (int)sizeof(CursorSelection));
        m = 0;
        for (int row = ps.row; row <= pe.row; row++) {
            if (folding && FoldMapLineHidden(&s->folds, row)) {
                continue;
            }
            int lineStart = RopeLineStartOffset(t, row);
            int lineLen = RopeLineLen(t, row);
            int a0 = lineStart + (col0 < lineLen ? col0 : lineLen);
            int a1 = lineStart + (col1 < lineLen ? col1 : lineLen);
            CursorSelection c;
            c.range = Selection{RopeClipOffset(t, a0, Bias::Left),
                                RopeClipOffset(t, a1, Bias::Left)};
            sels[m++] = c;
        }
    }
    if (m == 0) {
        sels = (CursorSelection*)Alloc(a, (int)sizeof(CursorSelection));
        CursorSelection c;
        c.range = SelectionAt(end.offset);
        sels[m++] = c;
    }
    SetAllCursors(s, sels, m);
    s->cursorLineEndAffinity = false;
    s->hasSelectedWordRange = false;
    Notify(app, win);
}

// selected_texts: what every non-empty selection holds, in document order,
// joined with newlines — one clipboard line per cursor.
static Str SelectedTexts(Arena* a, const InputState* s) {
    if (s->extraCursors.len == 0) {
        return InputSelectedValue(s);
    }
    Str t = InputValue(s);
    int n = 0;
    CursorSelection* all = AllCursors(a, s, &n);
    auto* order = (int*)Alloc(a, n * (int)sizeof(int));
    for (int i = 0; i < n; i++) {
        int j = i;
        while (j > 0 && all[order[j - 1]].range.start > all[i].range.start) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
    int total = 0;
    int parts = 0;
    for (int k = 0; k < n; k++) {
        const CursorSelection& c = all[order[k]];
        if (!c.IsEmpty()) {
            total += c.range.Len();
            parts++;
        }
    }
    if (parts == 0) {
        return {};
    }
    total += parts - 1;
    char* buf = (char*)Alloc(a, total + 1);
    int w = 0;
    for (int k = 0; k < n; k++) {
        const CursorSelection& c = all[order[k]];
        if (c.IsEmpty()) {
            continue;
        }
        if (w > 0) {
            buf[w++] = '\n';
        }
        memcpy(buf + w, t.s + c.range.start, (size_t)c.range.Len());
        w += c.range.Len();
    }
    buf[w] = 0;
    return Str(buf, w);
}

// ─── the edit path ────────────────────────────────────────────────────────

static bool ReplaceAtEveryCursor(InputState* s, App* app, Window* win,
                                 Str newText, bool hasIntent,
                                 EditIntent requested);

// is_valid_input: the validator, the mask, and (in Rust) a regex we have no
// engine for.
static bool IsValidInput(const InputState* s, Str text) {
    if (len(text) == 0) {
        return true;
    }
    if (s->validate && !s->validate(text, s->validateArg)) {
        return false;
    }
    return MaskIsValid(s->maskPattern, text);
}

// normalize_input: a number mask folds full-width digits to ASCII, and a
// single-line field never takes a newline.
static Str NormalizeInput(Arena* a, const InputState* s, Str newText) {
    Str out = s->maskPattern.kind == MaskKind::Number
                  ? NormalizeNumberInput(a, newText)
                  : newText;
    if (!InputIsSingleLine(s)) {
        return out;
    }
    bool hasBreak = false;
    for (int i = 0; i < len(out) && !hasBreak; i++) {
        hasBreak = out.s[i] == '\n' || out.s[i] == '\r';
    }
    if (!hasBreak) {
        return out;
    }
    char* buf = (char*)Alloc(a, len(out) + 1);
    int n = 0;
    for (int i = 0; i < len(out); i++) {
        if (out.s[i] != '\n' && out.s[i] != '\r') {
            buf[n++] = out.s[i];
        }
    }
    buf[n] = 0;
    return Str(buf, n);
}

// push_history. `oldAll` is the whole document as it was before the splice;
// `range` indexes into it.
static TokenDelta* EditTokens(InputState* s, int start, int end, int newLen) {
    InlineTokenStore* store = s->tokens;
    if (!store || store->replaying) {
        return nullptr;
    }
    InlineTokenSpan inserted = {};
    int n = 0;
    if (store->hasPending) {
        inserted.start = 0;
        inserted.end = newLen;
        inserted.token = store->pending;
        store->hasPending = false;
        store->pending = {};
        n = 1;
    }
    if (n == 0 && store->spans.len == 0) {
        return nullptr;
    }
    TokenDelta* delta = TokenStoreReplace(&store->spans, start, end, newLen,
                                          n ? &inserted : nullptr, n);
    if (n) {
        InlineTokenFree(&inserted.token);
    }
    return delta;
}

static void ReplayTokens(InputState* s, int start, int end, int newLen,
                         const TokenDelta* delta, bool undo) {
    const InlineTokenSpan* inserted = nullptr;
    int n = 0;
    if (delta) {
        if (undo) {
            inserted = delta->removed.els;
            n = delta->removed.len;
        } else {
            inserted = delta->inserted.els;
            n = delta->inserted.len;
        }
    }
    if (n > 0) {
        InputTokenStore(s, true);
    }
    if (!s->tokens) {
        return;
    }
    TokenDelta* scratch =
        TokenStoreReplace(&s->tokens->spans, start, end, newLen, inserted, n);
    TokenDeltaFree(scratch);
}

static void PushHistory(InputState* s, Str oldAll, Selection range, Str newText,
                        bool hasIntent, EditIntent requested,
                        Selection selBefore, const Selection* selAfter) {
    TokenDelta* delta = EditTokens(s, range.start, range.end, len(newText));
    if (UndoIsIgnoring(&s->undo)) {
        TokenDeltaFree(delta);
        return;
    }
    Selection r = {RopeClipOffset(oldAll, range.start, Bias::Left),
                   RopeClipOffset(oldAll, range.end, Bias::Right)};
    if (r.end < r.start) {
        r.end = r.start;
    }
    Str oldText = Str(oldAll.s + r.start, r.end - r.start);
    Selection newRange = {r.start, r.start + len(newText)};

    EditIntent intent = requested;
    if (!hasIntent) {
        bool typed = r.IsEmpty() && len(oldText) == 0 && len(newText) > 0;
        for (int i = 0; typed && i < len(newText); i++) {
            typed = newText.s[i] != '\n' && newText.s[i] != '\r';
        }
        intent = typed ? EditIntent::Typing : EditIntent::Atomic;
    }
    // A delete's "before" is where the caret stood, which is the far end of
    // what it removed; that is what an undo has to put back.
    Selection before = selBefore;
    if (intent == EditIntent::Backspace) {
        before = SelectionAt(r.end);
    } else if (intent == EditIntent::DeleteForward) {
        before = SelectionAt(r.start);
    }

    Change c = {};
    c.oldRange = r;
    c.oldText = StrDup(oldText);
    c.newRange = newRange;
    c.newText = StrDup(newText);
    c.selBefore = before;
    c.selAfter = selAfter ? *selAfter : SelectionAt(newRange.end);
    c.tokenDelta = delta;
    UndoRecordTransaction(&s->undo, c, intent);
}

static AutoClosingPair InputClosingPairAt(const LanguageConfig& config,
                                          int index) {
    if (config.hasAutoClosingPairs) {
        return config.autoClosingPairs[index];
    }
    const BracketPair& pair = config.brackets[index];
    return AutoClosingPair::New(pair.open, pair.close);
}

static int InputClosingPairCount(const LanguageConfig& config) {
    return config.hasAutoClosingPairs ? config.nAutoClosingPairs
                                      : config.nBrackets;
}

static bool InputSliceEq(Str text, int at, Str part) {
    return at >= 0 && len(part) >= 0 && at + len(part) <= len(text) &&
           (len(part) == 0 ||
            memcmp(text.s + at, part.s, (size_t)len(part)) == 0);
}

static bool InputEscapedAt(Str text, int at) {
    int slashes = 0;
    while (at > 0 && text.s[at - 1] == '\\') {
        slashes++;
        at--;
    }
    return (slashes & 1) != 0;
}

static bool InputPairBlocked(const AutoClosingPair& pair,
                             SyntaxContext context) {
    for (int i = 0; i < pair.nNotIn; i++) {
        if (pair.notIn[i] == context) {
            return true;
        }
    }
    return false;
}

static bool InputOneCodepoint(Str text) {
    uint32_t c = 0;
    return len(text) > 0 && Utf8At(text, 0, &c) == len(text);
}

static bool InputAutoCloseBefore(const LanguageConfig& config, Str all,
                                 int at) {
    if (at >= len(all)) {
        return true;
    }
    uint32_t c = 0;
    int n = Utf8At(all, at, &c);
    if (n <= 0 || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        return true;
    }
    return InputSliceEq(config.autoCloseBefore, 0, Str(all.s + at, n)) ||
           [&]() {
               for (int i = 0; i + n <= config.autoCloseBefore.len; i++) {
                   if (memcmp(config.autoCloseBefore.s + i, all.s + at,
                              (size_t)n) == 0) {
                       return true;
                   }
               }
               return false;
           }();
}

static SyntaxContext InputEditingContext(InputState* s, App* app, int at) {
    return InputSyntaxContextAt(s, app, InputValue(s), at);
}

static bool AutoClosedContains(const Vec<AutoClosedPairRange>& pairs,
                               int openStart, int openEnd, int closeStart,
                               int closeEnd) {
    for (int i = 0; i < pairs.len; i++) {
        const AutoClosedPairRange& p = pairs[i];
        if (p.openStart == openStart && p.openEnd == openEnd &&
            p.closeStart == closeStart && p.closeEnd == closeEnd) {
            return true;
        }
    }
    return false;
}

static bool AutoClosedContainsCloser(const Vec<AutoClosedPairRange>& pairs,
                                     int cursor, int index, int closeLen) {
    for (int i = 0; i < pairs.len; i++) {
        const AutoClosedPairRange& p = pairs[i];
        if (p.closeStart + index == cursor &&
            p.closeEnd - p.closeStart == closeLen) {
            return true;
        }
    }
    return false;
}

static void AutoClosedAdjust(Vec<AutoClosedPairRange>& pairs, int editStart,
                             int editEnd, int newLen) {
    int delta = newLen - (editEnd - editStart);
    int keep = 0;
    for (int i = 0; i < pairs.len; i++) {
        AutoClosedPairRange p = pairs[i];
        if (editEnd <= p.openStart) {
            p.openStart += delta;
            p.openEnd += delta;
            p.closeStart += delta;
            p.closeEnd += delta;
            pairs[keep++] = p;
        } else if (editStart >= p.openEnd && editEnd <= p.closeStart) {
            p.closeStart += delta;
            p.closeEnd += delta;
            pairs[keep++] = p;
        } else if (editStart < p.closeEnd) {
            continue;
        } else {
            pairs[keep++] = p;
        }
    }
    pairs.len = keep;
}

static AutoClosedPairRange* AutoClosedDup(const Vec<AutoClosedPairRange>& pairs,
                                          int* n) {
    *n = pairs.len;
    if (pairs.len <= 0) {
        return nullptr;
    }
    auto* out = (AutoClosedPairRange*)malloc((size_t)pairs.len *
                                             sizeof(AutoClosedPairRange));
    if (!out) {
        *n = 0;
        return nullptr;
    }
    memcpy(out, pairs.els, (size_t)pairs.len * sizeof(AutoClosedPairRange));
    return out;
}

static void AutoClosedRestore(Vec<AutoClosedPairRange>& pairs,
                              const AutoClosedPairRange* src, int n) {
    VecClear(pairs);
    for (int i = 0; i < n; i++) {
        VecAppend(pairs, src[i]);
    }
}

static void UndoRecordAutoClosedPairs(UndoManager* m,
                                      const AutoClosedPairRange* before,
                                      int nBefore,
                                      const AutoClosedPairRange* after,
                                      int nAfter);

static bool InputTrySkipCloser(InputState* s, App* app, Window* win, Str typed,
                               const LanguageConfig& config) {
    if (!InputOneCodepoint(typed) || !s->selectedRange.IsEmpty()) {
        return false;
    }
    Str all = InputValue(s);
    int cursor = InputCursor(s);
    int count = InputClosingPairCount(config);
    for (int p = 0; p < count; p++) {
        AutoClosingPair pair = InputClosingPairAt(config, p);
        if (!pair.open || !pair.close) {
            continue;
        }
        for (int i = 0; i < pair.close.len; i++) {
            if (!InputSliceEq(pair.close, i, typed) ||
                !InputSliceEq(all, cursor - i, Str(pair.close.s, i)) ||
                !InputSliceEq(all, cursor,
                              Str(pair.close.s + i, pair.close.len - i)) ||
                InputEscapedAt(all, cursor - i)) {
                continue;
            }
            SyntaxContext context = InputEditingContext(s, app, cursor);
            bool generated = AutoClosedContainsCloser(s->autoClosed, cursor, i,
                                                      pair.close.len);
            if (!generated && InputPairBlocked(pair, context) &&
                !(context == SyntaxContext::String &&
                  StrEq(pair.open, pair.close))) {
                continue;
            }
            s->selectedRange = SelectionAt(cursor + len(typed));
            s->selectionReversed = false;
            UpdatePreferredColumn(s);
            PauseBlink(s, app, win);
            Notify(app, win);
            return true;
        }
    }
    return false;
}

static Str InputAutoCloseText(InputState* s, App* app, Arena* a, Str typed,
                              const LanguageConfig& config, int at, int* caret,
                              int* openLen, int* closeLen) {
    *caret = -1;
    *openLen = 0;
    *closeLen = 0;
    if (!InputOneCodepoint(typed) ||
        !InputAutoCloseBefore(config, InputValue(s), at)) {
        return typed;
    }
    Str all = InputValue(s);
    int count = InputClosingPairCount(config);
    for (int p = 0; p < count; p++) {
        AutoClosingPair pair = InputClosingPairAt(config, p);
        if (!pair.open || !pair.close || pair.open.len < len(typed) ||
            !InputSliceEq(pair.open, pair.open.len - len(typed), typed)) {
            continue;
        }
        int prefix = pair.open.len - len(typed);
        int start = at - prefix;
        if (!InputSliceEq(all, start, Str(pair.open.s, prefix))) {
            continue;
        }
        if (StrEq(pair.open, pair.close)) {
            uint32_t previous = 0;
            int previousAt = Utf8Prev(all, start);
            if (previousAt < start) {
                Utf8At(all, previousAt, &previous);
            }
            bool word =
                previousAt < start &&
                (previous >= 128 || (previous >= 'a' && previous <= 'z') ||
                 (previous >= 'A' && previous <= 'Z') ||
                 (previous >= '0' && previous <= '9') || previous == '_');
            if (word || InputEscapedAt(all, start)) {
                continue;
            }
        }
        if (InputPairBlocked(pair, InputEditingContext(s, app, start))) {
            continue;
        }
        char* joined = (char*)Alloc(a, len(typed) + pair.close.len + 1);
        if (!joined) {
            return typed;
        }
        memcpy(joined, typed.s, (size_t)len(typed));
        memcpy(joined + len(typed), pair.close.s, (size_t)pair.close.len);
        joined[len(typed) + pair.close.len] = 0;
        *caret = at + len(typed);
        *openLen = pair.open.len;
        *closeLen = pair.close.len;
        return Str(joined, len(typed) + pair.close.len);
    }
    return typed;
}

static bool InputAutoCloseDeletion(InputState* s, App* app, Selection* out) {
    if (!s || s->kind != InputKind::Editor || !s->autoClose ||
        !s->selectedRange.IsEmpty() || s->extraCursors.len > 0) {
        return false;
    }
    Str all = InputValue(s);
    int cursor = InputCursor(s);
    LanguageConfig config = InputLanguageConfig(app, s->highlighter.Language());
    int count = InputClosingPairCount(config);
    for (int i = 0; i < count; i++) {
        AutoClosingPair pair = InputClosingPairAt(config, i);
        int start = cursor - pair.open.len;
        if (!pair.open || !pair.close || !InputSliceEq(all, start, pair.open) ||
            !InputSliceEq(all, cursor, pair.close) ||
            InputEscapedAt(all, start)) {
            continue;
        }
        bool generated = AutoClosedContains(s->autoClosed, start, cursor,
                                            cursor, cursor + pair.close.len);
        if (!generated &&
            InputPairBlocked(pair, InputEditingContext(s, app, start))) {
            continue;
        }
        *out = {start, cursor + pair.close.len};
        return true;
    }
    return false;
}

bool InputReplaceTextInRange(InputState* s, App* app, Window* win,
                             const Selection* range, Str newText) {
    bool hasIntent = s->undo.hasPendingIntent;
    EditIntent requested = s->undo.pendingIntent;
    s->undo.hasPendingIntent = false;
    if (!InputIsEditable(s)) {
        return false;
    }
    // replace_text_in_ranges: "Every edit invalidates provider responses for
    // the previous document, including deletion and indentation which do not
    // trigger completion." Typing asks again once the edit is in.
    InputHideContextMenu(s);
    InputClearInlineCompletion(s);
    // pause_blink_cursor, for every edit and before the cursors fan out: a
    // caret caught in its dark half comes back lit, whichever path the text
    // arrives by (a keystroke, the input method, a direct call).
    PauseBlink(s, app, win);
    if (InputIsMultiLine(s)) {
        // A keystroke with several cursors goes to all of them. An edit that
        // names its range — the input method, an undo, a server's edit list
        // — is about the active one and drops the others.
        bool multiCursor = !range && !s->imeMarking && s->extraCursors.len > 0;
        if (multiCursor) {
            return ReplaceAtEveryCursor(s, app, win, newText, hasIntent,
                                        requested);
        }
        if (range) {
            InputRemoveExtraCursors(s);
        }
    }
    Selection selBefore = s->selectedRange;

    Arena* tmp = GetTempArena();
    Str text = NormalizeInput(tmp, s, newText);
    // A commit with no range of its own replaces whatever the input method
    // had provisionally put in, not the selection: the marked text is what
    // the candidate was standing in for.
    Selection r = range           ? *range
                  : s->imeMarking ? s->imeMarked
                                  : s->selectedRange;
    if (!s->tokens || (!s->tokens->replaying && !s->tokens->validatedEdit)) {
        InputNormalizeTokenRange(s, &r.start, &r.end);
    }
    Str before = InputValue(s);
    // range_from_utf16 clamps both ends into the document: a stale or
    // out-of-range edit lands at the end rather than past it.
    if (r.start < 0) {
        r.start = 0;
    }
    if (r.start > len(before)) {
        r.start = len(before);
    }
    if (r.end > len(before)) {
        r.end = len(before);
    }
    if (r.end < r.start) {
        r.end = r.start;
    }
    int pairedCaret = -1;
    int pairedOpenLen = 0;
    int pairedCloseLen = 0;
    LanguageConfig language = LanguageConfig::Default();
    bool languageEdit = s->kind == InputKind::Editor && s->autoClose &&
                        !range && !s->imeMarking && r.IsEmpty() &&
                        s->extraCursors.len == 0;
    // Skip-over as a pure cursor move: Rust keys it on the range being the
    // caret, whether the platform named that range or left it to the
    // selection — not on a replay or a silent edit the editor made itself.
    bool skipEdit = s->kind == InputKind::Editor && s->autoClose &&
                    !s->imeMarking && r.IsEmpty() &&
                    r.start == InputCursor(s) && s->extraCursors.len == 0 &&
                    !s->silentReplace && !UndoIsIgnoring(&s->undo);
    if (skipEdit) {
        language = InputLanguageConfig(app, s->highlighter.Language());
        if (InputTrySkipCloser(s, app, win, text, language)) {
            return true;
        }
    }
    if (languageEdit) {
        language = InputLanguageConfig(app, s->highlighter.Language());
        text =
            InputAutoCloseText(s, app, tmp, text, language, r.start,
                               &pairedCaret, &pairedOpenLen, &pairedCloseLen);
    }
    // The document as it was, which push_history indexes and an invalid edit
    // is rolled back to.
    Str oldAll = StrDup(tmp, before);

    // adjust_folds_for_edit, before the splice, because the line numbers a
    // fold is written in are the old document's. A fold or a candidate that
    // spans the edited lines is dropped — its text is not what it was — and
    // the ones below it move by however many lines the edit added or took.
    if (LayoutModeIsFolding(s->mode)) {
        int editStartLine = RopeOffsetToPoint(before, r.start).row;
        int editEndLine = RopeOffsetToPoint(before, r.end).row;
        int removed = editEndLine - editStartLine;
        int added = 0;
        for (int i = 0; i < len(text); i++) {
            if (text.s[i] == '\n') {
                added++;
            }
        }
        FoldMapAdjustForEdit(&s->folds, editStartLine, editEndLine,
                             added - removed);
    }

    int nPairsBefore = 0;
    AutoClosedPairRange* pairsBefore = nullptr;
    if (!UndoIsIgnoring(&s->undo)) {
        pairsBefore = AutoClosedDup(s->autoClosed, &nPairsBefore);
    }

    TextSplice(s, r.start, r.end, text);
    int newOffset = r.start + len(text);
    if (newOffset > len(s->text)) {
        newOffset = len(s->text);
    }
    bool maskChanged = false;

    if (InputIsSingleLine(s)) {
        Str pending = InputValue(s);
        // Only reject the edit if the old text was valid, so a default_value
        // that does not conform cannot trap the field: the user can still edit
        // their way out of it.
        if (!IsValidInput(s, pending) && IsValidInput(s, oldAll)) {
            TextSet(s, oldAll);
            if (pairsBefore) {
                AutoClosedRestore(s->autoClosed, pairsBefore, nPairsBefore);
                free(pairsBefore);
            }
            return false;
        }
        if (!MaskIsNone(s->maskPattern)) {
            Str maskText = MaskApply(tmp, s->maskPattern, pending);
            maskChanged = !base::StrEq(maskText, pending);
            int grown = len(text) + len(maskText) - len(pending);
            if (grown < 0) {
                grown = 0;
            }
            TextSet(s, maskText);
            newOffset = r.start + grown;
            if (newOffset > len(maskText)) {
                newOffset = len(maskText);
            }
        }
    }

    if (!UndoIsIgnoring(&s->undo)) {
        AutoClosedAdjust(s->autoClosed, r.start, r.end, len(text));
        if (pairedCaret >= 0 && pairedOpenLen > 0 && pairedCloseLen > 0) {
            AutoClosedPairRange rec;
            rec.openStart = pairedCaret - pairedOpenLen;
            rec.openEnd = pairedCaret;
            rec.closeStart = pairedCaret;
            rec.closeEnd = pairedCaret + pairedCloseLen;
            VecAppend(s->autoClosed, rec);
        }
    }

    // adjust_annotations, or reset_annotations when masking rewrote the
    // whole document and recorded ranges no longer point at anything.
    if (maskChanged) {
        InputDecorationsReset(s);
    } else {
        InputDecorationsAdjustForEdit(s, r, len(text));
    }
    if (maskChanged) {
        // Masking rewrites the whole document, so a segment-based entry no
        // longer matches it — record a whole-document change instead, and
        // undo/redo can restore the text exactly.
        Selection after = SelectionAt(newOffset);
        PushHistory(s, oldAll, Selection{0, len(oldAll)}, InputValue(s), true,
                    EditIntent::Atomic, selBefore, &after);
    } else {
        Selection after = pairedCaret >= 0 ? SelectionAt(pairedCaret)
                                           : SelectionAt(r.start + len(text));
        PushHistory(s, oldAll, r, text, hasIntent, requested, selBefore,
                    pairedCaret >= 0 ? &after : nullptr);
    }
    if (!UndoIsIgnoring(&s->undo)) {
        UndoRecordAutoClosedPairs(&s->undo, pairsBefore, nPairsBefore,
                                  s->autoClosed.els, s->autoClosed.len);
    }
    free(pairsBefore);

    s->cursorLineEndAffinity = false;
    s->selectedRange = SelectionAt(newOffset);
    if (pairedCaret >= 0) {
        s->selectedRange = SelectionAt(pairedCaret);
    }
    s->selectionReversed = false;
    s->hasSelectedWordRange = false;
    // The text went in for real, so there is nothing provisional left.
    bool wasComposing = s->imeMarking;
    s->imeMarking = false;
    s->imeMarked = {};
    // A commit ends the composition, and neither platform follows it with an
    // unmark: macOS delivers `insertText:` for the confirmed candidate, and
    // Windows a GCS_RESULTSTR. Leaving the transaction open would merge every
    // later edit into it, so an undo would take back the whole of what was
    // typed after the composition — and put the caret back where the first
    // candidate started.
    if (wasComposing) {
        UndoCommitTransaction(&s->undo);
    }
    UpdatePreferredColumn(s);
    // update_search: every edit goes through here, so a find bar left open
    // follows what is typed.
    InputUpdateSearch(s);
    InputDismissTouchSelection(s, app, win);
    if (InputIsMultiLine(s) && s->mode.kind == LayoutModeKind::AutoGrow) {
        LayoutModeSetRows(&s->mode, RopeLinesLen(InputValue(s)));
    }
    // The colours the document names may have moved or changed, and so may
    // what a language server said about it, so the row builder asks both
    // again next frame — `Lsp::update`, which is the pair together.
    s->documentColorsDirty = true;
    s->semanticTokensDirty = true;
    // on_text_typed, which a silent replace skips: an edit the editor made
    // on the reader's behalf is not typing, so it asks for no suggestion.
    if (!s->silentReplace) {
        InputScheduleInlineCompletion(s);
    }
    // element.rs layout_cursors follows the caret on the frame an edit moved
    // it, straight to the viewport edge (upstream 03490654) rather than a line
    // at a time, so typing at a caret far off screen brings it back at once.
    // Only the vertical half: the caret's x is last frame's until it paints.
    if (InputIsMultiLine(s)) {
        InputScrollToOffset(s, InputCursor(s), InputMoveDir::None);
    }
    Emit(s, app, win, InputEvent{InputEventKind::Change});
    Notify(app, win);
    return true;
}

void InputSetAutoClose(InputState* s, bool enabled, App* app, Window* win) {
    if (!s || s->autoClose == enabled) {
        return;
    }
    s->autoClose = enabled;
    Notify(app, win);
}

void InputSetSmartIndent(InputState* s, bool enabled, App* app, Window* win) {
    if (!s || s->smartIndent == enabled) {
        return;
    }
    s->smartIndent = enabled;
    Notify(app, win);
}

// typing_intent: the intent of a batch the caller did not label. Inserting
// text at collapsed cursors is typing; anything else stands on its own.
static EditIntent TypingIntent(const Selection* ranges, int n, Str newText) {
    if (len(newText) == 0) {
        return EditIntent::Atomic;
    }
    for (int i = 0; i < len(newText); i++) {
        if (newText.s[i] == '\n' || newText.s[i] == '\r') {
            return EditIntent::Atomic;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!ranges[i].IsEmpty()) {
            return EditIntent::Atomic;
        }
    }
    return EditIntent::Typing;
}

bool InputReplaceTextInRanges(InputState* s, App* app, Window* win,
                              const Selection* ranges, const Str* texts,
                              int n) {
    bool hasIntent = s->undo.hasPendingIntent;
    EditIntent requested = s->undo.pendingIntent;
    s->undo.hasPendingIntent = false;
    if (!InputIsEditable(s) || n <= 0) {
        return false;
    }
    Arena* a = GetTempArena();
    // Highest offset first, so each edit leaves the ones below it where they
    // were.
    auto* desc = (int*)Alloc(a, n * (int)sizeof(int));
    for (int i = 0; i < n; i++) {
        int j = i;
        while (j > 0 && ranges[desc[j - 1]].start < ranges[i].start) {
            desc[j] = desc[j - 1];
            j--;
        }
        desc[j] = i;
    }
    // The cursors as the user left them, for an undo to put back. A delete
    // has already expanded them over the text it removes, so they collapse
    // back to the side they came from (collapse_for_intent).
    int nBefore = 0;
    CursorSelection* before = AllCursors(a, s, &nBefore);
    EditIntent collapse = hasIntent ? requested : EditIntent::Atomic;
    for (int i = 0; i < nBefore; i++) {
        if (collapse == EditIntent::Backspace) {
            before[i].range = SelectionAt(before[i].range.end);
            before[i].reversed = false;
        } else if (collapse == EditIntent::DeleteForward) {
            before[i].range = SelectionAt(before[i].range.start);
            before[i].reversed = false;
        }
    }
    // Several edits are one undo step; a single one records directly, which
    // keeps it eligible for the manager's typing coalescing.
    bool group = n > 1;
    if (group) {
        UndoBeginTransaction(&s->undo);
    }
    bool ok = true;
    for (int k = 0; k < n; k++) {
        int i = desc[k];
        s->undo.hasPendingIntent = hasIntent;
        s->undo.pendingIntent = requested;
        Selection r = ranges[i];
        if (!InputReplaceTextInRange(s, app, win, &r, texts[i])) {
            ok = false;
        }
    }
    // The resulting cursors: one collapsed after each edit's inserted text,
    // read in ascending order with the growth of the edits before it.
    Str t = InputValue(s);
    auto* result = (int*)Alloc(a, n * (int)sizeof(int));
    int delta = 0;
    for (int k = n - 1; k >= 0; k--) {
        int i = desc[k];
        int off = ranges[i].start + delta + texts[i].len;
        result[i] = off > len(t) ? len(t) : off;
        delta += texts[i].len - (ranges[i].end - ranges[i].start);
    }
    auto* out = (CursorSelection*)Alloc(a, n * (int)sizeof(CursorSelection));
    for (int i = 0; i < n; i++) {
        out[i] = CursorSelection{};
        out[i].range = SelectionAt(result[i]);
    }
    SetAllCursors(s, out, n);
    InputMergeOverlappingCursors(s);
    s->cursorLineEndAffinity = false;
    s->hasSelectedWordRange = false;
    UpdatePreferredColumn(s);
    if (group) {
        // Recorded before the bracket closes, so the cursors travel with the
        // step and not with whatever step happened to be on top.
        int nAfter = 0;
        CursorSelection* after = AllCursors(a, s, &nAfter);
        UndoRecordSelections(&s->undo, before, nBefore, after, nAfter);
        UndoCommitTransaction(&s->undo);
    }
    Notify(app, win);
    return ok;
}

// replace_text_in_range with several cursors: the same text at each of them,
// as one batch with the keystroke's intent, so a run of them coalesces into
// one undo just like single-cursor typing.
static bool ReplaceAtEveryCursor(InputState* s, App* app, Window* win,
                                 Str newText, bool hasIntent,
                                 EditIntent requested) {
    InputMergeOverlappingCursors(s);
    Arena* a = GetTempArena();
    int n = 0;
    CursorSelection* all = AllCursors(a, s, &n);
    auto* ranges = (Selection*)Alloc(a, n * (int)sizeof(Selection));
    auto* texts = (Str*)Alloc(a, n * (int)sizeof(Str));
    for (int i = 0; i < n; i++) {
        ranges[i] = all[i].range;
        texts[i] = newText;
    }
    EditIntent intent =
        hasIntent ? requested : TypingIntent(ranges, n, newText);
    UndoBeginTransactionWith(&s->undo, intent);
    s->undo.hasPendingIntent = true;
    s->undo.pendingIntent = intent;
    bool ok = InputReplaceTextInRanges(s, app, win, ranges, texts, n);
    UndoCommitTransaction(&s->undo);
    return ok;
}

bool InputMarkedRange(const InputState* s, Selection* out) {
    if (!s || !s->imeMarking) {
        return false;
    }
    if (out) {
        *out = s->imeMarked;
    }
    return true;
}

void InputUnmarkText(InputState* s, App* app, Window* win) {
    if (!s || !s->imeMarking) {
        return;
    }
    s->imeMarking = false;
    s->imeMarked = {};
    // The whole composition was one transaction, so it undoes as one thing
    // rather than one candidate at a time.
    UndoCommitTransaction(&s->undo);
    Notify(app, win);
}

void InputReplaceAndMarkText(InputState* s, App* app, Window* win,
                             const Selection* range, Str newText,
                             const Selection* sel) {
    if (!s || !InputIsEditable(s)) {
        return;
    }
    bool hasIntent = s->undo.hasPendingIntent;
    EditIntent requested = s->undo.pendingIntent;
    s->undo.hasPendingIntent = false;
    Selection selBefore = s->selectedRange;
    bool startsComposition = !s->imeMarking;
    if (startsComposition) {
        // "Even a canceled preedit separates the typing gestures on either
        // side; its no-op transaction must not reconnect those gestures."
        UndoBreakCoalescing(&s->undo);
        UndoBeginTransaction(&s->undo);
    }
    if (win && BlinkVisible(app, s->blink)) {
        PauseBlink(s, app, win);
    }
    Arena* tmp = GetTempArena();
    Str text = NormalizeInput(tmp, s, newText);
    Selection r = range           ? *range
                  : s->imeMarking ? s->imeMarked
                                  : s->selectedRange;
    if (!s->tokens || (!s->tokens->replaying && !s->tokens->validatedEdit)) {
        InputNormalizeTokenRange(s, &r.start, &r.end);
    }
    Str before = InputValue(s);
    if (r.start < 0) {
        r.start = 0;
    }
    if (r.end > len(before)) {
        r.end = len(before);
    }
    if (r.end < r.start) {
        r.end = r.start;
    }
    Str oldAll = StrDup(tmp, before);
    // The pairs the editor inserted move with the composition, and the
    // transaction keeps them from before it, the way the committed path does.
    int nPairsBefore = 0;
    AutoClosedPairRange* pairsBefore = nullptr;
    if (!UndoIsIgnoring(&s->undo)) {
        pairsBefore = AutoClosedDup(s->autoClosed, &nPairsBefore);
    }
    TextSplice(s, r.start, r.end, text);
    if (InputIsSingleLine(s)) {
        // The same rule the committed path uses: only refuse the edit when it
        // is the edit that broke the field, so a value that never conformed
        // cannot trap it.
        Str pending = InputValue(s);
        if (!IsValidInput(s, pending) && IsValidInput(s, oldAll)) {
            TextSet(s, oldAll);
            free(pairsBefore);
            if (startsComposition) {
                UndoCommitTransaction(&s->undo);
            }
            return;
        }
    }
    if (!UndoIsIgnoring(&s->undo)) {
        AutoClosedAdjust(s->autoClosed, r.start, r.end, len(text));
    }
    InputDecorationsAdjustForEdit(s, r, len(text));
    s->cursorLineEndAffinity = false;
    if (len(text) == 0) {
        // An empty insert is the composition being abandoned: the caret goes
        // back where it started and nothing is marked.
        s->selectedRange = SelectionAt(r.start);
        s->imeMarking = false;
        s->imeMarked = {};
    } else {
        s->imeMarking = true;
        s->imeMarked = Selection{r.start, r.start + len(text)};
        if (sel) {
            int lo = r.start + sel->start;
            int hi = r.start + sel->end;
            int end = r.start + len(text);
            s->selectedRange =
                Selection{lo < r.start ? r.start : (lo > end ? end : lo),
                          hi < r.start ? r.start : (hi > end ? end : hi)};
        } else {
            s->selectedRange = SelectionAt(r.start + len(text));
        }
    }
    s->selectionReversed = false;
    s->hasSelectedWordRange = false;
    // Every candidate is a change inside the open transaction, so an undo
    // after the composition takes the whole of it back rather than stepping
    // through the candidates one at a time.
    Selection after = s->selectedRange;
    PushHistory(s, oldAll, r, text, hasIntent, requested, selBefore, &after);
    if (!UndoIsIgnoring(&s->undo)) {
        UndoRecordAutoClosedPairs(&s->undo, pairsBefore, nPairsBefore,
                                  s->autoClosed.els, s->autoClosed.len);
    }
    free(pairsBefore);
    UpdatePreferredColumn(s);
    if (InputIsMultiLine(s) && s->mode.kind == LayoutModeKind::AutoGrow) {
        LayoutModeSetRows(&s->mode, RopeLinesLen(InputValue(s)));
    }
    if (len(text) == 0) {
        UndoCommitTransaction(&s->undo);
    }
    Notify(app, win);
}

// with_edits_allowed: disabled and readonly reject what the *user* does; a
// programmatic write always goes through.
struct EditsAllowed {
    InputState* s;
    bool wasDisabled;
    bool wasReadonly;

    explicit EditsAllowed(InputState* state)
        : s(state), wasDisabled(state->disabled), wasReadonly(state->readonly) {
        s->disabled = false;
        s->readonly = false;
    }
    ~EditsAllowed() {
        s->disabled = wasDisabled;
        s->readonly = wasReadonly;
    }
};

static void ReplaceText(InputState* s, App* app, Window* win, Str value) {
    EditsAllowed allow(s);
    s->undo.hasPendingIntent = true;
    s->undo.pendingIntent = EditIntent::Atomic;
    Selection all = {0, len(InputValue(s))};
    InputReplaceTextInRange(s, app, win, &all, value);
}

// reset_selection: a single-line field puts the caret at the end, matching an
// HTML <input>; a multi-line one goes back to 0..0.
static void ResetSelection(InputState* s) {
    InputRemoveExtraCursors(s);
    s->cursorLineEndAffinity = false;
    if (InputIsSingleLine(s)) {
        s->selectedRange = SelectionAt(len(InputValue(s)));
    } else {
        s->selectedRange = {};
    }
    s->selectionReversed = false;
    s->hasSelectedWordRange = false;
}

// set_value(): the programmatic write, which is not an edit — it clears the
// undo history rather than becoming a step in it. Rust takes a window and a
// context because it emits and notifies; this one suppresses both, so seeding
// a field before the window exists is the same call as changing it later.
static void InstallTokens(InputState* s, const InputContent& content) {
    bool supported = s->kind != InputKind::Editor && MaskIsNone(s->maskPattern);
    bool textKept = StrEq(InputValue(s), content.text);
    InlineTokenStore* store = InputTokenStore(s, content.tokens.len > 0);
    if (!store) {
        return;
    }
    InlineTokenSpansClear(&store->spans);
    if (!supported || !textKept) {
        return;
    }
    for (int i = 0; i < content.tokens.len; i++) {
        VecAppend(store->spans, InlineTokenSpanDup(content.tokens[i]));
    }
}

void InputSetValue(InputState* s, Str value) {
    App* app = nullptr;
    Window* win = nullptr;
    if (s->tokens) {
        InlineTokenSpansClear(&s->tokens->spans);
    }
    UndoSetIgnoring(&s->undo, true);
    s->emitEvents = false;
    ReplaceText(s, app, win, value);
    UndoSetIgnoring(&s->undo, false);
    s->emitEvents = true;
    VecClear(s->autoClosed);
    ResetSelection(s);
    UndoClear(&s->undo);
    Notify(app, win);
}

void InputDefaultValue(InputState* s, Str value) {
    // `self.text = Rope::from(self.normalize_input(&text))`, and the
    // pending update that has the highlighter read it on the next render —
    // which TextSet's whole-document edit is here.
    TextSet(s, NormalizeInput(GetTempArena(), s, value));
}

void InputSetValue(InputState* s, const InputContent& content) {
    InputSetValue(s, content.text);
    InstallTokens(s, content);
}

static InlineTokenError CheckTokenMode(const InputState* s) {
    if (s->imeMarking) {
        return InlineTokenError::CompositionActive;
    }
    const InlineTokenStore* store = InputTokenStore(s);
    if (s->kind == InputKind::Editor || s->masked || (store && store->secret) ||
        !MaskIsNone(s->maskPattern)) {
        return InlineTokenError::UnsupportedMode;
    }
    return InlineTokenError::Ok;
}

InlineTokenError InputReplaceRangeWithToken(InputState* s, App* app,
                                            Window* win, int start, int end,
                                            InlineToken token) {
    if (!s) {
        return InlineTokenError::InvalidRange;
    }
    InlineTokenError err = CheckTokenMode(s);
    if (err != InlineTokenError::Ok) {
        return err;
    }
    err = token.Validate();
    if (err != InlineTokenError::Ok) {
        return err;
    }
    Str text = InputValue(s);
    if (start < 0 || end < start || end > len(text)) {
        return InlineTokenError::InvalidRange;
    }
    if (start != end) {
        InputContent probe = InputContent::New(text);
        err = probe.WithToken(start, end, token);
        VecReset(probe.tokens);
        if (err != InlineTokenError::Ok &&
            err != InlineTokenError::OverlappingTokens) {
            return err;
        }
    }
    InputNormalizeTokenRange(s, &start, &end);
    if (start < 0) {
        start = 0;
    }
    if (end > len(text)) {
        end = len(text);
    }
    if (end < start) {
        end = start;
    }
    Arena* tmp = GetTempArena();
    StrBuilder next;
    next.Append(Str(text.s, start));
    next.Append(token.text);
    next.Append(Str(text.s + end, len(text) - end));
    Str built = next.TakeStr();
    Str normalized = NormalizeInput(tmp, s, built);
    if (!StrEq(normalized, built)) {
        StrFree(built);
        return InlineTokenError::TextMismatch;
    }
    if (!IsValidInput(s, normalized) && IsValidInput(s, text)) {
        StrFree(built);
        return InlineTokenError::ValidationRejected;
    }
    const Vec<InlineTokenSpan>* spans = InputTokens(s);
    if (spans) {
        for (int i = 0; i < spans->len; i++) {
            const InlineTokenSpan& span = (*spans)[i];
            if (span.start == start && span.end == end &&
                InlineTokenEq(span.token, token)) {
                StrFree(built);
                return InlineTokenError::Ok;
            }
        }
    }
    InlineTokenStore* store = InputTokenStore(s, true);
    if (store->hasPending) {
        InlineTokenFree(&store->pending);
    }
    store->pending = InlineTokenDup(token);
    store->hasPending = true;
    store->validatedEdit = true;
    UndoBreakCoalescing(&s->undo);
    s->undo.hasPendingIntent = true;
    s->undo.pendingIntent = EditIntent::Atomic;
    Selection range = {start, end};
    InputReplaceTextInRange(s, app, win, &range, token.text);
    store->validatedEdit = false;
    if (store->hasPending) {
        InlineTokenFree(&store->pending);
        store->hasPending = false;
    }
    StrFree(built);
    return InlineTokenError::Ok;
}

InlineTokenError InputReplaceWithToken(InputState* s, App* app, Window* win,
                                       InlineToken token) {
    if (!s) {
        return InlineTokenError::InvalidRange;
    }
    return InputReplaceRangeWithToken(s, app, win, s->selectedRange.start,
                                      s->selectedRange.end, token);
}

void InputReplaceAll(InputState* s, App* app, Window* win, Str value) {
    ReplaceText(s, app, win, value);
    ResetSelection(s);
    Notify(app, win);
}

void InputInsert(InputState* s, App* app, Window* win, Str value) {
    EditsAllowed allow(s);
    s->undo.hasPendingIntent = true;
    s->undo.pendingIntent = EditIntent::Atomic;
    Selection at = SelectionAt(InputCursor(s));
    InputReplaceTextInRange(s, app, win, &at, value);
    s->selectedRange = SelectionAt(s->selectedRange.end);
}

void InputReplace(InputState* s, App* app, Window* win, Str value) {
    EditsAllowed allow(s);
    s->undo.hasPendingIntent = true;
    s->undo.pendingIntent = EditIntent::Atomic;
    InputReplaceTextInRange(s, app, win, nullptr, value);
    s->selectedRange = SelectionAt(s->selectedRange.end);
}

void InputClean(InputState* s, App* app, Window* win) {
    ReplaceText(s, app, win, Str{});
    InputRemoveExtraCursors(s);
    s->selectedRange = {};
    s->selectionReversed = false;
    Notify(app, win);
}

void InputSetPlaceholder(InputState* s, Str value) {
    StrFree(s->placeholder);
    s->placeholder = StrDup(value);
}

void InputSetMaskPattern(InputState* s, MaskPattern pattern) {
    MaskPatternFree(&s->maskPattern);
    s->maskPattern = pattern;
    s->maskPatternSet = true;
    // Rust's `mask_pattern()` builder puts the derived cue in as well.
    Str cue = MaskPlaceholder(GetTempArena(), s->maskPattern);
    if (len(cue) > 0) {
        InputSetPlaceholder(s, cue);
    }
}

// ─── completion ───────────────────────────────────────────────────────────

static bool CompletionWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || (unsigned char)c >= 0x80;
}

Str InputCompletionQuery(const InputState* s, int* startOut) {
    Str t = InputValue(s);
    int at = InputCursor(s);
    if (at > len(t)) {
        at = len(t);
    }
    int start = at;
    while (start > 0 && CompletionWordChar(t.s[start - 1])) {
        start--;
    }
    if (startOut) {
        *startOut = start;
    }
    return Str(t.s + start, at - start);
}

Str InputCompletionDocumentation(InputState* s) {
    if (!s || !s->completion.open) {
        return Str{};
    }
    int sel = s->completion.selected;
    if (sel < 0 || sel >= s->completion.items.len) {
        return Str{};
    }
    CompletionItem& item = s->completion.items[sel];
    if (len(item.documentation) > 0 || !s->completionResolve) {
        return item.documentation;
    }
    // `resolve_completions`: asked once for the item being looked at, and the
    // answer is written back into the item so the next frame does not ask
    // again. Rust resolves a batch of indices and marks the list resolved;
    // one item is what a menu ever shows documentation for.
    if (item.resolved) {
        return item.documentation;
    }
    item.resolved = true;
    if (!s->completion.arena) {
        s->completion.arena = ArenaNew();
    }
    item.documentation =
        s->completionResolve(s->completionData, s->completion.arena, &item);
    return item.documentation;
}

CompletionSession::~CompletionSession() {
    VecReset(items);
    StrFree(query);
    if (arena) {
        ArenaDelete(arena);
    }
}

void InputDismissCompletion(InputState* s) {
    if (!s) {
        return;
    }
    if (s->completion.arena) {
        ArenaDelete(s->completion.arena);
        s->completion.arena = nullptr;
    }
    s->completion.open = false;
    s->completion.triggerStart = -1;
    s->completion.selected = 0;
    VecClear(s->completion.items);
    StrFree(s->completion.query);
    s->completion.query = {};
    s->completion.revision++;
}

void InputRequestCompletion(InputState* s, App* app, Window* win, bool force) {
    (void)app;
    if (!s || !s->completionProvider) {
        return;
    }
    int start = 0;
    Str query = InputCompletionQuery(s, &start);
    if (!force && len(query) == 0) {
        InputDismissCompletion(s);
        return;
    }
    // The items are the provider's own: it owns the strings, which outlive
    // the menu the way a decoration's do. A provider returns its total even
    // when the first buffer is short, so retry until the whole Rust Vec fits.
    Vec<CompletionItem> items;
    int cap = 32;
    if (!VecReserve(items, cap)) {
        return;
    }
    int n = 0;
    for (;;) {
        n = s->completionProvider(s->completionData, InputValue(s),
                                  InputCursor(s), query, items.els, cap);
        if (n < 0) {
            n = 0;
        }
        if (n <= cap) {
            break;
        }
        cap = n;
        if (!VecReserve(items, cap)) {
            return;
        }
    }
    VecClear(s->completion.items);
    for (int i = 0; i < n; i++) {
        VecAppend(s->completion.items, items.els[i]);
    }
    s->completion.open = n > 0;
    Str queryCopy = len(query) > 0 ? StrDup(query) : Str{};
    StrFree(s->completion.query);
    s->completion.query = queryCopy;
    s->completion.triggerStart = start;
    s->completion.offset = InputCursor(s);
    s->completion.selected = 0;
    s->completion.revision++;
    if (win) {
        AppInvalidate(win);
    }
}

void InputShowCompletions(InputState* s, App* app, Window* win) {
    InputRequestCompletion(s, app, win, true);
}

void InputAcceptCompletion(InputState* s, App* app, Window* win) {
    if (!s || !s->completion.open) {
        return;
    }
    int ix = s->completion.selected;
    if (ix < 0 || ix >= s->completion.items.len) {
        return;
    }
    const CompletionItem& item = s->completion.items[ix];
    Str text = item.insertText.len > 0 ? item.insertText : item.label;
    // insert_completion: the query is what the item replaces, from where the
    // word began to where the caret was when the menu came up.
    Selection range;
    range.start = s->completion.triggerStart >= 0 ? s->completion.triggerStart
                                                  : InputCursor(s);
    range.end = InputCursor(s);
    // `additionalTextEdits` go in with it — the import a name needs, at the
    // top of the document, while the name goes in at the caret. Both the
    // item's strings and its edits live in the menu's arena, so they are
    // copied out before it is dismissed.
    Vec<TextEditItem> edits;
    if (item.nAdditionalEdits < 0 ||
        (item.nAdditionalEdits > 0 && !item.additionalEdits) ||
        !VecReserve(edits, item.nAdditionalEdits + 1)) {
        return;
    }
    TextEditItem primary = {};
    primary.range = range;
    primary.newText = StrDup(GetTempArena(), text);
    VecAppend(edits, primary);
    for (int i = 0; i < item.nAdditionalEdits; i++) {
        TextEditItem edit = item.additionalEdits[i];
        edit.newText = StrDup(GetTempArena(), edit.newText);
        VecAppend(edits, edit);
    }
    InputDismissCompletion(s);
    s->silentReplace = true;
    if (len(edits) == 1) {
        InputReplaceTextInRange(s, app, win, &edits[0].range, edits[0].newText);
    } else {
        InputApplyEdits(s, app, win, edits.els, len(edits));
    }
    s->silentReplace = false;
}

// ─── the overlay seam (lsp/overlay.rs) ───────────────────────────────────

void InputPresentCompletionItems(InputState* s, int triggerStart, Str query,
                                 const CompletionItem* items, int n) {
    if (!s) {
        return;
    }
    VecClear(s->completion.items);
    for (int i = 0; i < n; i++) {
        VecAppend(s->completion.items, items[i]);
    }
    s->completion.triggerStart = triggerStart;
    s->completion.offset = InputCursor(s);
    s->completion.selected = 0;
    s->completion.open = n > 0;
    Str queryCopy = len(query) > 0 ? StrDup(query) : Str{};
    StrFree(s->completion.query);
    s->completion.query = queryCopy;
    s->completion.revision++;
}

void InputPresentCodeActions(InputState* s, const CodeActionItem* items,
                             int n) {
    if (!s) {
        return;
    }
    VecReset(s->codeActions.items);
    for (int i = 0; i < n; i++) {
        VecAppend(s->codeActions.items, items[i]);
    }
    s->codeActions.selected = 0;
    s->codeActions.open = n > 0;
    s->codeActions.revision++;
}

void InputPresentHover(InputState* s, Selection symbolRange, Str text) {
    if (!s) {
        return;
    }
    s->hoverRange = symbolRange;
    s->hoverText = text;
}

void InputPresentDiagnostic(InputState* s, int index) {
    if (!s) {
        return;
    }
    s->hoverDiagnostic = index >= 0 && index < s->diagnostics.len ? index : -1;
}

void InputClearDiagnosticPopover(InputState* s) {
    if (s) {
        s->hoverDiagnostic = -1;
    }
}

// hide_context_menu: closes the completion and the code-action menu alike.
// Rust also drops the request in flight; providers answer synchronously
// here, so there is only ever an open menu to put away.
void InputHideContextMenu(InputState* s) {
    if (!s) {
        return;
    }
    if (s->completion.open) {
        InputDismissCompletion(s);
    }
    if (s->codeActions.open) {
        InputDismissCodeActions(s);
    }
}

bool InputIsContextMenuOpen(const InputState* s) {
    return s && (s->completion.open || s->codeActions.open);
}

bool InputRouteOverlayAction(InputState* s, App* app, Window* win,
                             InputAction action) {
    if (!s) {
        return false;
    }
    // The host's own popover, if it drew one, is asked first: it is the thing
    // on screen and the keys are its.
    if (s->overlayAction && InputIsContextMenuOpen(s)) {
        InputOverlayKind kind = s->completion.open
                                    ? InputOverlayKind::Completion
                                    : InputOverlayKind::CodeAction;
        if (s->overlayAction(s->overlayActionData, kind, action)) {
            // handle_action_for_context_menu: a handled Enter or Escape is
            // the host confirming or dismissing, so both menus close.
            if (action == InputAction::Enter || action == InputAction::Escape) {
                InputHideContextMenu(s);
            }
            AppInvalidate(win);
            return true;
        }
    }
    if (InputCompletionAction(s, app, win, action)) {
        return true;
    }
    return InputCodeActionAction(s, app, win, action);
}

void InputDismissLspOverlays(InputState* s) {
    if (!s) {
        return;
    }
    InputDismissCompletion(s);
    InputDismissCodeActions(s);
    InputClearHoverDefinition(s);
    s->hoverText = Str{};
    s->hoverRange = Selection{};
    s->hoverDiagnostic = -1;
}

void InputInsertCompletion(InputState* s, App* app, Window* win,
                           const CompletionItem* item, Selection fallback) {
    if (!s || !item) {
        return;
    }
    Str text = item->insertText.len > 0 ? item->insertText : item->label;
    Selection range = fallback;
    // `insert_text` with no `text_edit`: the item goes in *after* the range
    // the query occupied rather than over it, which is what Rust's
    // `range.end..range.end` says.
    if (item->insertText.len > 0) {
        range.start = range.end;
    }
    Vec<TextEditItem> edits;
    if (item->nAdditionalEdits < 0 ||
        (item->nAdditionalEdits > 0 && !item->additionalEdits) ||
        !VecReserve(edits, item->nAdditionalEdits + 1)) {
        return;
    }
    TextEditItem primary = {};
    primary.range = range;
    primary.newText = StrDup(GetTempArena(), text);
    VecAppend(edits, primary);
    for (int i = 0; i < item->nAdditionalEdits; i++) {
        TextEditItem edit = item->additionalEdits[i];
        edit.newText = StrDup(GetTempArena(), edit.newText);
        VecAppend(edits, edit);
    }
    // completion_inserting: the write is not typing, so it opens no menu and
    // asks for no suggestion.
    s->silentReplace = true;
    if (len(edits) == 1) {
        InputReplaceTextInRange(s, app, win, &edits[0].range, edits[0].newText);
    } else {
        InputApplyEdits(s, app, win, edits.els, len(edits));
    }
    s->silentReplace = false;
}

bool InputCompletionAction(InputState* s, App* app, Window* win,
                           InputAction action) {
    if (!s || !s->completion.open) {
        return false;
    }
    int n = s->completion.items.len;
    switch (action) {
        case InputAction::MoveUp:
            s->completion.selected =
                s->completion.selected > 0 ? s->completion.selected - 1 : 0;
            AppInvalidate(win);
            return true;
        case InputAction::MoveDown:
            s->completion.selected = s->completion.selected + 1 < n
                                         ? s->completion.selected + 1
                                         : n - 1;
            AppInvalidate(win);
            return true;
        case InputAction::Enter:
            InputAcceptCompletion(s, app, win);
            return true;
        case InputAction::Escape:
            InputDismissCompletion(s);
            AppInvalidate(win);
            return true;
        default:
            return false;
    }
}

// ─── document colours ─────────────────────────────────────────────────────
//
// document_colors.rs: the provider is asked what colours the document names
// and the ranges it answers are painted behind the text. Rust asks on a timer
// after each edit and keeps the answer only when it differs; there is nothing
// to await here, so the frame after an edit asks and the answer replaces what
// was there.

static int DocumentColorCompare(const void* va, const void* vb) {
    const DocumentColor* a = (const DocumentColor*)va;
    const DocumentColor* b = (const DocumentColor*)vb;
    if (a->range.start != b->range.start) {
        return a->range.start < b->range.start ? -1 : 1;
    }
    if (a->range.end != b->range.end) {
        return a->range.end < b->range.end ? -1 : 1;
    }
    return 0;
}

void InputUpdateDocumentColors(InputState* s) {
    if (!s || !s->documentColorProvider || !s->documentColorsDirty) {
        return;
    }
    s->documentColorsDirty = false;
    // MAX_DOCUMENT_COLORS is 10,000 in Rust. It rejects the whole response
    // above that number; it does not keep a prefix. Start small for ordinary
    // documents and retry when the provider reports a larger total.
    Vec<DocumentColor> buf;
    int cap = 256;
    if (!VecReserve(buf, cap)) {
        return;
    }
    int n = 0;
    for (;;) {
        n = s->documentColorProvider(s->documentColorData, InputValue(s),
                                     buf.els, cap);
        if (n < 0) {
            n = 0;
        }
        if (n > kMaxDocumentColors) {
            return;
        }
        if (n <= cap) {
            break;
        }
        cap = n;
        if (!VecReserve(buf, cap)) {
            return;
        }
    }
    // document_colors_from_response sorts by range start; an LSP response is
    // not required to arrive in document order.
    qsort(buf.els, (size_t)n, sizeof(DocumentColor), DocumentColorCompare);
    VecClear(s->documentColors);
    for (int i = 0; i < n; i++) {
        VecAppend(s->documentColors, buf[i]);
    }
}

// ─── code actions ─────────────────────────────────────────────────────────
//
// code_actions.rs and CodeActionMenu, which between them are: ask every
// provider about the selection, put what came back in a list under the
// caret, and let the same four keys walk it. Rust's providers answer tasks
// and the answers are gathered as they land; a provider here answers into a
// buffer, so the asking and the gathering are the one call.

// ─── inline completion (lsp/completions.rs) ──────────────────────────────
//
// The ghost text in front of the caret: the provider is asked once the typing
// has stopped for the debounce, what it says is drawn after the caret in a
// muted colour, and Tab writes it into the document. Rust spawns a debounced
// task and checks on the way out that the caret has not moved; the frame is
// the clock here, and the same check is what the frame does.

InlineCompletion::~InlineCompletion() {
    if (arena) {
        ArenaDelete(arena);
    }
}

bool InputHasInlineCompletion(const InputState* s) {
    return s && len(s->inlineCompletion.text) > 0;
}

void InputClearInlineCompletion(InputState* s) {
    if (!s) {
        return;
    }
    s->inlineCompletion.text = Str{};
    s->inlineCompletion.at = -1;
    s->inlineCompletion.asked = true;
    if (s->inlineCompletion.arena) {
        ArenaDelete(s->inlineCompletion.arena);
        s->inlineCompletion.arena = nullptr;
    }
}

void InputScheduleInlineCompletion(InputState* s) {
    if (!s) {
        return;
    }
    // "Clear any existing inline completion on text change" — the suggestion
    // was about the document as it was.
    InputClearInlineCompletion(s);
    if (!s->inlineCompletionProvider) {
        return;
    }
    s->inlineCompletion
        .dueAt = TimeNow() +
                 (double)std::max(0.f, s->inlineCompletionDebounceMs) / 1000.0;
    s->inlineCompletion.asked = false;
    s->inlineCompletion.at = InputCursor(s);
}

bool InputUpdateInlineCompletion(InputState* s, bool menuOpen) {
    if (!s || !s->inlineCompletionProvider || s->inlineCompletion.asked) {
        return false;
    }
    // The caret moved while the debounce ran, or a menu opened over it: both
    // are checks Rust makes on the far side of the timer.
    if (menuOpen || InputCursor(s) != s->inlineCompletion.at) {
        InputClearInlineCompletion(s);
        return false;
    }
    if (TimeNow() < s->inlineCompletion.dueAt) {
        // Still waiting, and the window has to come back for the frame that
        // is not.
        return true;
    }
    s->inlineCompletion.asked = true;
    if (s->inlineCompletion.arena) {
        ArenaDelete(s->inlineCompletion.arena);
    }
    s->inlineCompletion.arena = ArenaNew();
    Str text = s->inlineCompletionProvider(
        s->inlineCompletionData, s->inlineCompletion.arena, InputValue(s),
        s->inlineCompletion.at);
    s->inlineCompletion.text = text;
    return false;
}

bool InputAcceptInlineCompletion(InputState* s, App* app, Window* win) {
    if (!InputHasInlineCompletion(s)) {
        return false;
    }
    // The text is in the suggestion's own arena, which the insert below is
    // about to drop, so it is copied first.
    Str keep = StrDup(s->inlineCompletion.text);
    if (!keep.s && len(s->inlineCompletion.text) > 0) {
        return false;
    }
    InputClearInlineCompletion(s);
    InputInsert(s, app, win, keep);
    StrFree(keep);
    return true;
}

// ─── range semantic tokens (lsp/semantic_tokens.rs) ──────────────────────

int SemanticTokensDecode(const SemanticToken* toks, int n, const Str* names,
                         int nNames, SemanticSpan* out, int cap) {
    if (!toks || !out || cap <= 0) {
        return 0;
    }
    int m = 0;
    uint32_t line = 0;
    uint32_t character = 0;
    for (int i = 0; i < n && m < cap; i++) {
        const SemanticToken& t = toks[i];
        // A new line resets the column; the same line carries on from the
        // token before it.
        if (t.deltaLine > 0) {
            line += t.deltaLine;
            character = t.deltaStart;
        } else {
            character += t.deltaStart;
        }
        if (!names || t.tokenType >= (uint32_t)nNames) {
            continue;
        }
        out[m].line = (int)line;
        out[m].col = (int)character;
        out[m].len = (int)t.length;
        out[m].name = names[t.tokenType];
        m++;
    }
    // The wire order is already document order — a delta is never negative —
    // so the sort Rust ends with has nothing to do here. It is written out
    // rather than skipped silently: an insertion sort over a list that is
    // already sorted is a walk.
    for (int i = 1; i < m; i++) {
        SemanticSpan v = out[i];
        int j = i - 1;
        while (j >= 0 && (out[j].line > v.line ||
                          (out[j].line == v.line && out[j].col > v.col))) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = v;
    }
    return m;
}

int SemanticTokensForRange(const SemanticSpan* toks, int n, Str text,
                           Selection visible, SemanticRange* out, int cap) {
    if (!toks || !out || cap <= 0 || n <= 0) {
        return 0;
    }
    RopePoint first = RopeOffsetToPoint(text, visible.start);
    RopePoint last = RopeOffsetToPoint(text, visible.end);
    // The cache is sorted by start. A token can only touch the viewport if
    // its start is before the end of it (the upper bound), and it is not on a
    // line wholly above the first visible one (the lower bound — a token
    // never spans a line, so an earlier line cannot reach in).
    int lo = 0, hi = n;
    {
        int a = 0, b = n;
        while (a < b) {
            int mid = (a + b) / 2;
            bool before =
                toks[mid].line < last.row ||
                (toks[mid].line == last.row && toks[mid].col < last.column);
            if (before) {
                a = mid + 1;
            } else {
                b = mid;
            }
        }
        hi = a;
        a = 0;
        b = n;
        while (a < b) {
            int mid = (a + b) / 2;
            if (toks[mid].line < first.row) {
                a = mid + 1;
            } else {
                b = mid;
            }
        }
        lo = a;
    }
    int m = 0;
    for (int i = lo; i < hi && m < cap; i++) {
        const SemanticSpan& t = toks[i];
        int start = RopePointToOffset(text, RopePoint{t.line, t.col});
        int end = RopePointToOffset(text, RopePoint{t.line, t.col + t.len});
        if (start >= end || start >= visible.end || end <= visible.start) {
            continue;
        }
        out[m].range = Selection{start, end};
        out[m].name = t.name;
        m++;
    }
    return m;
}

void InputLspUpdate(InputState* s) {
    InputUpdateDocumentColors(s);
    InputUpdateSemanticTokens(s);
}

void InputLspReset(InputState* s) {
    if (!s) {
        return;
    }
    VecClear(s->documentColors);
    s->documentColorsDirty = true;
    VecClear(s->semanticTokens);
    s->semanticTokensDirty = true;
    InputClearInlineCompletion(s);
    InputDismissLspOverlays(s);
}

void InputUpdateSemanticTokens(InputState* s) {
    if (!s || !s->semanticTokensProvider || !s->semanticTokensDirty) {
        return;
    }
    s->semanticTokensDirty = false;
    // Rust fetches the whole document and windows the answer at paint, so a
    // scroll never refetches; the same here.
    Vec<SemanticToken> buf;
    int cap = 256;
    if (!VecReserve(buf, cap)) {
        return;
    }
    Str text = InputValue(s);
    int n = 0;
    for (;;) {
        n = s->semanticTokensProvider(s->semanticTokensData, text,
                                      Selection{0, len(text)}, buf.els, cap);
        if (n < 0) {
            n = 0;
        }
        if (n <= cap) {
            break;
        }
        cap = n;
        if (!VecReserve(buf, cap)) {
            return;
        }
    }
    Vec<SemanticSpan> decoded;
    if (n > 0 && !VecReserve(decoded, n)) {
        return;
    }
    int m = SemanticTokensDecode(buf.els, n, s->semanticLegend,
                                 s->nSemanticLegend, decoded.els, n);
    VecClear(s->semanticTokens);
    for (int i = 0; i < m; i++) {
        VecAppend(s->semanticTokens, decoded[i]);
    }
}

// ─── go to definition (input/editor/lsp/definitions.rs) ───────────────────

HoverDefinition::~HoverDefinition() {
    VecReset(locations);
    if (arena) {
        ArenaDelete(arena);
    }
}

bool InputCanGoToDefinition(const InputState* s) {
    return s && s->definitionProvider != nullptr;
}

void InputClearHoverDefinition(InputState* s) {
    if (!s || s->hoverDef.locations.len == 0) {
        return;
    }
    s->hoverDef.symbolRange = Selection{};
    VecReset(s->hoverDef.locations);
    s->hoverDef.bounds = Bounds{};
}

void InputHoverDefinition(InputState* s, int offset) {
    if (!s || !s->definitionProvider) {
        return;
    }
    // `is_same`: while the pointer stays inside the symbol that was asked
    // about, what the provider said stands.
    if (s->hoverDef.locations.len > 0 &&
        offset >= s->hoverDef.symbolRange.start &&
        offset < s->hoverDef.symbolRange.end) {
        return;
    }
    Str text = InputValue(s);
    if (!s->hoverDef.arena) {
        s->hoverDef.arena = ArenaNew();
    } else {
        // Last time's uris go with it: nothing is left pointing at them.
        ArenaDelete(s->hoverDef.arena);
        s->hoverDef.arena = ArenaNew();
    }
    Vec<DefinitionLink> buf;
    int cap = 8;
    if (!VecReserve(buf, cap)) {
        return;
    }
    int n = 0;
    for (;;) {
        n = s->definitionProvider(s->definitionData, s->hoverDef.arena, text,
                                  offset, buf.els, cap);
        if (n < 0) {
            n = 0;
        }
        if (n <= cap) {
            break;
        }
        cap = n;
        if (!VecReserve(buf, cap)) {
            return;
        }
    }
    InputClearHoverDefinition(s);
    if (n <= 0) {
        return;
    }
    // The word under the pointer is what is underlined, unless the first
    // location named a range of its own.
    int a0 = offset, b0 = offset;
    if (!TextWordRangeAt(text, offset, &a0, &b0)) {
        a0 = b0 = offset;
    }
    Selection symbol = {a0, b0};
    if (!buf[0].origin.IsEmpty()) {
        symbol = buf[0].origin;
    }
    if (symbol.IsEmpty()) {
        return;
    }
    s->hoverDef.symbolRange = symbol;
    for (int i = 0; i < n; i++) {
        VecAppend(s->hoverDef.locations, buf[i]);
    }
}

void InputOnContextMenu(InputState* state, InputContextMenuFn handler,
                        void* data, void (*drop)(void* data)) {
    if (!state) {
        return;
    }
    if (state->contextMenuDrop && state->contextMenuData &&
        state->contextMenuData != data) {
        state->contextMenuDrop(state->contextMenuData);
    }
    state->contextMenuHandler = handler;
    state->contextMenuData = data;
    state->contextMenuDrop = drop;
}

// cx.defer_in: the handler with what it was captured with, run once the
// event that asked for it is over.
struct ContextMenuJob {
    InputContextMenuFn handler = nullptr;
    void* data = nullptr;
    InputContextMenuCapabilities caps = {};
    Point position = {};
    App* app = nullptr;
    Window* win = nullptr;
};

static void RunContextMenuJob(ContextMenuJob* job) {
    NativeMenu menu;
    job->handler(job->data, &menu, job->caps, job->position, job->app,
                 job->win);
    delete job;
}

void InputHandleRightClickMenu(InputState* s, App* app, Window* win,
                               Point position, int offset) {
    if (!s || s->disabled || BaseIsInDeferredContext(app)) {
        return;
    }
    if (!s->selectedRange.Contains(offset)) {
        InputMoveTo(s, app, win, offset);
    }
    if (s->kind == InputKind::Editor) {
        InputHoverDefinition(s, offset);
    }
    if (!s->contextMenuHandler) {
        return;
    }
    auto* job = new ContextMenuJob();
    job->handler = s->contextMenuHandler;
    job->data = s->contextMenuData;
    job->caps = InputContextMenuCapabilities::Of(s);
    job->position = position;
    job->app = app;
    job->win = win;
    ExecPost(MkFunc0(&RunContextMenuJob, job));
}

static bool DefinitionIsExternal(Str uri) {
    return base::StrStartsWithI(uri, "http://") ||
           base::StrStartsWithI(uri, "https://");
}

void InputFollowDefinition(InputState* s, App* app, Window* win,
                           const DefinitionLink& link) {
    if (!s) {
        return;
    }
    bool external = DefinitionIsExternal(link.uri);
    // window/showDocument: the host is asked first, so a virtual or external
    // uri can be opened the way the application wants — a docs pane of its
    // own, rather than the browser.
    if (s->showDocument &&
        s->showDocument(s->showDocumentData, link.uri, external, link.target)) {
        return;
    }
    if (external) {
        OpenUrl(link.uri);
        return;
    }
    // A uri that names another document is one this tree cannot open: there
    // is one buffer per field, and nothing to open it into.
    if (len(link.uri) > 0) {
        return;
    }
    InputMoveTo(s, app, win, link.target.start);
    InputSelectTo(s, app, win, link.target.end);
}

bool InputClickDefinition(InputState* s, App* app, Window* win, int offset,
                          bool secondary) {
    if (!s || !secondary || s->hoverDef.locations.len == 0) {
        return false;
    }
    if (offset < s->hoverDef.symbolRange.start ||
        offset >= s->hoverDef.symbolRange.end) {
        return false;
    }
    InputFollowDefinition(s, app, win, s->hoverDef.locations[0]);
    return true;
}

void InputGoToDefinition(InputState* s, App* app, Window* win) {
    if (!s) {
        return;
    }
    // on_action_go_to_definition: "A keyboard action must also work before
    // the symbol has been hovered", so the provider is asked about the caret.
    // Rust awaits the answer and drops it when the text, the caret or the
    // focus moved meanwhile; the provider answers synchronously here, so
    // nothing can move in between.
    if (!s->definitionProvider) {
        return;
    }
    Arena* a = GetTempArena();
    int at = InputCursor(s);
    DefinitionLink first = {};
    int n = s->definitionProvider(s->definitionData, a, InputValue(s), at,
                                  &first, 1);
    if (n > 0) {
        InputFollowDefinition(s, app, win, first);
    }
}

CodeActionSession::~CodeActionSession() {
    VecReset(items);
    if (arena) {
        ArenaDelete(arena);
    }
}

void InputDismissCodeActions(InputState* s) {
    if (!s) {
        return;
    }
    s->codeActions.open = false;
    VecReset(s->codeActions.items);
    s->codeActions.selected = 0;
    s->codeActions.revision++;
}

void InputAddCodeActionProvider(InputState* s, CodeActionFn fn, void* data,
                                CodeActionPerformFn perform) {
    if (!s || !fn) {
        return;
    }
    bool hadDirectProvider = s->codeActionProvider != nullptr;
    if (s->codeActionProviders.len == 0 && hadDirectProvider) {
        // A caller may have used the original one-provider field and then
        // added another. Preserve that first registration when the Vec is
        // materialized.
        VecAppend(s->codeActionProviders,
                  {s->codeActionProvider, s->codeActionData, nullptr});
    }
    if (!hadDirectProvider) {
        // The first one is the field the one-provider callers already write.
        s->codeActionProvider = fn;
        s->codeActionData = data;
    }
    VecAppend(s->codeActionProviders, {fn, data, perform});
}

// Every provider, in the order they were registered, with the field the
// one-provider callers write standing in for a first registration they never
// made.
static int CodeActionProviderCount(const InputState* s) {
    if (s->codeActionProviders.len > 0) {
        return s->codeActionProviders.len;
    }
    return s->codeActionProvider ? 1 : 0;
}

static CodeActionFn CodeActionProviderAt(const InputState* s, int i,
                                         void** data) {
    if (s->codeActionProviders.len > 0) {
        *data = s->codeActionProviders[i].data;
        return s->codeActionProviders[i].provide;
    }
    *data = s->codeActionData;
    return s->codeActionProvider;
}

void InputToggleCodeActions(InputState* s, App* app, Window* win) {
    if (!s || CodeActionProviderCount(s) == 0) {
        return;
    }
    // handle_code_action_trigger: the action always asks again, for the
    // selection as it is now, and a menu that is already up is replaced by
    // the answer — with a new revision, which is what rebuilds the overlay.
    if (!s->codeActions.arena) {
        s->codeActions.arena = ArenaNew();
    } else {
        // Last time's titles go with it: nothing is left pointing at them.
        ArenaDelete(s->codeActions.arena);
        s->codeActions.arena = ArenaNew();
    }
    VecReset(s->codeActions.items);
    // Every provider is asked and the answers go in one list, each item
    // remembering which one it came from.
    int nProviders = CodeActionProviderCount(s);
    for (int p = 0; p < nProviders; p++) {
        void* data = nullptr;
        CodeActionFn fn = CodeActionProviderAt(s, p, &data);
        if (!fn) {
            continue;
        }
        Vec<CodeActionItem> buf;
        int cap = 16;
        if (!VecReserve(buf, cap)) {
            continue;
        }
        int n = 0;
        for (;;) {
            n = fn(data, s->codeActions.arena, InputValue(s), s->selectedRange,
                   buf.els, cap);
            if (n < 0) {
                n = 0;
            }
            if (n <= cap) {
                break;
            }
            cap = n;
            if (!VecReserve(buf, cap)) {
                n = 0;
                break;
            }
        }
        for (int i = 0; i < n; i++) {
            buf[i].provider = p;
            VecAppend(s->codeActions.items, buf[i]);
        }
    }
    int n = s->codeActions.items.len;
    s->codeActions.selected = 0;
    s->codeActions.open = n > 0;
    s->codeActions.revision++;
    Notify(app, win);
}

void InputApplyEdits(InputState* s, App* app, Window* win,
                     const TextEditItem* edits, int n) {
    if (!s || !edits || n <= 0) {
        return;
    }
    // Each edit is its own undo step, which is what Rust's loop over
    // `replace_text_in_range_silent` records: `silent` suppresses the
    // completion trigger and says nothing about the history. The Atomic
    // intent is what keeps them from coalescing with the typing around them.
    for (int i = 0; i < n; i++) {
        s->undo.hasPendingIntent = true;
        s->undo.pendingIntent = EditIntent::Atomic;
        Str text = InputValue(s);
        Selection range = edits[i].range;
        if (range.start < 0) {
            range.start = 0;
        }
        if (range.end > len(text)) {
            range.end = len(text);
        }
        if (range.end < range.start) {
            range.end = range.start;
        }
        // The replacement is copied out: it may point into the document this
        // edit is about to rewrite.
        Str newText = StrDup(GetTempArena(), edits[i].newText);
        InputReplaceTextInRange(s, app, win, &range, newText);
    }
}

void InputPerformCodeAction(InputState* s, App* app, Window* win) {
    if (!s || !s->codeActions.open) {
        return;
    }
    int ix = s->codeActions.selected;
    if (ix < 0 || ix >= s->codeActions.items.len) {
        return;
    }
    // The item is copied out: dismissing the menu is what frees the arena its
    // strings were written into.
    CodeActionItem item = s->codeActions.items[ix];
    // The edits are in the menu's arena, which dismissing it frees, so they
    // are copied out first.
    Vec<TextEditItem> edits;
    if (item.nEdits > 0 && item.edits) {
        if (!VecReserve(edits, item.nEdits)) {
            return;
        }
        for (int i = 0; i < item.nEdits; i++) {
            TextEditItem edit = item.edits[i];
            edit.newText = StrDup(GetTempArena(), edit.newText);
            VecAppend(edits, edit);
        }
    } else {
        if (!VecReserve(edits, 1)) {
            return;
        }
        TextEditItem edit = {};
        edit.range = item.range;
        edit.newText = StrDup(GetTempArena(), item.newText);
        VecAppend(edits, edit);
    }
    // perform_code_action: the provider that answered with it does it, if it
    // said it would. Its edits are what the editor applies otherwise.
    CodeActionPerformFn perform = nullptr;
    void* performData = nullptr;
    if (item.provider >= 0 && item.provider < s->codeActionProviders.len) {
        const CodeActionProviderEntry& provider =
            s->codeActionProviders[item.provider];
        perform = provider.perform;
        performData = provider.data;
    }
    InputDismissCodeActions(s);
    if (perform && perform(performData, s, app, win, &item)) {
        Notify(app, win);
        return;
    }
    s->silentReplace = true;
    InputApplyEdits(s, app, win, edits.els, len(edits));
    s->silentReplace = false;
    Notify(app, win);
}

bool InputCodeActionAction(InputState* s, App* app, Window* win,
                           InputAction action) {
    if (!s || !s->codeActions.open) {
        return false;
    }
    int n = s->codeActions.items.len;
    switch (action) {
        case InputAction::MoveUp:
            s->codeActions.selected =
                s->codeActions.selected > 0 ? s->codeActions.selected - 1 : 0;
            AppInvalidate(win);
            return true;
        case InputAction::MoveDown:
            s->codeActions.selected = s->codeActions.selected + 1 < n
                                          ? s->codeActions.selected + 1
                                          : n - 1;
            AppInvalidate(win);
            return true;
        case InputAction::Enter:
            InputPerformCodeAction(s, app, win);
            return true;
        case InputAction::Escape:
            InputDismissCodeActions(s);
            AppInvalidate(win);
            return true;
        default:
            return false;
    }
}

void InputTypeChar(InputState* s, App* app, Window* win, uint32_t ch) {
    char buf[4];
    int n = 0;
    if (ch < 0x80) {
        buf[n++] = (char)ch;
    } else if (ch < 0x800) {
        buf[n++] = (char)(0xC0 | (ch >> 6));
        buf[n++] = (char)(0x80 | (ch & 0x3F));
    } else if (ch < 0x10000) {
        buf[n++] = (char)(0xE0 | (ch >> 12));
        buf[n++] = (char)(0x80 | ((ch >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (ch & 0x3F));
    } else {
        buf[n++] = (char)(0xF0 | (ch >> 18));
        buf[n++] = (char)(0x80 | ((ch >> 12) & 0x3F));
        buf[n++] = (char)(0x80 | ((ch >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (ch & 0x3F));
    }
    InputReplaceTextInRange(s, app, win, nullptr, Str(buf, n));
    PauseBlink(s, app, win);
    // A menu of actions on what was selected has nothing to say about a
    // document that has changed under it, so typing puts it away.
    InputDismissCodeActions(s);
    // is_completion_trigger. The provider decides where it has an opinion;
    // the rule underneath is the one every provider in this tree has wanted:
    // a word character carries a menu that is already up and opens one that
    // is not, `.` opens one where the caret stands, and anything else closes
    // it.
    if (s->completionProvider) {
        // n, not strlen: buf holds the UTF-8 bytes of one code point and is
        // not terminated, so strlen would read past the end of the array.
        Str typed = Str(buf, n);
        CompletionTrigger want;
        if (s->completionTrigger) {
            want = s->completionTrigger(s->completionData, InputValue(s),
                                        InputCursor(s), typed);
        } else if (CompletionWordChar(buf[0])) {
            want = CompletionTrigger::Continue;
        } else if (buf[0] == '.') {
            want = CompletionTrigger::Open;
        } else {
            want = CompletionTrigger::Close;
        }
        if (want == CompletionTrigger::Continue) {
            InputRequestCompletion(s, app, win, false);
        } else if (want == CompletionTrigger::Open) {
            InputRequestCompletion(s, app, win, true);
        } else {
            InputDismissCompletion(s);
        }
    }
}

// ─── movement ─────────────────────────────────────────────────────────────

// How tall a logical line is: one line height per visual row the wrap map
// gives it.
static float DisplayLineH(const InputState* s, int row, float lineH) {
    // A folded-away line is worth no height at all, which is what makes the
    // walk below step straight over it: the row moves on and the y it is
    // carrying does not, so a closed fold costs one press to cross rather
    // than one per line inside it.
    if (FoldMapLineHidden(&s->folds, row)) {
        return 0;
    }
    return (float)InputWrapRows(s, row, nullptr, nullptr) * lineH;
}

// Document y of `row`: the visual rows above it, less the ones a fold hides.
static float DisplayRowDocY(const InputState* s, int row, float lineH) {
    if (!s || row <= 0) {
        return 0;
    }
    const InputWrapMap* m = WrapMapOf(s, nullptr);
    bool folded = len(s->folds.folded) > 0;
    if (!folded) {
        if (!m) {
            return (float)row * lineH;
        }
        if (row < len(m->lines)) {
            return (float)m->lines[row].rowsAbove * lineH;
        }
        return (float)m->totalRows * lineH;
    }
    float y = 0;
    for (int i = 0; i < row; i++) {
        y += DisplayLineH(s, i, lineH);
    }
    return y;
}

// Where a vertical move of `lines` rows from `from` lands, and what to aim
// at on the next one — move_to and select_to both recompute the aim from
// where the caret ended up, and the whole point of a walk is to keep aiming
// at where it started. Exactly one of the two aims is set: the x when the
// display map answered, the column when it did not.
struct VerticalTarget {
    int offset = 0;
    float preferredX = -1;
    int preferredColumn = -1;
    bool lineEndAffinity = false;
    // The walk ran out of rows before it had moved "lines" of them: the caret
    // is already on the first (going up) or last (going down) visual row.
    bool noFurtherRow = false;
};

// One visual row of a line: its bytes [lo, hi), where its run starts from
// the text column's left edge, and the buffer offset of the line, which is
// what finds the inline tokens on it.
struct WrapRowSpan {
    int lo = 0;
    int hi = 0;
    float x = 0;
    int lineStart = 0;
};

static WrapRowSpan WrapRowSpanOf(const int* starts, int nRows, float indent,
                                 int lineLen, int k, int lineStart) {
    WrapRowSpan r;
    r.lo = starts[k];
    r.hi = k + 1 < nRows ? starts[k + 1] : lineLen;
    r.x = k > 0 ? indent : 0;
    r.lineStart = lineStart;
    return r;
}

static float WrapFontOf(const InputState* s) {
    if (s->lastFont > 0) {
        return s->lastFont;
    }
    return s->wrap.fontSize > 0 ? s->wrap.fontSize : 14.f;
}

// The advance of a run of the row's text, as its own shaped run.
static float WrapTextAdvance(PaintCtx* ctx, const InputState* s, Str text) {
    float x = 0, y = 0, h = 0;
    if (len(text) > 0 && ctx) {
        TextPointAt(ctx, text, WrapFontOf(s), 0, false, len(text), &x, &y, &h,
                    s->lastFontWord);
    }
    return x;
}

// The row's fragments, as Rust's display map lays them: the text runs
// between inline tokens, and each token as its chip, `width` wide.
struct WrapFragment {
    int lo = 0; // into the line
    int hi = 0;
    bool chip = false;
    float width = 0; // a chip's
};

static int WrapRowFragments(const InputState* s, Str line, WrapRowSpan r,
                            WrapFragment* out, int cap) {
    int n = 0;
    int at = r.lo;
    const Vec<InlineTokenSpan>* spans =
        InputTokensVisible(s) ? InputTokens(s) : nullptr;
    int nSpans = spans ? len(*spans) : 0;
    const Vec<float>& widths = s->wrap.tokenWidths;
    for (int i = 0; i < nSpans && n + 2 < cap; i++) {
        const InlineTokenSpan& span = (*spans)[i];
        int lo = span.start - r.lineStart;
        int hi = span.end - r.lineStart;
        if (hi <= r.lo || lo >= r.hi) {
            continue;
        }
        if (lo > at) {
            out[n++] = {at, lo, false, 0};
        }
        float w = len(widths) == nSpans ? widths[i] : 0;
        out[n++] = {lo, hi, true, w};
        at = hi;
    }
    if (at < r.hi || n == 0) {
        out[n++] = {at, r.hi, false, 0};
    }
    (void)line;
    return n;
}

// x_for_index within the row, from the text column's left edge. An offset
// inside a chip stands at the chip's left edge.
static float WrapRowX(PaintCtx* ctx, const InputState* s, Str line,
                      WrapRowSpan r, int local) {
    WrapFragment frags[64];
    int n = WrapRowFragments(s, line, r, frags, (int)dimof(frags));
    float x = r.x;
    for (int i = 0; i < n; i++) {
        const WrapFragment& f = frags[i];
        if (local <= f.lo) {
            break;
        }
        if (f.chip) {
            if (local < f.hi) {
                break;
            }
            x += f.width;
            continue;
        }
        int end = local < f.hi ? local : f.hi;
        x += WrapTextAdvance(ctx, s, Str(line.s + f.lo, end - f.lo));
        if (local < f.hi) {
            break;
        }
    }
    return x;
}

// closest_index_for_x within the row: a byte offset into the line. A press on
// a chip lands before it on its left half and after it on its right half.
static int WrapRowIndexAt(PaintCtx* ctx, const InputState* s, Str line,
                          WrapRowSpan r, float x) {
    if (x <= r.x || r.hi <= r.lo || !ctx) {
        return r.lo;
    }
    WrapFragment frags[64];
    int n = WrapRowFragments(s, line, r, frags, (int)dimof(frags));
    float at = r.x;
    float font = WrapFontOf(s);
    for (int i = 0; i < n; i++) {
        const WrapFragment& f = frags[i];
        if (f.chip) {
            if (x < at + f.width) {
                return x < at + f.width * 0.5f ? f.lo : f.hi;
            }
            at += f.width;
            continue;
        }
        Str text = Str(line.s + f.lo, f.hi - f.lo);
        float w = WrapTextAdvance(ctx, s, text);
        if (x < at + w || i == n - 1) {
            return f.lo + TextIndexAt(ctx, text, font, 0, false, x - at, 0,
                                      s->lastFontWord);
        }
        at += w;
    }
    return r.hi;
}

// display_map.rs: a vertical move walks *display* rows, so a wrapped line
// takes as many presses to cross as it has visual rows. The caret's row and x
// come off the wrap map, the walk steps a row at a time through the lines
// around it, skipping what a fold hides, and the x maps back to an offset in
// the row it lands on. False when the field does not wrap, which leaves the
// logical-line walk.
static bool VerticalTargetDisplay(const InputState* s, Window* win, int lines,
                                  Str t, int from, VerticalTarget* out) {
    if (!win) {
        return false;
    }
    PaintCtx* ctx = &win->paint;
    if (!WrapMapOf(s, ctx)) {
        return false;
    }
    RopePoint p = RopeOffsetToPoint(t, from);
    Str text = RopeSliceLine(t, p.row);
    int start = RopeLineStartOffset(t, p.row);
    const int* starts = nullptr;
    float indent = 0;
    int nRows = InputWrapRows(s, p.row, &starts, &indent);
    int k =
        WrapRowOfOffset(starts, nRows, from - start, s->cursorLineEndAffinity);
    float cx = WrapRowX(
        ctx, s, text, WrapRowSpanOf(starts, nRows, indent, len(text), k, start),
        from - start);
    // The x the whole walk aims at, so crossing a short row and coming back
    // lands where it started.
    float wantX = s->preferredX >= 0 ? s->preferredX : cx;
    int maxLine = RopeLinesLen(t) - 1;
    int line = p.row;
    int row = k;
    for (int step = lines; step < 0; step++) {
        if (row > 0) {
            row--;
            continue;
        }
        int prev = line - 1;
        while (prev >= 0 && FoldMapLineHidden(&s->folds, prev)) {
            prev--;
        }
        if (prev < 0) {
            break;
        }
        line = prev;
        row = InputWrapRows(s, line, nullptr, nullptr) - 1;
    }
    for (int step = lines; step > 0; step--) {
        if (row + 1 < InputWrapRows(s, line, nullptr, nullptr)) {
            row++;
            continue;
        }
        int next = line + 1;
        while (next <= maxLine && FoldMapLineHidden(&s->folds, next)) {
            next++;
        }
        if (next > maxLine) {
            break;
        }
        line = next;
        row = 0;
    }
    out->noFurtherRow = line == p.row && row == k;
    Str target = RopeSliceLine(t, line);
    nRows = InputWrapRows(s, line, &starts, &indent);
    WrapRowSpan r = WrapRowSpanOf(starts, nRows, indent, len(target), row,
                                  RopeLineStartOffset(t, line));
    int local = WrapRowIndexAt(ctx, s, target, r, wantX);
    out->offset = RopeLineStartOffset(t, line) + local;
    // The end of a row that is not its line's last is the start of the next
    // one: the affinity keeps the caret on the row it was aimed at.
    out->lineEndAffinity = local == r.hi && row + 1 < nRows;
    out->preferredX = wantX;
    return true;
}

// The same move without a display map: whole lines, at the column the walk
// started from.
static VerticalTarget VerticalTargetFor(const InputState* s, Window* win,
                                        int lines, Str t, int from) {
    VerticalTarget out;
    if (VerticalTargetDisplay(s, win, lines, t, from, &out)) {
        return out;
    }
    RopePoint p = RopeOffsetToPoint(t, from);
    int column = s->preferredColumn >= 0 ? s->preferredColumn : p.column;
    int maxRow = RopeLinesLen(t) - 1;
    int row = p.row + lines;
    if (row < 0) {
        row = 0;
    }
    if (row > maxRow) {
        row = maxRow;
    }
    out.noFurtherRow = row == p.row;
    int lineLen = RopeLineLen(t, row);
    int want = column < lineLen ? column : lineLen;
    out.offset =
        RopeClipOffset(t, RopeLineStartOffset(t, row) + want, Bias::Left);
    out.preferredColumn = column;
    return out;
}

// Where one cursor goes. `anchors` says the column and x are the walk's own
// and are kept as given, -1 included; otherwise the column is the offset's.
struct MoveTarget {
    int offset = 0;
    bool lineEndAffinity = false;
    bool anchors = false;
    int preferredColumn = -1;
    float preferredX = -1;
};

static MoveTarget TargetAt(int offset, bool lineEndAffinity = false) {
    MoveTarget t;
    t.offset = offset;
    t.lineEndAffinity = lineEndAffinity;
    return t;
}

static MoveTarget TargetOf(const VerticalTarget& v) {
    MoveTarget t;
    t.offset = v.offset;
    t.lineEndAffinity = v.lineEndAffinity;
    t.anchors = true;
    t.preferredColumn = v.preferredColumn;
    t.preferredX = v.preferredX;
    return t;
}

// The anchors a moved cursor ends up with.
static void ApplyAnchors(InputState* s, CursorSelection* c,
                         const MoveTarget& t) {
    if (t.anchors) {
        c->preferredColumn = t.preferredColumn;
        c->preferredX = t.preferredX;
    } else {
        c->preferredColumn = RopeOffsetToPoint(InputValue(s), c->Cursor())
                                 .column;
        c->preferredX = -1;
    }
}

// move_all_cursors: `f(cursor, isActive)` says where each caret goes; each
// collapses there and overlapping ones merge. The active one's move is the
// single-cursor move_to — scroll, blink, the coalescing break — with the
// others put back after it.
template <class F>
static void MoveAllCursors(InputState* s, App* app, Window* win, F f) {
    Arena* a = GetTempArena();
    int n = 0;
    CursorSelection* all = AllCursors(a, s, &n);
    auto* targets = (MoveTarget*)Alloc(a, n * (int)sizeof(MoveTarget));
    for (int i = 0; i < n; i++) {
        targets[i] = f(all[i], i == 0);
    }
    InputMoveToWithAffinity(s, app, win, targets[0].offset,
                            targets[0].lineEndAffinity);
    CursorSelection active = ActiveCursor(s);
    ApplyAnchors(s, &active, targets[0]);
    SetActiveCursor(s, active);
    int textLen = len(InputValue(s));
    for (int i = 1; i < n; i++) {
        int off = InputCursorBoundary(s, targets[i].offset, Bias::Left);
        CursorSelection c;
        c.range = SelectionAt(off < 0 ? 0 : (off > textLen ? textLen : off));
        ApplyAnchors(s, &c, targets[i]);
        VecAppend(s->extraCursors, c);
    }
    InputMergeOverlappingCursors(s);
}

// extend_selection: the moving end follows the offset, and a selection
// dragged back through its anchor turns around.
static void ExtendSelection(CursorSelection* c, int offset) {
    if (c->reversed) {
        c->range.start = offset;
    } else {
        c->range.end = offset;
    }
    if (c->range.end < c->range.start) {
        c->reversed = !c->reversed;
        int t = c->range.start;
        c->range.start = c->range.end;
        c->range.end = t;
    }
}

// select_all_cursors_to: every selection extends to where `f` says, then the
// ones that now overlap merge. The active one goes through select_to, which
// is what keeps a double click's word whole.
template <class F>
static void SelectAllCursorsTo(InputState* s, App* app, Window* win, F f) {
    UndoBreakCoalescing(&s->undo);
    PauseBlink(s, app, win);
    Arena* a = GetTempArena();
    int n = 0;
    CursorSelection* all = AllCursors(a, s, &n);
    auto* targets = (MoveTarget*)Alloc(a, n * (int)sizeof(MoveTarget));
    for (int i = 0; i < n; i++) {
        targets[i] = f(all[i], i == 0);
    }
    InputSelectToWithAffinity(s, app, win, targets[0].offset,
                              targets[0].lineEndAffinity);
    if (targets[0].anchors) {
        s->preferredX = targets[0].preferredX;
        s->preferredColumn = targets[0].preferredColumn;
    }
    int textLen = len(InputValue(s));
    for (int i = 1; i < n; i++) {
        int off = InputCursorBoundary(s, targets[i].offset, Bias::Left);
        CursorSelection c = s->extraCursors[i - 1];
        ExtendSelection(&c, off < 0 ? 0 : (off > textLen ? textLen : off));
        if (c.IsEmpty() || targets[i].anchors) {
            ApplyAnchors(s, &c, targets[i]);
        }
        s->extraCursors[i - 1] = c;
    }
    InputMergeOverlappingCursors(s);
}

// page_up / page_down: how many display rows the box currently shows, which
// is `input_bounds.height / line_height` in Rust. LayoutModeRows is the
// textarea's configured height and is 1 on a code editor, so using it made
// PageDown a one-line move.
static int InputPageLines(const InputState* s) {
    float lineH = s->lastLineH > 0 ? s->lastLineH : kInputLineH;
    float h = s->inputBounds.h > 0 ? s->inputBounds.h : s->viewH;
    if (lineH <= 0) {
        return 1;
    }
    int lines = (int)(h / lineH);
    return lines > 1 ? lines : 1;
}

// move_vertical. With `collapse`, a selection first collapses to its start
// (up) or end (down) and walks from there at that column.
static void MoveVertical(InputState* s, App* app, Window* win, int lines,
                         bool collapse) {
    if (InputIsSingleLine(s)) {
        return;
    }
    Str t = InputValue(s);
    PauseBlink(s, app, win);
    MoveAllCursors(s, app, win, [&](const CursorSelection& c, bool active) {
        CursorSelection from = c;
        if (collapse && !c.IsEmpty()) {
            from.range = SelectionAt(lines < 0 ? c.range.start : c.range.end);
            from.reversed = false;
            from.preferredColumn = -1;
            from.preferredX = -1;
        }
        return WithCursor(s, from, active, [&] {
            return TargetOf(VerticalTargetFor(s, win, lines, t, from.Cursor()));
        });
    });
}

// select_up / select_down: the caret goes where the arrow alone would have
// taken it and the selection follows, rather than swallowing the whole line
// either side of it. Same walk, same aim kept across it — so shift+Down over
// a wrapped line takes one visual row at a time, and holding it and coming
// back leaves the selection where it was.
static void SelectVertical(InputState* s, App* app, Window* win, int lines) {
    if (InputIsSingleLine(s)) {
        return;
    }
    Str t = InputValue(s);
    // vertical_selection_target: with no further visual row, plain movement
    // keeps its column but a selection still reaches the rest of the text on
    // the first or last row. Rust asks only once the field has a layout.
    bool laidOut = s->lastBounds.w > 0;
    SelectAllCursorsTo(s, app, win, [&](const CursorSelection& c, bool active) {
        return WithCursor(s, c, active, [&] {
            VerticalTarget v = VerticalTargetFor(s, win, lines, t, c.Cursor());
            MoveTarget m = TargetOf(v);
            if (laidOut && v.noFurtherRow) {
                m.offset = lines < 0 ? 0 : len(t);
                m.lineEndAffinity = false;
            }
            return m;
        });
    });
    // scroll_to: the moving end of the selection takes the view with it, the
    // way move_to does for the caret.
    InputScrollToCursor(s, lines < 0 ? InputMoveDir::Up : InputMoveDir::Down);
}

// add_cursor_above / add_cursor_below: one more caret a row away from each
// existing one, at its column. A cursor that would not move — on the first
// or last row — or that would land on an existing caret adds nothing.
static void AddCursorVertical(InputState* s, App* app, Window* win, int lines) {
    if (!InputIsMultiLine(s)) {
        return;
    }
    PauseBlink(s, app, win);
    // Changing the cursor set ends the editing gesture that came before it.
    UndoBreakCoalescing(&s->undo);
    Str t = InputValue(s);
    Arena* a = GetTempArena();
    int n = 0;
    CursorSelection* all = AllCursors(a, s, &n);
    int newest = -1;
    for (int i = 0; i < n; i++) {
        const CursorSelection& c = all[i];
        int off = c.Cursor();
        VerticalTarget to = WithCursor(s, c, i == 0, [&] {
            return VerticalTargetFor(s, win, lines, t, off);
        });
        if (to.offset == off || HasCursorAt(s, to.offset)) {
            continue;
        }
        CursorSelection added;
        added.range = SelectionAt(to.offset);
        added.preferredColumn = to.preferredColumn;
        added.preferredX = to.preferredX;
        VecAppend(s->extraCursors, added);
        newest = to.offset;
    }
    // scroll_to the caret added last, so the row it went to comes into view.
    if (newest >= 0) {
        InputScrollToOffset(s, newest, InputMoveDir::None);
    }
    Notify(app, win);
}

// delete_selections: what backspace and forward delete do with several
// cursors. A collapsed cursor takes the character beside it; a selection
// takes itself. The cursors the user had go into the step first, since the
// expanded ranges cannot say where the carets stood.
static void DeleteSelections(InputState* s, App* app, Window* win,
                             EditIntent collapsedIntent, bool forward) {
    if (!InputIsEditable(s)) {
        return;
    }
    Arena* a = GetTempArena();
    int n = 0;
    CursorSelection* cursors = AllCursors(a, s, &n);
    bool allEmpty = true;
    for (int i = 0; i < n; i++) {
        allEmpty = allEmpty && cursors[i].IsEmpty();
    }
    EditIntent intent = allEmpty ? collapsedIntent : EditIntent::Atomic;
    auto* targets =
        (CursorSelection*)Alloc(a, n * (int)sizeof(CursorSelection));
    for (int i = 0; i < n; i++) {
        CursorSelection c = cursors[i];
        if (c.IsEmpty()) {
            int off = c.Cursor();
            int other = WithCursor(s, c, i == 0, [&] {
                return forward ? InputNextBoundary(s, off)
                               : InputPreviousBoundary(s, off);
            });
            c.range =
                Selection{off < other ? off : other, off < other ? other : off};
            c.reversed = false;
        }
        targets[i] = c;
    }
    UndoBeginTransactionWith(&s->undo, intent);
    UndoRecordSelections(&s->undo, cursors, n, cursors, n);
    SetAllCursors(s, targets, n);
    s->undo.hasPendingIntent = true;
    s->undo.pendingIntent = intent;
    InputReplaceTextInRange(s, app, win, nullptr, Str{});
    int nAfter = 0;
    CursorSelection* after = AllCursors(a, s, &nAfter);
    UndoRecordSelections(&s->undo, cursors, n, after, nAfter);
    UndoCommitTransaction(&s->undo);
    PauseBlink(s, app, win);
}

// paste with several cursors: one clipboard line per selection when the
// counts match, in document order. False when they do not, and the whole
// text goes in at every cursor instead.
static bool PasteLinesToCursors(InputState* s, App* app, Window* win,
                                Str text) {
    InputMergeOverlappingCursors(s);
    int count = InputCursorCount(s);
    int lines = 1;
    for (int i = 0; i < len(text); i++) {
        if (text.s[i] == '\n') {
            lines++;
        }
    }
    if (count < 2 || lines != count) {
        return false;
    }
    Arena* a = GetTempArena();
    int n = 0;
    CursorSelection* all = AllCursors(a, s, &n);
    auto* ranges = (Selection*)Alloc(a, n * (int)sizeof(Selection));
    auto* texts = (Str*)Alloc(a, n * (int)sizeof(Str));
    auto* parts = (Str*)Alloc(a, n * (int)sizeof(Str));
    int at = 0;
    for (int k = 0; k < n; k++) {
        int end = at;
        while (end < len(text) && text.s[end] != '\n') {
            end++;
        }
        parts[k] = Str(text.s + at, end - at);
        at = end + 1;
    }
    for (int i = 0; i < n; i++) {
        // The cursor's rank in document order picks its line.
        int rank = 0;
        for (int j = 0; j < n; j++) {
            if (all[j].range.start < all[i].range.start ||
                (all[j].range.start == all[i].range.start && j < i)) {
                rank++;
            }
        }
        ranges[i] = all[i].range;
        texts[i] = parts[rank];
    }
    InputReplaceTextInRanges(s, app, win, ranges, texts, n);
    InputScrollToCursor(s, InputMoveDir::None);
    return true;
}

// restore_selections: the cursors a step recorded, clamped to the text as it
// is now.
static void RestoreCursors(InputState* s, const CursorSelection* sels, int n) {
    Arena* a = GetTempArena();
    int textLen = len(InputValue(s));
    auto* out = (CursorSelection*)Alloc(a, n * (int)sizeof(CursorSelection));
    for (int i = 0; i < n; i++) {
        out[i] = sels[i];
        if (out[i].range.start > textLen) {
            out[i].range.start = textLen;
        }
        if (out[i].range.end > textLen) {
            out[i].range.end = textLen;
        }
    }
    SetAllCursors(s, out, n);
    InputMergeOverlappingCursors(s);
    s->cursorLineEndAffinity = false;
}

// ─── actions ──────────────────────────────────────────────────────────────

static void DeleteRange(InputState* s, App* app, Window* win, int a, int b) {
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    Selection r = {a, b};
    InputReplaceTextInRange(s, app, win, &r, Str{});
    PauseBlink(s, app, win);
}

static void DoCopy(InputState* s, Window* win) {
    if (!InputIsCopyable(s) || !win) {
        return;
    }
    ClipboardSetText(win, SelectedTexts(GetTempArena(), s));
}

static bool TransactionHasTokenDelta(const UndoTransaction* t) {
    if (!t) {
        return false;
    }
    for (int i = 0; i < t->len; i++) {
        if (t->changes[i].tokenDelta) {
            return true;
        }
    }
    return false;
}

static void DoUndo(InputState* s, App* app, Window* win) {
    UndoSetIgnoring(&s->undo, true);
    const UndoTransaction* t = UndoPopUndo(&s->undo);
    if (t && t->len > 0) {
        bool tokenAware = TransactionHasTokenDelta(t);
        if (s->tokens) {
            s->tokens->replaying = tokenAware;
        }
        // The list is applied backwards, so the selection to restore is the
        // one recorded before the first change in it.
        Selection sel = t->changes[0].selBefore;
        for (int i = t->len - 1; i >= 0; i--) {
            Selection r = t->changes[i].newRange;
            InputReplaceTextInRange(s, app, win, &r, t->changes[i].oldText);
            if (tokenAware) {
                ReplayTokens(s, r.start, r.end, len(t->changes[i].oldText),
                             t->changes[i].tokenDelta, true);
            }
        }
        if (t->nSelsBefore > 0) {
            RestoreCursors(s, t->selsBefore, t->nSelsBefore);
        } else {
            s->cursorLineEndAffinity = false;
            s->selectedRange = sel;
            s->selectionReversed = false;
        }
        if (s->tokens) {
            s->tokens->replaying = false;
        }
        AutoClosedRestore(s->autoClosed, t->pairsBefore, t->nPairsBefore);
    }
    UndoSetIgnoring(&s->undo, false);
}

static void DoRedo(InputState* s, App* app, Window* win) {
    UndoSetIgnoring(&s->undo, true);
    const UndoTransaction* t = UndoPopRedo(&s->undo);
    if (t && t->len > 0) {
        bool tokenAware = TransactionHasTokenDelta(t);
        if (s->tokens) {
            s->tokens->replaying = tokenAware;
        }
        Selection sel = t->changes[t->len - 1].selAfter;
        for (int i = 0; i < t->len; i++) {
            Selection r = t->changes[i].oldRange;
            InputReplaceTextInRange(s, app, win, &r, t->changes[i].newText);
            if (tokenAware) {
                ReplayTokens(s, r.start, r.end, len(t->changes[i].newText),
                             t->changes[i].tokenDelta, false);
            }
        }
        if (t->nSelsAfter > 0) {
            RestoreCursors(s, t->selsAfter, t->nSelsAfter);
        } else {
            s->cursorLineEndAffinity = false;
            s->selectedRange = sel;
            s->selectionReversed = false;
        }
        if (s->tokens) {
            s->tokens->replaying = false;
        }
        AutoClosedRestore(s->autoClosed, t->pairsAfter, t->nPairsAfter);
    }
    UndoSetIgnoring(&s->undo, false);
}

// ─── indent ───────────────────────────────────────────────────────────────
//
// indent.rs. Tab and shift-tab inside a field, which Rust binds to
// IndentInline / OutdentInline in the input's key context — innermost, so the
// window's focus ring only gets the keystroke when the field does not want it.

// LayoutMode::is_indentable. An auto-growing field has no blocks to indent;
// a plain textarea and a code editor do.
static bool ModeIsIndentable(const InputState* s) {
    return s->mode.kind == LayoutModeKind::PlainText ||
           s->mode.kind == LayoutModeKind::CodeEditor;
}

// TabSize::to_string. Soft tabs only: the mode carries a width, not the
// hard_tabs flag Rust also has.
static Str TabIndent(const InputState* s) {
    int n = s->mode.tabSize > 0 ? s->mode.tabSize : 2;
    Str tab = AllocStrTemp(n);
    memset(tab.s, ' ', (size_t)n);
    return tab;
}

// A field that has nothing to indent returns false, which is cx.propagate().
static bool IndentReady(const InputState* s) {
    return InputIsMultiLine(s) && ModeIsIndentable(s);
}

enum class IndentDirection {
    Indent,
    Outdent
};

// Whether the line starting at `lineStart` begins with the tab.
static bool LineHasTab(Str t, int lineStart, Str tab) {
    return len(t) - lineStart >= len(tab) &&
           StrEq(Str(t.s + lineStart, len(tab)), tab);
}

static int CountEditsWithStartAtOrBefore(const Selection* edits, int n,
                                         int offset) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (edits[i].start <= offset) {
            c++;
        }
    }
    return c;
}

// compute_block_indent: one edit at the start of every line any selection
// touches, and each selection's ends mapped through the edits before them —
// all of them, other cursors' included. A point inside indentation an outdent
// removes stays on its line.
static void ComputeBlockIndent(Arena* a, const InputState* s,
                               IndentDirection dir, Str tab,
                               const CursorSelection* sels, int n,
                               Selection** outEdits, int* nEdits,
                               CursorSelection** outSels) {
    Str t = InputValue(s);
    int cap = 0;
    for (int i = 0; i < n; i++) {
        cap += RopeOffsetToPoint(t, sels[i].range.end).row -
               RopeOffsetToPoint(t, sels[i].range.start).row + 1;
    }
    int* rows = (int*)Alloc(a, cap * (int)sizeof(int));
    int nRows = 0;
    for (int i = 0; i < n; i++) {
        int r0 = RopeOffsetToPoint(t, sels[i].range.start).row;
        int r1 = RopeOffsetToPoint(t, sels[i].range.end).row;
        for (int r = r0; r <= r1; r++) {
            // Sorted and unique as it fills, the set Rust sorts afterwards.
            int j = nRows;
            while (j > 0 && rows[j - 1] > r) {
                j--;
            }
            if (j > 0 && rows[j - 1] == r) {
                continue;
            }
            memmove(rows + j + 1, rows + j, (size_t)(nRows - j) * sizeof(int));
            rows[j] = r;
            nRows++;
        }
    }
    auto* edits = (Selection*)Alloc(a, nRows * (int)sizeof(Selection));
    int m = 0;
    for (int k = 0; k < nRows; k++) {
        int lineStart = RopeLineStartOffset(t, rows[k]);
        if (dir == IndentDirection::Indent) {
            edits[m++] = Selection{lineStart, lineStart};
        } else if (LineHasTab(t, lineStart, tab)) {
            edits[m++] = Selection{lineStart, lineStart + len(tab)};
        }
    }
    auto mapOffset = [&](int offset) {
        if (dir == IndentDirection::Indent) {
            return offset +
                   CountEditsWithStartAtOrBefore(edits, m, offset) * len(tab);
        }
        int preceding = 0;
        while (preceding < m && edits[preceding].end <= offset) {
            preceding++;
        }
        int partial = 0;
        if (preceding < m && offset > edits[preceding].start) {
            partial = offset - edits[preceding].start;
        }
        return offset - preceding * len(tab) - partial;
    };
    auto* out = (CursorSelection*)Alloc(a, n * (int)sizeof(CursorSelection));
    for (int i = 0; i < n; i++) {
        out[i] = sels[i];
        out[i].range.start = mapOffset(sels[i].range.start);
        out[i].range.end = mapOffset(sels[i].range.end);
        out[i].preferredColumn = -1;
        out[i].preferredX = -1;
    }
    *outEdits = edits;
    *nEdits = m;
    *outSels = out;
}

// compute_inline_indent: a tab at each collapsed caret, or the tab off the
// front of each caret's line if it has one. An edit that would overlap an
// earlier one is dropped; every caret then moves by the surviving edits at
// or before it, so one whose own edit was dropped stays where it is.
static void ComputeInlineIndent(Arena* a, const InputState* s,
                                IndentDirection dir, Str tab,
                                const CursorSelection* sels, int n,
                                Selection** outEdits, int* nEdits,
                                CursorSelection** outSels) {
    Str t = InputValue(s);
    auto* ranges = (Selection*)Alloc(a, n * (int)sizeof(Selection));
    int nRanges = 0;
    for (int i = 0; i < n; i++) {
        int cursor = sels[i].Cursor();
        Selection r;
        if (dir == IndentDirection::Indent) {
            r = Selection{cursor, cursor};
        } else {
            int start =
                RopeLineStartOffset(t, RopeOffsetToPoint(t, cursor).row);
            if (!LineHasTab(t, start, tab)) {
                continue;
            }
            r = Selection{start, start + len(tab)};
        }
        int j = nRanges;
        while (j > 0 && ranges[j - 1].start > r.start) {
            ranges[j] = ranges[j - 1];
            j--;
        }
        ranges[j] = r;
        nRanges++;
    }
    auto* edits = (Selection*)Alloc(a, n * (int)sizeof(Selection));
    int m = 0;
    int lastEnd = -1;
    for (int i = 0; i < nRanges; i++) {
        if (lastEnd >= 0 && ranges[i].start < lastEnd) {
            continue;
        }
        lastEnd = ranges[i].end;
        edits[m++] = ranges[i];
    }
    auto* out = (CursorSelection*)Alloc(a, n * (int)sizeof(CursorSelection));
    for (int i = 0; i < n; i++) {
        int cursor = sels[i].Cursor();
        int at = cursor;
        if (dir == IndentDirection::Indent) {
            at += CountEditsWithStartAtOrBefore(edits, m, cursor) * len(tab);
        } else {
            int removed = 0;
            for (int k = 0; k < m; k++) {
                int e = edits[k].end < cursor ? edits[k].end : cursor;
                int b = edits[k].start < cursor ? edits[k].start : cursor;
                removed += e - b;
            }
            at -= removed;
        }
        out[i] = CursorSelection{};
        out[i].range = SelectionAt(at);
    }
    *outEdits = edits;
    *nEdits = m;
    *outSels = out;
}

// apply_indent: an indent or outdent across every cursor as one batch edit,
// which keeps it a single undo step that puts every cursor back. A selection
// anywhere, or the block pair, moves whole lines; collapsed carets with the
// inline pair get the tab where they stand. A field with nothing to indent
// returns false, which is cx.propagate().
static bool ApplyIndent(InputState* s, App* app, Window* win,
                        IndentDirection dir, bool block) {
    if (!InputIsEditable(s) || !IndentReady(s)) {
        return false;
    }
    Str tab = TabIndent(s);
    Arena* a = GetTempArena();
    int n = 0;
    CursorSelection* before = AllCursors(a, s, &n);
    bool useBlock = block;
    for (int i = 0; i < n; i++) {
        useBlock = useBlock || !before[i].IsEmpty();
    }
    Selection* edits = nullptr;
    int nEdits = 0;
    CursorSelection* after = nullptr;
    if (useBlock) {
        ComputeBlockIndent(a, s, dir, tab, before, n, &edits, &nEdits, &after);
    } else {
        ComputeInlineIndent(a, s, dir, tab, before, n, &edits, &nEdits, &after);
    }
    if (nEdits == 0) {
        return true;
    }
    auto* texts = (Str*)Alloc(a, nEdits * (int)sizeof(Str));
    for (int i = 0; i < nEdits; i++) {
        texts[i] = dir == IndentDirection::Indent ? tab : Str{};
    }
    UndoBeginTransaction(&s->undo);
    s->undo.hasPendingIntent = true;
    s->undo.pendingIntent = EditIntent::Atomic;
    InputReplaceTextInRanges(s, app, win, edits, texts, nEdits);
    SetAllCursors(s, after, n);
    UndoRecordSelections(&s->undo, before, n, after, n);
    UndoCommitTransaction(&s->undo);
    InputScrollToCursor(s, InputMoveDir::None);
    PauseBlink(s, app, win);
    Notify(app, win);
    return true;
}

static Str InputNextLineIndent(InputState* s, App* app, Arena* a,
                               int* caretInText) {
    *caretInText = -1;
    if (s->kind != InputKind::Editor) {
        return StrL("\n");
    }
    Str all = InputValue(s);
    int cursor = InputCursor(s);
    int lineStart = cursor;
    while (lineStart > 0 && all.s[lineStart - 1] != '\n' &&
           all.s[lineStart - 1] != '\r') {
        lineStart--;
    }
    int indentEnd = lineStart;
    while (indentEnd < len(all) &&
           (all.s[indentEnd] == ' ' || all.s[indentEnd] == '\t')) {
        indentEnd++;
    }
    Str indent(all.s + lineStart, indentEnd - lineStart);
    LanguageConfig config = InputLanguageConfig(app, s->highlighter.Language());
    bool code = InputEditingContext(s, app, cursor) == SyntaxContext::Code;
    bool split = false;
    if (s->smartIndent && code && s->selectedRange.IsEmpty()) {
        for (int i = 0; i < config.nBrackets; i++) {
            const BracketPair& pair = config.brackets[i];
            if (pair.open && pair.close && !StrEq(pair.open, pair.close) &&
                InputSliceEq(all, cursor - pair.open.len, pair.open) &&
                InputSliceEq(all, cursor, pair.close)) {
                split = true;
                break;
            }
        }
    }
    bool increase = false;
    if (s->smartIndent && code) {
        Str before(all.s + lineStart, cursor - lineStart);
        before = StrTrimAscii(before);
        if (config.hasIndentationRules && config.indentation.increaseIndent) {
            increase = config.indentation
                           .increaseIndent(config.indentation.data, before);
        } else if (config.hasIndentationRules && config.indentation
                                                     .increasePattern.s) {
            increase =
                IndentPatternMatch(config.indentation.increasePattern, before);
        } else {
            for (int i = 0; i < config.nBrackets; i++) {
                Str open = config.brackets[i].open;
                if (open && len(before) >= len(open) &&
                    InputSliceEq(before, len(before) - len(open), open)) {
                    increase = true;
                    break;
                }
            }
        }
    }
    Str tab = TabIndent(s);
    if (!increase && !split && s->smartIndent && code) {
        int lineEnd = cursor;
        while (lineEnd < len(all) && all.s[lineEnd] != '\n' &&
               all.s[lineEnd] != '\r') {
            lineEnd++;
        }
        Str after(all.s + cursor, lineEnd - cursor);
        bool decrease = false;
        if (config.hasIndentationRules && config.indentation.decreaseIndent) {
            decrease = config.indentation
                           .decreaseIndent(config.indentation.data, after);
        } else if (config.hasIndentationRules && config.indentation
                                                     .decreasePattern.s) {
            decrease =
                IndentPatternMatch(config.indentation.decreasePattern, after);
        }
        if (decrease && len(indent) > 0) {
            if (indent.s[len(indent) - 1] == '\t') {
                indent = Str(indent.s, len(indent) - 1);
            } else {
                int tabN = len(tab);
                int end = len(indent);
                int n = 0;
                while (n < tabN && end > 0 && indent.s[end - 1] == ' ') {
                    end--;
                    n++;
                }
                indent = Str(indent.s, end);
            }
        }
    }
    int innerLen = len(indent) + (increase || split ? len(tab) : 0);
    int total = 1 + innerLen + (split ? 1 + len(indent) : 0);
    char* out = (char*)Alloc(a, total + 1);
    if (!out) {
        return StrL("\n");
    }
    int at = 0;
    out[at++] = '\n';
    if (len(indent) > 0) {
        memcpy(out + at, indent.s, (size_t)len(indent));
        at += len(indent);
    }
    if (increase || split) {
        memcpy(out + at, tab.s, (size_t)len(tab));
        at += len(tab);
    }
    if (split) {
        *caretInText = at;
        out[at++] = '\n';
        if (len(indent) > 0) {
            memcpy(out + at, indent.s, (size_t)len(indent));
            at += len(indent);
        }
    }
    out[at] = 0;
    return Str(out, at);
}

// paste_target: where a paste would go right now — the document as edited so
// far and the selections it would replace. Rust's document_revision is
// docVersion here, which every splice of the text moves.
InputPasteTarget InputPasteTargetOf(const InputState* s) {
    InputPasteTarget t;
    if (!s) {
        return t;
    }
    t.documentRevision = s->docVersion;
    t.active = s->selectedRange;
    t.reversed = s->selectionReversed;
    for (int i = 0; i < s->extraCursors.len; i++) {
        VecAppend(t.extra, s->extraCursors[i].range);
    }
    return t;
}

bool InputPasteTarget::operator==(const InputPasteTarget& o) const {
    if (documentRevision != o.documentRevision ||
        active.start != o.active.start || active.end != o.active.end ||
        reversed != o.reversed || extra.len != o.extra.len) {
        return false;
    }
    for (int i = 0; i < extra.len; i++) {
        if (extra[i].start != o.extra[i].start || extra[i].end != o.extra[i]
                                                                      .end) {
            return false;
        }
    }
    return true;
}

// insert_clipboard: the text the way Paste inserts it — one atomic edit, one
// line per cursor when the counts match on a multi-line field. A clipboard
// without text (an image, say) is left alone rather than replacing the
// selection with nothing.
void InputInsertClipboard(InputState* s, App* app, Window* win,
                          const ClipboardItem& item) {
    if (!s) {
        return;
    }
    Str text = item.text;
    if (len(text) == 0 && item.externalPaths.len > 0) {
        text = item.externalPaths;
    }
    if (len(text) == 0) {
        return;
    }
    // A paste is one atomic edit, never part of a typing run.
    s->undo.hasPendingIntent = true;
    s->undo.pendingIntent = EditIntent::Atomic;
    if (InputIsMultiLine(s) && s->extraCursors.len > 0 &&
        PasteLinesToCursors(s, app, win, text)) {
        return;
    }
    InputReplaceTextInRange(s, app, win, nullptr, text);
}

static void PendingPasteResolved(void*, App* app, Window* win,
                                 const ClipboardItem& item) {
    InputState* s = gPendingPaste.state;
    gPendingPaste.state = nullptr;
    if (!s) {
        return;
    }
    // The read can sit behind a permission prompt for as long as the user
    // likes. If they edited, moved the caret or left the input meanwhile,
    // the paste would land where they no longer mean it to; drop it, as the
    // browser's own paste event only reaches the focused input too.
    bool focused = s->focused && s->focusWin == win;
    if (InputIsEditable(s) && focused &&
        InputPasteTargetOf(s) == gPendingPaste.target) {
        InputInsertClipboard(s, app, win, item);
        AppInvalidate(win);
    }
}

// state.rs's ActivateToken listener: the token the selection is exactly,
// handed to on_token_click as a keyboard click on the box it was drawn in
// (token_activation). Nothing happens without one, or while the field is
// disabled or its tokens are not shown.
static void ActivateSelectedToken(InputState* s, App* app, Window* win) {
    InlineTokenStore* store = s->tokens;
    const Vec<InlineTokenSpan>* spans = InputTokens(s);
    if (!store || !store->click || !spans || s->disabled ||
        !InputTokensVisible(s)) {
        return;
    }
    Selection sel = s->selectedRange;
    if (sel.start > sel.end) {
        sel = {sel.end, sel.start};
    }
    for (int i = 0; i < spans->len; i++) {
        const InlineTokenSpan& span = (*spans)[i];
        if (span.start != sel.start || span.end != sel.end) {
            continue;
        }
        Bounds bounds = {};
        if (!InputRangeToBounds(s, win, {span.start, span.end}, &bounds)) {
            return;
        }
        InlineTokenClickEvent ev = {};
        ev.span = span;
        ev.bounds = bounds;
        ev.click.keyboard = true;
        ev.click.el = bounds;
        Ctx cx = {};
        cx.app = app;
        cx.win = win;
        cx.a = win ? win->frameArena : nullptr;
        // The listener may write the field — a reentrant activation — so
        // nothing of `s` is read after it.
        store->click(&ev, &cx, store->clickUser);
        return;
    }
}

bool InputPerform(InputState* s, App* app, Window* win, InputAction action,
                  bool shift) {
    if (!s) {
        return false;
    }
    // handle_action_for_context_menu: while a menu is up it takes the four
    // keys that drive it before the field does — the host's own popover
    // first, if it drew one, and then the editor's.
    if (InputRouteOverlayAction(s, app, win, action)) {
        return true;
    }
    Str t = InputValue(s);
    switch (action) {
        case InputAction::None:
            return false;

        // Every move and select below runs over all the cursors
        // (move_all_cursors / select_all_cursors_to); the ones that place the
        // caret outright — the document ends, select all — keep only the
        // active one, the way move_to does.
        case InputAction::MoveLeft:
            PauseBlink(s, app, win);
            MoveAllCursors(s, app, win, [&](const CursorSelection& c, bool) {
                return TargetAt(c.IsEmpty()
                                    ? InputPreviousBoundary(s, c.Cursor())
                                    : c.range.start);
            });
            return true;
        case InputAction::MoveRight:
            PauseBlink(s, app, win);
            MoveAllCursors(s, app, win, [&](const CursorSelection& c, bool) {
                return TargetAt(c.IsEmpty() ? InputNextBoundary(s, c.range.end)
                                            : c.range.end);
            });
            return true;
        case InputAction::MoveUp:
            if (InputIsSingleLine(s)) {
                return false;
            }
            MoveVertical(s, app, win, -1, true);
            return true;
        case InputAction::MoveDown:
            if (InputIsSingleLine(s)) {
                return false;
            }
            MoveVertical(s, app, win, 1, true);
            return true;
        case InputAction::MovePageUp:
            if (InputIsSingleLine(s)) {
                return false;
            }
            MoveVertical(s, app, win, -InputPageLines(s), false);
            return true;
        case InputAction::MovePageDown:
            if (InputIsSingleLine(s)) {
                return false;
            }
            MoveVertical(s, app, win, InputPageLines(s), false);
            return true;
        case InputAction::MoveHome:
            PauseBlink(s, app, win);
            MoveAllCursors(s, app, win,
                           [&](const CursorSelection& c, bool active) {
                               return WithCursor(s, c, active, [&] {
                                   return TargetAt(InputStartOfLine(s, win));
                               });
                           });
            return true;
        case InputAction::MoveEnd:
            PauseBlink(s, app, win);
            MoveAllCursors(
                s, app, win, [&](const CursorSelection& c, bool active) {
                    return WithCursor(s, c, active, [&] {
                        return TargetAt(InputEndOfLine(s, win), true);
                    });
                });
            return true;
        case InputAction::MoveToStart:
            InputMoveTo(s, app, win, 0);
            return true;
        case InputAction::MoveToEnd:
            InputMoveTo(s, app, win, len(t));
            return true;
        case InputAction::MoveToPreviousWord:
            MoveAllCursors(s, app, win,
                           [&](const CursorSelection& c, bool active) {
                               return WithCursor(s, c, active, [&] {
                                   return TargetAt(InputPreviousStartOfWord(s));
                               });
                           });
            return true;
        case InputAction::MoveToNextWord:
            MoveAllCursors(s, app, win,
                           [&](const CursorSelection& c, bool active) {
                               return WithCursor(s, c, active, [&] {
                                   return TargetAt(InputNextEndOfWord(s));
                               });
                           });
            return true;

        case InputAction::SelectLeft:
            SelectAllCursorsTo(
                s, app, win, [&](const CursorSelection& c, bool) {
                    return TargetAt(InputPreviousBoundary(s, c.Cursor()));
                });
            return true;
        case InputAction::SelectRight:
            SelectAllCursorsTo(
                s, app, win, [&](const CursorSelection& c, bool) {
                    return TargetAt(InputNextBoundary(s, c.Cursor()));
                });
            return true;
        case InputAction::SelectUp:
            SelectVertical(s, app, win, -1);
            return InputIsMultiLine(s);
        case InputAction::SelectDown:
            SelectVertical(s, app, win, 1);
            return InputIsMultiLine(s);
        case InputAction::SelectAll:
            InputSelectAll(s, app, win);
            return true;
        case InputAction::SelectToStart:
            SelectAllCursorsTo(s, app, win, [&](const CursorSelection&, bool) {
                return TargetAt(0);
            });
            return true;
        case InputAction::SelectToEnd:
            SelectAllCursorsTo(s, app, win, [&](const CursorSelection&, bool) {
                return TargetAt(len(t));
            });
            return true;
        case InputAction::SelectToStartOfLine:
            SelectAllCursorsTo(
                s, app, win, [&](const CursorSelection& c, bool active) {
                    return WithCursor(s, c, active, [&] {
                        return TargetAt(InputStartOfLine(s, win));
                    });
                });
            return true;
        case InputAction::SelectToEndOfLine:
            SelectAllCursorsTo(
                s, app, win, [&](const CursorSelection& c, bool active) {
                    return WithCursor(s, c, active, [&] {
                        return TargetAt(InputEndOfLine(s, win), true);
                    });
                });
            return true;
        case InputAction::SelectToPreviousWordStart:
            SelectAllCursorsTo(
                s, app, win, [&](const CursorSelection& c, bool active) {
                    return WithCursor(s, c, active, [&] {
                        return TargetAt(InputPreviousStartOfWord(s));
                    });
                });
            return true;
        case InputAction::SelectToNextWordEnd:
            SelectAllCursorsTo(s, app, win,
                               [&](const CursorSelection& c, bool active) {
                                   return WithCursor(s, c, active, [&] {
                                       return TargetAt(InputNextEndOfWord(s));
                                   });
                               });
            return true;
        case InputAction::AddCursorAbove:
            AddCursorVertical(s, app, win, -1);
            return InputIsMultiLine(s);
        case InputAction::AddCursorBelow:
            AddCursorVertical(s, app, win, 1);
            return InputIsMultiLine(s);

        case InputAction::Backspace: {
            if (s->extraCursors.len > 0) {
                DeleteSelections(s, app, win, EditIntent::Backspace, false);
                return true;
            }
            Selection paired;
            if (InputAutoCloseDeletion(s, app, &paired)) {
                s->undo.hasPendingIntent = true;
                s->undo.pendingIntent = EditIntent::Atomic;
                InputReplaceTextInRange(s, app, win, &paired, Str{});
                PauseBlink(s, app, win);
                return true;
            }
            EditIntent intent = EditIntent::Atomic;
            if (s->selectedRange.IsEmpty()) {
                InputSelectTo(s, app, win,
                              InputPreviousBoundary(s, InputCursor(s)));
                intent = EditIntent::Backspace;
            }
            s->undo.hasPendingIntent = true;
            s->undo.pendingIntent = intent;
            // The edit puts an open menu away (replace_text_in_ranges): a
            // deletion is no completion trigger, so nothing asks again.
            InputReplaceTextInRange(s, app, win, nullptr, Str{});
            PauseBlink(s, app, win);
            return true;
        }
        case InputAction::Delete: {
            if (s->extraCursors.len > 0) {
                DeleteSelections(s, app, win, EditIntent::DeleteForward, true);
                return true;
            }
            EditIntent intent = EditIntent::Atomic;
            if (s->selectedRange.IsEmpty()) {
                InputSelectTo(s, app, win,
                              InputNextBoundary(s, InputCursor(s)));
                intent = EditIntent::DeleteForward;
            }
            s->undo.hasPendingIntent = true;
            s->undo.pendingIntent = intent;
            InputReplaceTextInRange(s, app, win, nullptr, Str{});
            PauseBlink(s, app, win);
            return true;
        }
        case InputAction::DeleteToBeginningOfLine: {
            if (!s->selectedRange.IsEmpty()) {
                InputReplaceTextInRange(s, app, win, nullptr, Str{});
                PauseBlink(s, app, win);
                return true;
            }
            int offset = InputStartOfLine(s, win);
            if (offset == InputCursor(s) && offset > 0) {
                offset--;
            }
            DeleteRange(s, app, win, offset, InputCursor(s));
            return true;
        }
        case InputAction::DeleteToEndOfLine: {
            if (!s->selectedRange.IsEmpty()) {
                InputReplaceTextInRange(s, app, win, nullptr, Str{});
                PauseBlink(s, app, win);
                return true;
            }
            int offset = InputEndOfLine(s, win);
            if (offset == InputCursor(s)) {
                offset = offset + 1 > len(t) ? len(t) : offset + 1;
            }
            DeleteRange(s, app, win, InputCursor(s), offset);
            return true;
        }
        case InputAction::DeleteToPreviousWordStart: {
            if (!s->selectedRange.IsEmpty()) {
                InputReplaceTextInRange(s, app, win, nullptr, Str{});
                PauseBlink(s, app, win);
                return true;
            }
            DeleteRange(s, app, win, InputPreviousStartOfWord(s),
                        InputCursor(s));
            return true;
        }
        case InputAction::DeleteToNextWordEnd: {
            if (!s->selectedRange.IsEmpty()) {
                InputReplaceTextInRange(s, app, win, nullptr, Str{});
                PauseBlink(s, app, win);
                return true;
            }
            DeleteRange(s, app, win, InputCursor(s), InputNextEndOfWord(s));
            return true;
        }

        case InputAction::Enter: {
            // A multi-line input takes a newline, unless it submits on Enter —
            // then only Shift+Enter does, and a plain Enter is the submit.
            bool insertNewline =
                InputIsMultiLine(s) && (!s->submitOnEnter || shift);
            bool handled = false;
            if (insertNewline) {
                int oldCursor = InputCursor(s);
                int caretInText = -1;
                Str newline =
                    InputNextLineIndent(s, app, GetTempArena(), &caretInText);
                InputReplaceTextInRange(s, app, win, nullptr, newline);
                if (caretInText >= 0) {
                    s->selectedRange = SelectionAt(oldCursor + caretInText);
                    s->selectionReversed = false;
                    UpdatePreferredColumn(s);
                    // record_selections(cursors, cursors): the split leaves
                    // the caret between the pair, and a redo puts it back
                    // there rather than after the inserted text. Rust's
                    // `before` only fills a gap the edit left, and the edit
                    // here already recorded where the caret stood.
                    int n = 0;
                    CursorSelection* cursors =
                        AllCursors(GetTempArena(), s, &n);
                    UndoRecordSelections(&s->undo, nullptr, 0, cursors, n);
                }
                PauseBlink(s, app, win);
                handled = true;
            } else {
                UndoBreakCoalescing(&s->undo);
            }
            InputEvent ev = {};
            ev.kind = InputEventKind::PressEnter;
            ev.shift = shift;
            Emit(s, app, win, ev);
            return handled;
        }
        case InputAction::IndentInline:
            // indent_inline tries the suggestion first: Tab is what accepts
            // one, and only indents when there is none.
            if (InputAcceptInlineCompletion(s, app, win)) {
                return true;
            }
            return ApplyIndent(s, app, win, IndentDirection::Indent, false);
        case InputAction::Indent:
            return ApplyIndent(s, app, win, IndentDirection::Indent, true);
        case InputAction::OutdentInline:
            return ApplyIndent(s, app, win, IndentDirection::Outdent, false);
        case InputAction::Outdent:
            return ApplyIndent(s, app, win, IndentDirection::Outdent, true);

        case InputAction::Escape:
            // "Escape also dismisses a request whose popup has not arrived
            // yet." An open menu took the key before it got here.
            InputHideContextMenu(s);
            // Collapse extra cursors back to the active one first.
            if (s->extraCursors.len > 0) {
                UndoBreakCoalescing(&s->undo);
                InputRemoveExtraCursors(s);
                Notify(app, win);
                return true;
            }
            // "Clear inline completion on escape", and consume the key: the
            // escape said no to the suggestion and nothing else.
            if (InputHasInlineCompletion(s)) {
                InputClearInlineCompletion(s);
                Notify(app, win);
                return true;
            }
            // A suggestion still being debounced is dropped too.
            InputClearInlineCompletion(s);
            // The handles and the edit menu are the topmost surface to
            // dismiss.
            if (win ? InputTouchSelection(s, win, nullptr) : s->touchLive) {
                InputDismissTouchSelection(s, app, win);
                return true;
            }
            if (s->cleanOnEscape) {
                InputClean(s, app, win);
                return true;
            }
            return false;

        case InputAction::Copy:
            DoCopy(s, win);
            return true;
        case InputAction::Cut:
            // A masked value stays where it is: a cut would put it on the
            // clipboard just as a copy would.
            if (!InputIsCopyable(s)) {
                return true;
            }
            DoCopy(s, win);
            s->undo.hasPendingIntent = true;
            s->undo.pendingIntent = EditIntent::Atomic;
            InputReplaceTextInRange(s, app, win, nullptr, Str{});
            return true;
        case InputAction::Paste: {
            if (!win || !InputIsEditable(s)) {
                return true;
            }
            ClipboardItem item = ClipboardGetItem(GetTempArena(), win);
            if (s->pasteHandler &&
                s->pasteHandler(s->pasteHandlerData, item, app, win)) {
                return true;
            }
            if (!item.IsEmpty()) {
                InputInsertClipboard(s, app, win, item);
                return true;
            }
            // The synchronous read is empty on platforms whose clipboard is
            // asynchronous and permission-gated (the web), so fall back to
            // the real read. It has to start here, still inside the user
            // activation that dispatched Paste, or the browser refuses it.
            // Rust's spawn_in holds the state weakly; here the one pending
            // read names the field, and ~InputState forgets it.
            gPendingPaste.state = s;
            gPendingPaste.target = InputPasteTargetOf(s);
            if (!ClipboardReadAsync(win, &PendingPasteResolved, nullptr)) {
                gPendingPaste.state = nullptr;
            }
            return true;
        }
        case InputAction::ToggleCodeActions:
            // on_action_toggle_code_actions. A field with no provider leaves
            // the chord alone, the way Rust propagates it.
            if (!s->codeActionProvider) {
                return false;
            }
            InputToggleCodeActions(s, app, win);
            return true;
        case InputAction::ActivateToken:
            ActivateSelectedToken(s, app, win);
            return true;
        case InputAction::Search:
        case InputAction::Replace:
            // on_action_search / on_action_replace. An input that is not
            // searchable leaves the shortcut to its ancestors, so a custom
            // search UI can take it — Rust's cx.propagate().
            if (!s->searchable) {
                return false;
            }
            InputOpenSearch(s, app, win, action == InputAction::Replace);
            return true;
        case InputAction::Undo:
            DoUndo(s, app, win);
            Notify(app, win);
            return true;
        case InputAction::Redo:
            DoRedo(s, app, win);
            Notify(app, win);
            return true;
    }
    return false;
}

// The keymap state.rs::init installs, folded into one function. GPUI resolves
// a chord against a bound action list; there is one input context here, so a
// switch says the same thing. The `cmd-` bindings are the macOS spelling of
// the `ctrl-` ones below them and land on the same actions.
InputAction InputActionForKey(const InputState* s, int vk, bool shift,
                              bool ctrl, bool alt, bool platform) {
    // The table is `input_keys.cpp` now — state.rs::init, chord for chord —
    // so this is the lookup and nothing else. It used to be a switch over the
    // key code, which had no way to say that ctrl-a and cmd-a are different
    // chords on a Mac and which no application could rebind.
    InputInitKeys();
    KeyChord c = {};
    c.vk = vk;
    c.shift = shift;
    c.ctrl = ctrl;
    c.alt = alt;
    c.platform = platform;
    uint32_t ctx = KeyContextOf(InputContext());
    KeyMatch m = KeymapMatch(c, &ctx, 1);
    InputAction act = InputActionOf(m.action, m.arg);
    (void)s;
    return act;
}

// ─── the find bar ─────────────────────────────────────────────────────────

bool InputIsReplaceable(const InputState* s) {
    return s && s->replaceable && InputIsEditable(s);
}

// sync_search_matcher: recompute the matches if the text changed since the
// last scan.
static void SyncSearchMatcher(InputState* s) {
    SearchMatcherUpdate(&s->search.matcher, InputValue(s));
}

// update_search: keep the matches in step with an edit. A closed search
// skips the scan — it copies and searches the whole document, and nothing
// reads the matches until the search is resumed or navigated, which sync
// first.
void InputUpdateSearch(InputState* s) {
    if (!s || !s->search.active) {
        return;
    }
    SyncSearchMatcher(s);
}

// last_layout.visible_range_offset.start. Rust knows which rows it laid out;
// this tree builds them all, so the first visible one is worked back out of
// how far the field has scrolled. Same answer, one frame stale.
static int FirstVisibleOffset(const InputState* s) {
    Str text = InputValue(s);
    if (s->scrollY <= 0) {
        return 0;
    }
    float lineH = s->lastLineH > 0 ? s->lastLineH : kInputLineH;
    int rows = InputLinesLen(s);
    int row = rows - 1;
    float at = 0;
    for (int i = 0; i < rows; i++) {
        float h = DisplayLineH(s, i, lineH);
        if (at + h > s->scrollY) {
            row = i;
            break;
        }
        at += h;
    }
    row = FoldMapNearestVisibleLine(&s->folds, row);
    return RopeLineStartOffset(text, row);
}

void InputOpenSearch(InputState* s, App* app, Window* win, bool replaceMode) {
    if (!s || !s->searchable) {
        return;
    }
    s->searchActivationRevision++;
    s->search.open = true;
    s->search.active = true;
    s->search.replaceMode = replaceMode && InputIsReplaceable(s);
    // Whatever is selected becomes the query, which is what makes ctrl-f on
    // a word search for that word. An empty selection leaves the last one.
    Str selected = InputSelectedValue(s);
    Str query = len(selected) > 0 ? selected : s->search.query;
    bool queryChanged = !StrEq(query, s->search.query);
    // A retained query resumes its previous occurrence. Only a new query is
    // anchored to the current viewport.
    s->search.anchorOffset = queryChanged ? FirstVisibleOffset(s) : -1;
    SearchSessionSetQuery(&s->search, query, s->search.caseInsensitive);
    SearchMatcherUpdate(&s->search.matcher, InputValue(s));
    if (queryChanged && s->search.anchorOffset >= 0) {
        SearchMatcherCursorByOffset(&s->search.matcher, s->search.anchorOffset);
    }
    Notify(app, win);
}

uint64_t InputSearchActivationRevision(const InputState* s) {
    return s ? s->searchActivationRevision : 0;
}

void InputCloseSearch(InputState* s, App* app, Window* win) {
    if (!s) {
        return;
    }
    s->search.open = false;
    s->search.active = false;
    Notify(app, win);
}

void InputSetSearchReplaceMode(InputState* s, App* app, Window* win, bool on) {
    if (!s) {
        return;
    }
    s->search.replaceMode = on && InputIsReplaceable(s);
    Notify(app, win);
}

void InputSetSearchQuery(InputState* s, App* app, Window* win, Str query,
                         bool insensitive) {
    if (!s) {
        return;
    }
    s->search.active = true;
    SearchSessionSetQuery(&s->search, query, insensitive);
    SearchMatcherUpdate(&s->search.matcher, InputValue(s));
    Notify(app, win);
}

bool InputSearchNext(InputState* s, App* app, Window* win, Selection* out) {
    if (!s) {
        return false;
    }
    SyncSearchMatcher(s);
    Selection r = {};
    if (!SearchMatcherNext(&s->search.matcher, &r)) {
        return false;
    }
    // Match order does not describe viewport direction after a manual
    // scroll. Always allow search navigation to reveal the active match, with
    // the surrounding lines a directed move would keep.
    InputScrollToSearchOffset(s, win, r.end);
    Notify(app, win);
    if (out) {
        *out = r;
    }
    return true;
}

bool InputSearchPrev(InputState* s, App* app, Window* win, Selection* out) {
    if (!s) {
        return false;
    }
    SyncSearchMatcher(s);
    Selection r = {};
    if (!SearchMatcherPrev(&s->search.matcher, &r)) {
        return false;
    }
    // Match order does not describe viewport direction after a manual
    // scroll. Always allow search navigation to reveal the active match.
    InputScrollToSearchOffset(s, win, r.start);
    Notify(app, win);
    if (out) {
        *out = r;
    }
    return true;
}

bool InputSearchReplaceOne(InputState* s, App* app, Window* win, Str with) {
    if (!InputIsReplaceable(s)) {
        return false;
    }
    SyncSearchMatcher(s);
    SearchMatcher* m = &s->search.matcher;
    Selection r = {};
    if (!SearchMatcherCurrent(m, &r)) {
        return false;
    }
    // Where the view goes afterwards is the match *after* this one, so a run
    // of replacements walks down the document rather than standing still.
    Selection next = r;
    SearchMatcherPeek(m, &next);
    bool down = SearchMatcherHasNextWithoutWrap(m);
    if (!down) {
        // The last match: what replaces it leaves the cursor at the top,
        // which is where the shorter list starts again.
        SearchMatcherSetIndex(m, 0);
    }
    SearchMatcherBeginReplacement(m);
    InputScrollToOffset(s, next.end,
                        down ? InputMoveDir::Down : InputMoveDir::None);
    // Rust calls the silent form, which only skips the LSP hook this port
    // does not have.
    InputReplaceTextInRange(s, app, win, &r, with);
    return true;
}

int InputSearchReplaceAll(InputState* s, App* app, Window* win, Str with) {
    if (!InputIsReplaceable(s)) {
        return 0;
    }
    SyncSearchMatcher(s);
    SearchMatcher* m = &s->search.matcher;
    int count = SearchMatcherLen(m);
    if (count == 0) {
        return 0;
    }
    // Back to front, so the offsets ahead of each edit are still good. Rust
    // builds the whole new text and writes it in one go, and so does this —
    // one undo step for the lot.
    Str text = InputValue(s);
    StrBuilder sb;
    int at = 0;
    for (int i = 0; i < count; i++) {
        Selection r = m->ranges[i];
        sb.Append(Str(text.s + at, r.start - at));
        sb.Append(with);
        at = r.end;
    }
    sb.Append(Str(text.s + at, len(text) - at));
    Str whole = sb.TakeStr();
    SearchMatcherBeginReplacement(m);
    Selection all = {0, len(text)};
    InputReplaceTextInRange(s, app, win, &all, whole);
    StrFree(whole);
    InputScrollToOffset(s, 0, InputMoveDir::Down);
    return count;
}

// ─── focus ────────────────────────────────────────────────────────────────

void InputFocus(InputState* s, App* app, Window* win) {
    if (!s || !win) {
        return;
    }
    if (win->input && win->input != s) {
        InputBlur(win->input, app, win);
    }
    if (!s->focus.IsValid()) {
        s->focus = FocusHandleNew(app);
    }
    FocusHandleFocus(win, s->focus);
    s->focused = true;
    s->focusWin = win;
    win->input = s;
    win->prevInput = s;
    BlinkStart(app, win, &s->blink);
    Emit(s, app, win, InputEvent{InputEventKind::Focus});
    Notify(app, win);
}

void InputBlur(InputState* s, App* app, Window* win) {
    if (!s) {
        return;
    }
    // Blurring ends the typing session, so a later undo stops here rather than
    // swallowing everything typed before the field lost focus.
    UndoBreakCoalescing(&s->undo);
    // on_blur's overlays: the hover popover and the hovered definition, the
    // diagnostic popover and any inline suggestion go with the focus. Rust
    // skips all of on_blur while a context menu is open (the menu took the
    // focus); the focus bookkeeping below has to happen here regardless, so
    // only the overlays wait for the menu.
    if (!InputIsContextMenuOpen(s)) {
        InputClearHoverDefinition(s);
        s->hoverText = Str{};
        s->hoverRange = Selection{};
        s->hoverDiagnostic = -1;
        InputClearInlineCompletion(s);
        InputDismissTouchSelection(s, app, win);
    }
    // NumberInput tolerates an out-of-range value while it is being typed —
    // otherwise entering "12" with a minimum of 6 would rewrite the first
    // keystroke to 6. A completed value is clamped only when editing ends.
    if (s->numberHasMin || s->numberHasMax) {
        double value = 0;
        if (NumberParseValue(InputValue(s), &value)) {
            double clamped = value;
            if (s->numberHasMin && clamped < s->numberMin) {
                clamped = s->numberMin;
            }
            if (s->numberHasMax && clamped > s->numberMax) {
                clamped = s->numberMax;
            }
            if (clamped != value) {
                InputSetValue(s, fmt("%g", clamped));
            }
        }
    }
    s->focused = false;
    s->selecting = false;
    s->focusWin = nullptr;
    if (win) {
        BlinkStop(app, win, &s->blink);
        if (FocusHandleIsFocused(win, s->focus)) {
            WindowSetFocusId(win, 0);
        }
        if (win->input == s) {
            win->input = nullptr;
            win->prevInput = nullptr;
        }
    }
    Emit(s, app, win, InputEvent{InputEventKind::Blur});
    Notify(app, win);
}

// index_for_mouse_position. The element recorded the run it painted, so the
// press is measured against that rather than against the whole field. Rust
// asks the display map which visible row the y landed on and then the shaped
// row for the x; the logical line is found by walking the lines' heights in
// visual rows, the visual row inside it by the y left over, and the x is
// measured against that row's own run, past its wrap indent.
int InputIndexForPosition(const InputState* s, PaintCtx* ctx, float x, float y,
                          bool* lineEndAffinity, int* columnsPastLineEnd) {
    if (lineEndAffinity) {
        *lineEndAffinity = false;
    }
    if (columnsPastLineEnd) {
        *columnsPastLineEnd = 0;
    }
    Str t = InputValue(s);
    if (len(t) == 0 || !ctx) {
        return 0;
    }
    const Bounds& b = s->lastBounds;
    if (b.w <= 0 && b.h <= 0 && s->inputBounds.w <= 0 &&
        s->inputBounds.h <= 0 && s->contentBox.h <= 0) {
        return 0;
    }
    float font = s->lastFont > 0 ? s->lastFont : 14.f;
    if (InputIsSingleLine(s)) {
        // A line with chips in it: a press on a chip lands before it on its
        // left half and after it on its right, as in a wrapped row.
        if (s->chipLine) {
            WrapRowSpan r;
            r.hi = len(t);
            return WrapRowIndexAt(ctx, s, t, r, x - b.x);
        }
        if (x <= b.x) {
            return 0;
        }
        return TextIndexAt(ctx, t, font, 0, false, x - b.x, 0, s->lastFontWord);
    }
    float lineH = s->lastLineH > 0 ? s->lastLineH : b.h;
    int rows = InputLinesLen(s);
    // lastBounds is the first row's text, which is only painted while that
    // row is on screen, and contentBox.y is the column's last *painted*
    // origin, which embeds that frame's scrollY — a click after scrolling
    // from line 700 to 900 would still map as if the top were 700, and
    // scroll_to would jump the view back. The clip box does not move; adding
    // the live scrollY is the document.
    float originY = b.y;
    if (s->inputBounds.h > 0) {
        originY = s->inputBounds.y - s->scrollY;
    }
    float docY = y - originY;
    int row = FoldMapNearestVisibleLine(&s->folds, rows - 1);
    // How far down its own line the press landed, which is which of a
    // wrapped line's visual rows it wanted.
    float relY = 0;
    float at = 0;
    for (int i = 0; i < rows; i++) {
        float h = DisplayLineH(s, i, lineH);
        if (h <= 0) {
            continue;
        }
        if (docY < at + h) {
            row = i;
            relY = docY - at;
            if (relY < 0) {
                relY = 0;
            }
            break;
        }
        at += h;
        if (i == rows - 1) {
            relY = h - 1;
        }
    }
    Str line = InputSliceLine(s, row);
    int start = InputLineStartOffset(s, row);
    const int* starts = nullptr;
    float indent = 0;
    int nRows = InputWrapRows(s, row, &starts, &indent);
    int k = lineH > 0 ? (int)(relY / lineH) : 0;
    if (k > nRows - 1) {
        k = nRows - 1;
    }
    WrapRowSpan r = WrapRowSpanOf(starts, nRows, indent, len(line), k, start);
    float localX = x - b.x;
    int local = WrapRowIndexAt(ctx, s, line, r, localX);
    // The end of a row that is not the line's last is where the next row
    // starts; a press past it wants the caret at the end of this one.
    if (lineEndAffinity && local == r.hi && k + 1 < nRows) {
        *lineEndAffinity = true;
    }
    if (columnsPastLineEnd && local == len(line) && k == nRows - 1) {
        float endX = WrapRowX(ctx, s, line, r, local);
        float spaceX = 0, spaceY = 0, spaceH = 0;
        if (localX > endX &&
            TextPointAt(ctx, StrL(" "), font, 0, false, 1, &spaceX, &spaceY,
                        &spaceH, s->lastFontWord, 0, true) &&
            spaceX > 0) {
            float columns = (localX - endX) / spaceX;
            *columnsPastLineEnd = (int)(columns + 0.5f);
        }
    }
    return start + local;
}

/* Port of crates/base/src/input/editor/search.rs — the matcher behind the
   find bar. Rust builds an aho-corasick automaton over one literal pattern,
   which is a substring scan; the fold that comes with
   `ascii_case_insensitive(true)` is ASCII only, so this is too. */

static char FoldAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

// The first occurrence of `needle` in `hay` at or after `from`, or -1.
static int FindFrom(Str hay, Str needle, int from, bool fold) {
    if (len(needle) <= 0 || len(needle) > len(hay)) {
        return -1;
    }
    for (int i = from; i + len(needle) <= len(hay); i++) {
        int k = 0;
        for (; k < len(needle); k++) {
            char a = hay.s[i + k], b = needle.s[k];
            if (fold) {
                a = FoldAscii(a);
                b = FoldAscii(b);
            }
            if (a != b) {
                break;
            }
        }
        if (k == len(needle)) {
            return i;
        }
    }
    return -1;
}

static Str MatcherText(const SearchMatcher* m) {
    return Str(m->text.els, len(m->text));
}

// update_matches. `stream_find_iter` answers leftmost non-overlapping
// matches, which is what stepping past the end of each one comes to.
static void MatcherUpdateMatches(SearchMatcher* m) {
    VecClear(m->ranges);
    m->ranges.len = 0;
    if (len(m->query) > 0) {
        Str hay = MatcherText(m);
        int at = 0;
        for (;;) {
            int lo = FindFrom(hay, m->query, at, m->caseInsensitive);
            if (lo < 0) {
                break;
            }
            VecAppend(m->ranges, Selection{lo, lo + len(m->query)});
            at = lo + len(m->query);
        }
    }
    if (!m->replacing || m->ranges.len == 0) {
        m->current = 0;
    } else if (m->current > m->ranges.len - 1) {
        m->current = m->ranges.len - 1;
    }
    m->replacing = false;
}

void SearchMatcherReset(SearchMatcher* m) {
    m->ranges.len = 0;
    m->text.len = 0;
    StrFree(m->query);
    m->query = {};
    m->current = 0;
    m->replacing = false;
}

void SearchMatcherUpdate(SearchMatcher* m, Str text) {
    // The unchanged text is Rust's early return, and it clears `replacing`
    // on the way out — a replacement that did not move a byte still ends.
    if (StrEq(Str(m->text.els, len(m->text)), text)) {
        m->replacing = false;
        return;
    }
    m->text.len = 0;
    if (len(text) > 0) {
        char* dst = VecAppendBlanks(m->text, len(text));
        if (dst) {
            memcpy(dst, text.s, (size_t)len(text));
        }
    }
    MatcherUpdateMatches(m);
}

void SearchMatcherUpdateQuery(SearchMatcher* m, Str query, bool insensitive) {
    StrFree(m->query);
    m->query = len(query) > 0 ? StrDup(query) : Str{};
    m->caseInsensitive = insensitive;
    MatcherUpdateMatches(m);
}

Str SearchMatcherLabel(Arena* a, const SearchMatcher* m) {
    int ix = SearchMatcherCurrentIndex(m);
    if (ix < 0) {
        return StrDup(a, StrL("0/0"));
    }
    return StrDup(a, fmt("%d/%d", ix + 1, m->ranges.len));
}

void SearchMatcherSetIndex(SearchMatcher* m, int ix) {
    int most = m->ranges.len > 0 ? m->ranges.len - 1 : 0;
    if (ix > most) {
        ix = most;
    }
    m->current = ix < 0 ? 0 : ix;
}

void SearchMatcherBeginReplacement(SearchMatcher* m) {
    m->replacing = true;
}

bool SearchMatcherHasNextWithoutWrap(const SearchMatcher* m) {
    return m->current < (m->ranges.len > 0 ? m->ranges.len - 1 : 0);
}

// next_index: the one after, or back to the top.
static int MatcherNextIndex(const SearchMatcher* m) {
    if (m->ranges.len == 0) {
        return -1;
    }
    return SearchMatcherHasNextWithoutWrap(m) ? m->current + 1 : 0;
}

bool SearchMatcherPeek(const SearchMatcher* m, Selection* out) {
    int ix = MatcherNextIndex(m);
    if (ix < 0) {
        return false;
    }
    *out = m->ranges[ix];
    return true;
}

bool SearchMatcherCurrent(const SearchMatcher* m, Selection* out) {
    if (m->current < 0 || m->current >= m->ranges.len) {
        return false;
    }
    *out = m->ranges[m->current];
    return true;
}

void SearchMatcherCursorByOffset(SearchMatcher* m, int offset) {
    for (int i = 0; i < m->ranges.len; i++) {
        m->current = i;
        if (m->ranges[i].Contains(offset) || m->ranges[i].end >= offset) {
            return;
        }
    }
}

bool SearchMatcherNext(SearchMatcher* m, Selection* out) {
    int ix = MatcherNextIndex(m);
    if (ix < 0) {
        return false;
    }
    m->current = ix;
    *out = m->ranges[ix];
    return true;
}

bool SearchMatcherPrev(SearchMatcher* m, Selection* out) {
    if (m->ranges.len == 0) {
        return false;
    }
    if (m->current == 0) {
        m->current = m->ranges.len;
    }
    m->current--;
    *out = m->ranges[m->current];
    return true;
}

void SearchSessionSetQuery(SearchSession* s, Str query, bool insensitive) {
    // update_query: the same query with the same case rule is the query the
    // matcher already has, and rebuilding it would throw away the occurrence
    // the reader is on. Reopening Find and the styled panel echoing its
    // retained query both come through here.
    if (StrEq(s->query, query) && s->caseInsensitive == insensitive) {
        return;
    }
    StrFree(s->query);
    s->query = len(query) > 0 ? StrDup(query) : Str{};
    s->caseInsensitive = insensitive;
    SearchMatcherUpdateQuery(&s->matcher, s->query, insensitive);
}

void SearchSessionSetReplacement(SearchSession* s, Str replacement) {
    StrFree(s->replacement);
    s->replacement = len(replacement) > 0 ? StrDup(replacement) : Str{};
}

/* Port of crates/base/src/input/base/mask_pattern.rs.

   Rust parses the pattern once into a `Vec<MaskToken>` and keeps it beside the
   pattern string. A token is a pure function of its character, so here the
   pattern string is the whole state and MaskTokenAt reads it — the patterns
   are a dozen characters long and every walk over them is already a walk over
   the text beside it.

   Rust indexes both the pattern and the text by *character*, not by byte, so
   everything below steps codepoints. */

static bool IsAsciiDigit(uint32_t c) {
    return c >= '0' && c <= '9';
}
static bool IsAsciiAlpha(uint32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static bool IsAsciiAlnum(uint32_t c) {
    return IsAsciiDigit(c) || IsAsciiAlpha(c);
}
static bool IsSign(uint32_t c) {
    return c == '+' || c == '-';
}

// MaskToken::is_match. A separator matches only itself.
static bool TokenIsMatch(MaskToken tok, uint32_t sep, uint32_t ch) {
    switch (tok) {
        case MaskToken::Digit:
            return IsAsciiDigit(ch);
        case MaskToken::Letter:
            return IsAsciiAlpha(ch);
        case MaskToken::LetterOrDigit:
            return IsAsciiAlnum(ch);
        case MaskToken::Any:
            return true;
        case MaskToken::Sep:
            return sep == ch;
    }
    return false;
}

// MaskToken::mask_char.
static uint32_t TokenMaskChar(MaskToken tok, uint32_t sep, uint32_t ch) {
    return tok == MaskToken::Sep ? sep : ch;
}

// MaskToken::unmask_char. A separator contributes nothing — Rust's `None`.
static bool TokenUnmaskChar(MaskToken tok) {
    return tok != MaskToken::Sep;
}

static MaskToken TokenOf(uint32_t ch, uint32_t* sep) {
    *sep = 0;
    switch (ch) {
        case '9':
            return MaskToken::Digit;
        case 'A':
            return MaskToken::Letter;
        case '#':
            return MaskToken::LetterOrDigit;
        case '*':
            return MaskToken::Any;
        default:
            *sep = ch;
            return MaskToken::Sep;
    }
}

MaskPattern MaskPatternNew(Str pattern) {
    MaskPattern p = {};
    p.kind = MaskKind::Pattern;
    p.pattern = StrDup(pattern);
    return p;
}

MaskPattern MaskPatternNumber(uint32_t separator) {
    MaskPattern p = {};
    p.kind = MaskKind::Number;
    p.separator = separator;
    p.fraction = -1;
    return p;
}

void MaskPatternFree(MaskPattern* p) {
    if (!p) {
        return;
    }
    StrFree(p->pattern);
    p->pattern = {};
    p->kind = MaskKind::None;
}

bool MaskTokenAt(const MaskPattern& p, int pos, MaskToken* out, uint32_t* sep) {
    *out = MaskToken::Any;
    *sep = 0;
    if (p.kind != MaskKind::Pattern || pos < 0) {
        return false;
    }
    int i = RopeCharIndexToOffset(p.pattern, pos);
    uint32_t ch = 0;
    if (RopeCharAt(p.pattern, i, &ch) == 0) {
        return false;
    }
    *out = TokenOf(ch, sep);
    return true;
}

bool MaskIsNone(const MaskPattern& p) {
    switch (p.kind) {
        case MaskKind::Pattern:
            return len(p.pattern) == 0;
        case MaskKind::Number:
            return false;
        case MaskKind::None:
            return true;
    }
    return true;
}

// The number half of is_valid: at most one dot, at most one sign and only at
// the front, digits or the group separator everywhere else.
static bool NumberIsValid(const MaskPattern& p, Str text) {
    if (len(text) == 0) {
        return true;
    }
    int dot = -1;
    for (int i = 0; i < len(text); i++) {
        if (text.s[i] != '.') {
            continue;
        }
        if (dot >= 0) {
            return false; // only one dot is valid
        }
        dot = i;
    }
    int intEnd = dot < 0 ? len(text) : dot;
    int charPos = 0;
    for (int i = 0; i < intEnd;) {
        uint32_t c = 0;
        i += Utf8At(text, i, &c);
        if (IsSign(c)) {
            // Only one sign, and only at the beginning of the string.
            if (charPos != 0) {
                return false;
            }
        } else if (!IsAsciiDigit(c) && !(p.separator && c == p.separator)) {
            return false;
        }
        charPos++;
    }
    for (int i = intEnd + 1; i < len(text) && dot >= 0;) {
        uint32_t c = 0;
        i += Utf8At(text, i, &c);
        if (!IsAsciiDigit(c) && !(p.separator && c == p.separator)) {
            return false;
        }
    }
    return true;
}

bool MaskIsValid(const MaskPattern& p, Str maskText) {
    if (MaskIsNone(p)) {
        return true;
    }
    if (p.kind == MaskKind::Number) {
        return NumberIsValid(p, maskText);
    }
    // Rust walks the tokens, consuming a text character for each one that
    // matches, and calls the text valid when every character was consumed.
    int ti = 0;
    int tokens = RopeOffsetToCharIndex(p.pattern, len(p.pattern));
    for (int pos = 0; pos < tokens; pos++) {
        if (ti >= len(maskText)) {
            break;
        }
        MaskToken tok = MaskToken::Any;
        uint32_t sep = 0;
        MaskTokenAt(p, pos, &tok, &sep);
        uint32_t ch = 0;
        int n = Utf8At(maskText, ti, &ch);
        if (TokenIsMatch(tok, sep, ch)) {
            ti += n;
        }
    }
    return ti == len(maskText);
}

bool MaskIsValidAt(const MaskPattern& p, uint32_t ch, int pos) {
    if (MaskIsNone(p) || p.kind != MaskKind::Pattern) {
        return true;
    }
    MaskToken tok = MaskToken::Any;
    uint32_t sep = 0;
    if (!MaskTokenAt(p, pos, &tok, &sep)) {
        return false;
    }
    if (TokenIsMatch(tok, sep, ch)) {
        return true;
    }
    // A separator is skipped over: if the token after it takes the character,
    // typing it here is valid and the separator fills itself in.
    if (tok == MaskToken::Sep) {
        MaskToken next = MaskToken::Any;
        uint32_t nextSep = 0;
        if (MaskTokenAt(p, pos + 1, &next, &nextSep) &&
            TokenIsMatch(next, nextSep, ch)) {
            return true;
        }
    }
    return false;
}

// Append one codepoint as UTF-8.
static void PushChar(StrBuilder& sb, uint32_t c) {
    if (c < 0x80) {
        sb.AppendChar((char)c);
    } else if (c < 0x800) {
        sb.AppendChar((char)(0xC0 | (c >> 6)));
        sb.AppendChar((char)(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        sb.AppendChar((char)(0xE0 | (c >> 12)));
        sb.AppendChar((char)(0x80 | ((c >> 6) & 0x3F)));
        sb.AppendChar((char)(0x80 | (c & 0x3F)));
    } else {
        sb.AppendChar((char)(0xF0 | (c >> 18)));
        sb.AppendChar((char)(0x80 | ((c >> 12) & 0x3F)));
        sb.AppendChar((char)(0x80 | ((c >> 6) & 0x3F)));
        sb.AppendChar((char)(0x80 | (c & 0x3F)));
    }
}

// The Number arm of mask(): regroup the integer part in threes, keep at most
// `fraction` decimals, and put the sign back on the front.
static Str MaskNumber(Arena* a, const MaskPattern& p, Str text) {
    if (!p.separator) {
        return StrDup(a, text);
    }
    // Remove the existing group separator, then split on the dot.
    StrBuilder bare;
    int dot = -1;
    for (int i = 0; i < len(text);) {
        uint32_t c = 0;
        int n = Utf8At(text, i, &c);
        if (c != p.separator) {
            if (c == '.' && dot < 0) {
                dot = bare.len;
            }
            for (int k = 0; k < n; k++) {
                bare.AppendChar(text.s[i + k]);
            }
        }
        i += n;
    }
    Str flat = Str(bare.els, bare.len);
    int intEnd = dot < 0 ? len(flat) : dot;

    // Reverse the integer part for easier grouping, taking the sign out first
    // so the result cannot come out as `-,123`.
    uint32_t sign = 0;
    StrBuilder digits;
    for (int i = intEnd - 1; i >= 0; i--) {
        char c = flat.s[i];
        if (IsSign((uint32_t)(unsigned char)c) && !sign) {
            sign = (uint32_t)(unsigned char)c;
            continue;
        }
        digits.AppendChar(c);
    }
    StrBuilder grouped;
    for (int i = 0; i < digits.len; i++) {
        if (i > 0 && i % 3 == 0) {
            PushChar(grouped, p.separator);
        }
        grouped.AppendChar(digits.els[i]);
    }
    StrBuilder out;
    if (sign) {
        PushChar(out, sign);
    }
    for (int i = grouped.len - 1; i >= 0; i--) {
        out.AppendChar(grouped.els[i]);
    }
    if (dot >= 0 && p.fraction != 0) {
        out.AppendChar('.');
        int kept = 0;
        for (int i = intEnd + 1; i < len(flat);) {
            uint32_t c = 0;
            int n = Utf8At(flat, i, &c);
            if (p.fraction >= 0 && kept >= p.fraction) {
                break;
            }
            PushChar(out, c);
            kept++;
            i += n;
        }
    }
    return StrDup(a, Str(out.els, out.len));
}

Str MaskApply(Arena* a, const MaskPattern& p, Str text) {
    if (MaskIsNone(p)) {
        return StrDup(a, text);
    }
    if (p.kind == MaskKind::Number) {
        return MaskNumber(a, p, text);
    }
    StrBuilder out;
    int ti = 0;
    int tokens = RopeOffsetToCharIndex(p.pattern, len(p.pattern));
    for (int pos = 0; pos < tokens; pos++) {
        if (ti >= len(text)) {
            break;
        }
        MaskToken tok = MaskToken::Any;
        uint32_t sep = 0;
        MaskTokenAt(p, pos, &tok, &sep);
        uint32_t ch = 0;
        int n = Utf8At(text, ti, &ch);
        // Break if the expected character does not match.
        if (tok != MaskToken::Sep && !MaskIsValidAt(p, ch, pos)) {
            break;
        }
        uint32_t masked = TokenMaskChar(tok, sep, ch);
        PushChar(out, masked);
        // A separator the text did not supply is filled in without consuming
        // anything, so the next token sees the same character.
        if (ch == masked) {
            ti += n;
        }
    }
    return StrDup(a, Str(out.els, out.len));
}

Str MaskUnapply(Arena* a, const MaskPattern& p, Str maskText) {
    if (p.kind == MaskKind::Number) {
        if (!p.separator) {
            return StrDup(a, maskText);
        }
        StrBuilder out;
        bool hasDot = false;
        for (int i = 0; i < len(maskText);) {
            uint32_t c = 0;
            int n = Utf8At(maskText, i, &c);
            if (c != p.separator) {
                PushChar(out, c);
                hasDot = hasDot || c == '.';
            }
            i += n;
        }
        int len = out.len;
        if (hasDot) {
            while (len > 0 && out.els[len - 1] == '0') {
                len--;
            }
        }
        return StrDup(a, Str(out.els, len));
    }
    if (p.kind == MaskKind::None) {
        return StrDup(a, maskText);
    }
    // Pattern: Rust walks the tokens against the *character* at the same
    // index, so a separator drops out and everything else is kept.
    StrBuilder out;
    int tokens = RopeOffsetToCharIndex(p.pattern, len(p.pattern));
    int ti = 0;
    for (int pos = 0; pos < tokens; pos++) {
        uint32_t ch = 0;
        int n = RopeCharAt(maskText, ti, &ch);
        if (n == 0) {
            break;
        }
        MaskToken tok = MaskToken::Any;
        uint32_t sep = 0;
        MaskTokenAt(p, pos, &tok, &sep);
        if (TokenUnmaskChar(tok)) {
            PushChar(out, ch);
        }
        ti += n;
    }
    return StrDup(a, Str(out.els, out.len));
}

Str MaskPlaceholder(Arena* a, const MaskPattern& p) {
    if (p.kind != MaskKind::Pattern) {
        return {};
    }
    StrBuilder out;
    int tokens = RopeOffsetToCharIndex(p.pattern, len(p.pattern));
    for (int pos = 0; pos < tokens; pos++) {
        MaskToken tok = MaskToken::Any;
        uint32_t sep = 0;
        MaskTokenAt(p, pos, &tok, &sep);
        // MaskToken::placeholder: a separator shows itself, everything else an
        // underscore.
        PushChar(out, tok == MaskToken::Sep ? sep : (uint32_t)'_');
    }
    return StrDup(a, Str(out.els, out.len));
}

// Every mapping is one character to one character with the same UTF-16 length,
// so IME marked-range offsets stay valid across it; the UTF-8 byte length may
// shrink from 3 to 1, which is why the caller must go on using the normalized
// string for its byte offsets.
static uint32_t NormalizeChar(uint32_t ch) {
    if (ch >= 0xFF10 && ch <= 0xFF19) { // full-width digits 0-9
        return ch - 0xFF10 + '0';
    }
    switch (ch) {
        case 0xFF0B: // ＋
            return '+';
        case 0xFF0D: // －
        case 0x2212: // −
            return '-';
        case 0xFF0E: // ．
        case 0x3002: // 。
            return '.';
        case 0xFF0C: // ，
            return ',';
        default:
            return ch;
    }
}

Str NormalizeNumberInput(Arena* a, Str text) {
    bool any = false;
    for (int i = 0; i < len(text) && !any;) {
        uint32_t c = 0;
        i += Utf8At(text, i, &c);
        any = NormalizeChar(c) != c;
    }
    if (!any) {
        return StrDup(a, text); // Rust's Cow::Borrowed
    }
    StrBuilder out;
    for (int i = 0; i < len(text);) {
        uint32_t c = 0;
        i += Utf8At(text, i, &c);
        PushChar(out, NormalizeChar(c));
    }
    return StrDup(a, Str(out.els, out.len));
}

/* Port of crates/base/src/input/base/rope_ext.rs.

   Rust implements `RopeExt` for `ropey::Rope`, whose own API is char-indexed;
   every method there converts to and from byte offsets around a char index.
   The document here is a flat UTF-8 `Str`, so a byte offset is the native
   unit and the conversions run the other way — `char_index_to_offset` and
   `offset_to_char_index` are the two that still have to walk.

   Lines are split on LF alone (`LineType::LF`), so a CRLF document keeps the
   CR at the end of the line: `slice_line` on "World\r\n" is "World\r", and
   `line_end_offset` points at the LF. `word_range` and `word_at` are not
   here — they belong to the language-server hover path, and the word range a
   double click uses is text_boundary.rs's, which is TextWordRangeAt. */

int RopeClipOffset(Str text, int offset, Bias bias) {
    if (offset <= 0 || !text.s) {
        return 0;
    }
    if (offset >= len(text)) {
        return len(text);
    }
    if (bias == Bias::Left) {
        return Utf8ClipLeft(text, offset);
    }
    // Bias::Right: forward to the next boundary instead.
    while (offset < len(text) && ((uint8_t)text.s[offset] & 0xC0) == 0x80) {
        offset++;
    }
    return offset;
}

int RopeCharAt(Str text, int offset, uint32_t* out) {
    *out = 0;
    if (!text.s || offset < 0 || offset >= len(text)) {
        return 0;
    }
    return Utf8At(text, offset, out);
}

int RopeLinesLen(Str text) {
    // len_lines(LineType::LF): one more than the number of LFs, and an empty
    // rope still has one line.
    int n = 1;
    for (int i = 0; i < len(text); i++) {
        if (text.s[i] == '\n') {
            n++;
        }
    }
    return n;
}

int RopeLineStartOffset(Str text, int row) {
    // point_to_offset(Point::new(row, 0)): a row past the end is the end.
    if (row <= 0) {
        return 0;
    }
    int seen = 0;
    for (int i = 0; i < len(text); i++) {
        if (text.s[i] != '\n') {
            continue;
        }
        seen++;
        if (seen == row) {
            return i + 1;
        }
    }
    return len(text);
}

Str RopeSliceLine(Str text, int row) {
    if (row < 0 || row >= RopeLinesLen(text)) {
        return {};
    }
    int a = RopeLineStartOffset(text, row);
    int b = a;
    while (b < len(text) && text.s[b] != '\n') {
        b++;
    }
    return Str(text.s + a, b - a);
}

int RopeLineLen(Str text, int row) {
    return len(RopeSliceLine(text, row));
}

int RopeLineEndOffset(Str text, int row) {
    return RopeLineStartOffset(text, row) + RopeLineLen(text, row);
}

RopePoint RopeOffsetToPoint(Str text, int offset) {
    offset = RopeClipOffset(text, offset, Bias::Left);
    RopePoint p = {};
    int lineStart = 0;
    for (int i = 0; i < offset; i++) {
        if (text.s[i] == '\n') {
            p.row++;
            lineStart = i + 1;
        }
    }
    p.column = offset - lineStart;
    return p;
}

int RopePointToOffset(Str text, RopePoint point) {
    // Rust does not clamp the column: the callers hand it one they measured
    // off a line, and a column past the end is their bug, not this one's.
    if (point.row < 0 || point.row >= RopeLinesLen(text)) {
        return len(text);
    }
    return RopeLineStartOffset(text, point.row) + point.column;
}

// The two UTF-16 conversions the IME and every `*_utf16` range in state.rs go
// through. A character outside the BMP is one UTF-16 surrogate pair, so it
// counts as two.
int RopeOffsetToOffsetUtf16(Str text, int offset) {
    if (offset > len(text)) {
        offset = len(text);
    }
    int n = 0;
    int i = 0;
    while (i < offset) {
        uint32_t c = 0;
        i += Utf8At(text, i, &c);
        n += c >= 0x10000 ? 2 : 1;
    }
    return n;
}

int RopeOffsetUtf16ToOffset(Str text, int offsetUtf16) {
    int n = 0;
    int i = 0;
    while (i < len(text) && n < offsetUtf16) {
        uint32_t c = 0;
        int len = Utf8At(text, i, &c);
        n += c >= 0x10000 ? 2 : 1;
        i += len;
    }
    return i;
}

int RopeCharIndexToOffset(Str text, int charIndex) {
    int i = 0;
    int n = 0;
    while (i < len(text) && n < charIndex) {
        uint32_t c = 0;
        i += Utf8At(text, i, &c);
        n++;
    }
    return i;
}

int RopeOffsetToCharIndex(Str text, int offset) {
    // Clips right, so an offset landing inside a character counts that whole
    // character.
    offset = RopeClipOffset(text, offset, Bias::Right);
    int i = 0;
    int n = 0;
    while (i < offset) {
        uint32_t c = 0;
        i += Utf8At(text, i, &c);
        n++;
    }
    return n;
}

/* Port of crates/base/src/input/base/undo_manager.rs and change.rs.

   Each edit first makes a transaction. Compatible adjacent transactions then
   coalesce until an explicit boundary — a cursor move, a paste, a blur — so a
   run of typing undoes as one step rather than a character at a time. A caller
   that performs one logical edit through several callbacks (IME composition)
   brackets them with UndoBeginTransaction / UndoCommitTransaction.

   Rust clones changes in and out of the stacks; ownership is explicit here, so
   a Change moves and the stack that holds it frees its two strings. */

static const int kMaxUndoTransactions = 1000;
static const int kMaxChangesPerTransaction = 1000;

static void ChangeFree(Change* c) {
    // Not a StrDup2 pair: UndoRecordTransaction can replace newText while
    // keeping oldText, so the two allocations have to be independent.
    StrFree(c->oldText);
    StrFree(c->newText);
    TokenDeltaFree(c->tokenDelta);
    c->oldText = {};
    c->newText = {};
    c->tokenDelta = nullptr;
}

static void TransactionFree(UndoTransaction* t) {
    for (int i = 0; i < t->len; i++) {
        ChangeFree(&t->changes[i]);
    }
    free(t->changes);
    free(t->selsBefore);
    free(t->selsAfter);
    free(t->pairsBefore);
    free(t->pairsAfter);
    *t = {};
}

static void TransactionPush(UndoTransaction* t, Change c) {
    if (t->len == t->cap) {
        int cap = t->cap ? t->cap * 2 : 4;
        auto* p = (Change*)realloc(t->changes, (size_t)cap * sizeof(Change));
        if (!p) {
            ChangeFree(&c);
            return;
        }
        t->changes = p;
        t->cap = cap;
    }
    t->changes[t->len++] = c;
}

// A heap copy of a cursor list, which is what a transaction owns.
static CursorSelection* SelsDup(const CursorSelection* sels, int n) {
    if (!sels || n <= 0) {
        return nullptr;
    }
    auto* p = (CursorSelection*)malloc((size_t)n * sizeof(CursorSelection));
    if (p) {
        memcpy(p, sels, (size_t)n * sizeof(CursorSelection));
    }
    return p;
}

static void StackClear(Vec<UndoTransaction>& v) {
    for (int i = 0; i < len(v); i++) {
        TransactionFree(&v[i]);
    }
    v.len = 0;
}

UndoManager::~UndoManager() {
    StackClear(undos);
    StackClear(redos);
    TransactionFree(&pending);
}

// is_adjacent: whether the change coming in continues the one before it, which
// is what lets a run of the same intent stay one undo step.
static bool IsAdjacent(EditIntent intent, const Change& prev,
                       const Change& cur) {
    auto hasNewline = [](Str s) {
        for (int i = 0; i < len(s); i++) {
            if (s.s[i] == '\n' || s.s[i] == '\r') {
                return true;
            }
        }
        return false;
    };
    switch (intent) {
        case EditIntent::Typing:
            return prev.oldRange.IsEmpty() && cur.oldRange.IsEmpty() &&
                   !hasNewline(prev.newText) && !hasNewline(cur.newText) &&
                   prev.newRange.end == cur.oldRange.start;
        case EditIntent::Backspace:
            return len(prev.newText) == 0 && len(cur.newText) == 0 &&
                   cur.oldRange.end == prev.oldRange.start;
        case EditIntent::DeleteForward:
            return len(prev.newText) == 0 && len(cur.newText) == 0 &&
                   cur.oldRange.start == prev.oldRange.start;
        case EditIntent::Atomic:
            return false;
    }
    return false;
}

// Change::shifted: the same edit as it reads once the text below it has
// moved by `by`. Shallow — the strings are only compared, never freed.
static Change Shifted(Change c, int by) {
    auto shift = [by](int off) {
        int v = off + by;
        return v < 0 ? 0 : v;
    };
    c.oldRange = Selection{shift(c.oldRange.start), shift(c.oldRange.end)};
    c.newRange = Selection{shift(c.newRange.start), shift(c.newRange.end)};
    return c;
}

// is_adjacent_batch: a batch continues the one before it when each of its
// changes continues the matching change of that batch, read where the change
// stands now. A batch is applied highest offset first, so the changes after
// one in the list sit below it and have moved it by their own growth.
static bool IsAdjacentBatch(EditIntent intent, const Change* prev,
                            const Change* cur, int n) {
    int shift = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (!IsAdjacent(intent, Shifted(prev[i], shift), cur[i])) {
            return false;
        }
        shift += len(prev[i].newText) - prev[i].oldText.len;
    }
    return true;
}

static bool RangeSame(Selection a, Selection b) {
    return a.start == b.start && a.end == b.end;
}

// is_noop_batch: a bracket whose changes chain into one another and end where
// they began — a composition typed and then abandoned — puts nothing in the
// history. Rust compares the ranges; the text is compared too, so a bracket
// that swapped one word for another of the same length still records.
static bool IsNoopBatch(const UndoTransaction* t) {
    if (t->len == 0) {
        return true;
    }
    const Change* last = &t->changes[0];
    for (int i = 1; i < t->len; i++) {
        const Change* c = &t->changes[i];
        if (!RangeSame(last->newRange, c->oldRange)) {
            return false;
        }
        last = c;
    }
    return RangeSame(t->changes[0].oldRange, last->newRange) &&
           base::StrEq(t->changes[0].oldText, last->newText);
}

// push_batch. `batch` is owned: its changes and cursor records move onto the
// stack, or into the step before it when the batch continues that step.
static void PushBatch(UndoManager* m, UndoTransaction batch,
                      EditIntent intent) {
    if (batch.len == 0) {
        TransactionFree(&batch);
        return;
    }
    StackClear(m->redos);
    bool canCoalesce = false;
    if (!m->coalescingBoundary && intent != EditIntent::Atomic &&
        m->undos.len > 0) {
        UndoTransaction& prev = m->undos[m->undos.len - 1];
        canCoalesce =
            prev.intent == intent && prev.lastBatchLen == batch.len &&
            prev.recordedChanges + batch.len <= kMaxChangesPerTransaction &&
            IsAdjacentBatch(intent, prev.changes + prev.len - prev.lastBatchLen,
                            batch.changes, batch.len);
    }
    if (canCoalesce) {
        UndoTransaction& prev = m->undos[m->undos.len - 1];
        prev.recordedChanges += batch.len;
        // "Adjacent single-cursor keystrokes form one contiguous insertion.
        // Keep it as one change so undo and redo replay it as a single edit
        // rather than once per keystroke." A change carrying a token delta
        // stays its own, since the delta describes that change alone.
        Change* last = prev.len > 0 ? &prev.changes[prev.len - 1] : nullptr;
        if (intent == EditIntent::Typing && batch.len == 1 && last &&
            !last->tokenDelta && !batch.changes[0].tokenDelta) {
            Change* c = &batch.changes[0];
            StrBuilder sb;
            sb.Append(last->newText);
            sb.Append(c->newText);
            StrFree(last->newText);
            last->newText = sb.TakeStr();
            last->newRange.end = c->newRange.end;
            last->selAfter = c->selAfter;
            ChangeFree(c);
        } else {
            for (int i = 0; i < batch.len; i++) {
                TransactionPush(&prev, batch.changes[i]);
            }
        }
        prev.lastBatchLen = batch.len;
        // The run keeps the cursors it began with and takes the latest after.
        if (batch.selsBefore && !prev.selsBefore) {
            prev.selsBefore = batch.selsBefore;
            prev.nSelsBefore = batch.nSelsBefore;
            batch.selsBefore = nullptr;
            batch.nSelsBefore = 0;
        }
        if (batch.selsAfter) {
            free(prev.selsAfter);
            prev.selsAfter = batch.selsAfter;
            prev.nSelsAfter = batch.nSelsAfter;
            batch.selsAfter = nullptr;
            batch.nSelsAfter = 0;
        }
        if (batch.pairsBefore && !prev.pairsBefore) {
            prev.pairsBefore = batch.pairsBefore;
            prev.nPairsBefore = batch.nPairsBefore;
            batch.pairsBefore = nullptr;
            batch.nPairsBefore = 0;
        }
        if (batch.pairsAfter) {
            free(prev.pairsAfter);
            prev.pairsAfter = batch.pairsAfter;
            prev.nPairsAfter = batch.nPairsAfter;
            batch.pairsAfter = nullptr;
            batch.nPairsAfter = 0;
        }
        // The changes moved; only the array and what was not taken remain.
        batch.len = 0;
        TransactionFree(&batch);
        return;
    }
    if (m->undos.len >= kMaxUndoTransactions) {
        TransactionFree(&m->undos[0]);
        memmove(m->undos.els, m->undos.els + 1,
                (size_t)(m->undos.len - 1) * sizeof(UndoTransaction));
        m->undos.len--;
    }
    batch.intent = intent;
    batch.lastBatchLen = batch.len;
    batch.recordedChanges = batch.len;
    VecAppend(m->undos, batch);
    m->coalescingBoundary = intent == EditIntent::Atomic;
}

void UndoRecordTransaction(UndoManager* m, Change change, EditIntent intent) {
    if (m->ignoring) {
        ChangeFree(&change);
        return;
    }
    // A no-op edit records nothing, but still ends the run before it, so the
    // undo history keeps whatever it already had. An identity-only token
    // association changes no text and still has to be undoable.
    if (RangeSame(change.oldRange, change.newRange) &&
        base::StrEq(change.oldText, change.newText) && !change.tokenDelta) {
        ChangeFree(&change);
        UndoBreakCoalescing(m);
        return;
    }
    if (m->transactionDepth > 0) {
        TransactionPush(&m->pending, change);
        return;
    }
    UndoTransaction t = {};
    TransactionPush(&t, change);
    PushBatch(m, t, intent);
}

void UndoBeginTransactionWith(UndoManager* m, EditIntent intent) {
    m->transactionDepth++;
    if (m->transactionDepth == 1) {
        TransactionFree(&m->pending);
        m->pending.intent = intent;
    }
}

void UndoBeginTransaction(UndoManager* m) {
    UndoBeginTransactionWith(m, EditIntent::Atomic);
}

void UndoCommitTransaction(UndoManager* m) {
    if (m->transactionDepth == 0) {
        return;
    }
    m->transactionDepth--;
    if (m->transactionDepth > 0) {
        return;
    }
    UndoTransaction t = m->pending;
    m->pending = {};
    if (t.len == 0 || IsNoopBatch(&t)) {
        TransactionFree(&t);
        return;
    }
    PushBatch(m, t, t.intent);
}

// commit_all_transactions: close every open bracket, committing what they
// collected.
static void CommitAllTransactions(UndoManager* m) {
    if (m->transactionDepth == 0) {
        return;
    }
    m->transactionDepth = 1;
    UndoCommitTransaction(m);
}

static AutoClosedPairRange* PairsDup(const AutoClosedPairRange* src, int n) {
    if (!src || n <= 0) {
        return nullptr;
    }
    auto* out =
        (AutoClosedPairRange*)malloc((size_t)n * sizeof(AutoClosedPairRange));
    if (!out) {
        return nullptr;
    }
    memcpy(out, src, (size_t)n * sizeof(AutoClosedPairRange));
    return out;
}

static void UndoRecordAutoClosedPairs(UndoManager* m,
                                      const AutoClosedPairRange* before,
                                      int nBefore,
                                      const AutoClosedPairRange* after,
                                      int nAfter) {
    if (m->ignoring) {
        return;
    }
    UndoTransaction* t = nullptr;
    if (m->transactionDepth > 0) {
        t = &m->pending;
    } else if (m->undos.len > 0) {
        t = &m->undos[m->undos.len - 1];
    }
    if (!t) {
        return;
    }
    if (before && nBefore > 0 && !t->pairsBefore) {
        t->pairsBefore = PairsDup(before, nBefore);
        t->nPairsBefore = t->pairsBefore ? nBefore : 0;
    }
    if (after) {
        free(t->pairsAfter);
        t->pairsAfter = PairsDup(after, nAfter);
        t->nPairsAfter = t->pairsAfter ? nAfter : 0;
    } else if (nAfter == 0) {
        free(t->pairsAfter);
        t->pairsAfter = nullptr;
        t->nPairsAfter = 0;
    }
}

void UndoRecordSelections(UndoManager* m, const CursorSelection* before,
                          int nBefore, const CursorSelection* after,
                          int nAfter) {
    if (m->ignoring) {
        return;
    }
    UndoTransaction* t = nullptr;
    if (m->transactionDepth > 0) {
        t = &m->pending;
    } else if (m->undos.len > 0) {
        t = &m->undos[m->undos.len - 1];
    }
    if (!t) {
        return;
    }
    if (before && nBefore > 0 && !t->selsBefore) {
        t->selsBefore = SelsDup(before, nBefore);
        t->nSelsBefore = nBefore;
    }
    if (after && nAfter > 0) {
        free(t->selsAfter);
        t->selsAfter = SelsDup(after, nAfter);
        t->nSelsAfter = nAfter;
    }
}

void UndoBreakCoalescing(UndoManager* m) {
    // While a batch is open the boundary applies to that batch, so never
    // close a bracket the caller still owns.
    m->coalescingBoundary = true;
}

bool UndoIsIgnoring(const UndoManager* m) {
    return m->ignoring;
}

void UndoSetIgnoring(UndoManager* m, bool ignoring) {
    m->ignoring = ignoring;
    if (ignoring) {
        CommitAllTransactions(m);
    }
}

void UndoClear(UndoManager* m) {
    StackClear(m->undos);
    StackClear(m->redos);
    m->transactionDepth = 0;
    TransactionFree(&m->pending);
    m->hasPendingIntent = false;
    m->coalescingBoundary = false;
}

const UndoTransaction* UndoPopUndo(UndoManager* m) {
    CommitAllTransactions(m);
    if (m->undos.len == 0) {
        return nullptr;
    }
    UndoTransaction t = m->undos[m->undos.len - 1];
    m->undos.len--;
    VecAppend(m->redos, t);
    m->coalescingBoundary = true;
    // The caller applies the changes in reverse, which is what Rust's
    // `.iter().rev()` hands it.
    return &m->redos[m->redos.len - 1];
}

const UndoTransaction* UndoPopRedo(UndoManager* m) {
    CommitAllTransactions(m);
    if (m->redos.len == 0) {
        return nullptr;
    }
    UndoTransaction t = m->redos[m->redos.len - 1];
    m->redos.len--;
    VecAppend(m->undos, t);
    m->coalescingBoundary = true;
    return &m->undos[m->undos.len - 1];
}

// ─── touch selection (input/base/touch.rs) ────────────────────────────────

bool InputLastCaretPoint(const InputState* s, Window* win, int offset,
                         Point* out) {
    if (!s || !win || !out) {
        return false;
    }
    PaintCtx* ctx = &win->paint;
    float font = s->lastFont > 0 ? s->lastFont : 14.f;
    float lineH = s->lastLineH;
    if (lineH <= 0) {
        return false;
    }
    Str text = InputValue(s);
    offset = offset < 0 ? 0 : (offset > len(text) ? len(text) : offset);
    if (InputIsSingleLine(s)) {
        const Bounds& b = s->lastBounds;
        if (len(text) == 0) {
            // An empty field draws no run: the caret stands at the row's
            // left edge, centred in the field the way the line would be.
            if (s->inputBounds.w <= 0 && s->inputBounds.h <= 0) {
                return false;
            }
            *out = {s->inputBounds.x,
                    s->inputBounds.y + (s->inputBounds.h - lineH) * 0.5f};
            return true;
        }
        if (b.w <= 0 && b.h <= 0) {
            return false;
        }
        if (s->chipLine) {
            WrapRowSpan r;
            r.hi = len(text);
            float x = WrapRowX(ctx, s, text, r, offset);
            *out = {b.x + x, b.y + (b.h - lineH) * 0.5f};
            return true;
        }
        Str run = text;
        int at = offset;
        if (s->masked) {
            run = MaskedRun(GetTempArena(), text);
            at = MaskedOffset(text, offset);
        }
        float x = 0, y = 0, h = 0;
        if (!TextPointAt(ctx, run, font, 0, false, at, &x, &y, &h,
                         s->lastFontWord)) {
            return false;
        }
        *out = {b.x + x, b.y + y};
        return true;
    }
    // A multi-line field: the row the offset is on, if the last frame built
    // it, measured from where its run landed.
    const InputPaintedRows* pr = LastPaintedRows(s, win);
    if (!pr) {
        return false;
    }
    for (int i = 0; i < pr->geometry.nRows; i++) {
        const RangeDecorationRow& row = pr->geometry.rows[i];
        if (offset < row.start || offset > row.start + row.len || !row.text) {
            continue;
        }
        // A soft-wrap boundary is the end of one visual row and the start of
        // the next; the affinity says which the caret is drawn on.
        bool affinity = s->cursorLineEndAffinity;
        if (offset == row.start + row.len && !row.last && !affinity) {
            continue;
        }
        if (offset == row.start && !row.first && affinity) {
            continue;
        }
        if (row.text->kind != ElKind::Text) {
            // A row with chips in it is a row of fragments, walked the way
            // the hit test walks it.
            WrapRowSpan r;
            r.lo = row.start - row.lineStart;
            r.hi = r.lo + row.len;
            r.lineStart = row.lineStart;
            float x = WrapRowX(ctx, s, Str(text.s + row.lineStart, row.lineLen),
                               r, offset - row.lineStart);
            *out = {row.text->x + x, row.text->y};
            return true;
        }
        Str line = Str(text.s + row.start, row.len);
        float lineMult = lineH / font;
        float x = 0, y = 0, h = 0;
        if (!TextPointAt(ctx, line, font, 0, false, offset - row.start, &x, &y,
                         &h, s->lastFontWord, lineMult, affinity)) {
            return false;
        }
        *out = {row.text->x + x, row.text->y + y};
        return true;
    }
    return false;
}

bool InputRangeToBounds(const InputState* s, Window* win, Selection range,
                        Bounds* out) {
    Point start = {}, end = {};
    if (!s || !out || !InputLastCaretPoint(s, win, range.start, &start) ||
        !InputLastCaretPoint(s, win, range.end, &end)) {
        return false;
    }
    *out = {start.x, start.y, end.x - start.x, end.y + s->lastLineH - start.y};
    return true;
}

// The active selection as the range a touch selection is matched against.
static Selection ActiveTouchRange(const InputState* s) {
    Selection r = s->selectedRange;
    if (r.start > r.end) {
        int t = r.start;
        r.start = r.end;
        r.end = t;
    }
    return r;
}

// retain_touch_selection: the current selection is the one the gesture made.
static void RetainTouchSelection(InputState* s) {
    s->touchLive = true;
    s->touchRange = ActiveTouchRange(s);
}

static void ResetTouchSelection(InputState* s) {
    s->touchLive = false;
    s->touchRange = {};
    s->touchMenuOpen = false;
    s->touchDragging = false;
    s->touchDragEdge = 0;
    s->touchDragOffset = {};
}

bool InputTouchSelection(const InputState* s, Window* win,
                         TouchSelectionSnapshot* out) {
    if (!s || !s->touchLive) {
        return false;
    }
    Selection selection = ActiveTouchRange(s);
    if (selection.start != s->touchRange.start || selection.end != s->touchRange
                                                                       .end) {
        return false;
    }
    float lineH = s->lastLineH;
    if (!win || lineH <= 0) {
        return false;
    }
    Bounds viewport = s->inputBounds;
    // An end scrolled out of the input gets no handle. Its line may not even
    // be laid out; it then stands just outside the viewport on its side,
    // which is all the other end's drag needs to know about it.
    auto caretBox = [&](int offset, float standInY) {
        Point at = {};
        if (InputLastCaretPoint(s, win, offset, &at)) {
            return TouchCaretLineBox(at, lineH);
        }
        return TouchCaretLineBox({viewport.x, standInY}, lineH);
    };
    Bounds start = caretBox(s->touchRange.start, viewport.y - lineH);
    Bounds end = caretBox(s->touchRange.end, viewport.y + viewport.h);
    TouchSelectionSnapshot snapshot =
        TouchSelectionSnapshot::New(start, end)
            .WithEdgeVisible(SelectionEdge::Start,
                             TouchCaretInView(start, viewport))
            .WithEdgeVisible(SelectionEdge::End,
                             TouchCaretInView(end, viewport))
            .WithMenuOpen(s->touchMenuOpen);
    if (s->touchDragging) {
        snapshot = snapshot.WithDragging((SelectionEdge)s->touchDragEdge);
    }
    if (out) {
        *out = snapshot;
    }
    return true;
}

void InputKeepTouchSelection(InputState* s, App* app, Window* win) {
    if (!s) {
        return;
    }
    RetainTouchSelection(s);
    s->touchMenuOpen = true;
    s->touchDragging = false;
    Notify(app, win);
}

void InputDismissTouchSelection(InputState* s, App* app, Window* win) {
    if (!s || !s->touchLive) {
        return;
    }
    ResetTouchSelection(s);
    Notify(app, win);
}

void InputCloseEditMenu(InputState* s, App* app, Window* win) {
    if (!s || !s->touchMenuOpen) {
        return;
    }
    s->touchMenuOpen = false;
    Notify(app, win);
}

bool InputReopenEditMenuAt(InputState* s, App* app, Window* win, Point at) {
    TouchSelectionSnapshot snapshot;
    if (!s || !win || !InputTouchSelection(s, win, &snapshot) ||
        snapshot.IsEmpty()) {
        return false;
    }
    int offset = InputIndexForPosition(s, &win->paint, at.x, at.y);
    Selection selection = ActiveTouchRange(s);
    if (offset <= selection.start || offset >= selection.end) {
        return false;
    }
    s->touchMenuOpen = true;
    Notify(app, win);
    return true;
}

void InputEditMenuOnScroll(InputState* s, App* app, Window* win,
                           TouchPhase phase) {
    if (!s || !s->touchLive) {
        return;
    }
    if (phase == TouchPhase::Ended || phase == TouchPhase::Cancelled) {
        if (!s->touchMenuOpen) {
            s->touchMenuOpen = true;
            Notify(app, win);
        }
        return;
    }
    InputCloseEditMenu(s, app, win);
}

void InputSelectAllFromEditMenu(InputState* s, App* app, Window* win) {
    if (!s) {
        return;
    }
    bool touch = s->touchLive;
    InputSelectAll(s, app, win);
    if (touch) {
        RetainTouchSelection(s);
        s->touchMenuOpen = true;
        Notify(app, win);
    }
}

static bool AllWhitespace(Str text) {
    for (int i = 0; i < len(text); i++) {
        char c = text.s[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f' &&
            c != '\v') {
            return false;
        }
    }
    return true;
}

bool InputOnLongPress(InputState* s, App* app, Window* win,
                      const LongPressEvent& event) {
    if (!s || !win) {
        return false;
    }
    switch (event.phase) {
        case TouchPhase::Started: {
            if (s->disabled) {
                return false;
            }
            if (!s->focused) {
                InputFocus(s, app, win);
            }
            // The input selects on its own; keep the window text selection
            // from starting a drag under it.
            BaseSuppressTextSelection(app);
            UndoBreakCoalescing(&s->undo);
            InputClearInlineCompletion(s);
            ResetTouchSelection(s);

            bool affinity = false;
            int offset =
                InputIndexForPosition(s, &win->paint, event.startPosition.x,
                                      event.startPosition.y, &affinity);
            InputRemoveExtraCursors(s);
            // set_cursor_to: the caret where the finger went down, which is
            // what stays when there is no word there to select.
            s->selectedRange = {offset, offset};
            s->selectionReversed = false;
            InputSelectWord(s, app, win, offset);
            Selection selected = ActiveTouchRange(s);
            bool pressedWord =
                !selected.IsEmpty() && !AllWhitespace(InputSelectedValue(s));
            if (!pressedWord) {
                // Whitespace or an empty field: the press places the caret,
                // and the menu offers Paste and Select All.
                InputMoveToWithAffinity(s, app, win, offset, affinity);
                s->hasSelectedWordRange = false;
            }
            s->selecting = true;
            RetainTouchSelection(s);
            Notify(app, win);
            return true;
        }
        case TouchPhase::Moved: {
            bool affinity = false;
            int offset = InputIndexForPosition(s, &win->paint, event.position.x,
                                               event.position.y, &affinity);
            if (s->hasSelectedWordRange) {
                // The press took a word; the sweep grows it a word at a time.
                InputSelectToWithAffinity(s, app, win, offset, affinity);
            } else {
                // The press placed the caret; the sweep carries it.
                InputMoveToWithAffinity(s, app, win, offset, affinity);
            }
            RetainTouchSelection(s);
            return true;
        }
        case TouchPhase::Ended:
        case TouchPhase::Cancelled:
            s->selecting = false;
            s->hasSelectedWordRange = false;
            if (s->touchLive) {
                s->touchMenuOpen = true;
            }
            Notify(app, win);
            return true;
    }
    return false;
}

void InputBeginEdgeDrag(InputState* s, App* app, Window* win,
                        SelectionEdge edge, Point finger) {
    TouchSelectionSnapshot snapshot;
    if (!s || !InputTouchSelection(s, win, &snapshot)) {
        return;
    }
    UndoBreakCoalescing(&s->undo);
    s->hasSelectedWordRange = false;
    s->selectionReversed = edge == SelectionEdge::Start;
    TouchEdgeDrag drag =
        TouchEdgeDrag::Begin(edge, snapshot.Edge(edge), finger);
    s->touchDragging = true;
    s->touchDragEdge = (uint8_t)drag.edge;
    s->touchDragOffset = drag.offset;
    s->touchMenuOpen = false;
    Notify(app, win);
}

void InputExtendEdgeDragTo(InputState* s, App* app, Window* win,
                           Point position) {
    if (!s->touchDragging || !win) {
        return;
    }
    bool affinity = false;
    int offset = InputIndexForPosition(s, &win->paint, position.x, position.y,
                                       &affinity);
    Selection before = s->selectedRange;
    bool beforeReversed = s->selectionReversed;
    InputSelectToWithAffinity(s, app, win, offset, affinity);
    // A handle never collapses the selection: at the other end it stops, and
    // the finger has to pass that end to swap the two.
    if (s->selectedRange.IsEmpty()) {
        s->selectedRange = before;
        s->selectionReversed = beforeReversed;
        return;
    }
    // Dragging one end past the other swaps them: the selection now runs the
    // other way and the finger holds what became the other handle.
    SelectionEdge edge =
        s->selectionReversed ? SelectionEdge::Start : SelectionEdge::End;
    s->touchDragEdge = (uint8_t)edge;
    RetainTouchSelection(s);
    Notify(app, win);
}

void InputUpdateEdgeDrag(InputState* s, App* app, Window* win, Point finger) {
    if (!s || !s->touchDragging) {
        return;
    }
    TouchEdgeDrag drag;
    drag.edge = (SelectionEdge)s->touchDragEdge;
    drag.offset = s->touchDragOffset;
    Point position = drag.TextPosition(finger);
    InputExtendEdgeDragTo(s, app, win, position);
    if (InputIsSingleLine(s)) {
        return;
    }
    // Past the top or bottom of a multi-line input the content scrolls under
    // the finger. The frame is this tree's auto-scroll clock (see
    // WindowDrawFrame), which re-runs the selection at the last position.
    s->autoScroll.lastDrag = position;
    s->autoScroll.hasLastDrag = true;
    float delta = 0;
    if (AutoScrollComputeDelta(position.y, s->inputBounds, &delta)) {
        s->autoScroll.Set(delta);
        WindowRequestAnimationFrame(win);
    } else {
        s->autoScroll.SetNone();
    }
}

void InputEndEdgeDrag(InputState* s, App* app, Window* win) {
    if (!s || !s->touchDragging) {
        return;
    }
    s->touchDragging = false;
    s->autoScroll.Stop();
    if (s->selectedRange.IsEmpty()) {
        s->selectionReversed = false;
    }
    s->touchMenuOpen = true;
    Notify(app, win);
}

bool InputIsEditMenuOpen(const InputState* s) {
    return s && s->touchMenuOpen;
}

bool InputTouchSelectionRange(const InputState* s, Selection* out) {
    if (!s || !s->touchLive) {
        return false;
    }
    if (out) {
        *out = s->touchRange;
    }
    return true;
}

} // namespace gpui
