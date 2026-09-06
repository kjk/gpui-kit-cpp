#include "ui/highlighter.h"
#include "ui/input.h"
#include "ui/popover.h"
#include "ui/text.h"
#include "base/positioner.h"
#include "sys/executor.h"

namespace gpui {

namespace component {

// CompletionMenu and CodeActionMenu are retained entities in Rust. Their
// durable data already lives on InputState here; this small keyed entity is
// the listener owner that gives the frame rows the same pointer behavior.
struct InputMenuViewState {
    InputState* input = nullptr;

    static void CompletionClick(InputMenuViewState* self, Ctx* cx,
                                const ClickEvent*, intptr_t ix) {
        if (!self || !self->input || ix < 0 ||
            ix >= self->input->completion.items.len) {
            return;
        }
        self->input->completion.selected = (int)ix;
        InputAcceptCompletion(self->input, cx->app, cx->win);
        Notify(cx);
    }

    static void CompletionHover(InputMenuViewState* self, Ctx* cx,
                                const HoverEvent* event, intptr_t ix) {
        if (!self || !self->input || !event || !event->hovered || ix < 0 ||
            ix >= self->input->completion.items.len ||
            self->input->completion.selected == ix) {
            return;
        }
        self->input->completion.selected = (int)ix;
        Notify(cx);
    }

    static void CompletionOutside(InputMenuViewState* self, Ctx* cx,
                                  const MouseDownEvent*) {
        if (!self || !self->input) return;
        InputDismissCompletion(self->input);
        Notify(cx);
    }

    static void CodeActionClick(InputMenuViewState* self, Ctx* cx,
                                const ClickEvent*, intptr_t ix) {
        if (!self || !self->input || ix < 0 ||
            ix >= self->input->codeActions.items.len) {
            return;
        }
        self->input->codeActions.selected = (int)ix;
        InputPerformCodeAction(self->input, cx->app, cx->win);
        Notify(cx);
    }

    static void CodeActionHover(InputMenuViewState* self, Ctx* cx,
                                const HoverEvent* event, intptr_t ix) {
        if (!self || !self->input || !event || !event->hovered || ix < 0 ||
            ix >= self->input->codeActions.items.len ||
            self->input->codeActions.selected == ix) {
            return;
        }
        self->input->codeActions.selected = (int)ix;
        Notify(cx);
    }

    static void CodeActionOutside(InputMenuViewState* self, Ctx* cx,
                                  const MouseDownEvent*) {
        if (!self || !self->input) return;
        InputDismissCodeActions(self->input);
        Notify(cx);
    }

