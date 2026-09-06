/* Port of crates/base/src/input/ — the text field and the editing engine
   behind it. Rust splits the module across input/input/mod.rs,
   input/textarea/mod.rs and input/base/{state,movement,selection,mode,
   mask_pattern,rope_ext,change,undo_manager}.rs; the directory is one file
   here. blink_cursor.rs stays in gpui/gpui.cpp beside the window timers it
   needs. */

#include "base/input.h"
#include "base/element_ext.h"
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

static float DisplayLineH(const InputState* s, int row, float lineH);
static float DisplayRowDocY(const InputState* s, int row, float lineH);

// One bullet per character, not per byte, for a masked field.
static Str MaskedRun(Arena* a, Str text) {
    int chars = 0;
    for (int i = 0; i < text.len; i++) {
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
    for (int i = 0; i < off && i < text.len; i++) {
        if (((unsigned char)text.s[i] & 0xc0) != 0x80) {
            chars++;
        }
    }
    return chars * 3;
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
    state->lastMono = style.mono;
    Str text = InputValue(state);
    bool masked = style.mask || state->masked;
    // show_cursor: focused, not disabled, and this half of the blink is the
    // lit one.
    bool caret =
        state->focused && !state->disabled && BlinkVisible(cx, state->blink);

    // The row fills its field, so a press to the right of the text still
    // lands on the editor — Rust's InputElement takes the whole content box.
    El* row = Div(a)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->H(kInputLineH)
                  ->Flex1()
                  ->BindInput(state);
    if (style.align == 1) {
        row->W(kFill)->JustifyCenter();
    } else if (style.align == 2) {
        row->W(kFill)->JustifyEnd();
    }

    if (text.len == 0) {
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
    El* el = TextEl(a, run)
                 ->Font(font)
                 ->LineHeight(lineMult)
                 ->Fg(style.foreground)
                 ->BindInput(state);
    // A single-line field is one row, so the whole document is its slice.
    // A masked one is not searched: what it holds is not what it shows.
    if (!masked) {
        int matchAt = 0;
        RowMatchWashes(a, el, style, state, 0, run.len, &matchAt);
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

// Whether the pointer is over the gutter, which is what decides if the
// chevrons show. Rust inserts one hitbox over the whole line-number column
// (fold icons included) at the editor's visible height; the column is the
// same x on every row, so last frame's gutter strip locates it. rowBoxes
// are only filled when the text wraps, so they cannot be the vertical
// extent — without wrap that test never passed and the chevrons stayed
// hidden until a click folded the line.
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
    state->lastMono = style.mono;
    Str text = InputValue(state);
    bool caret =
        state->focused && !state->disabled && BlinkVisible(cx, state->blink);
    int cursor = InputCursor(state);
    Selection sel = state->selectedRange;

    // soft_wrap: a row is as tall as the wrapped text in it rather than one
    // line, so the map of where the rows are cannot be arithmetic.
    bool wrap = state->softWrap;
    El* col = Div(a)->FlexCol()->W(kFill)->BindInput(state);
    col->BoundsOut(&state->contentBox);
    if (text.len == 0) {
        VecClear(state->rowBoxes);
        if (caret) {
            col->Caret(0, style.caret);
        }
        El* ph = TextEl(a, state->placeholder)
                     ->Font(font)
                     ->LineHeight(lineMult)
                     ->Fg(style.mutedForeground);
        if (style.mono) {
            ph->Mono();
        }
        return col->Child(ph);
    }

    int rows = InputLinesLen(state);
    // The scrolled height, which is what scroll_to clamps against. A wrapping
    // editor takes it off the box the rows were laid out in last frame, since
    // nothing here can tell how many times a line will break; until there is
    // one, a line apiece is the estimate.
    state->contentH = (float)rows * lineH;
    if (LayoutModeIsFolding(state->mode)) {
        FoldMapRebuild(&state->folds, rows);
        state->contentH = (float)FoldMapDisplayRowCount(&state->folds) * lineH;
    }
    if (wrap) {
        // Sum laid-out heights. contentBox.h is the last painted column,
        // which includes that frame's scroll and is wrong the moment a
        // spacer is rebuilt.
        float wrapped = DisplayRowDocY(state, rows, lineH);
        if (wrapped > 0) {
            state->contentH = wrapped;
        }
    }
    // The boxes the rows will report into. Sized here, before any of them is
    // built, so the pointers handed out stay put for the frame. The values
    // are last frame's until this frame paints — clearing them would leave
    // every reader with zeros for the length of a frame.
    if (!wrap) {
        VecClear(state->rowBoxes);
    } else if (state->rowBoxes.len != rows) {
        VecClear(state->rowBoxes);
        if (Bounds* slots = VecAppendBlanks(state->rowBoxes, rows)) {
            for (int i = 0; i < rows; i++) {
                slots[i] = Bounds{};
            }
        }
    }
    float numW = 0;
    if (lineNumbers) {
        numW = 12.f + 7.f * (float)(rows >= 100 ? 3 : (rows >= 10 ? 2 : 1));
    }
    // The fold gutter. Rust widens the line-number column by the hitbox and
    // lays the icons into the space it made; the column here is a flex row,
    // so the icons get a cell of their own that is the same width.
    bool folding = lineNumbers && LayoutModeIsFolding(state->mode);
    float foldW = folding ? kFoldIconHitbox : 0.f;
    if (folding) {
        FoldMapRebuild(&state->folds, rows);
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
    if (style.activeLine.a != 0 || folding) {
        caretRow = InputOffsetToPoint(state, cursor).row;
        caretRow = FoldMapNearestVisibleLine(&state->folds, caretRow);
    }
    bool caretFolded =
        folding && caret &&
        FoldMapLineHidden(&state->folds, InputOffsetToPoint(state, cursor).row);
    // A monospace column, for the indent guides. The glyphs are all one width
    // in the family the editor asks for, so one measurement does.
    float colW = 0;
    if (style.indentGuide.a != 0 && style.indentWidth > 0) {
        colW = font * 0.6f;
    }
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
        if (!wrap || state->rowBoxes.len != rows) {
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
        bool secondary =
            inside && !state->selecting && cx->win->mouseModifiers.Secondary();
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
        firstRow < lineStarts.len) {
        Selection vis = {lineStarts[firstRow], endRow < lineStarts.len
                                                   ? lineStarts[endRow]
                                                   : text.len};
        TextSpan* hl = nullptr;
        int nHl = state->highlighter
                      .Styles(vis, &style.highlightStyles, a, &hl);
        if (style.nSpans > 0) {
            // Compose in the arena: every decoration adds at most itself and
            // one split, so the bound is exact.
            int cap = nHl + 2 * style.nSpans;
            auto* buf = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * cap);
            auto* tmp = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * cap);
            if (buf && tmp) {
                if (nHl > 0) {
                    memcpy(buf, hl, (size_t)nHl * sizeof(TextSpan));
                }
                nDocSpans = InputComposeSpans(buf, nHl, style.spans,
                                              style.nSpans, cap, tmp);
                docSpans = buf;
            }
        } else {
            docSpans = hl;
            nDocSpans = nHl;
        }
    }
    for (int row = firstRow; row < endRow; row++) {
        int start = lineStarts[row];
        int lineEnd =
            row + 1 < lineStarts.len ? lineStarts[row + 1] - 1 : text.len;
        Str line = Str(text.s + start, lineEnd - start);
        // A line inside a closed fold is not built at all, which is what
        // makes the rows below it move up. Its box is zeroed rather than left
        // at last frame's, so the hit test and the vertical walk read it as
        // gone the moment the fold closes.
        if (folding && FoldMapLineHidden(&state->folds, row)) {
            if (row < state->rowBoxes.len) {
                state->rowBoxes[row] = Bounds{};
            }
            continue;
        }
        // The spans and matches are in document order and the walk carries on
        // where the last row left off, so a range that does not start at the
        // top has to skip what came before it.
        if (row == firstRow) {
            while (spanAt < nDocSpans && docSpans[spanAt].hi <= start) {
                spanAt++;
            }
            while (matchAt < style.nMatches && style.matches[matchAt]
                                                       .end <= start) {
                matchAt++;
            }
        }
        El* el = TextEl(a, line)->Font(font)->LineHeight(lineMult)->Fg(
            style.foreground);
        if (style.mono) {
            el->Mono();
        }
        // element.rs MAX_HIGHLIGHT_LINE_LENGTH: a line longer than this —
        // minified output, generated code — draws in the default style
        // rather than styled, which is Rust's guard against laying spans
        // over an enormous run. The cursor is not advanced here; the next
        // row's catch-up loop above skips whatever this one left behind.
        const int kMaxHighlightLineLen = 10000;
        // The runs that fall inside this row, rebased onto it. The document's
        // are in order, so the walk carries on where the last row left off.
        if (nDocSpans > 0 && line.len <= kMaxHighlightLineLen) {
            while (spanAt < nDocSpans && docSpans[spanAt].hi <= start) {
                spanAt++;
            }
            int first = spanAt;
            int count = 0;
            while (first + count < nDocSpans &&
                   docSpans[first + count].lo < start + line.len) {
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
                    if (hi > line.len) {
                        hi = line.len;
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
        if (state->diagnostics.len > 0) {
            int nDiag = 0;
            for (int d = 0; d < state->diagnostics.len; d++) {
                const Diagnostic& dg = state->diagnostics[d];
                if (dg.range.end <= start ||
                    dg.range.start >= start + line.len) {
                    continue;
                }
                nDiag++;
            }
            if (nDiag > 0) {
                auto* runs = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * nDiag);
                int n = 0;
                for (int d = 0; d < state->diagnostics.len && runs; d++) {
                    const Diagnostic& dg = state->diagnostics[d];
                    int lo = dg.range.start - start;
                    int hi = dg.range.end - start;
                    if (lo < 0) {
                        lo = 0;
                    }
                    if (hi > line.len) {
                        hi = line.len;
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
        // Rust pushes it as another highlight style over the row; here it is
        // one more underline run, which is the same list the diagnostics use.
        if (state->hoverDef.locations.len > 0 && style.linkText.a != 0) {
            Selection sym = state->hoverDef.symbolRange;
            int lo = sym.start - start;
            int hi = sym.end - start;
            if (lo < 0) {
                lo = 0;
            }
            if (hi > line.len) {
                hi = line.len;
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
                // Where it landed, for the hand cursor. The row's own box is
                // reported after layout, so the x pair is measured against
                // the run and the y comes off the row.
                el->RangeOut(lo, hi, &state->hoverDef.bounds);
            }
        }
        // input/popovers::Popover::trigger_bounds: use the exact shaped range,
        // not the pointer that happened to ask for it. Diagnostic and hover
        // popovers are mutually exclusive, as they are in the source.
        Selection popoverRange = state->hoverRange;
        if (state->hoverDiagnostic >= 0 &&
            state->hoverDiagnostic < state->diagnostics.len) {
            popoverRange = state->diagnostics[state->hoverDiagnostic].range;
        }
        int popoverLo = popoverRange.start - start;
        int popoverHi = popoverRange.end - start;
        if (popoverLo < 0) popoverLo = 0;
        if (popoverHi > line.len) popoverHi = line.len;
        if (popoverHi > popoverLo) {
            if (state->popoverTriggerRange.start != popoverRange.start ||
                state->popoverTriggerRange.end != popoverRange.end) {
                state->popoverTriggerRange = popoverRange;
                state->popoverTriggerBounds = {};
                WindowRequestAnimationFrame(cx->win);
            }
            el->RangeOut(popoverLo, popoverHi, &state->popoverTriggerBounds);
        }
        RowMatchWashes(a, el, style, state, start, line.len, &matchAt);
        if (state->softWrap) {
            // flex_1: the run is bounded by what the gutter leaves, so it
            // breaks at the text column's edge and its second line starts
            // under its first rather than under the line number.
            el->Wrap();
            if (lineNumbers) {
                el->Flex1();
            }
        }
        // The first row is the one the state measures against; every row below
        // it is a whole lastLineH further down.
        if (row == 0) {
            el->BindInput(state);
        }
        int lo = sel.start - start;
        int hi = sel.end - start;
        if (lo < 0) {
            lo = 0;
        }
        if (hi > line.len) {
            hi = line.len;
        }
        if (!sel.IsEmpty() && lo < hi) {
            el->SelRange(lo, hi, style.selection);
        }
        if (caretFolded) {
            if (row == caretRow) {
                el->Caret(0, style.caret);
            }
        } else if (caret && cursor >= start && cursor <= start + line.len) {
            el->Caret(cursor - start, style.caret, 2,
                      state->cursorLineEndAffinity);
            // Where it lands is the anchor a completion menu hangs off.
            el->CaretOut(&state->caretWinX, &state->caretWinY);
        }
        // indent_guides: a hairline every tab stop of the row's own leading
        // whitespace, drawn behind the text. show_whitespaces shares the
        // same underlay: a mid-dot on every space and an arrow on every tab.
        El* guides = nullptr;
        if (colW > 0) {
            int lead = 0;
            while (lead < line.len && line.s[lead] == ' ') {
                lead++;
            }
            int stops = lead / style.indentWidth;
            if (stops > 0) {
                guides = Div(a)->Absolute()->Left(0)->Top(0)->H(kFill);
                for (int g = 0; g < stops; g++) {
                    guides->Child(
                        Div(a)
                            ->Absolute()
                            ->Left(colW * (float)(g * style.indentWidth))
                            ->Top(0)
                            ->W(1)
                            ->H(kFill)
                            ->Bg(style.indentGuide));
                }
            }
        }
        if (state->showWhitespaces) {
            float charW = colW > 0 ? colW : font * 0.6f;
            if (charW > 0) {
                if (!guides) {
                    guides = Div(a)->Absolute()->Left(0)->Top(0)->H(kFill);
                }
                int displayCol = 0;
                Rgba invis = style.mutedForeground;
                for (int i = 0; i < line.len;) {
                    unsigned char c = (unsigned char)line.s[i];
                    if (c == ' ' || c == '\t') {
                        float startX = charW * (float)displayCol;
                        float x = c == ' '
                                      ? startX + charW * 0.5f - font * 0.25f
                                      : startX;
                        if (x < 0) {
                            x = 0;
                        }
                        El* mark = TextEl(a, c == ' ' ? StrL("\xE2\x80\xA2")
                                                      : StrL("\xE2\x86\x92"))
                                       ->Font(c == ' ' ? font * 0.5f : font)
                                       ->Fg(invis);
                        guides->Child(Div(a)
                                          ->Absolute()
                                          ->Left(x)
                                          ->Top(0)
                                          ->H(kFill)
                                          ->ItemsCenter()
                                          ->Child(mark));
                        displayCol++;
                        i++;
                        continue;
                    }
                    if ((c & 0x80) == 0) {
                        i++;
                    } else if ((c & 0xE0) == 0xC0) {
                        i += 2;
                    } else if ((c & 0xF0) == 0xE0) {
                        i += 3;
                    } else {
                        i += 4;
                    }
                    if (i > line.len) {
                        i = line.len;
                    }
                    displayCol++;
                }
            }
        }
        if (!lineNumbers) {
            El* only = el;
            if (guides) {
                only = Div(a)->W(kFill)->Child(guides)->Child(el);
                if (!wrap) {
                    only->H(lineH);
                }
            }
            if (wrap && row < state->rowBoxes.len) {
                only->BoundsOut(&state->rowBoxes[row]);
            }
            col->Child(only);
            continue;
        }
        El* band = Div(a)->FlexRow()->W(kFill)->Gap(8);
        if (wrap) {
            // A wrapped row is as tall as its own text, and its line number
            // sits at the top of it rather than in the middle.
            band->MinH(lineH)->ItemsStart();
            if (row < state->rowBoxes.len) {
                band->BoundsOut(&state->rowBoxes[row]);
            }
        } else {
            band->H(lineH);
        }
        // active_line: the wash under the row the caret is on, gutter and all.
        if (row == caretRow && style.activeLine.a != 0) {
            band->Bg(style.activeLine);
        }
        El* num = TextEl(a, StrDup(a, fmt("%d", row + 1)))
                      ->Font(font - 1)
                      ->LineHeight(lineMult)
                      ->Fg(style.mutedForeground);
        if (style.mono) {
            num->Mono();
        }
        El* numCell = Div(a)->W(numW)->JustifyEnd()->Child(num);
        if (folding) {
            // The line-number column and the fold icons are one hit strip,
            // the way Rust's line_number_hitbox covers both. PathClick on
            // every row so entering the gutter from the text changes hover
            // id and rebuilds — otherwise the editor already owns hover and
            // a move over the numbers would not show the chevrons.
            El* gutter = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->PathClick(
                StrDup(a, fmt("gutter-%d", row)));
            if (!wrap) {
                gutter->H(lineH);
            }
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
        if (guides) {
            El* pane = Div(a)->Flex1()->Child(guides)->Child(el);
            if (!wrap) {
                pane->H(kFill);
            }
            band->Child(pane);
        } else {
            band->Child(el);
        }
        col->Child(band);
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
            textLeft = numW + 8.f + (folding ? foldW + 8.f : 0.f);
        }
        // Where the caret was last painted, in the column's own coordinates.
        float gx = state->caretWinX - state->contentBox.x;
        float gy = state->caretWinY - state->contentBox.y - lineH;
        Str rest = state->inlineCompletion.text;
        for (int line = 0; rest.len > 0 || line == 0; line++) {
            int nl = -1;
            for (int i = 0; i < rest.len; i++) {
                if (rest.s[i] == '\n') {
                    nl = i;
                    break;
                }
            }
            Str one = nl >= 0 ? Str(rest.s, nl) : rest;
            rest = nl >= 0 ? Str(rest.s + nl + 1, rest.len - nl - 1) : Str{};
            if (one.len > 0) {
                El* ghost = Div(a)
                                ->Absolute()
                                ->Left(line == 0 ? gx : textLeft)
                                ->Top(gy + (float)line * lineH)
                                ->H(lineH)
                                ->Bg(style.background);
                El* run = TextEl(a, one)->Font(font)->LineHeight(lineMult)->Fg(
                    ghostFg);
                if (style.mono) {
                    run->Mono();
                }
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
    if (!s || s->text.len <= 0) {
        return {};
    }
    return Str(s->text.els, s->text.len);
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
    while (at < t.len) {
        const char* nl =
            (const char*)memchr(t.s + at, '\n', (size_t)(t.len - at));
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
    if (row >= starts.len) {
        return s->text.len;
    }
    return starts[row];
}

Str InputSliceLine(const InputState* s, int row) {
    const Vec<int>& starts = InputLineStarts(s);
    if (row < 0 || row >= starts.len) {
        return {};
    }
    int a = starts[row];
    int b = row + 1 < starts.len ? starts[row + 1] - 1 : s->text.len;
    return Str(s->text.els + a, b - a);
}

RopePoint InputOffsetToPoint(const InputState* s, int offset) {
    const Vec<int>& starts = InputLineStarts(s);
    offset = RopeClipOffset(InputValue(s), offset, Bias::Left);
    // The last line whose start is at or before the offset.
    int lo = 0;
    int hi = starts.len - 1;
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

InputState::~InputState() {
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
    if (highlighter.drop) {
        highlighter.drop(highlighter.data);
    }
}

static void TextReserve(InputState* s, int want) {
    VecReserve(s->text, want + 1);
}

// Rope::replace, over the flat buffer.
static void TextSplice(InputState* s, int a, int b, Str ins) {
    int len = s->text.len;
    if (a < 0) {
        a = 0;
    }
    if (b > len) {
        b = len;
    }
    if (b < a) {
        b = a;
    }
    int insLen = ins.len > 0 ? ins.len : 0;
    int out = len - (b - a) + insLen;
    TextReserve(s, out);
    if (!s->text.els) {
        return;
    }
    memmove(s->text.els + a + insLen, s->text.els + b, (size_t)(len - b));
    if (insLen > 0) {
        memcpy(s->text.els + a, ins.s, (size_t)insLen);
    }
    s->text.len = out;
    s->text.els[out] = 0;
    s->docVersion++;
    // The envelope InputHighlighter::update is handed. One splice is exact;
    // a second before the last was consumed is more than one envelope can
    // say, so it collapses to the whole-document marker.
    if (s->hasPendingEdit) {
        s->pendingEdit = InputEdit{0, -1, s->text.len};
    } else {
        s->pendingEdit = InputEdit{a, b, a + insLen};
        s->hasPendingEdit = true;
    }
}

static void TextSet(InputState* s, Str v) {
    int n = v.len > 0 ? v.len : 0;
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
    s->pendingEdit = InputEdit{0, -1, n};
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
        (int*)Alloc(GetTempArena(), (int)sizeof(int) * (folded.len + 1));
    int nCovering = 0;
    for (int i = 0; i < folded.len; i++) {
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
    for (int i = 0; i < v.len; i++) {
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
    m->cachedLineCount = lineCount;
    m->needsRebuild = false;
    VecClear(m->visibleLines);
    VecClear(m->lineToDisplayRow);
    // With nothing folded the projection is the identity, and the two vectors
    // are left empty rather than filled with it — every reader below answers
    // from `cachedLineCount` in that case, which is the whole point of the
    // fast path in Rust's rebuild.
    if (m->folded.len == 0) {
        return;
    }
    if (int* rows = VecAppendBlanks(m->lineToDisplayRow, lineCount)) {
        for (int i = 0; i < lineCount; i++) {
            rows[i] = -1;
        }
    }
    // Which lines a closed fold hides: the ones *between* its ends. Both the
    // line the fold starts on and the one it ends on stay on screen, so a
    // folded block reads as its opening line and its closing brace.
    for (int line = 0; line < lineCount; line++) {
        bool hidden = false;
        for (int i = 0; i < m->folded.len; i++) {
            const FoldRange& f = m->folded[i];
            if (line > f.startLine && line < f.endLine) {
                hidden = true;
                break;
            }
        }
        if (hidden) {
            continue;
        }
        m->lineToDisplayRow[line] = m->visibleLines.len;
        VecAppend(m->visibleLines, line);
    }
}

int FoldMapDisplayRowCount(const FoldMap* m) {
    if (!m || m->folded.len == 0) {
        return m ? m->cachedLineCount : 0;
    }
    return m->visibleLines.len;
}

int FoldMapDisplayRow(const FoldMap* m, int line) {
    if (!m || m->folded.len == 0) {
        return (m && line >= 0 && line < m->cachedLineCount) ? line : -1;
    }
    if (line < 0 || line >= m->lineToDisplayRow.len) {
        return -1;
    }
    return m->lineToDisplayRow[line];
}

int FoldMapLineAt(const FoldMap* m, int displayRow) {
    if (!m || m->folded.len == 0) {
        return (m && displayRow >= 0 && displayRow < m->cachedLineCount)
                   ? displayRow
                   : -1;
    }
    if (displayRow < 0 || displayRow >= m->visibleLines.len) {
        return -1;
    }
    return m->visibleLines[displayRow];
}

bool FoldMapLineHidden(const FoldMap* m, int line) {
    return m && m->folded.len > 0 && FoldMapDisplayRow(m, line) < 0;
}

int FoldMapNearestVisibleLine(const FoldMap* m, int line) {
    if (!FoldMapLineHidden(m, line)) {
        return line;
    }
    // Hidden means something above it is folded, so the line the fold starts
    // on is both visible and the row the hidden text now reads as.
    for (int i = line - 1; i >= 0; i--) {
        if (!FoldMapLineHidden(m, i)) {
            return i;
        }
    }
    return 0;
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

RopePoint InputCursorPosition(const InputState* s) {
    return InputOffsetToPoint(s, InputCursor(s));
}

Str InputSelectedValue(const InputState* s) {
    Str t = InputValue(s);
    Selection r = s->selectedRange;
    if (r.IsEmpty() || r.start < 0 || r.end > t.len) {
        return {};
    }
    return Str(t.s + r.start, r.end - r.start);
}

Str InputUnmaskValue(Arena* a, const InputState* s) {
    return MaskUnapply(a, s->maskPattern, InputValue(s));
}

int InputPreviousBoundary(const InputState* s, int offset) {
    Str t = InputValue(s);
    int off = RopeClipOffset(t, offset > 0 ? offset - 1 : 0, Bias::Left);
    uint32_t c = 0;
    if (RopeCharAt(t, off, &c) && c == '\r' && off > 0) {
        off--;
    }
    return off;
}

int InputNextBoundary(const InputState* s, int offset) {
    Str t = InputValue(s);
    int off = RopeClipOffset(t, offset + 1, Bias::Right);
    uint32_t c = 0;
    if (RopeCharAt(t, off, &c) && c == '\r' && off < t.len) {
        off++;
    }
    return off;
}

// The visual row the caret is on, as a range of the logical line holding it.
// Rust reads it straight off `display_map.line(row).wrapped_lines[wrap_point
// .local_row]`; the wrap here belongs to the shaped run rather than to a map
// beside it, so the row is found by measuring where the caret landed and
// asking the run what sits at either end of the row it landed on. False when
// there is nothing laid out to measure against, which is what leaves Home and
// End on the logical line.
static bool WrappedRowOfCaret(const InputState* s, Window* win, Str line,
                              int rel, int* outLo, int* outHi) {
    // `soft_wrap && is_code_editor()`: a plain textarea keeps the logical
    // line even when it wraps, which is what Rust gates this on.
    if (!win || !s->softWrap || s->kind != InputKind::Editor || line.len == 0) {
        return false;
    }
    PaintCtx* ctx = &win->paint;
    float maxW = s->lastBounds.w;
    float font = s->lastFont;
    float lineH = s->lastLineH > 0 ? s->lastLineH : kInputLineH;
    if (maxW <= 0 || font <= 0 || lineH <= 0) {
        return false;
    }
    float lineMult = lineH / font;
    float cx = 0, cy = 0, ch = lineH;
    if (!TextPointAt(ctx, line, font, maxW, true, rel, &cx, &cy, &ch,
                     s->lastMono, lineMult, s->cursorLineEndAffinity)) {
        return false;
    }
    // The middle of the row rather than its top edge, for the same reason the
    // vertical walk aims there: a hit test exactly on the boundary between
    // two rows could answer either.
    float mid = cy + ch * 0.5f;
    int lo =
        TextIndexAt(ctx, line, font, maxW, true, 0, mid, s->lastMono, lineMult);
    // Past the right edge of the box, which lands on the last character the
    // row holds however far the run reaches.
    int hi = TextIndexAt(ctx, line, font, maxW, true, maxW + font, mid,
                         s->lastMono, lineMult);
    if (lo > rel || hi < rel) {
        return false;
    }
    *outLo = lo;
    *outHi = hi;
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
    if (WrappedRowOfCaret(s, win, RopeSliceLine(t, row), cursor - start, &lo,
                          &hi) &&
        cursor != start + lo) {
        return start + lo;
    }
    return start;
}

int InputEndOfLine(const InputState* s, Window* win) {
    Str t = InputValue(s);
    if (InputIsSingleLine(s)) {
        return t.len;
    }
    int cursor = InputCursor(s);
    int row = RopeOffsetToPoint(t, cursor).row;
    int start = RopeLineStartOffset(t, row);
    int lo = 0, hi = 0;
    if (WrappedRowOfCaret(s, win, RopeSliceLine(t, row), cursor - start, &lo,
                          &hi) &&
        cursor != start + hi) {
        return start + hi;
    }
    return RopeLineEndOffset(t, row);
}

// previous_start_of_word / next_end_of_word. Rust asks
// unicode-segmentation for the word bounds and takes the nearest one whose
// text is not all whitespace; the same answer falls out of walking the
// character classes text_boundary.rs already sorts characters into.
int InputPreviousStartOfWord(const InputState* s) {
    if (s->masked) {
        // Every character shows as the same bullet, so there are no word
        // boundaries on screen to move or delete by: the word is the whole
        // of it.
        return 0;
    }
    Str t = InputValue(s);
    int off = RopeClipOffset(t, s->selectedRange.start, Bias::Left);
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
    return off;
}

int InputNextEndOfWord(const InputState* s) {
    Str t = InputValue(s);
    if (s->masked) {
        // See InputPreviousStartOfWord.
        return t.len;
    }
    int off = RopeClipOffset(t, InputCursor(s), Bias::Left);
    while (off < t.len) {
        uint32_t c = 0;
        int n = Utf8At(t, off, &c);
        CharKind k = CharKindOf(c);
        if (k != CharKind::Whitespace && k != CharKind::Newline) {
            break;
        }
        off += n;
    }
    if (off >= t.len) {
        return t.len;
    }
    uint32_t first = 0;
    Utf8At(t, off, &first);
    CharKind kind = CharKindOf(first);
    while (off < t.len) {
        uint32_t c = 0;
        int n = Utf8At(t, off, &c);
        if (CharKindOf(c) != kind) {
            break;
        }
        off += n;
    }
    return off;
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
        bool surrounding = dir != InputMoveDir::None &&
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

void InputScrollToOffset(InputState* s, int offset, InputMoveDir dir) {
    if (!s) {
        return;
    }
    float lineH = s->lastLineH > 0 ? s->lastLineH : kInputLineH;
    int row = RopeOffsetToPoint(InputValue(s), offset).row;
    row = FoldMapNearestVisibleLine(&s->folds, row);
    float y = DisplayRowDocY(s, row, lineH);
    InputScrollToCaret(s, -1, y, dir);
}

void InputMoveToWithAffinity(InputState* s, App* app, Window* win, int offset,
                             bool lineEndAffinity) {
    UndoBreakCoalescing(&s->undo);
    Str t = InputValue(s);
    if (offset < 0) {
        offset = 0;
    }
    if (offset > t.len) {
        offset = t.len;
    }
    s->cursorLineEndAffinity = lineEndAffinity;
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
    Str t = InputValue(s);
    if (offset < 0) {
        offset = 0;
    }
    if (offset > t.len) {
        offset = t.len;
    }
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
    s->cursorLineEndAffinity = false;
    s->selectedRange = Selection{0, InputValue(s).len};
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
    if (!TextWordRangeAt(InputValue(s), offset, &a, &b)) {
        return;
    }
    UndoBreakCoalescing(&s->undo);
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
    s->cursorLineEndAffinity = false;
    s->selectedRange = Selection{a, b};
    s->selectionReversed = false;
    s->hasSelectedWordRange = false;
    Notify(app, win);
}

// ─── the edit path ────────────────────────────────────────────────────────

// is_valid_input: the validator, the mask, and (in Rust) a regex we have no
// engine for.
static bool IsValidInput(const InputState* s, Str text) {
    if (text.len == 0) {
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
    for (int i = 0; i < out.len && !hasBreak; i++) {
        hasBreak = out.s[i] == '\n' || out.s[i] == '\r';
    }
    if (!hasBreak) {
        return out;
    }
    char* buf = (char*)Alloc(a, out.len + 1);
    int n = 0;
    for (int i = 0; i < out.len; i++) {
        if (out.s[i] != '\n' && out.s[i] != '\r') {
            buf[n++] = out.s[i];
        }
    }
    buf[n] = 0;
    return Str(buf, n);
}

// push_history. `oldAll` is the whole document as it was before the splice;
// `range` indexes into it.
static void PushHistory(InputState* s, Str oldAll, Selection range, Str newText,
                        bool hasIntent, EditIntent requested,
                        Selection selBefore, const Selection* selAfter) {
    if (UndoIsIgnoring(&s->undo)) {
        return;
    }
    Selection r = {RopeClipOffset(oldAll, range.start, Bias::Left),
                   RopeClipOffset(oldAll, range.end, Bias::Right)};
    if (r.end < r.start) {
        r.end = r.start;
    }
    Str oldText = Str(oldAll.s + r.start, r.end - r.start);
    Selection newRange = {r.start, r.start + newText.len};

    EditIntent intent = requested;
    if (!hasIntent) {
        bool typed = r.IsEmpty() && oldText.len == 0 && newText.len > 0;
        for (int i = 0; typed && i < newText.len; i++) {
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
    UndoRecordTransaction(&s->undo, c, intent);
}

bool InputReplaceTextInRange(InputState* s, App* app, Window* win,
                             const Selection* range, Str newText) {
    bool hasIntent = s->undo.hasPendingIntent;
    EditIntent requested = s->undo.pendingIntent;
    s->undo.hasPendingIntent = false;
    if (!InputIsEditable(s)) {
        return false;
    }
    Selection selBefore = s->selectedRange;
    if (win && BlinkVisible(app, s->blink)) {
        PauseBlink(s, app, win);
    }

    Arena* tmp = GetTempArena();
    Str text = NormalizeInput(tmp, s, newText);
    // A commit with no range of its own replaces whatever the input method
    // had provisionally put in, not the selection: the marked text is what
    // the candidate was standing in for.
    Selection r = range           ? *range
                  : s->imeMarking ? s->imeMarked
                                  : s->selectedRange;
    Str before = InputValue(s);
    if (r.start < 0) {
        r.start = 0;
    }
    if (r.end > before.len) {
        r.end = before.len;
    }
    if (r.end < r.start) {
        r.end = r.start;
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
        for (int i = 0; i < text.len; i++) {
            if (text.s[i] == '\n') {
                added++;
            }
        }
        FoldMapAdjustForEdit(&s->folds, editStartLine, editEndLine,
                             added - removed);
    }

    TextSplice(s, r.start, r.end, text);
    int newOffset = r.start + text.len;
    if (newOffset > s->text.len) {
        newOffset = s->text.len;
    }
    bool maskChanged = false;

    if (InputIsSingleLine(s)) {
        Str pending = InputValue(s);
        // Only reject the edit if the old text was valid, so a default_value
        // that does not conform cannot trap the field: the user can still edit
        // their way out of it.
        if (!IsValidInput(s, pending) && IsValidInput(s, oldAll)) {
            TextSet(s, oldAll);
            return false;
        }
        if (!MaskIsNone(s->maskPattern)) {
            Str maskText = MaskApply(tmp, s->maskPattern, pending);
            maskChanged = !base::StrEq(maskText, pending);
            int grown = text.len + maskText.len - pending.len;
            if (grown < 0) {
                grown = 0;
            }
            TextSet(s, maskText);
            newOffset = r.start + grown;
            if (newOffset > maskText.len) {
                newOffset = maskText.len;
            }
        }
    }

    if (maskChanged) {
        // Masking rewrites the whole document, so a segment-based entry no
        // longer matches it — record a whole-document change instead, and
        // undo/redo can restore the text exactly.
        Selection after = SelectionAt(newOffset);
        PushHistory(s, oldAll, Selection{0, oldAll.len}, InputValue(s), true,
                    EditIntent::Atomic, selBefore, &after);
    } else {
        PushHistory(s, oldAll, r, text, hasIntent, requested, selBefore,
                    nullptr);
    }

    s->cursorLineEndAffinity = false;
    s->selectedRange = SelectionAt(newOffset);
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
    Emit(s, app, win, InputEvent{InputEventKind::Change});
    Notify(app, win);
    return true;
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
    Str before = InputValue(s);
    if (r.start < 0) {
        r.start = 0;
    }
    if (r.end > before.len) {
        r.end = before.len;
    }
    if (r.end < r.start) {
        r.end = r.start;
    }
    Str oldAll = StrDup(tmp, before);
    TextSplice(s, r.start, r.end, text);
    if (InputIsSingleLine(s)) {
        // The same rule the committed path uses: only refuse the edit when it
        // is the edit that broke the field, so a value that never conformed
        // cannot trap it.
        Str pending = InputValue(s);
        if (!IsValidInput(s, pending) && IsValidInput(s, oldAll)) {
            TextSet(s, oldAll);
            if (startsComposition) {
                UndoCommitTransaction(&s->undo);
            }
            return;
        }
    }
    s->cursorLineEndAffinity = false;
    if (text.len == 0) {
        // An empty insert is the composition being abandoned: the caret goes
        // back where it started and nothing is marked.
        s->selectedRange = SelectionAt(r.start);
        s->imeMarking = false;
        s->imeMarked = {};
    } else {
        s->imeMarking = true;
        s->imeMarked = Selection{r.start, r.start + text.len};
        if (sel) {
            int lo = r.start + sel->start;
            int hi = r.start + sel->end;
            int end = r.start + text.len;
            s->selectedRange =
                Selection{lo < r.start ? r.start : (lo > end ? end : lo),
                          hi < r.start ? r.start : (hi > end ? end : hi)};
        } else {
            s->selectedRange = SelectionAt(r.start + text.len);
        }
    }
    s->selectionReversed = false;
    s->hasSelectedWordRange = false;
    // Every candidate is a change inside the open transaction, so an undo
    // after the composition takes the whole of it back rather than stepping
    // through the candidates one at a time.
    Selection after = s->selectedRange;
    PushHistory(s, oldAll, r, text, hasIntent, requested, selBefore, &after);
    UpdatePreferredColumn(s);
    if (InputIsMultiLine(s) && s->mode.kind == LayoutModeKind::AutoGrow) {
        LayoutModeSetRows(&s->mode, RopeLinesLen(InputValue(s)));
    }
    if (text.len == 0) {
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
    Selection all = {0, InputValue(s).len};
    InputReplaceTextInRange(s, app, win, &all, value);
}

// reset_selection: a single-line field puts the caret at the end, matching an
// HTML <input>; a multi-line one goes back to 0..0.
static void ResetSelection(InputState* s) {
    s->cursorLineEndAffinity = false;
    if (InputIsSingleLine(s)) {
        s->selectedRange = SelectionAt(InputValue(s).len);
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
void InputSetValue(InputState* s, Str value) {
    App* app = nullptr;
    Window* win = nullptr;
    UndoSetIgnoring(&s->undo, true);
    s->emitEvents = false;
    ReplaceText(s, app, win, value);
    UndoSetIgnoring(&s->undo, false);
    s->emitEvents = true;
    ResetSelection(s);
    UndoClear(&s->undo);
    Notify(app, win);
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

void InputClean(InputState* s, App* app, Window* win) {
    ReplaceText(s, app, win, Str{});
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
    if (cue.len > 0) {
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
    if (at > t.len) {
        at = t.len;
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
    if (item.documentation.len > 0 || !s->completionResolve) {
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
    if (!force && query.len == 0) {
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
    Str queryCopy = query.len > 0 ? StrDup(query) : Str{};
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
    if (edits.len == 1) {
        InputReplaceTextInRange(s, app, win, &edits[0].range, edits[0].newText);
    } else {
        InputApplyEdits(s, app, win, edits.els, edits.len);
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
    Str queryCopy = query.len > 0 ? StrDup(query) : Str{};
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
    if (edits.len == 1) {
        InputReplaceTextInRange(s, app, win, &edits[0].range, edits[0].newText);
    } else {
        InputApplyEdits(s, app, win, edits.els, edits.len);
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
    return s && s->inlineCompletion.text.len > 0;
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
    if (!keep.s && s->inlineCompletion.text.len > 0) {
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
                                      Selection{0, text.len}, buf.els, cap);
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
    VecReset(lastLocations);
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
    // What it found is kept as the last answer: the underline goes as soon as
    // the modifier comes up, and the action still has to know where the
    // symbol under the caret went.
    s->hoverDef.lastRange = s->hoverDef.symbolRange;
    VecReset(s->hoverDef.lastLocations);
    for (int i = 0; i < s->hoverDef.locations.len; i++) {
        VecAppend(s->hoverDef.lastLocations, s->hoverDef.locations[i]);
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
    if (link.uri.len > 0) {
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
    // on_action_go_to_definition: the caret has to still be inside the symbol
    // the last hover found, or the action has nothing to go on.
    int at = InputCursor(s);
    if (s->hoverDef.lastLocations.len == 0 ||
        at < s->hoverDef.lastRange.start || at > s->hoverDef.lastRange.end) {
        return;
    }
    InputFollowDefinition(s, app, win, s->hoverDef.lastLocations[0]);
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
    // A menu that is up goes down, which is what a toggle is.
    if (s->codeActions.open) {
        InputDismissCodeActions(s);
        Notify(app, win);
        return;
    }
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
        if (range.end > text.len) {
            range.end = text.len;
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
    InputApplyEdits(s, app, win, edits.els, edits.len);
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

// move_vertical, on logical lines. Rust walks the display map so a soft-wrapped
// row counts as its own line; without one, a wrapped line moves as a whole.
// How tall a logical line was laid out. The rows reported their boxes last
// frame; without them every line is one row high.
static float DisplayLineH(const InputState* s, int row, float lineH) {
    // A folded-away line is worth no height at all, which is what makes the
    // walk below step straight over it: the row moves on and the y it is
    // carrying does not, so a closed fold costs one press to cross rather
    // than one per line inside it.
    if (FoldMapLineHidden(&s->folds, row)) {
        return 0;
    }
    if (row >= 0 && row < s->rowBoxes.len && s->rowBoxes[row].h > 0) {
        return s->rowBoxes[row].h;
    }
    return lineH;
}

// Document y of `row`: sum of laid-out heights above it. Window y on
// rowBoxes is last-painted and goes stale the moment that row leaves the
// viewport, so nothing that scrolls may subtract two of those.
static float DisplayRowDocY(const InputState* s, int row, float lineH) {
    if (!s || row <= 0) {
        return 0;
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
};

// Whether `offset` is the end of the visual row containing `relY`. The two
// shaped points differ only at a soft-wrap boundary; everywhere else there
// is no affinity to retain.
static bool InputLineEndAffinityAt(PaintCtx* ctx, Str line, float font,
                                   float maxW, int offset, float relY,
                                   bool mono, float lineMult) {
    if (!ctx || offset <= 0 || offset >= line.len) {
        return false;
    }
    float endX = 0, endY = 0, endH = 0;
    float startX = 0, startY = 0, startH = 0;
    if (!TextPointAt(ctx, line, font, maxW, true, offset, &endX, &endY, &endH,
                     mono, lineMult, true) ||
        !TextPointAt(ctx, line, font, maxW, true, offset, &startX, &startY,
                     &startH, mono, lineMult, false)) {
        return false;
    }
    float dy = endY - startY;
    if (dy > -0.5f && dy < 0.5f) {
        return false;
    }
    float endMid = endY + endH * 0.5f;
    float startMid = startY + startH * 0.5f;
    float toEnd = relY - endMid;
    float toStart = relY - startMid;
    if (toEnd < 0) toEnd = -toEnd;
    if (toStart < 0) toStart = -toStart;
    return toEnd <= toStart;
}

// display_map.rs: a vertical move walks *display* rows, so a wrapped line
// takes as many presses to cross as it has visual rows. The caret's point
// comes off the shaped run, the walk moves it a row at a time through the
// lines around it, and the point maps back to an offset. False when there is
// nothing laid out to measure against, which leaves the logical-line walk.
static bool VerticalTargetDisplay(const InputState* s, Window* win, int lines,
                                  Str t, int from, VerticalTarget* out) {
    if (!win || !s->softWrap) {
        return false;
    }
    PaintCtx* ctx = &win->paint;
    float maxW = s->lastBounds.w;
    float font = s->lastFont;
    float lineH = s->lastLineH > 0 ? s->lastLineH : kInputLineH;
    if (maxW <= 0 || font <= 0 || lineH <= 0) {
        return false;
    }
    RopePoint p = RopeOffsetToPoint(t, from);
    Str line = RopeSliceLine(t, p.row);
    int start = RopeLineStartOffset(t, p.row);
    float cx = 0, cy = 0, ch = lineH;
    float lineMult = lineH / font;
    if (!TextPointAt(ctx, line, font, maxW, true, from - start, &cx, &cy, &ch,
                     s->lastMono, lineMult, s->cursorLineEndAffinity)) {
        return false;
    }
    // The x the whole walk aims at, so crossing a short row and coming back
    // lands where it started.
    float wantX = s->preferredX >= 0 ? s->preferredX : cx;
    int maxRow = RopeLinesLen(t) - 1;
    int row = p.row;
    float y = cy + (float)lines * lineH;
    while (y < 0 && row > 0) {
        row--;
        y += DisplayLineH(s, row, lineH);
    }
    float h = DisplayLineH(s, row, lineH);
    while (y >= h && row < maxRow) {
        y -= h;
        row++;
        h = DisplayLineH(s, row, lineH);
    }
    if (y < 0) {
        y = 0;
    }
    // Aim at the middle of the visual row rather than at its top edge: a hit
    // test exactly on the boundary between two rows could answer either.
    y = ((float)(int)(y / lineH) + 0.5f) * lineH;
    if (y > h - 1) {
        y = h - 1;
    }
    // The ends of the document are the one place the walk can stop on a
    // hidden row: it runs out of rows before it runs out of y.
    row = FoldMapNearestVisibleLine(&s->folds, row);
    Str target = RopeSliceLine(t, row);
    int targetStart = RopeLineStartOffset(t, row);
    out->offset = targetStart;
    if (target.len > 0) {
        int local = TextIndexAt(ctx, target, font, maxW, true, wantX, y,
                                s->lastMono, lineMult);
        out->offset += local;
        out->lineEndAffinity = InputLineEndAffinityAt(
            ctx, target, font, maxW, local, y, s->lastMono, lineMult);
    }
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
    int lineLen = RopeLineLen(t, row);
    int want = column < lineLen ? column : lineLen;
    out.offset =
        RopeClipOffset(t, RopeLineStartOffset(t, row) + want, Bias::Left);
    out.preferredColumn = column;
    return out;
}

static void MoveVertical(InputState* s, App* app, Window* win, int lines) {
    if (InputIsSingleLine(s)) {
        return;
    }
    Str t = InputValue(s);
    VerticalTarget to = VerticalTargetFor(s, win, lines, t, InputCursor(s));
    PauseBlink(s, app, win);
    InputMoveToWithAffinity(s, app, win, to.offset, to.lineEndAffinity);
    s->preferredX = to.preferredX;
    s->preferredColumn = to.preferredColumn;
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
    UndoBreakCoalescing(&s->undo);
    Str t = InputValue(s);
    VerticalTarget to = VerticalTargetFor(s, win, lines, t, InputCursor(s));
    PauseBlink(s, app, win);
    InputSelectToWithAffinity(s, app, win, to.offset, to.lineEndAffinity);
    s->preferredX = to.preferredX;
    s->preferredColumn = to.preferredColumn;
    // scroll_to: the moving end of the selection takes the view with it, the
    // way move_to does for the caret.
    InputScrollToCursor(s, lines < 0 ? InputMoveDir::Up : InputMoveDir::Down);
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
    ClipboardSetText(win, InputSelectedValue(s));
}

static void DoUndo(InputState* s, App* app, Window* win) {
    UndoSetIgnoring(&s->undo, true);
    const UndoTransaction* t = UndoPopUndo(&s->undo);
    if (t && t->len > 0) {
        // The list is applied backwards, so the selection to restore is the
        // one recorded before the first change in it.
        Selection sel = t->changes[0].selBefore;
        for (int i = t->len - 1; i >= 0; i--) {
            Selection r = t->changes[i].newRange;
            InputReplaceTextInRange(s, app, win, &r, t->changes[i].oldText);
        }
        s->cursorLineEndAffinity = false;
        s->selectedRange = sel;
        s->selectionReversed = false;
    }
    UndoSetIgnoring(&s->undo, false);
}

static void DoRedo(InputState* s, App* app, Window* win) {
    UndoSetIgnoring(&s->undo, true);
    const UndoTransaction* t = UndoPopRedo(&s->undo);
    if (t && t->len > 0) {
        Selection sel = t->changes[t->len - 1].selAfter;
        for (int i = 0; i < t->len; i++) {
            Selection r = t->changes[i].oldRange;
            InputReplaceTextInRange(s, app, win, &r, t->changes[i].newText);
        }
        s->cursorLineEndAffinity = false;
        s->selectedRange = sel;
        s->selectionReversed = false;
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
    int n = s->mode.tabSize > 0 ? s->mode.tabSize : 4;
    Str tab = AllocStrTemp(n);
    memset(tab.s, ' ', (size_t)n);
    return tab;
}

// start_of_line_of_selection: where the line the selection begins on starts.
static int StartOfLineOfSelection(const InputState* s) {
    if (InputIsSingleLine(s)) {
        return 0;
    }
    Str t = InputValue(s);
    Selection r = s->selectedRange;
    int off = r.start < r.end ? r.start : r.end;
    return RopeLineStartOffset(t, RopeOffsetToPoint(t, off).row);
}

// The bytes of the line starting at `at` in `text`, up to the next newline
// or the end. The \r of a CRLF pair counts as part of the line, the way
// Rust's split('\n') leaves it there.
static int LineLenAt(Str text, int at) {
    int i = at;
    while (i < text.len && text.s[i] != '\n') {
        i++;
    }
    return i - at;
}

// A field that has nothing to indent returns false, which is cx.propagate().
static bool IndentReady(const InputState* s) {
    return InputIsMultiLine(s) && ModeIsIndentable(s);
}

// indent(). With a selection every line it touches is pushed over by one tab.
// With none, the inline variant puts one tab in at the caret and the block
// variant puts it at the start of the caret's line. Rust walks the lines one
// replace_text_in_range each and Rust's history brackets them; ours records a
// change per call and merges an open bracket first-old-to-last-new, which is
// right for an IME composition and wrong for edits at growing offsets — so
// the whole span is rewritten in one edit instead, which is also one undo
// step and one Change event.
static bool DoIndent(InputState* s, App* app, Window* win, bool block) {
    if (!IndentReady(s)) {
        return false;
    }
    Str tab = TabIndent(s);
    Selection sel = s->selectedRange;
    bool isSelected = !sel.IsEmpty();
    s->undo.hasPendingIntent = true;
    s->undo.pendingIntent = EditIntent::Atomic;
    if (!isSelected && !block) {
        Selection at = SelectionAt(sel.start);
        InputReplaceTextInRange(s, app, win, &at, tab);
        s->selectedRange = SelectionAt(sel.start + tab.len);
        s->selectionReversed = false;
        PauseBlink(s, app, win);
        return true;
    }
    // The lines the selection touches, from the start of the first one, each
    // with a tab in front of it. With no selection that span is the caret's
    // own line back to its start, which is where the block variant puts the
    // tab whatever column the caret is in.
    int startOffset = StartOfLineOfSelection(s);
    Str before = InputValue(s);
    Str src = Str(before.s + startOffset, sel.end - startOffset);
    int nLines = 1;
    for (int i = 0; i < src.len; i++) {
        if (src.s[i] == '\n') {
            nLines++;
        }
    }
    int added = tab.len * nLines;
    Str out = AllocStrTemp(src.len + added);
    int w = 0;
    int i = 0;
    for (;;) {
        int lineLen = LineLenAt(src, i);
        memcpy(out.s + w, tab.s, (size_t)tab.len);
        w += tab.len;
        memcpy(out.s + w, src.s + i, (size_t)lineLen);
        w += lineLen;
        i += lineLen;
        if (i >= src.len) {
            break;
        }
        out.s[w++] = '\n';
        i++;
    }
    Selection r = {startOffset, sel.end};
    InputReplaceTextInRange(s, app, win, &r, out);
    // A selection keeps the lines it grew to cover; a bare caret rides along
    // with the text it sits in.
    s->selectedRange = isSelected
                           ? Selection{startOffset, sel.end + added}
                           : Selection{sel.start + added, sel.end + added};
    s->selectionReversed = false;
    PauseBlink(s, app, win);
    Notify(app, win);
    return true;
}

static int SatSub(int a, int b) {
    return a > b ? a - b : 0;
}

// outdent(). One tab comes off the front of every line that has one; a line
// that does not is left alone. Same one-edit shape as DoIndent. There is no
// block variant of it to write: Rust's two differ only in where an unselected
// indent starts, and outdent starts at the line either way.
static bool DoOutdent(InputState* s, App* app, Window* win) {
    if (!IndentReady(s)) {
        return false;
    }
    Str tab = TabIndent(s);
    Selection sel = s->selectedRange;
    Str before = InputValue(s);
    s->undo.hasPendingIntent = true;
    s->undo.pendingIntent = EditIntent::Atomic;
    if (sel.IsEmpty()) {
        // The caret's own line, whichever column the caret sits in.
        int offset = StartOfLineOfSelection(s);
        if (before.len - offset < tab.len ||
            !StrEq(Str(before.s + offset, tab.len), tab)) {
            s->undo.hasPendingIntent = false;
            return true;
        }
        Selection r = {offset, offset + tab.len};
        InputReplaceTextInRange(s, app, win, &r, Str{});
        s->selectedRange = SelectionAt(SatSub(sel.start, tab.len));
        s->selectionReversed = false;
        PauseBlink(s, app, win);
        return true;
    }
    int startOffset = StartOfLineOfSelection(s);
    Str src = Str(before.s + startOffset, sel.end - startOffset);
    Str out = AllocStrTemp(src.len);
    int removed = 0;
    int w = 0;
    int i = 0;
    for (;;) {
        int lineLen = LineLenAt(src, i);
        int skip = 0;
        if (lineLen >= tab.len && StrEq(Str(src.s + i, tab.len), tab)) {
            skip = tab.len;
            removed += tab.len;
        }
        memcpy(out.s + w, src.s + i + skip, (size_t)(lineLen - skip));
        w += lineLen - skip;
        i += lineLen;
        if (i >= src.len) {
            break;
        }
        out.s[w++] = '\n';
        i++;
    }
    if (removed == 0) {
        s->undo.hasPendingIntent = false;
        return true;
    }
    out.len = w;
    out.s[w] = 0;
    Selection r = {startOffset, sel.end};
    InputReplaceTextInRange(s, app, win, &r, out);
    s->selectedRange = Selection{startOffset, SatSub(sel.end, removed)};
    s->selectionReversed = false;
    PauseBlink(s, app, win);
    Notify(app, win);
    return true;
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

        case InputAction::MoveLeft:
            PauseBlink(s, app, win);
            InputMoveTo(s, app, win,
                        s->selectedRange.IsEmpty()
                            ? InputPreviousBoundary(s, InputCursor(s))
                            : s->selectedRange.start);
            return true;
        case InputAction::MoveRight:
            PauseBlink(s, app, win);
            InputMoveTo(s, app, win,
                        s->selectedRange.IsEmpty()
                            ? InputNextBoundary(s, s->selectedRange.end)
                            : s->selectedRange.end);
            return true;
        case InputAction::MoveUp:
            if (InputIsSingleLine(s)) {
                return false;
            }
            if (!s->selectedRange.IsEmpty()) {
                InputMoveTo(s, app, win, s->selectedRange.start);
            }
            MoveVertical(s, app, win, -1);
            return true;
        case InputAction::MoveDown:
            if (InputIsSingleLine(s)) {
                return false;
            }
            if (!s->selectedRange.IsEmpty()) {
                InputMoveTo(s, app, win, s->selectedRange.end);
            }
            MoveVertical(s, app, win, 1);
            return true;
        case InputAction::MovePageUp:
            MoveVertical(s, app, win, -LayoutModeRows(s->mode));
            return InputIsMultiLine(s);
        case InputAction::MovePageDown:
            MoveVertical(s, app, win, LayoutModeRows(s->mode));
            return InputIsMultiLine(s);
        case InputAction::MoveHome:
            PauseBlink(s, app, win);
            InputMoveTo(s, app, win, InputStartOfLine(s, win));
            return true;
        case InputAction::MoveEnd:
            PauseBlink(s, app, win);
            InputMoveToWithAffinity(s, app, win, InputEndOfLine(s, win), true);
            return true;
        case InputAction::MoveToStart:
            InputMoveTo(s, app, win, 0);
            return true;
        case InputAction::MoveToEnd:
            InputMoveTo(s, app, win, t.len);
            return true;
        case InputAction::MoveToPreviousWord:
            InputMoveTo(s, app, win, InputPreviousStartOfWord(s));
            return true;
        case InputAction::MoveToNextWord:
            InputMoveTo(s, app, win, InputNextEndOfWord(s));
            return true;

        case InputAction::SelectLeft:
            UndoBreakCoalescing(&s->undo);
            InputSelectTo(s, app, win,
                          InputPreviousBoundary(s, InputCursor(s)));
            return true;
        case InputAction::SelectRight:
            UndoBreakCoalescing(&s->undo);
            InputSelectTo(s, app, win, InputNextBoundary(s, InputCursor(s)));
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
            UndoBreakCoalescing(&s->undo);
            InputSelectTo(s, app, win, 0);
            return true;
        case InputAction::SelectToEnd:
            UndoBreakCoalescing(&s->undo);
            InputSelectTo(s, app, win, t.len);
            return true;
        case InputAction::SelectToStartOfLine:
            UndoBreakCoalescing(&s->undo);
            InputSelectTo(s, app, win, InputStartOfLine(s, win));
            return true;
        case InputAction::SelectToEndOfLine:
            UndoBreakCoalescing(&s->undo);
            InputSelectToWithAffinity(s, app, win, InputEndOfLine(s, win),
                                      true);
            return true;
        case InputAction::SelectToPreviousWordStart:
            UndoBreakCoalescing(&s->undo);
            InputSelectTo(s, app, win, InputPreviousStartOfWord(s));
            return true;
        case InputAction::SelectToNextWordEnd:
            UndoBreakCoalescing(&s->undo);
            InputSelectTo(s, app, win, InputNextEndOfWord(s));
            return true;

        case InputAction::Backspace: {
            EditIntent intent = EditIntent::Atomic;
            if (s->selectedRange.IsEmpty()) {
                InputSelectTo(s, app, win,
                              InputPreviousBoundary(s, InputCursor(s)));
                intent = EditIntent::Backspace;
            }
            s->undo.hasPendingIntent = true;
            s->undo.pendingIntent = intent;
            InputReplaceTextInRange(s, app, win, nullptr, Str{});
            PauseBlink(s, app, win);
            // The word behind the caret is one shorter: a menu that is up
            // asks again, and closes when nothing matches any more.
            if (s->completion.open) {
                InputRequestCompletion(s, app, win, false);
            }
            return true;
        }
        case InputAction::Delete: {
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
                offset = offset + 1 > t.len ? t.len : offset + 1;
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
                InputReplaceTextInRange(s, app, win, nullptr, StrL("\n"));
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
            return DoIndent(s, app, win, false);
        case InputAction::Indent:
            return DoIndent(s, app, win, true);
        case InputAction::OutdentInline:
        case InputAction::Outdent:
            return DoOutdent(s, app, win);

        case InputAction::Escape:
            // "Clear inline completion on escape", and consume the key: the
            // escape said no to the suggestion and nothing else.
            if (InputHasInlineCompletion(s)) {
                InputClearInlineCompletion(s);
                Notify(app, win);
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
            if (!win) {
                return true;
            }
            Str text = ClipboardGetText(GetTempArena(), win);
            if (text.len == 0) {
                return true;
            }
            s->undo.hasPendingIntent = true;
            s->undo.pendingIntent = EditIntent::Atomic;
            InputReplaceTextInRange(s, app, win, nullptr, text);
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
        case InputAction::Search:
        case InputAction::Replace:
            // on_action_search / on_action_replace, both of which are the
            // same call with the replace row already out or not.
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
    // Both open the find bar, and a field that is not searchable answers
    // neither — Rust's handlers propagate instead of handling.
    if ((act == InputAction::Search || act == InputAction::Replace) &&
        !(s && s->searchable)) {
        return InputAction::None;
    }
    return act;
}

// ─── the find bar ─────────────────────────────────────────────────────────

bool InputIsReplaceable(const InputState* s) {
    return s && s->replaceable && InputIsEditable(s);
}

void InputUpdateSearch(InputState* s) {
    if (s) {
        SearchMatcherUpdate(&s->search.matcher, InputValue(s));
    }
}

// last_layout.visible_range_offset.start. Rust knows which rows it laid out;
// this tree builds them all, so the first visible one is worked back out of
// how far the field has scrolled. Same answer, one frame stale.
static int FirstVisibleOffset(const InputState* s) {
    Str text = InputValue(s);
    if (s->scrollY <= 0) {
        return 0;
    }
    int row = 0;
    float lineH = s->lastLineH > 0 ? s->lastLineH : kInputLineH;
    if (s->rowBoxes.len > 0) {
        float at = 0;
        for (int i = 0; i < s->rowBoxes.len; i++) {
            float h = DisplayLineH(s, i, lineH);
            if (at + h > s->scrollY) {
                row = i;
                break;
            }
            at += h;
            row = i;
        }
        row = FoldMapNearestVisibleLine(&s->folds, row);
    } else {
        row = (int)(s->scrollY / lineH);
    }
    return RopeLineStartOffset(text, row);
}

void InputOpenSearch(InputState* s, App* app, Window* win, bool replaceMode) {
    if (!s || !s->searchable) {
        return;
    }
    s->searchActivationRevision++;
    s->search.open = true;
    s->search.replaceMode = replaceMode && InputIsReplaceable(s);
    // Whatever is selected becomes the query, which is what makes ctrl-f on
    // a word search for that word. An empty selection leaves the last one.
    Str selected = InputSelectedValue(s);
    if (selected.len > 0) {
        StrFree(s->search.query);
        s->search.query = StrDup(selected);
    }
    s->search.anchorOffset = FirstVisibleOffset(s);
    SearchMatcherUpdateQuery(&s->search.matcher, s->search.query,
                             s->search.caseInsensitive);
    SearchMatcherUpdate(&s->search.matcher, InputValue(s));
    SearchMatcherCursorByOffset(&s->search.matcher, s->search.anchorOffset);
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
    SearchSessionSetQuery(&s->search, query, insensitive);
    SearchMatcherUpdate(&s->search.matcher, InputValue(s));
    Notify(app, win);
}

bool InputSearchNext(InputState* s, App* app, Window* win, Selection* out) {
    if (!s) {
        return false;
    }
    int was = SearchMatcherIndex(&s->search.matcher);
    Selection r = {};
    if (!SearchMatcherNext(&s->search.matcher, &r)) {
        return false;
    }
    // A step that wrapped back to the top is not a downward move, so the
    // clamp that stops a scroll going the wrong way is not applied to it.
    InputMoveDir dir = SearchMatcherIndex(&s->search.matcher) > was
                           ? InputMoveDir::Down
                           : InputMoveDir::None;
    InputScrollToOffset(s, r.end, dir);
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
    int was = SearchMatcherIndex(&s->search.matcher);
    Selection r = {};
    if (!SearchMatcherPrev(&s->search.matcher, &r)) {
        return false;
    }
    InputMoveDir dir = SearchMatcherIndex(&s->search.matcher) < was
                           ? InputMoveDir::Up
                           : InputMoveDir::None;
    InputScrollToOffset(s, r.start, dir);
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
    sb.Append(Str(text.s + at, text.len - at));
    Str whole = sb.TakeStr();
    SearchMatcherBeginReplacement(m);
    Selection all = {0, text.len};
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
// line for the x; the rows here are the logical lines, evenly spaced from the
// first one, so the row is arithmetic and only the x needs shaping.
int InputIndexForPosition(const InputState* s, PaintCtx* ctx, float x, float y,
                          bool* lineEndAffinity) {
    if (lineEndAffinity) {
        *lineEndAffinity = false;
    }
    Str t = InputValue(s);
    if (t.len == 0 || !ctx) {
        return 0;
    }
    const Bounds& b = s->lastBounds;
    if (b.w <= 0 && b.h <= 0 && s->inputBounds.w <= 0 &&
        s->inputBounds.h <= 0 && s->contentBox.h <= 0) {
        return 0;
    }
    float font = s->lastFont > 0 ? s->lastFont : 14.f;
    if (InputIsSingleLine(s)) {
        if (x <= b.x) {
            return 0;
        }
        return TextIndexAt(ctx, t, font, 0, false, x - b.x, 0, s->lastMono);
    }
    float lineH = s->lastLineH > 0 ? s->lastLineH : b.h;
    int rows = InputLinesLen(s);
    int row = 0;
    // How far down its own row the press landed, which is which of a wrapped
    // line's visual rows it wanted.
    float relY = 0;
    if (s->rowBoxes.len == rows && rows > 0) {
        // The rows are uneven, so the one under the press is found by walking
        // heights rather than window y. Off-screen boxes keep the y they had
        // when last painted; after a scroll those still cover the viewport
        // and a click would map to the old band, then scroll_to would jump
        // the view back there.
        float originY = b.y;
        if (s->inputBounds.h > 0) {
            originY = s->inputBounds.y - s->scrollY;
        }
        float docY = y - originY;
        float at = 0;
        row = FoldMapNearestVisibleLine(&s->folds, rows - 1);
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
        }
    } else {
        // lastBounds is the first row's text, which is only painted while
        // that row is on screen. contentBox.y is the column's last *painted*
        // origin, so it embeds that frame's scrollY — a click after scrolling
        // from line 700 to 900 would still map as if the top were 700, and
        // scroll_to would jump the view back. The clip box does not move;
        // adding the live scrollY is the document.
        float originY = b.y;
        if (s->inputBounds.h > 0) {
            originY = s->inputBounds.y - s->scrollY;
        }
        row = lineH > 0 ? (int)((y - originY) / lineH) : 0;
        if (row < 0) {
            row = 0;
        }
        if (row > rows - 1) {
            row = rows - 1;
        }
        row = FoldMapNearestVisibleLine(&s->folds, row);
    }
    Str line = InputSliceLine(s, row);
    int start = InputLineStartOffset(s, row);
    // A wrapped line still needs shaping at its left edge: the same x starts
    // every visual row, and relY is what distinguishes those offsets.  The
    // logical-line shortcut is valid only when the line does not wrap.
    if (line.len == 0 || (x <= b.x && !s->softWrap)) {
        return start;
    }
    float maxW = s->softWrap ? b.w : 0;
    float lineMult = s->lastLineH > 0 ? s->lastLineH / font : 0;
    int local = TextIndexAt(ctx, line, font, maxW, s->softWrap, x - b.x, relY,
                            s->lastMono, lineMult);
    if (lineEndAffinity && s->softWrap) {
        *lineEndAffinity = InputLineEndAffinityAt(ctx, line, font, maxW, local,
                                                  relY, s->lastMono, lineMult);
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
    if (needle.len <= 0 || needle.len > hay.len) {
        return -1;
    }
    for (int i = from; i + needle.len <= hay.len; i++) {
        int k = 0;
        for (; k < needle.len; k++) {
            char a = hay.s[i + k], b = needle.s[k];
            if (fold) {
                a = FoldAscii(a);
                b = FoldAscii(b);
            }
            if (a != b) {
                break;
            }
        }
        if (k == needle.len) {
            return i;
        }
    }
    return -1;
}

static Str MatcherText(const SearchMatcher* m) {
    return Str(m->text.els, m->text.len);
}

// update_matches. `stream_find_iter` answers leftmost non-overlapping
// matches, which is what stepping past the end of each one comes to.
static void MatcherUpdateMatches(SearchMatcher* m) {
    VecClear(m->ranges);
    m->ranges.len = 0;
    if (m->query.len > 0) {
        Str hay = MatcherText(m);
        int at = 0;
        for (;;) {
            int lo = FindFrom(hay, m->query, at, m->caseInsensitive);
            if (lo < 0) {
                break;
            }
            VecAppend(m->ranges, Selection{lo, lo + m->query.len});
            at = lo + m->query.len;
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
    if (StrEq(Str(m->text.els, m->text.len), text)) {
        m->replacing = false;
        return;
    }
    m->text.len = 0;
    if (text.len > 0) {
        char* dst = VecAppendBlanks(m->text, text.len);
        if (dst) {
            memcpy(dst, text.s, (size_t)text.len);
        }
    }
    MatcherUpdateMatches(m);
}

void SearchMatcherUpdateQuery(SearchMatcher* m, Str query, bool insensitive) {
    StrFree(m->query);
    m->query = query.len > 0 ? StrDup(query) : Str{};
    m->caseInsensitive = insensitive;
    MatcherUpdateMatches(m);
}

Str SearchMatcherLabel(Arena* a, const SearchMatcher* m) {
    if (m->ranges.len == 0) {
        return StrDup(a, StrL("0/0"));
    }
    return StrDup(a, fmt("%d/%d", m->current + 1, m->ranges.len));
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
    StrFree(s->query);
    s->query = query.len > 0 ? StrDup(query) : Str{};
    s->caseInsensitive = insensitive;
    SearchMatcherUpdateQuery(&s->matcher, s->query, insensitive);
}

void SearchSessionSetReplacement(SearchSession* s, Str replacement) {
    StrFree(s->replacement);
    s->replacement = replacement.len > 0 ? StrDup(replacement) : Str{};
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
            return p.pattern.len == 0;
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
    if (text.len == 0) {
        return true;
    }
    int dot = -1;
    for (int i = 0; i < text.len; i++) {
        if (text.s[i] != '.') {
            continue;
        }
        if (dot >= 0) {
            return false; // only one dot is valid
        }
        dot = i;
    }
    int intEnd = dot < 0 ? text.len : dot;
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
    for (int i = intEnd + 1; i < text.len && dot >= 0;) {
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
    int tokens = RopeOffsetToCharIndex(p.pattern, p.pattern.len);
    for (int pos = 0; pos < tokens; pos++) {
        if (ti >= maskText.len) {
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
    return ti == maskText.len;
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
    for (int i = 0; i < text.len;) {
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
    int intEnd = dot < 0 ? flat.len : dot;

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
        for (int i = intEnd + 1; i < flat.len;) {
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
    int tokens = RopeOffsetToCharIndex(p.pattern, p.pattern.len);
    for (int pos = 0; pos < tokens; pos++) {
        if (ti >= text.len) {
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
        for (int i = 0; i < maskText.len;) {
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
    int tokens = RopeOffsetToCharIndex(p.pattern, p.pattern.len);
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
    int tokens = RopeOffsetToCharIndex(p.pattern, p.pattern.len);
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
    for (int i = 0; i < text.len && !any;) {
        uint32_t c = 0;
        i += Utf8At(text, i, &c);
        any = NormalizeChar(c) != c;
    }
    if (!any) {
        return StrDup(a, text); // Rust's Cow::Borrowed
    }
    StrBuilder out;
    for (int i = 0; i < text.len;) {
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
    if (offset >= text.len) {
        return text.len;
    }
    if (bias == Bias::Left) {
        return Utf8ClipLeft(text, offset);
    }
    // Bias::Right: forward to the next boundary instead.
    while (offset < text.len && ((uint8_t)text.s[offset] & 0xC0) == 0x80) {
        offset++;
    }
    return offset;
}

int RopeCharAt(Str text, int offset, uint32_t* out) {
    *out = 0;
    if (!text.s || offset < 0 || offset >= text.len) {
        return 0;
    }
    return Utf8At(text, offset, out);
}

int RopeLinesLen(Str text) {
    // len_lines(LineType::LF): one more than the number of LFs, and an empty
    // rope still has one line.
    int n = 1;
    for (int i = 0; i < text.len; i++) {
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
    for (int i = 0; i < text.len; i++) {
        if (text.s[i] != '\n') {
            continue;
        }
        seen++;
        if (seen == row) {
            return i + 1;
        }
    }
    return text.len;
}

Str RopeSliceLine(Str text, int row) {
    if (row < 0 || row >= RopeLinesLen(text)) {
        return {};
    }
    int a = RopeLineStartOffset(text, row);
    int b = a;
    while (b < text.len && text.s[b] != '\n') {
        b++;
    }
    return Str(text.s + a, b - a);
}

int RopeLineLen(Str text, int row) {
    return RopeSliceLine(text, row).len;
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
        return text.len;
    }
    return RopeLineStartOffset(text, point.row) + point.column;
}

// The two UTF-16 conversions the IME and every `*_utf16` range in state.rs go
// through. A character outside the BMP is one UTF-16 surrogate pair, so it
// counts as two.
int RopeOffsetToOffsetUtf16(Str text, int offset) {
    if (offset > text.len) {
        offset = text.len;
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
    while (i < text.len && n < offsetUtf16) {
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
    while (i < text.len && n < charIndex) {
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
    c->oldText = {};
    c->newText = {};
}

static void TransactionFree(UndoTransaction* t) {
    for (int i = 0; i < t->len; i++) {
        ChangeFree(&t->changes[i]);
    }
    free(t->changes);
    t->changes = nullptr;
    t->len = 0;
    t->cap = 0;
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

static void StackClear(Vec<UndoTransaction>& v) {
    for (int i = 0; i < v.len; i++) {
        TransactionFree(&v[i]);
    }
    v.len = 0;
}

UndoManager::~UndoManager() {
    StackClear(undos);
    StackClear(redos);
    if (hasPending) {
        ChangeFree(&pending);
    }
}

// is_adjacent: whether the change coming in continues the one before it, which
// is what lets a run of the same intent stay one undo step.
static bool IsAdjacent(EditIntent intent, const Change& prev,
                       const Change& cur) {
    auto hasNewline = [](Str s) {
        for (int i = 0; i < s.len; i++) {
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
            return prev.newText.len == 0 && cur.newText.len == 0 &&
                   cur.oldRange.end == prev.oldRange.start;
        case EditIntent::DeleteForward:
            return prev.newText.len == 0 && cur.newText.len == 0 &&
                   cur.oldRange.start == prev.oldRange.start;
        case EditIntent::Atomic:
            return false;
    }
    return false;
}

static bool RangeSame(Selection a, Selection b) {
    return a.start == b.start && a.end == b.end;
}

static void PushTransaction(UndoManager* m, Change change, EditIntent intent) {
    StackClear(m->redos);
    bool canCoalesce = false;
    if (!m->coalescingBoundary && intent != EditIntent::Atomic &&
        m->undos.len > 0) {
        UndoTransaction& prev = m->undos[m->undos.len - 1];
        canCoalesce = prev.intent == intent &&
                      prev.len < kMaxChangesPerTransaction && prev.len > 0 &&
                      IsAdjacent(intent, prev.changes[prev.len - 1], change);
    }
    if (canCoalesce) {
        TransactionPush(&m->undos[m->undos.len - 1], change);
        return;
    }
    if (m->undos.len >= kMaxUndoTransactions) {
        TransactionFree(&m->undos[0]);
        memmove(m->undos.els, m->undos.els + 1,
                (size_t)(m->undos.len - 1) * sizeof(UndoTransaction));
        m->undos.len--;
    }
    UndoTransaction t = {};
    t.intent = intent;
    TransactionPush(&t, change);
    VecAppend(m->undos, t);
    m->coalescingBoundary = intent == EditIntent::Atomic;
}

void UndoRecordTransaction(UndoManager* m, Change change, EditIntent intent) {
    if (m->ignoring) {
        ChangeFree(&change);
        return;
    }
    // A no-op edit records nothing, but still ends the run before it, so the
    // undo history keeps whatever it already had.
    if (RangeSame(change.oldRange, change.newRange) &&
        base::StrEq(change.oldText, change.newText)) {
        ChangeFree(&change);
        UndoBreakCoalescing(m);
        return;
    }
    if (m->transactionOpen) {
        if (m->hasPending) {
            // The bracket keeps the first change's old side and takes the
            // latest new side, so the whole composition undoes at once.
            StrFree(m->pending.newText);
            m->pending.newRange = change.newRange;
            m->pending.newText = change.newText;
            m->pending.selAfter = change.selAfter;
            StrFree(change.oldText);
        } else {
            m->pending = change;
            m->hasPending = true;
        }
        return;
    }
    PushTransaction(m, change, intent);
}

void UndoBeginTransaction(UndoManager* m) {
    if (m->transactionOpen) {
        return;
    }
    m->transactionOpen = true;
    if (m->hasPending) {
        ChangeFree(&m->pending);
        m->hasPending = false;
    }
}

void UndoCommitTransaction(UndoManager* m) {
    if (!m->transactionOpen) {
        return;
    }
    m->transactionOpen = false;
    if (!m->hasPending) {
        return;
    }
    Change c = m->pending;
    m->hasPending = false;
    m->pending = {};
    if (!RangeSame(c.oldRange, c.newRange) ||
        !base::StrEq(c.oldText, c.newText)) {
        PushTransaction(m, c, EditIntent::Atomic);
    } else {
        ChangeFree(&c);
    }
}

void UndoBreakCoalescing(UndoManager* m) {
    UndoCommitTransaction(m);
    m->coalescingBoundary = true;
}

bool UndoIsIgnoring(const UndoManager* m) {
    return m->ignoring;
}

void UndoSetIgnoring(UndoManager* m, bool ignoring) {
    m->ignoring = ignoring;
    if (ignoring) {
        UndoCommitTransaction(m);
    }
}

void UndoClear(UndoManager* m) {
    StackClear(m->undos);
    StackClear(m->redos);
    m->transactionOpen = false;
    if (m->hasPending) {
        ChangeFree(&m->pending);
        m->hasPending = false;
    }
    m->hasPendingIntent = false;
    m->coalescingBoundary = false;
}

const UndoTransaction* UndoPopUndo(UndoManager* m) {
    UndoCommitTransaction(m);
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
    UndoCommitTransaction(m);
    if (m->redos.len == 0) {
        return nullptr;
    }
    UndoTransaction t = m->redos[m->redos.len - 1];
    m->redos.len--;
    VecAppend(m->undos, t);
    m->coalescingBoundary = true;
    return &m->undos[m->undos.len - 1];
}

} // namespace gpui
