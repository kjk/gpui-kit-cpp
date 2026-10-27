/* crates/base/src/input/editor/lsp provider and overlay facades. */

#include "base/input_lsp.h"

namespace gpui {

void CompletionProvider::Install(InputState* state,
                                 CompletionMenuOptions options) const {
    if (!state) {
        return;
    }
    state->completionProvider = completions;
    state->completionData = data;
    state->inlineCompletionProvider = inlineCompletion;
    state->inlineCompletionData = data;
    state->completionTrigger = isCompletionTrigger;
    state->completionResolve = resolveCompletions;
    state->inlineCompletionDebounceMs =
        std::max(0.f, inlineCompletionDebounceMs);
    state->completionMenuMaxW = std::max(0.f, options.maxWidth);
}

void CodeActionProvider::Install(InputState* state) const {
    if (state && codeActions) {
        InputAddCodeActionProvider(state, codeActions, data, performCodeAction);
    }
}

void DefinitionProvider::Install(InputState* state) const {
    if (state) {
        state->definitionProvider = definitions;
        state->definitionData = data;
    }
}

void DocumentColorProvider::Install(InputState* state) const {
    if (state) {
        state->documentColorProvider = documentColors;
        state->documentColorData = data;
        state->documentColorsDirty = true;
    }
}

void HoverProvider::Install(InputState* state) const {
    if (state) {
        state->hoverProvider = hover;
        state->hoverData = data;
    }
}

void DocumentRangeSemanticTokensProvider::Install(InputState* state) const {
    if (state) {
        state->semanticTokensProvider = semanticTokens;
        state->semanticTokensData = data;
        state->semanticLegend = legend;
        state->nSemanticLegend = std::max(0, nLegend);
        state->semanticTokensDirty = true;
    }
}

bool ShowDocumentHandler::Show(Str uri, bool external,
                               Selection selection) const {
    return show && show(data, uri, external, selection);
}

void ShowDocumentHandler::Install(InputState* state) const {
    if (state) {
        state->showDocument = show;
        state->showDocumentData = data;
    }
}

CompletionMenuState CompletionMenuState::Of(const InputState* state) {
    CompletionMenuState result;
    if (!state) {
        return result;
    }
    result.open = state->completion.open;
    result.triggerStartOffset = state->completion.triggerStart;
    result.query = state->completion.query;
    result.items = state->completion.items.els;
    result.nItems = state->completion.items.len;
    result.selected = state->completion.selected;
    result.revision = state->completion.revision;
    return result;
}

CodeActionMenuState CodeActionMenuState::Of(const InputState* state) {
    CodeActionMenuState result;
    if (!state) {
        return result;
    }
    result.open = state->codeActions.open;
    result.items = state->codeActions.items.els;
    result.nItems = state->codeActions.items.len;
    result.selected = state->codeActions.selected;
    result.revision = state->codeActions.revision;
    return result;
}

HoverPopoverState HoverPopoverState::Of(const InputState* state) {
    HoverPopoverState result;
    if (!state) {
        return result;
    }
    result.symbolRange = state->hoverRange;
    result.hover = state->hoverText;
    result.open = state->hoverText.len > 0;
    return result;
}

Lsp& Lsp::Completion(const CompletionProvider& provider) {
    completionProvider = provider;
    return *this;
}

Lsp& Lsp::AddCodeAction(const CodeActionProvider& provider) {
    if (provider.IsValid()) {
        VecAppend(codeActionProviders, provider);
    }
    return *this;
}

Lsp& Lsp::Hover(const gpui::HoverProvider& provider) {
    hoverProvider = provider;
    return *this;
}

Lsp& Lsp::Definition(const DefinitionProvider& provider) {
    definitionProvider = provider;
    return *this;
}

Lsp& Lsp::DocumentColors(const DocumentColorProvider& provider) {
    documentColorProvider = provider;
    return *this;
}

Lsp& Lsp::SemanticTokens(const DocumentRangeSemanticTokensProvider& provider) {
    semanticTokensProvider = provider;
    return *this;
}

Lsp& Lsp::ShowDocuments(const ShowDocumentHandler& handler) {
    showDocument = handler;
    return *this;
}

Lsp& Lsp::CompletionMenu(const CompletionMenuOptions& options) {
    completionMenu = options;
    return *this;
}

Lsp& Lsp::WithCompletionHighlighter(CompletionHighlighter highlighter) {
    SetCompletionHighlighter(highlighter);
    return *this;
}

void Lsp::SetCompletionHighlighter(CompletionHighlighter highlighter) {
    completionHighlighter = highlighter;
    if (state) {
        state->completionHighlight = completionHighlighter;
        if (state->focusWin) {
            AppInvalidate(state->focusWin);
        }
    }
}

CompletionHighlighter Lsp::GetCompletionHighlighter() const {
    return completionHighlighter;
}

void Lsp::Install(InputState* input) {
    state = input;
    if (!state) {
        return;
    }
    // Reinstalling capabilities replaces the server's provider list.
    VecClear(state->codeActionProviders);
    state->codeActionProvider = nullptr;
    state->codeActionData = nullptr;
    completionProvider.Install(state, completionMenu);
    state->completionHighlight = completionHighlighter;
    hoverProvider.Install(state);
    definitionProvider.Install(state);
    documentColorProvider.Install(state);
    semanticTokensProvider.Install(state);
    showDocument.Install(state);
    for (int i = 0; i < codeActionProviders.len; i++) {
        codeActionProviders[i].Install(state);
    }
}

void Lsp::Update() {
    InputLspUpdate(state);
}

void Lsp::Reset() {
    InputLspReset(state);
}

int Lsp::DocumentColorsForRange(Selection visible, DocumentColor* out,
                                int cap) const {
    if (!state) {
        return 0;
    }
    int total = 0;
    for (int i = 0; i < state->documentColors.len; i++) {
        const DocumentColor& color = state->documentColors[i];
        if (color.range.start >= visible.end ||
            color.range.end <= visible.start ||
            color.range.start >= color.range.end) {
            continue;
        }
        if (out && total < cap) {
            out[total] = color;
        }
        total++;
    }
    return total;
}

int Lsp::SemanticTokensForRange(Selection visible,
                                const HighlightStyleResolver& resolver,
                                TextSpan* out, int cap) const {
    if (!state || state->semanticTokens.len <= 0) {
        return 0;
    }
    Vec<SemanticRange> ranges;
    if (!VecReserve(ranges, state->semanticTokens.len)) {
        return 0;
    }
    int n = gpui::SemanticTokensForRange(
        state->semanticTokens.els, state->semanticTokens.len, InputValue(state),
        visible, ranges.els, ranges.cap);
    int total = 0;
    for (int i = 0; i < n; i++) {
        TextSpan span;
        if (!resolver.Style(ranges[i].name, &span)) {
            continue;
        }
        span.lo = ranges[i].range.start;
        span.hi = ranges[i].range.end;
        if (out && total < cap) {
            out[total] = span;
        }
        total++;
    }
    return total;
}

// chars_eq_ignore_case. Equality, then ASCII case, then a 1:1 lowercase.
// Rust's `char::to_lowercase` also covers the rest of Unicode. Mappings
// that expand (U+0130 İ) are not a single-code-point equality there either
// unless both sides expand the same way; those stay unmatched here.
static uint32_t CompletionLower(uint32_t c) {
    if (c >= 'A' && c <= 'Z') {
        return c + 0x20;
    }
    if ((c >= 0x00c0 && c <= 0x00d6) || (c >= 0x00d8 && c <= 0x00de)) {
        return c + 0x20;
    }
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

static bool CharsEqIgnoreCase(uint32_t left, uint32_t right) {
    if (left == right) {
        return true;
    }
    uint32_t al = left;
    uint32_t ar = right;
    if (al >= 'A' && al <= 'Z') {
        al += 0x20;
    }
    if (ar >= 'A' && ar <= 'Z') {
        ar += 0x20;
    }
    if (al < 0x80 && ar < 0x80 && al == ar) {
        return true;
    }
    return CompletionLower(left) == CompletionLower(right);
}

static int Utf8Next(Str s, int i, uint32_t* cp) {
    if (i < 0 || i >= len(s) || !s.s) {
        return 0;
    }
    unsigned char c = (unsigned char)s.s[i];
    int n = 1;
    uint32_t v = c;
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        n = 2;
        v = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3;
        v = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4;
        v = c & 0x07;
    } else {
        *cp = 0xFFFD;
        return 1;
    }
    if (i + n > len(s)) {
        *cp = 0xFFFD;
        return 1;
    }
    for (int k = 1; k < n; k++) {
        unsigned char cc = (unsigned char)s.s[i + k];
        if ((cc & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return 1;
        }
        v = (v << 6) | (cc & 0x3F);
    }
    *cp = v;
    return n;
}

static bool Utf8Boundary(Str s, int index) {
    if (index < 0 || index > len(s)) {
        return false;
    }
    if (index == 0 || index == len(s)) {
        return true;
    }
    return ((unsigned char)s.s[index] & 0xC0) != 0x80;
}

// str::floor_char_boundary.
static int FloorCharBoundary(Str s, int index) {
    if (index < 0) {
        return 0;
    }
    if (index > len(s)) {
        index = len(s);
    }
    while (index > 0 && !Utf8Boundary(s, index)) {
        index--;
    }
    return index;
}

static int WriteRanges(const ByteRange* ranges, int n, ByteRange* out,
                       int cap) {
    int write = 0;
    if (out && cap > 0) {
        write = n < cap ? n : cap;
        for (int i = 0; i < write; i++) {
            out[i] = ranges[i];
        }
    }
    return n;
}

int HighlightPrefix(void* data, Str query, const CompletionItem* item,
                    ByteRange* out, int cap) {
    (void)data;
    if (!item) {
        return 0;
    }
    int matched = item->hasFilterText ? len(item->filterText) : len(query);
    if (matched > len(item->label)) {
        matched = len(item->label);
    }
    int end = FloorCharBoundary(item->label, matched);
    if (end == 0) {
        return 0;
    }
    ByteRange range = {0, end};
    return WriteRanges(&range, 1, out, cap);
}

int HighlightContains(void* data, Str query, const CompletionItem* item,
                      ByteRange* out, int cap) {
    (void)data;
    if (!item || len(query) == 0 || !query.s || !item->label.s) {
        return 0;
    }
    int queryChars = 0;
    for (int i = 0; i < len(query);) {
        uint32_t cp = 0;
        int adv = Utf8Next(query, i, &cp);
        if (adv <= 0) {
            break;
        }
        i += adv;
        queryChars++;
    }
    if (queryChars == 0) {
        return 0;
    }
    int labelChars = 0;
    for (int i = 0; i < len(item->label);) {
        uint32_t cp = 0;
        int adv = Utf8Next(item->label, i, &cp);
        if (adv <= 0) {
            break;
        }
        i += adv;
        labelChars++;
    }
    if (labelChars < queryChars) {
        return 0;
    }
    int startByte = 0;
    for (int start = 0; start <= labelChars - queryChars; start++) {
        int li = startByte;
        int qi = 0;
        int lastEnd = startByte;
        bool matched = true;
        for (int k = 0; k < queryChars; k++) {
            uint32_t qc = 0;
            uint32_t lc = 0;
            int qa = Utf8Next(query, qi, &qc);
            int la = Utf8Next(item->label, li, &lc);
            if (qa <= 0 || la <= 0 || !CharsEqIgnoreCase(qc, lc)) {
                matched = false;
                break;
            }
            qi += qa;
            li += la;
            lastEnd = li;
        }
        if (matched) {
            ByteRange range = {startByte, lastEnd};
            return WriteRanges(&range, 1, out, cap);
        }
        uint32_t skip = 0;
        int sa = Utf8Next(item->label, startByte, &skip);
        if (sa <= 0) {
            break;
        }
        startByte += sa;
    }
    return 0;
}

int HighlightCharacters(void* data, Str query, const CompletionItem* item,
                        ByteRange* out, int cap) {
    (void)data;
    if (!item || len(query) == 0 || !query.s) {
        return 0;
    }
    Vec<ByteRange> ranges;
    int base = 0;
    Str rest = item->label;
    for (int qi = 0; qi < len(query);) {
        uint32_t qc = 0;
        int qa = Utf8Next(query, qi, &qc);
        if (qa <= 0) {
            break;
        }
        qi += qa;
        int index = 0;
        int matchedLen = 0;
        bool found = false;
        for (int i = 0; i < len(rest);) {
            uint32_t lc = 0;
            int la = Utf8Next(rest, i, &lc);
            if (la <= 0) {
                break;
            }
            if (CharsEqIgnoreCase(qc, lc)) {
                index = i;
                matchedLen = la;
                found = true;
                break;
            }
            i += la;
        }
        if (!found) {
            return 0;
        }
        int start = base + index;
        int end = start + matchedLen;
        int next = index + matchedLen;
        base += next;
        if (rest.s) {
            rest = Str(rest.s + next, len(rest) - next);
        }
        if (len(ranges) > 0 && ranges[len(ranges) - 1].end == start) {
            ranges[len(ranges) - 1].end = end;
        } else {
            ByteRange range = {start, end};
            VecAppend(ranges, range);
        }
    }
    int n = len(ranges);
    WriteRanges(ranges.els, n, out, cap);
    return n;
}

int RetainHighlightRanges(Str label, const ByteRange* ranges, int n,
                          ByteRange* out, int cap) {
    Vec<ByteRange> kept;
    for (int i = 0; i < n; i++) {
        const ByteRange& range = ranges[i];
        if (range.start < range.end && range.end <= len(label) &&
            Utf8Boundary(label, range.start) &&
            Utf8Boundary(label, range.end)) {
            VecAppend(kept, range);
        }
    }
    for (int i = 1; i < len(kept); i++) {
        ByteRange key = kept[i];
        int j = i - 1;
        while (j >= 0 && kept[j].start > key.start) {
            kept[j + 1] = kept[j];
            j--;
        }
        kept[j + 1] = key;
    }
    Vec<ByteRange> merged;
    for (int i = 0; i < len(kept); i++) {
        if (len(merged) > 0 && kept[i].start <= merged[len(merged) - 1].end) {
            if (kept[i].end > merged[len(merged) - 1].end) {
                merged[len(merged) - 1].end = kept[i].end;
            }
        } else {
            VecAppend(merged, kept[i]);
        }
    }
    int total = len(merged);
    WriteRanges(merged.els, total, out, cap);
    return total;
}

} // namespace gpui