    static void PopoverOutside(InputMenuViewState* self, Ctx* cx,
                               const MouseDownEvent*) {
        if (!self || !self->input) return;
        self->input->hoverText = {};
        self->input->hoverRange = {};
        self->input->hoverAsked = true;
        self->input->hoverDiagnostic = -1;
        self->input->popoverBounds = {};
        Notify(cx);
    }
};

static Entity<InputMenuViewState> InputMenuView(Ctx* cx, InputState* input,
                                                const char* kind) {
    Entity<InputMenuViewState> entity = ElementStateEntity<InputMenuViewState>(
        cx, fmt("%s-%p", Str(kind), (void*)input),
        StrL("gpui::component::InputMenuViewState"));
    if (InputMenuViewState* state = entity.Get(cx)) state->input = input;
    return entity;
}

Highlighter* Highlighter::New(Ctx* cx, InputState* state) {
    return New(cx, StrL("editor"), state);
}
Highlighter* Highlighter::New(Ctx* cx, Str id, InputState* state) {
    Arena* a = cx->a;
    Highlighter* h = ArenaNew<Highlighter>(a);
    h->a = a;
    h->cx = cx;
    h->id = id;
    h->state = state;
    return h;
}
Highlighter* Highlighter::H(float v) {
    h = v;
    return this;
}
Highlighter* Highlighter::Font(float px) {
    fontSize = px;
    return this;
}
Highlighter* Highlighter::Language(Str name) {
    lang = SyntaxLangFor(name);
    return this;
}
Highlighter* Highlighter::Decorations(const TextSpan* runs, int n) {
    decorations = runs;
    nDecorations = n;
    return this;
}
Highlighter* Highlighter::ActiveLine(bool v) {
    activeLine = v;
    return this;
}
Highlighter* Highlighter::IndentGuides(bool v) {
    indentGuides = v;
    return this;
}

CompletionMenu* CompletionMenu::New(Ctx* cx, InputState* editor) {
    CompletionMenu* menu = ArenaNew<CompletionMenu>(cx->a);
    menu->a = cx->a;
    menu->cx = cx;
    menu->editor = editor;
    return menu;
}

CompletionMenu* CompletionMenu::UpdateQuery(int startOffset, Str value) {
    query = value;
    if (editor && editor->completion.triggerStart < 0) {
        editor->completion.triggerStart = startOffset;
    }
    return this;
}

CompletionMenu* CompletionMenu::Show(int offset, const CompletionItem* items,
                                     int n) {
    if (editor) {
        int start = editor->completion.triggerStart >= 0
                        ? editor->completion.triggerStart
                        : offset;
        InputPresentCompletionItems(editor, start, query, items, n);
        editor->completion.offset = offset;
        if (cx->win) AppInvalidate(cx->win);
    }
    return this;
}

void CompletionMenu::Hide() {
    InputDismissCompletion(editor);
    if (cx && cx->win) AppInvalidate(cx->win);
}

bool CompletionMenu::HandleAction(InputAction action) {
    return editor && InputCompletionAction(editor, cx->app, cx->win, action);
}

El* CompletionMenu::IntoEl() {
    if (!editor || !editor->completion.open ||
        editor->completion.items.len <= 0) {
        return nullptr;
    }
    const Theme& theme = ThemeNow(cx->app);
    float x =
        editor->caretWinX > 0 ? editor->caretWinX - 4.f : editor->inputBounds.x;
    float lineH = editor->lastLineH > 0 ? editor->lastLineH : 20.f;
    float y = editor->caretWinY > 0 ? editor->caretWinY + 4.f
                                    : editor->inputBounds.y + lineH + 4.f;
    float configuredMax = editor->completionMenuMaxW;
    float windowW = cx->win ? WindowSize(cx->win).dipW : 0.f;
    float maxW = configuredMax;
    if (windowW > 0 && windowW - x < maxW) maxW = windowW - x;
    if (maxW < 120.f) maxW = 120.f;
    const float gap = 4.f;
    bool vertical =
        windowW > 0 && x + configuredMax + gap + configuredMax + gap > windowW;
    Entity<InputMenuViewState> view =
        InputMenuView(cx, editor, "completion-menu");

    El* list = PopoverSurface(
        cx,
        Div(a)->FlexCol()->MinW(120)->MaxW(maxW)->MaxH(240)->ClipY()->Pad(4));
    for (int i = 0; i < editor->completion.items.len; i++) {
        const CompletionItem& item = editor->completion.items[i];
        bool selected = i == editor->completion.selected;
        El* row = Div(a)
                      ->FlexRow()
                      ->Gap(8)
                      ->Pad(4)
                      ->ItemsCenter()
                      ->Radius(theme.radius * 0.5f)
                      ->Font(12)
                      ->HoverBg(BackgroundOpacity(theme.tokens.accent, 0.8f))
                      ->OnClick(ListenTo(
                          view, &InputMenuViewState::CompletionClick, i))
                      ->OnHover(ListenTo(
                          view, &InputMenuViewState::CompletionHover, i));
        if (selected) row->Bg(theme.tokens.accent)->Fg(theme.accentFg);
        El* label = TextEl(a, item.label)->LineHeight(1.f);
        int matched = editor->completion.query.len;
        if (matched > item.label.len) matched = item.label.len;
        if (matched > 0) {
            TextSpan* prefix = ArenaNew<TextSpan>(a);
            prefix->lo = 0;
            prefix->hi = matched;
            prefix->color = theme.blue;
            label->Spans(prefix, 1);
        }
        if (item.deprecated) label->Strikethrough();
        row->Child(label);
        if (item.detail.len > 0) {
            El* detail = TextEl(a, item.detail)
                             ->LineHeight(1.f)
                             ->Italic()
                             ->Fg(selected ? theme.accentFg : theme.mutedFg);
            if (item.deprecated) detail->Strikethrough();
            row->Child(detail);
        }
        list->Child(row);
    }
    El* menu = Div(a)->Gap(gap)->ItemsStart()->Child(list);
    vertical ? menu->FlexCol() : menu->FlexRow();

    Str documentation = InputCompletionDocumentation(editor);
    if (documentation.len > 0) {
        if (vertical) {
            for (int i = 0; i < documentation.len; i++) {
                if (documentation.s[i] == '\n') {
                    documentation = Str(documentation.s, i);
                    break;
                }
            }
        }
        TextViewStyle textStyle = TextViewStyle::Default();
        textStyle.WithParagraphGap(8);
        menu->Child(
            PopoverSurface(
                cx,
                Div(a)->W(configuredMax)->MaxH(240)->ClipY()->PadX(8)->PadY(4))
                ->Child(TextView::New(cx, documentation)
                            ->Font(12)
                            ->Style(textStyle)
                            ->Selectable()
                            ->IntoEl()));
    }
    return Div(a)
        ->Fixed()
        ->Left(x)
        ->Top(y)
        ->OnMouseDownOut(ListenTo(view, &InputMenuViewState::CompletionOutside))
        ->Child(menu);
}

CodeActionMenu* CodeActionMenu::New(Ctx* cx, InputState* state) {
    CodeActionMenu* menu = ArenaNew<CodeActionMenu>(cx->a);
    menu->a = cx->a;
    menu->cx = cx;
    menu->state = state;
    return menu;
}

CodeActionMenu* CodeActionMenu::Show(int offset, const CodeActionItem* items,
                                     int n) {
    (void)offset;
    InputPresentCodeActions(state, items, n);
    if (cx->win) AppInvalidate(cx->win);
    return this;
}

void CodeActionMenu::Hide() {
    InputDismissCodeActions(state);
    if (cx && cx->win) AppInvalidate(cx->win);
}

bool CodeActionMenu::HandleAction(InputAction action) {
    return state && InputCodeActionAction(state, cx->app, cx->win, action);
}

El* CodeActionMenu::IntoEl() {
    if (!state || !state->codeActions.open || state->codeActions.items.len <= 0)
        return nullptr;
    const Theme& theme = ThemeNow(cx->app);
    float x =
        state->caretWinX > 0 ? state->caretWinX - 4.f : state->inputBounds.x;
    float lineH = state->lastLineH > 0 ? state->lastLineH : 20.f;
    float y = state->caretWinY > 0 ? state->caretWinY + 4.f
                                   : state->inputBounds.y + lineH + 4.f;
    float windowW = cx->win ? WindowSize(cx->win).dipW : 320.f;
    float maxW = windowW > x ? windowW - x : 120.f;
    if (maxW > 320.f) maxW = 320.f;
    if (maxW < 120.f) maxW = 120.f;
    El* list = PopoverSurface(
        cx,
        Div(a)->FlexCol()->MinW(120)->MaxW(maxW)->MaxH(480)->ClipY()->Pad(4));
    Entity<InputMenuViewState> view =
        InputMenuView(cx, state, "code-action-menu");
    for (int i = 0; i < state->codeActions.items.len; i++) {
        bool selected = i == state->codeActions.selected;
        El* row = Div(a)
                      ->FlexRow()
                      ->W(kFill)
                      ->Gap(8)
                      ->Pad(4)
                      ->ItemsCenter()
                      ->Radius(theme.radius * 0.5f)
                      ->Font(12)
                      ->HoverBg(BackgroundOpacity(theme.tokens.accent, 0.8f))
                      ->OnClick(ListenTo(
                          view, &InputMenuViewState::CodeActionClick, i))
                      ->OnHover(ListenTo(
                          view, &InputMenuViewState::CodeActionHover, i));
        if (selected) row->Bg(theme.tokens.accent)->Fg(theme.accentFg);
        row->Child(TextEl(a, state->codeActions.items[i].title)
                       ->LineHeight(1.f));
        list->Child(row);
    }
    return Div(a)
        ->Fixed()
        ->Left(x)
        ->Top(y)
        ->OnMouseDownOut(ListenTo(view, &InputMenuViewState::CodeActionOutside))
        ->Child(list);
}

DiagnosticPopover* DiagnosticPopover::New(Ctx* cx, InputState* state,
                                          int diagnostic) {
    DiagnosticPopover* popover = ArenaNew<DiagnosticPopover>(cx->a);
    popover->a = cx->a;
    popover->cx = cx;
    popover->state = state;
    popover->diagnostic = diagnostic;
    return popover;
}

El* DiagnosticPopover::IntoEl() {
    if (!state || diagnostic < 0 || diagnostic >= state->diagnostics.len)
        return nullptr;
    const Theme& theme = ThemeNow(cx->app);
    const Diagnostic& item = state->diagnostics[diagnostic];
    Rgba foreground = theme.blue;
    if (item.severity == DiagnosticSeverity::Error)
        foreground = theme.red;
    else if (item.severity == DiagnosticSeverity::Warning)
        foreground = theme.yellow;
    else if (item.severity == DiagnosticSeverity::Hint)
        foreground = theme.cyan;
    Rgba background = RgbaMix(theme.background, foreground, 0.8f);
    TextViewStyle textStyle = TextViewStyle::Default();
    textStyle.WithParagraphGap(8);
    El* body = TextView::New(cx, item.message)
                   ->Font(12)
                   ->Style(textStyle)
                   ->Selectable()
                   ->IntoEl();
    El* surface = Div(a)
                      ->MinW(200)
                      ->MaxW(500)
                      ->MaxH(320)
                      ->ClipY()
                      ->PadX(4)
                      ->PadY(2)
                      ->Radius(theme.radius)
                      ->Bg(background)
                      ->Fg(foreground)
                      ->Border(1, foreground)
                      ->BoundsOut(&state->popoverBounds)
                      ->Child(body);
    Entity<InputMenuViewState> view =
        InputMenuView(cx, state, "diagnostic-popover");
    surface
        ->OnMouseDownOut(ListenTo(view, &InputMenuViewState::PopoverOutside));
    Bounds trigger = state->popoverTriggerBounds;
    if (trigger.w <= 0 || trigger.h <= 0) {
        trigger = {state->hoverDiagnosticX, state->hoverDiagnosticY, 1, 1};
    }
    return Positioner::Side(cx, trigger)
        ->Placement(gpui::Placement::Top)
        ->Align(gpui::Align::Start)
        ->Margin(8)
        ->Child(surface)
        ->IntoEl();
}

HoverPopover* HoverPopover::New(Ctx* cx, InputState* editor,
                                Selection symbolRange, Str hover) {
    HoverPopover* popover = ArenaNew<HoverPopover>(cx->a);
    popover->a = cx->a;
    popover->cx = cx;
    popover->editor = editor;
    popover->symbolRange = symbolRange;
    popover->hover = hover;
    return popover;
}

El* HoverPopover::IntoEl() {
    if (!editor || hover.len <= 0) return nullptr;
    const Theme& theme = ThemeNow(cx->app);
    TextViewStyle textStyle = TextViewStyle::Default();
    textStyle.WithParagraphGap(8);
    El* surface = PopoverSurface(
        cx, Div(a)->MinW(200)->MaxW(500)->MaxH(320)->ClipY()->PadX(8)->PadY(4));
    surface
        ->Child(TextView::New(cx, hover)
                    ->Font(12)
                    ->Style(textStyle)
                    ->Selectable()
                    ->IntoEl())
        ->Fg(theme.foreground)
        ->BoundsOut(&editor->popoverBounds);
    Entity<InputMenuViewState> view =
        InputMenuView(cx, editor, "hover-popover");
    surface
        ->OnMouseDownOut(ListenTo(view, &InputMenuViewState::PopoverOutside));
    Bounds trigger = editor->popoverTriggerBounds;
    if (trigger.w <= 0 || trigger.h <= 0) {
        trigger = {editor->hoverX, editor->hoverY, 1, 1};
    }
    return Positioner::Side(cx, trigger)
        ->Placement(gpui::Placement::Top)
        ->Align(gpui::Align::Start)
        ->Margin(8)
        ->Child(surface)
        ->IntoEl();
}

// HighlightTheme::style — what registry.rs resolves a capture name with.
// Rust's theme holds every tree-sitter capture name; this tree's palette is
// the handful of kinds `syntax.cpp` scans for, so a name is mapped onto one
// of those. A dotted name falls back to its head — `keyword.modifier` is a
// keyword — which is the rule registry.rs applies, and a name nothing
// recognises has no style and is skipped. Shared by the resolver the facade
// projects into the highlighter and by the semantic-token colours.
static bool HighlightNameColor(Str name, ThemeMode mode, Rgba fallback,
                               Rgba* out) {
    static const struct {
        const char* name;
        SyntaxTok tok;
    } kMap[] = {
        {"keyword", SyntaxTok::Keyword},
        {"type", SyntaxTok::Type},
        {"class", SyntaxTok::Type},
        {"struct", SyntaxTok::Type},
        {"enum", SyntaxTok::Type},
        {"interface", SyntaxTok::Type},
        {"function", SyntaxTok::Function},
        {"method", SyntaxTok::Function},
        {"macro", SyntaxTok::Function},
        {"property", SyntaxTok::Property},
        {"variable", SyntaxTok::Property},
        {"parameter", SyntaxTok::Property},
        {"string", SyntaxTok::String},
        {"number", SyntaxTok::Number},
        {"boolean", SyntaxTok::Boolean},
        {"comment", SyntaxTok::Comment},
        {"tag", SyntaxTok::Tag},
        {"attribute", SyntaxTok::Attribute},
        {"title", SyntaxTok::Title},
        {"text.literal", SyntaxTok::Literal},
        {"text.code.span", SyntaxTok::Literal},
    };
    Str head = name;
    for (int pass = 0; pass < 2; pass++) {
        for (const auto& row : kMap) {
            if (base::StrEqI(head, row.name)) {
                *out = SyntaxTokColor(row.tok, mode, fallback);
                return true;
            }
        }
        // Try the head of a dotted name once, and then give up.
        int dot = -1;
        for (int i = 0; i < head.len; i++) {
            if (head.s[i] == '.') {
                dot = i;
                break;
            }
        }
        if (dot < 0) {
            break;
        }
        head = Str(head.s, dot);
    }
    return false;
}

/* The lexer-backed InputHighlighter — this tree's
   crates/ui/src/highlighter/input_adapter.rs, without tree-sitter. Upstream
   wraps a tree-sitter parser behind the parser-independent seam
   crates/base defines (and even there tree-sitter is an optional cargo
   feature — base depends on no parser, and the wasm build highlights
   nothing); this wraps the scanner in syntax.cpp behind the same seam. A
   tree-sitter port would be a second implementation beside it.

   What is cached: the document's token runs and the brace-pair fold
   candidates, both produced by one lex per document version. Runs keep the
   token *kind*; styles() resolves kinds to colours from the palette the
   facade projects each render, so a theme flip restyles without a re-lex —
   the resolver half of Rust's `styles(range, resolver)`, folded into the
   implementation. */

struct HlRun {
    int lo;
    int hi;
    SyntaxTok tok;
};

struct SynHlJob;

struct SyntaxInputHighlighter {
    SyntaxLang lang = SyntaxLangNone;
    uint64_t version = 0;
    bool valid = false;
    // Ordered, non-overlapping, Text runs omitted — a gap is unstyled.
    Vec<HlRun> runs;
    Vec<FoldRange> folds;
    // The palette styles() resolves with, projected by the facade every
    // render the way crates/ui projects the whole editor style.
    ThemeMode mode = ThemeMode::Light;
    Rgba foreground = {};
    // The background lex in flight for a document past the sync budget, and
    // the debounce in front of it — input_adapter.rs's parse_task and
    // PARSE_DEBOUNCE, over ExecSpawn instead of the background executor.
    SynHlJob* flight = nullptr;
    TaskId flightTask = 0;
    double lexDueAt = 0;
    uint64_t lexDueVersion = ~(uint64_t)0;
};

/* input_adapter.rs's policy, constants and all: a document at or under
   SYNC_PARSE_MAX_BYTES is lexed synchronously in update(); a bigger one
   keeps showing its stale styles, waits out PARSE_DEBOUNCE, and is lexed on
   the pool from a snapshot — the frame is the debounce clock, the way the
   hover delay keeps time. What the simplification drops is the 2 ms
   sync-parse timeout: the lexer cannot stop halfway the way tree-sitter's
   progress callback can, so the size cap alone decides which side a
   document lands on. */
static const int kSyncLexMaxBytes = 256 * 1024;
static const double kLexDebounce = 0.150;

// The snapshot a pool thread lexes. The worker touches only the text and
// the two result Vecs; `hl` is read and written on the main thread alone,
// and SynHlDrop clears it when the implementation dies first — the job then
// lands orphaned and frees itself.
struct SynHlJob {
    SyntaxInputHighlighter* hl = nullptr;
    App* app = nullptr;
    EntityId view = {};
    uint64_t version = 0;
    SyntaxLang lang = SyntaxLangNone;
    char* text = nullptr; // owned
    int len = 0;
    Vec<HlRun> runs;
    Vec<FoldRange> folds;
};

/* Fold candidates — upstream's extract_fold_ranges walks the tree-sitter
   tree and offers every named node spanning two rows or more; with no tree,
   this counts brace pairs outside strings and comments, the way the
   showcase's own highlighter does (`brace_fold_ranges` in
   examples/showcase/syntect_highlighter.rs), sharing the one lex that also
   produces the token runs. A language with no braces has no candidates,
   which is where this stops short of the tree: Rust would fold a Python
   suite and this cannot see one. */
static void SynHlLexInto(SyntaxLang lang, Str text, Vec<HlRun>* runs,
                         Vec<FoldRange>* folds) {
    VecClear(*runs);
    VecClear(*folds);
    if (lang == SyntaxLangNone || text.len == 0) {
        return;
    }
    // Brace nesting deeper than this folds no further; startLine per open
    // brace, and the line each byte is on walked alongside the scan.
    int starts[64];
    int nOpen = 0;
    int line = 0;
    int at = 0;
    SyntaxLexer lx;
    SyntaxLexStart(&lx, lang, text);
    while (SyntaxLexNext(&lx)) {
        int tokStart = (int)(lx.text.s - text.s);
        for (; at < tokStart && at < text.len; at++) {
            if (text.s[at] == '\n') {
                line++;
            }
        }
        bool literal =
            lx.tok == SyntaxTok::String || lx.tok == SyntaxTok::Comment;
        for (int i = 0; i < lx.text.len; i++) {
            char c = lx.text.s[i];
            if (c == '\n') {
                line++;
                continue;
            }
            if (literal) {
                continue;
            }
            if (c == '{') {
                if (nOpen < (int)(sizeof(starts) / sizeof(starts[0]))) {
                    starts[nOpen] = line;
                }
                nOpen++;
            } else if (c == '}' && nOpen > 0) {
                nOpen--;
                if (nOpen >= (int)(sizeof(starts) / sizeof(starts[0]))) {
                    continue;
                }
                int startLine = starts[nOpen];
                // A block that opens and closes on one line has nothing to
                // hide, and Rust drops it the same way.
                if (startLine < line) {
                    FoldRange fr;
                    fr.startLine = startLine;
                    fr.endLine = line;
                    VecAppend(*folds, fr);
                }
            }
        }
        at = tokStart + lx.text.len;
        if (lx.tok == SyntaxTok::Text) {
            continue;
        }
        // Adjacent runs of one kind are one run, which keeps a document of a
        // few hundred lines to a few hundred of them.
        int hi = tokStart + lx.text.len;
        if (runs->len > 0 && (*runs)[runs->len - 1].hi == tokStart &&
            (*runs)[runs->len - 1].tok == lx.tok) {
            (*runs)[runs->len - 1].hi = hi;
            continue;
        }
        HlRun run;
        run.lo = tokStart;
        run.hi = hi;
        run.tok = lx.tok;
        VecAppend(*runs, run);
    }
}

static Str SynHlLanguage(void* data) {
    return SyntaxLangName(((SyntaxInputHighlighter*)data)->lang);
}

static void SynHlUpdate(void* data, const InputEdit* edit, Str text,
                        bool folding) {
    auto* hl = (SyntaxInputHighlighter*)data;
    // A tree-sitter implementation would hand `edit` to its tree and reparse
    // incrementally; the lexer keeps no incremental state and re-lexes the
    // document whole. The facade gates calls on docVersion, which is
    // upstream's `self.text.eq(text)` early-out one level up, and the folds
    // ride along in the same pass whether or not the gutter shows them.
    (void)edit;
    (void)folding;
    SynHlLexInto(hl->lang, text, &hl->runs, &hl->folds);
}

// The pool half and the landing half of the background lex. The worker
// touches only the snapshot it was handed; the landing runs on the main
// thread, swaps the answer in unless the implementation died first, and
// notifies the view the way FpsResourceDone does — a stale entity makes
// that a no-op rather than a crash.
static void SynHlLexWork(SynHlJob* job) {
    SynHlLexInto(job->lang, Str(job->text, job->len), &job->runs, &job->folds);
}

static void SynHlLexDone(SynHlJob* job) {
    SyntaxInputHighlighter* hl = job->hl;
    if (hl) {
        hl->flight = nullptr;
        hl->flightTask = 0;
        VecClear(hl->runs);
        if (job->runs.len > 0) {
            if (HlRun* dst = VecAppendBlanks(hl->runs, job->runs.len)) {
                memcpy(dst, job->runs.els,
                       (size_t)job->runs.len * sizeof(HlRun));
            }
        }
        VecClear(hl->folds);
        if (job->folds.len > 0) {
            if (FoldRange* dst = VecAppendBlanks(hl->folds, job->folds.len)) {
                memcpy(dst, job->folds.els,
                       (size_t)job->folds.len * sizeof(FoldRange));
            }
        }
        hl->valid = true;
        hl->version = job->version;
        NotifyEntity(job->app, job->view, nullptr);
    }
    Free(nullptr, job->text);
    delete job;
}

// The canonical capture name each token kind carries, which is what the
// resolver is asked about — upstream's queries capture `@keyword`,
// `@string` and the rest, and registry.rs resolves those names.
static Str SynHlTokName(SyntaxTok tok) {
    switch (tok) {
        case SyntaxTok::Keyword:
            return StrL("keyword");
        case SyntaxTok::Type:
            return StrL("type");
        case SyntaxTok::Function:
            return StrL("function");
        case SyntaxTok::Property:
            return StrL("property");
        case SyntaxTok::String:
            return StrL("string");
        case SyntaxTok::Number:
            return StrL("number");
        case SyntaxTok::Boolean:
            return StrL("boolean");
        case SyntaxTok::Comment:
            return StrL("comment");
        case SyntaxTok::Tag:
            return StrL("tag");
        case SyntaxTok::Attribute:
            return StrL("attribute");
        case SyntaxTok::Title:
            return StrL("title");
        case SyntaxTok::Literal:
            return StrL("text.literal");
        default:
            return Str{};
    }
}

static int SynHlStyles(void* data, Selection range,
                       const HighlightStyleResolver* resolver, Arena* a,
                       TextSpan** out) {
    auto* hl = (SyntaxInputHighlighter*)data;
    *out = nullptr;
    if (hl->runs.len == 0 || range.end <= range.start) {
        return 0;
    }
    // The first run that ends after the range starts, then every run that
    // begins before it ends.
    int lo = 0;
    int hi = hl->runs.len;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (hl->runs[mid].hi <= range.start) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    int first = lo;
    int count = 0;
    while (first + count < hl->runs.len && hl->runs[first + count]
                                                   .lo < range.end) {
        count++;
    }
    if (count == 0) {
        return 0;
    }
    auto* spans = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * count);
    if (!spans) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < count; i++) {
        const HlRun& r = hl->runs[first + i];
        int spanLo = r.lo < range.start ? range.start : r.lo;
        int spanHi = r.hi > range.end ? range.end : r.hi;
        if (spanHi <= spanLo) {
            continue;
        }
        // The resolver first — upstream looks the capture name up in the
        // HighlightTheme — and the scanner's own palette where it has no
        // answer or there is none.
        Rgba c = SyntaxTokColor(r.tok, hl->mode, hl->foreground);
        TextSpan resolved = {};
        if (resolver && resolver->Style(SynHlTokName(r.tok), &resolved)) {
            c = resolved.color;
        }
        // Two kinds the palette gives one colour still merge.
        if (n > 0 && spans[n - 1].hi == spanLo &&
            RgbaEq(spans[n - 1].color, c)) {
            spans[n - 1].hi = spanHi;
            continue;
        }
        TextSpan sp = {};
        sp.lo = spanLo;
        sp.hi = spanHi;
        sp.color = c;
        sp.bg = Rgba8(0, 0, 0, 0);
        spans[n++] = sp;
    }
    *out = spans;
    return n;
}

static int SynHlFoldRanges(void* data, Str, Selection, Arena* a,
                           FoldRange** out) {
    // fold_ranges_for_edit's changedRange refinement is unused: the whole
    // list is cached, so answering all of it is a copy either way.
    auto* hl = (SyntaxInputHighlighter*)data;
    *out = nullptr;
    if (hl->folds.len == 0) {
        return 0;
    }
    auto* folds = (FoldRange*)Alloc(a, (int)sizeof(FoldRange) * hl->folds.len);
    if (!folds) {
        return 0;
    }
    memcpy(folds, hl->folds.els, (size_t)hl->folds.len * sizeof(FoldRange));
    *out = folds;
    return hl->folds.len;
}

static void SynHlDrop(void* data) {
    auto* hl = (SyntaxInputHighlighter*)data;
    // A flight in the air is orphaned, not waited for: the landing sees the
    // null and frees the job. One still in the queue never runs at all —
    // ExecCancel true is the fps destructor's cue to free it here.
    if (hl->flight) {
        hl->flight->hl = nullptr;
        if (hl->flightTask && ExecCancel(hl->flightTask)) {
            Free(nullptr, hl->flight->text);
            delete hl->flight;
        }
    }
    delete hl;
}

// input_highlighter_factory: what the factory answers for a language this
// tree can scan. Installed onto the state the first time the facade renders
// with a language, retargeted when the language changes, dropped when there
// is none — upstream returns None from the factory then, and an editor with
// no highlighter neither styles nor folds.
static SyntaxInputHighlighter* SynHlEnsure(InputState* s, SyntaxLang lang) {
    if (lang == SyntaxLangNone) {
        if (s->highlighter.drop) {
            s->highlighter.drop(s->highlighter.data);
        }
        s->highlighter = InputHighlighter{};
        return nullptr;
    }
    if (s->highlighter.data && s->highlighter.update == &SynHlUpdate) {
        auto* hl = (SyntaxInputHighlighter*)s->highlighter.data;
        if (hl->lang != lang) {
            hl->lang = lang;
            hl->valid = false;
        }
        return hl;
    }
    if (s->highlighter.drop) {
        s->highlighter.drop(s->highlighter.data);
    }
    auto* hl = new SyntaxInputHighlighter();
    hl->lang = lang;
    s->highlighter.data = hl;
    s->highlighter.language = &SynHlLanguage;
    s->highlighter.update = &SynHlUpdate;
    s->highlighter.styles = &SynHlStyles;
    s->highlighter.foldRanges = &SynHlFoldRanges;
    s->highlighter.drop = &SynHlDrop;
    return hl;
}

// The resolver the facade projects into styles(): capture names looked up
// in the scanner's palette, the same table SemanticTokColor answers from —
// what registry.rs's HighlightTheme is here.
struct SynHlResolver {
    ThemeMode mode = ThemeMode::Light;
    Rgba foreground = {};
};

static bool SynHlResolveStyle(void* data, Str name, TextSpan* out) {
    auto* r = (SynHlResolver*)data;
    Rgba c;
    if (!HighlightNameColor(name, r->mode, r->foreground, &c)) {
        return false;
    }
    *out = TextSpan{};
    out->color = c;
    return true;
}

// The names a semantic token carries, resolved with the same table.
static bool SemanticTokColor(Ctx* cx, Str name, Rgba* out) {
    const Theme& th = ThemeNow(cx->app);
    return HighlightNameColor(name, ThemeGet(cx->app), th.foreground, out);
}

// The semantic tokens a provider published, over the document, as runs the
// rows slice out of — the same shape the language's own captures come in.
static int SemanticSpans(Ctx* cx, InputState* state, Str text, TextSpan* out,
                         int cap) {
    if (!state || state->semanticTokens.len == 0 || !text.s) {
        return 0;
    }
    const int kWindow = std::min(1024, state->semanticTokens.len);
    auto* window =
        (SemanticRange*)Alloc(cx->a, (int)sizeof(SemanticRange) * kWindow);
    if (!window) {
        return 0;
    }
    // Rust windows to what is on screen; the rows here are built from spans
    // over the whole document, so the whole document is the window.
    int n = SemanticTokensForRange(state->semanticTokens.els,
                                   state->semanticTokens.len, text,
                                   Selection{0, text.len}, window, kWindow);
    int m = 0;
    for (int i = 0; i < n && m < cap; i++) {
        Rgba c;
        if (!SemanticTokColor(cx, window[i].name, &c)) {
            continue;
        }
        out[m].lo = window[i].range.start;
        out[m].hi = window[i].range.end;
        out[m].color = c;
        out[m].bg = Rgba8(0, 0, 0, 0);
        out[m].underline = false;
        out[m].wavy = false;
        m++;
    }
    return m;
}

Highlighter* Highlighter::Diagnostics(const Diagnostic* items, int n) {
    diagnostics = items;
    nDiagnostics = n;
    return this;
}

Highlighter* Highlighter::Folding(bool v) {
    folding = v;
    return this;
}

Highlighter* Highlighter::Searchable(bool v) {
    searchable = v;
    return this;
}

El* Highlighter::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    if (state) {
        state->searchable = searchable;
    }
    InputEditorStyle style;
    style.foreground = th.foreground;
    style.mutedForeground = th.mutedFg;
    style.caret = th.caret;
    style.selection = RgbaOpacity(th.selection, 0.4f);
    // theme.mono_font_size, which is 13 rather than the 12 this drew at: a
    // narrower row is a row that does not soft-wrap where Rust's does. A
    // caller that set a size of its own refines over it, and the rows follow
    // that size — Rust's `line_height(relative(1.5))` on the editor.
    style.fontSize = fontSize > 0 ? fontSize : 13;
    // .font_family(theme.mono_font_family).text_size(theme.mono_font_size)
    style.mono = true;
    if (activeLine) {
        style.activeLine = RgbaOpacity(th.accent, 0.4f);
    }
    if (indentGuides) {
        style.indentGuide = RgbaOpacity(th.border, 0.8f);
    }
    // EditorStyle::diagnostics — theme.danger, warning, info and the muted
    // foreground a hint is drawn in.
    // highlighter/registry.rs's own defaults for the status colours a
    // diagnostic is drawn in.
    // hover_definition_style: Rust takes `link_text` out of the highlight
    // theme, which this tree's scanner palette has no entry for — the UI
    // theme's link colour is the same blue a Link is drawn in.
    style.linkText = th.blue;
    // What a ghost line covers the row under it with.
    style.background = th.background;
    style.diagnostics.error = th.red;
    style.diagnostics.warning = th.yellow;
    style.diagnostics.info = th.blue;
    style.diagnostics.hint = th.cyan;
    if (state) {
        // The set the caller published, kept on the state so the row builder
        // and a hover both read the same one.
        VecClear(state->diagnostics);
        for (int i = 0; i < nDiagnostics; i++) {
            VecAppend(state->diagnostics, diagnostics[i]);
        }
    }
    if (state) {
        // This façade is Rust's code editor — the highlighter is bound to
        // an EditorState, whose LayoutMode is CodeEditor — so it is what
        // says so, rather than every caller having to.
        state->mode.kind = LayoutModeKind::CodeEditor;
        state->mode.folding = folding;
    }
    Str text = state ? InputValue(state) : Str{};
    if (state) {
        // drive_highlighter: install the implementation for the language,
        // project the palette onto it, and hand over the edit envelope the
        // text funnels recorded. The call is gated on docVersion — the
        // flat-buffer spelling of upstream update()'s text-equality
        // early-out — so driving it every render costs nothing.
        SyntaxInputHighlighter* hl = SynHlEnsure(state, lang);
        if (hl) {
            hl->mode = ThemeGet(cx->app);
            hl->foreground = th.foreground;
            if (!hl->valid || hl->version != state->docVersion) {
                if (text.len <= kSyncLexMaxBytes) {
                    InputEdit whole = {};
                    whole.oldEndByte = -1;
                    whole.newEndByte = text.len;
                    const InputEdit* edit =
                        state->hasPendingEdit ? &state->pendingEdit : &whole;
                    state->highlighter.Update(edit, text, folding);
                    hl->valid = true;
                    hl->version = state->docVersion;
                } else if (!hl->flight) {
                    // Past the sync budget: stale styles stay up while the
                    // debounce runs out, then a snapshot goes to the pool.
                    // The frame is the clock, as it is for the hover delay.
                    // A version that moves while a flight is up is caught
                    // when it lands: the landed version still differs from
                    // docVersion, so this path debounces again.
                    if (hl->lexDueVersion != state->docVersion) {
                        hl->lexDueVersion = state->docVersion;
                        hl->lexDueAt = TimeNow() + kLexDebounce;
                    }
                    if (TimeNow() < hl->lexDueAt) {
                        WindowRequestAnimationFrame(cx->win);
                    } else {
                        auto* job = new SynHlJob();
                        job->hl = hl;
                        job->app = cx->app;
                        job->view = cx->self;
                        job->version = state->docVersion;
                        job->lang = hl->lang;
                        job->text = (char*)Alloc(nullptr, text.len + 1);
                        if (job->text) {
                            memcpy(job->text, text.s, (size_t)text.len);
                            job->text[text.len] = 0;
                            job->len = text.len;
                            hl->flightTask =
                                ExecSpawn(MkFunc0(SynHlLexWork, job),
                                          MkFunc0(SynHlLexDone, job));
                        }
                        if (job->text && hl->flightTask) {
                            hl->flight = job;
                        } else {
                            Free(nullptr, job->text);
                            delete job;
                        }
                    }
                }
            }
            state->hasPendingEdit = false;
            if (folding) {
                FoldRange* ranges = nullptr;
                int nRanges = state->highlighter.FoldRanges(
                    text, Selection{0, text.len}, a, &ranges);
                InputSetFoldCandidates(state, ranges, nRanges);
            }
            // highlight_styles: how the element resolves what the
            // highlighter answers, projected fresh each render the way
            // crates/ui projects the whole editor style.
            auto* resolver = ArenaNew<SynHlResolver>(a);
            resolver->mode = hl->mode;
            resolver->foreground = th.foreground;
            style.highlightStyles.data = resolver;
            style.highlightStyles.style = &SynHlResolveStyle;
        }
    }
    // The decoration collection the element lays over what the highlighter
    // answers: the semantic tokens first, the caller's runs over them —
    // Rust's `combine_highlights` folds overlapping styles out of a HashSet,
    // so which wins where both speak is not defined there. It is here: what
    // the server said wins, which is what the protocol means by layering
    // semantic tokens over a lexer. The syntax runs no longer pass through
    // this list — the element queries the highlighter for the visible range
    // (Textarea::New) — so the cap bounds only the decorations, which are
    // tens of runs, never a document's worth.
    const int kMaxSpans = 4096;
    int semanticCap = state ? std::min(1024, state->semanticTokens.len) : 0;
    int cap = std::min(kMaxSpans,
                       semanticCap + 2 * std::min(kMaxSpans, nDecorations));
    auto* spans =
        cap > 0 ? (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * cap) : nullptr;
    int n = spans ? SemanticSpans(cx, state, text, spans, semanticCap) : 0;
    if (nDecorations > 0 && spans) {
        auto* tmp = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * cap);
        if (tmp) {
            n = InputComposeSpans(spans, n, decorations, nDecorations, cap,
                                  tmp);
        }
    }
    style.spans = n > 0 ? spans : nullptr;
    style.nSpans = n;
    // layout_search_matches: the matches are painted only while the bar is
    // open, which is when Rust builds paths for them at all.
    if (state && state->search.open) {
        const SearchMatcher* m = &state->search.matcher;
        style.matches = m->ranges.els;
        style.nMatches = m->ranges.len;
        style.currentMatch = SearchMatcherIndex(m);
        style.matchBg = RgbaOpacity(th.warning, 0.35f);
        style.currentMatchBg = RgbaOpacity(th.warning, 0.75f);
    }
    // The rows are virtualized against the box they scroll in, and paint only
    // learns its height a frame later; the builder knows it now.
    if (state && h > 0) {
        state->viewH = h;
    }
    El* editor = gpui::Editor::New(cx, state, style);
    El* scroller = editor;
    if (h > 0) {
        // The scroll handle is the editor's: the rows slide under this box as
        // the caret moves, and the wheel moves them too. ScrollFromPath is
        // what the thumb drag looks the box up by next frame — without it
        // scrollId stays 0 and a grab does not move. A field that wraps has
        // nothing to reach sideways; one that does not is as wide as its
        // longest row, the way Textarea::IntoEl hangs ScrollX off !softWrap.
        scroller =
            InputBase::New(cx, id, true, AccessibilityRole::MultilineTextInput)
                ->BindInput(state)
                ->FlexCol()
                ->W(kFill)
                ->H(h)
                ->ClipY()
                ->ScrollY(state ? state->scrollY : 0)
                ->ScrollFromPath();
        if (state && !state->softWrap) {
            scroller->ScrollX(state->scrollX);
        }
        scroller->Child(editor);
    }
    El* completionMenu = CompletionMenu::New(cx, state)->IntoEl();
    if (!completionMenu) {
        completionMenu = CodeActionMenu::New(cx, state)->IntoEl();
    }

    El* diagPopover = nullptr;
    if (state) {
        diagPopover = DiagnosticPopover::New(cx, state, state->hoverDiagnostic)
                          ->IntoEl();
        if (!diagPopover && state->hoverDiagnostic < 0) {
            diagPopover = HoverPopover::New(cx, state, state->hoverRange,
                                            state->hoverText)
                              ->IntoEl();
        }
    }
    if (completionMenu) {
        // The menu is over the rows either way, so it goes in beside them
        // rather than inside the scroller.
        El* box =
            Div(a)->FlexCol()->W(kFill)->Child(scroller)->Child(completionMenu);
        if (diagPopover) {
            box->Child(diagPopover);
        }
        if (!searchable) {
            return box;
        }
        // `v_flex().size_full().children(search_panel)` -- the bar is a
        // sibling of the editor under a box that names itself nothing, and
        // upstream's `.id("search-panel")` is a bare constant because the
        // panel is a view of its own and rendering an entity pushes its
        // identity on the stack. The port has no entity to push, so the
        // editor's name is what stands in for it.
        return Div(a)
            ->FlexCol()
            ->W(kFill)
            ->Child(SearchPanel::New(cx, StrDup(a, fmt("%s-search", id)), state)
                        ->IntoEl())
            ->Child(box);
    }
    if (!searchable) {
        if (diagPopover) {
            return Div(a)->FlexCol()->W(kFill)->Child(scroller)->Child(
                diagPopover);
        }
        return scroller;
    }
    // The bar sits over the rows and takes its height off them, the way
    // Rust's overlay docks it at the top of the input.
    El* box =
        Div(a)
            ->FlexCol()
            ->W(kFill)
            ->Child(SearchPanel::New(cx, StrDup(a, fmt("%s-search", id)), state)
                        ->IntoEl())
            ->Child(scroller);
    if (diagPopover) {
        box->Child(diagPopover);
    }
    return box;
}

} // namespace component
} // namespace gpui
