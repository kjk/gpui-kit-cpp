/* crates/story/examples/markdown.rs — the markdown source on the left, what
   it renders on the right, and a status bar under both.

   The two panes are one `h_resizable` with the editor in the first and a
   TextView over the editor's own text in the second, so every keystroke on
   the left is reparsed and redrawn on the right. The editor is the code
   editor with line numbers, a two-space tab and the find bar on ctrl-f, and
   the fenced blocks in the preview carry the actions the Rust example hangs
   on them: a Clipboard for the code, and a Run button on the two languages
   it knows. Two of its markdown plugins are registered too — a `$SYMBOL`
   paragraph draws as a quote card, and a `<UserCard id=".." />` block as a
   card with an avatar and a Follow button that remembers being pressed.

   Rust marks TODO / FIXME / XXX / HACK / NOTE in the source through an
   LSP-style `DocumentRangeSemanticTokensProvider`. There is no language
   server here, so the same marks are found by scanning the text and handed to
   the editor as a decoration collection — the seam `create_decorations_
   collection` uses, and the colours are the same five syntax token types
   upstream maps the markers to.

   Selection: Plain / Source is in the status bar beside them. Upstream does
   not map a selection back to the source it was parsed from either — it
   rebuilds the markdown out of the block tree it rendered (node.rs
   `text_by_kind`) — and so does this, with each painted run carrying the
   marks and the block fences around it (gpui::SelSource).

   What this does not have, and why:

   - **KaTeX**: upstream's math plugin renders a formula by handing it to
     KaTeX in node and reading the SVG back. The plugin is here and takes the
     example's own fallback path instead — the formula set italic with the
     Greek names spelled out and the scripts folded into Unicode. */

#include "gpui.h"

using namespace gpui;

// MARKERS, and the highlight-theme token type each is drawn as.
struct MarkerDef {
    const char* word;
    component::SyntaxTok tok;
};

static const MarkerDef kMarkers[] = {
    {"TODO", component::SyntaxTok::Keyword},
    {"FIXME", component::SyntaxTok::String},
    {"XXX", component::SyntaxTok::Number},
    {"HACK", component::SyntaxTok::Function},
    {"NOTE", component::SyntaxTok::Type},
};
static const int kMarkerCount = (int)(sizeof(kMarkers) / sizeof(kMarkers[0]));

// Room for the markers one document can hold; the fixture has a handful.
static const int kMaxMarkers = 256;

struct MarkdownApp {
    InputState source;
    float previewScroll = 0;
    // Which of the two table layouts the preview uses. Rust defaults to the
    // scrolling one and the status bar's button says which is on.
    bool tableWrap = false;
    // What a copy of a selection in the preview says: the text as rendered,
    // or the markdown it was rendered from. Rust defaults to Plain.
    SelectionFormat selFormat = SelectionFormat::Plain;
    // The last link the preview reported, shown in the status bar — Rust
    // prints it and opens the URL; opening a browser mid-demo is not what a
    // screenshot wants, so this says the handler ran instead.
    char lastLink[512] = {};
    bool seeded = false;

    static El* Render(MarkdownApp* self, Ctx* cx);
};

// memcmp over the document, from `from`, since the editor's text is not a
// C string and the markers are plain words.
static int FindFrom(Str hay, Str needle, int from) {
    for (int i = from; i + len(needle) <= len(hay); i++) {
        if (StrEq(Str(hay.s + i, len(needle)), needle)) {
            return i;
        }
    }
    return -1;
}

// The provider's job, without the provider: every marker word in the text,
// as a decoration in the colour its token type paints.
static int FindMarkers(Ctx* cx, Str text, TextSpan* out, int cap) {
    const Theme& th = ThemeNow(cx->app);
    ThemeMode mode = ThemeGet(cx->app);
    int n = 0;
    for (int i = 0; i < kMarkerCount && n < cap; i++) {
        Str word = Str(kMarkers[i].word);
        int at = 0;
        while (n < cap) {
            int lo = FindFrom(text, word, at);
            if (lo < 0) {
                break;
            }
            out[n].lo = lo;
            out[n].hi = lo + len(word);
            out[n].color =
                component::SyntaxTokColor(kMarkers[i].tok, mode, th.foreground);
            out[n].bg = Rgba8(0, 0, 0, 0);
            out[n].underline = false;
            n++;
            at = lo + len(word);
        }
    }
    // The decorations arrive in document order, which is what the merge with
    // the language's own captures walks both lists in — and what a semantic
    // tokens response is, since it is delta-encoded from the one before it.
    for (int i = 1; i < n; i++) {
        TextSpan sp = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].lo > sp.lo) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = sp;
    }
    return n;
}

// ─── the three plugins the Rust example registers ─────────────────────────

// TickerQuote: what the story hands the plugin, since there is no market
// behind it.
struct TickerQuote {
    const char* symbol;
    const char* name;
    float price;
    float change;
};

static const TickerQuote kQuotes[] = {
    {"AAPL.US", "Apple Inc.", 300.21f, 5.2f},
    {"TSLA.US", "Tesla, Inc.", 412.05f, -2.13f},
};

static const TickerQuote* QuoteFor(Str symbol) {
    for (const TickerQuote& q : kQuotes) {
        Str name = Str(q.symbol);
        if (StrEq(name, symbol)) {
            return &q;
        }
    }
    return nullptr;
}

// ticker_symbol: `$` then letters, digits and at least one dot.
static bool TickerSymbol(Str text, Str* out) {
    if (len(text) < 2 || text.s[0] != '$') {
        return false;
    }
    Str sym = Str(text.s + 1, len(text) - 1);
    bool dot = false;
    for (int i = 0; i < len(sym); i++) {
        char c = sym.s[i];
        bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9');
        if (c == '.') {
            dot = true;
        } else if (!alnum) {
            return false;
        }
    }
    if (!dot) {
        return false;
    }
    *out = sym;
    return true;
}

static bool TickerParse(Ctx* cx, component::MdNode* n, Str text, void*,
                        component::MdPluginNode* out) {
    (void)cx;
    // A paragraph whose one child is text: `[Node::Text(text)]` in Rust.
    if (n->kind != component::MdKind::Paragraph || !n->runFirst ||
        n->runFirst->next) {
        return false;
    }
    Str sym;
    if (!TickerSymbol(text, &sym)) {
        return false;
    }
    out->text = text;
    out->markdown = text;
    out->data = (void*)QuoteFor(sym);
    if (!out->data) {
        // An unknown symbol still draws, the way Rust's `_ =>` arm does.
        static const TickerQuote unknown = {"", "Unknown", 0.f, 0.f};
        out->data = (void*)&unknown;
    }
    return true;
}

static El* TickerRender(Ctx* cx, const component::MdPluginNode* node, void*) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const auto* q = (const TickerQuote*)node->data;
    bool up = q->change >= 0.f;
    Rgba trend = up ? th.green : th.red;

    El* head = Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween();
    El* names = Div(a)->FlexCol()->Gap(4);
    names->Child(TextEl(a, node->text)->Font(14)->LineHeight(1.f)->Semibold());
    names->Child(
        TextEl(a, Str(q->name))->Font(12)->LineHeight(1.f)->Fg(th.mutedFg));
    head->Child(names);
    El* chip = Div(a)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->Gap(2)
                   ->PadX(4)
                   ->PadY(2)
                   ->Radius(th.radius)
                   ->Bg(RgbaOpacity(trend, 0.12f))
                   ->Fg(trend);
    chip->Child(IconEl(a, up ? IconName::ArrowUp : IconName::ArrowDown, 12));
    chip->Child(TextEl(a, StrDup(a, fmt("%+.1f%%", (double)q->change)))
                    ->Font(12)
                    ->LineHeight(1.f)
                    ->Medium());
    head->Child(chip);

    El* last = Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween();
    last->Child(TextEl(a, StrDup(a, fmt("%.2f", (double)q->price)))
                    ->Font(18)
                    ->LineHeight(1.f)
                    ->Semibold());
    last->Child(
        TextEl(a, StrL("Last"))->Font(12)->LineHeight(1.f)->Fg(th.mutedFg));

    return Div(a)
        ->FlexCol()
        ->W(240)
        ->Gap(6)
        ->PadX(12)
        ->PadY(8)
        ->Radius(th.radius)
        ->Border(1, th.border)
        ->Bg(th.tokens.background)
        ->Child(head)
        ->Child(last);
}

// The two people the example knows, by the id its `<UserCard />` names.
struct UserCardDef {
    const char* id;
    const char* name;
    const char* avatar;
};

static const UserCardDef kUsers[] = {
    {"huacnlee", "Jason Lee",
     "https://avatars.githubusercontent.com/u/5518?v=4"},
    {"madcodelife", "Floyd Wang",
     "https://avatars.githubusercontent.com/u/28998859?v=4"},
};

// html_tag_name / html_attr: the two readings of a raw block the Rust example
// makes, without a regex.
static bool HtmlTagIs(Str raw, const char* name) {
    int at = 0;
    while (at < len(raw) && (raw.s[at] == ' ' || raw.s[at] == '\n')) {
        at++;
    }
    if (at >= len(raw) || raw.s[at] != '<') {
        return false;
    }
    at++;
    Str want = Str(name);
    if (at + len(want) > len(raw) || !StrEq(Str(raw.s + at, len(want)), want)) {
        return false;
    }
    char after = at + len(want) < len(raw) ? raw.s[at + len(want)] : '\0';
    return after == ' ' || after == '/' || after == '>' || after == '\n';
}

static bool HtmlAttr(Str raw, const char* name, Str* out) {
    TempStr pattern = fmt("%s=\"", Str(name));
    int n = len(pattern);
    for (int i = 0; i + n <= len(raw); i++) {
        if (!StrEq(Str(raw.s + i, n), pattern)) {
            continue;
        }
        int start = i + n;
        for (int j = start; j < len(raw); j++) {
            if (raw.s[j] == '"') {
                *out = Str(raw.s + start, j - start);
                return true;
            }
        }
        return false;
    }
    return false;
}

static bool UserCardParse(Ctx* cx, component::MdNode* n, Str text, void*,
                          component::MdPluginNode* out) {
    (void)cx;
    if (n->kind != component::MdKind::Html || !HtmlTagIs(text, "UserCard")) {
        return false;
    }
    Str id;
    if (!HtmlAttr(text, "id", &id)) {
        return false;
    }
    out->text = id;
    out->markdown = text;
    const UserCardDef* found = nullptr;
    for (const UserCardDef& u : kUsers) {
        Str uid = Str(u.id);
        if (StrEq(uid, id)) {
            found = &u;
        }
    }
    static const UserCardDef unknown = {"", "Unknown", ""};
    out->data = (void*)(found ? found : &unknown);
    return true;
}

// window.use_keyed_state("user-card-follow-{id}"): the button remembers
// whether it was pressed, and nothing else does.
struct FollowState {
    bool following = false;
    static void OnClick(FollowState* self, Ctx* cx, const ClickEvent*) {
        self->following = !self->following;
        Notify(cx);
    }
};

static El* UserCardRender(Ctx* cx, const component::MdPluginNode* node, void*) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const auto* u = (const UserCardDef*)node->data;
    Str id = node->text;
    Entity<FollowState> follow = KeyedEntity<FollowState>(
        cx, KeyedKey(HashClickId(id), HashClickId(StrL("user-card-follow"))));
    FollowState* st = follow.Get(cx);
    bool following = st && st->following;

    component::Avatar* av = component::Avatar::New(cx)->Name(Str(u->name));
    if (u->avatar[0]) {
        av->Src(Str(u->avatar));
    }
    return Div(a)
        ->FlexRow()
        ->W(300)
        ->ItemsCenter()
        ->Gap(12)
        ->PadX(12)
        ->PadY(8)
        ->Radius(th.radius)
        ->Border(1, th.border)
        ->Child(av->Size(24)->IntoEl())
        ->Child(
            Div(a)->Flex1()->Child(TextEl(a, Str(u->name))->Font(14)->Medium()))
        ->Child(component::Button::New(cx, StrDup(a, fmt("follow-%s", id)))
                    ->Outline()
                    ->WithSize(UiSize::Small)
                    ->Label(following ? StrL("Following") : StrL("Follow"))
                    ->OnClick(ListenTo(follow, &FollowState::OnClick))
                    ->IntoEl());
}

// ─── the math plugin ──────────────────────────────────────────────────────
//
// Upstream renders a formula by handing it to KaTeX in node and reading the
// SVG back; there is no node here and no SVG text engine to lay one out, so
// this is the example's own fallback path — italic text with Greek names
// spelled out and scripts folded into Unicode super- and subscripts. The
// parse-time plugins are otherwise the same shape: InlineMath is an atomic
// object in a paragraph and Math is a block of its own.

static bool MathIsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// The name-to-glyph table prettify_math_source walks, in its own order.
struct MathReplacement {
    const char* from;
    const char* to;
};

static const MathReplacement kMathNames[] = {
    {"\\alpha", "α"}, {"\\beta", "β"}, {"\\gamma", "γ"}, {"\\delta", "δ"},
    {"\\pi", "π"},    {"\\sum", "∑"},  {"\\sqrt", "√"},  {"\\times", "×"},
    {"\\cdot", "⋅"},  {"\\leq", "≤"},  {"\\geq", "≥"},   {"\\neq", "≠"},
    {"\\infty", "∞"}, {"\\left", ""},  {"\\right", ""},
};

// script_char: the digit, sign and letter forms, as UTF-8.
static const char* MathScriptChar(char c, bool super) {
    if (super) {
        switch (c) {
            case '0':
                return "⁰";
            case '1':
                return "¹";
            case '2':
                return "²";
            case '3':
                return "³";
            case '4':
                return "⁴";
            case '5':
                return "⁵";
            case '6':
                return "⁶";
            case '7':
                return "⁷";
            case '8':
                return "⁸";
            case '9':
                return "⁹";
            case '+':
                return "⁺";
            case '-':
                return "⁻";
            case '=':
                return "⁼";
            case '(':
                return "⁽";
            case ')':
                return "⁾";
            case 'i':
                return "ⁱ";
            case 'n':
                return "ⁿ";
            default:
                return nullptr;
        }
    }
    switch (c) {
        case '0':
            return "₀";
        case '1':
            return "₁";
        case '2':
            return "₂";
        case '3':
            return "₃";
        case '4':
            return "₄";
        case '5':
            return "₅";
        case '6':
            return "₆";
        case '7':
            return "₇";
        case '8':
            return "₈";
        case '9':
            return "₉";
        case '+':
            return "₊";
        case '-':
            return "₋";
        case '=':
            return "₌";
        case '(':
            return "₍";
        case ')':
            return "₎";
        case 'a':
            return "ₐ";
        case 'e':
            return "ₑ";
        case 'h':
            return "ₕ";
        case 'i':
            return "ᵢ";
        case 'j':
            return "ⱼ";
        case 'k':
            return "ₖ";
        case 'l':
            return "ₗ";
        case 'm':
            return "ₘ";
        case 'n':
            return "ₙ";
        case 'o':
            return "ₒ";
        case 'p':
            return "ₚ";
        case 'r':
            return "ᵣ";
        case 's':
            return "ₛ";
        case 't':
            return "ₜ";
        case 'u':
            return "ᵤ";
        case 'v':
            return "ᵥ";
        case 'x':
            return "ₓ";
        default:
            return nullptr;
    }
}

// prettify_math_source: the whitespace collapsed, the names spelled out, and
// `^{..}` / `_{..}` folded into the script forms where every character has
// one. take_script's braces are read the same way.
static Str MathPrettify(Ctx* cx, Str src) {
    Arena* a = cx->a;
    StrBuilder sb;
    // split_whitespace().join(" ")
    int at = 0;
    bool first = true;
    while (at < len(src)) {
        while (at < len(src) && MathIsSpace(src.s[at])) {
            at++;
        }
        int start = at;
        while (at < len(src) && !MathIsSpace(src.s[at])) {
            at++;
        }
        if (at > start) {
            if (!first) {
                sb.AppendChar(' ');
            }
            sb.Append(Str(src.s + start, at - start));
            first = false;
        }
    }
    Str joined = sb.TakeStr();

    StrBuilder named;
    for (int i = 0; i < len(joined);) {
        const MathReplacement* hit = nullptr;
        for (const MathReplacement& r : kMathNames) {
            Str from = Str(r.from);
            if (i + len(from) <= len(joined) &&
                StrEq(Str(joined.s + i, len(from)), from)) {
                hit = &r;
                break;
            }
        }
        if (hit) {
            named.Append(Str(hit->to));
            i += (int)strlen(hit->from);
            continue;
        }
        named.AppendChar(joined.s[i]);
        i++;
    }
    Str replaced = named.TakeStr();
    StrFree(joined);

    StrBuilder out;
    for (int i = 0; i < len(replaced);) {
        char c = replaced.s[i];
        if (c != '^' && c != '_') {
            out.AppendChar(c);
            i++;
            continue;
        }
        bool super = c == '^';
        // take_script: a braced run, or the one character after the mark.
        int from = i + 1;
        int to = from;
        bool braced = from < len(replaced) && replaced.s[from] == '{';
        if (braced) {
            int depth = 1;
            from++;
            to = from;
            while (to < len(replaced) && depth > 0) {
                if (replaced.s[to] == '{') {
                    depth++;
                } else if (replaced.s[to] == '}') {
                    depth--;
                    if (depth == 0) {
                        break;
                    }
                }
                to++;
            }
        } else if (from < len(replaced)) {
            to = from + 1;
        }
        if (to <= from) {
            out.AppendChar(c);
            i++;
            continue;
        }
        for (int k = from; k < to; k++) {
            const char* sub = MathScriptChar(replaced.s[k], super);
            if (sub) {
                out.Append(Str(sub));
            } else {
                out.AppendChar(replaced.s[k]);
            }
        }
        i = braced ? to + 1 : to;
    }
    Str result = out.TakeStr();
    StrFree(replaced);
    Str owned = StrDup(a, result);
    StrFree(result);
    return owned;
}

// render_math_text: the formula italic, a size up when it is a block of its
// own, and the line height GPUI gives each case.
static El* MathFormula(Ctx* cx, Str source, bool inlineMath, float fontSize) {
    const Theme& th = ThemeNow(cx->app);
    float size = inlineMath
                     ? (fontSize > 10.f ? fontSize : 10.f)
                     : (fontSize * 1.18f > 12.f ? fontSize * 1.18f : 12.f);
    return TextEl(cx->a, MathPrettify(cx, source))
        ->Font(size)
        ->LineHeight(inlineMath ? 1.f : 1.2f)
        ->Fg(th.foreground)
        ->Italic()
        ->Shrink0();
}

static bool MathInlineParse(const markdown::Node* source,
                            const MarkdownParseContext* context, void*,
                            MarkdownNode* out) {
    if (source->kind != markdown::NodeKind::InlineMath) {
        return false;
    }
    Str value = context->Value(source, markdown::NodeStrKind::Value);
    StrBuilder literal(context->arena);
    literal.AppendChar('$');
    literal.Append(value);
    literal.AppendChar('$');
    *out = MarkdownNode::New(context->Copy(StrL("math-inline")))
               .Text(context->Copy(value))
               .Markdown(literal.TakeStr());
    return true;
}

static InlineElement MathInlineRender(Ctx* cx, const MarkdownNode* node,
                                      const InlineRenderContext* context,
                                      void*) {
    return InlineElement::New(
               MathFormula(cx, node->text, true, context->fontSize))
        .WithBaseline(context->lineHeight * 0.75f);
}

static bool MathBlockParse(const markdown::Node* source,
                           const MarkdownParseContext* context, void*,
                           MarkdownNode* out) {
    if (source->kind != markdown::NodeKind::Math) {
        return false;
    }
    Str value = context->Value(source, markdown::NodeStrKind::Value);
    *out = MarkdownNode::New(context->Copy(StrL("math-block")))
               .Text(context->Copy(value));
    return true;
}

static El* MathBlockRender(Ctx* cx, const MarkdownNode* node, void*) {
    return Div(cx->a)->FlexRow()->W(kFill)->JustifyCenter()->PadY(4)->Child(
        MathFormula(cx, node->text, false, 16.f));
}

static void OnLink(MarkdownApp* self, Ctx* cx, const ClickEvent*,
                   intptr_t href) {
    StrCopyZ(self->lastLink, (int)sizeof(self->lastLink),
             href ? (const char*)href : "");
    Notify(cx);
}

// The Open action, ctrl-o (cmd-o on macOS) and the status bar's button:
// upstream hangs it off the app menu and the same chord, and it does what
// `on_action_open` does — the desktop's own dialog, and what it answers is
// read into the editor.
static void OpenDocument(MarkdownApp* self, Ctx* cx) {
    PathPrompt prompt;
    prompt.title = StrL("Select a Markdown file");
    TempStr path = PromptForPathTemp(cx->win, prompt);
    if (!path) {
        return;
    }
    FILE* f = fopen(path.s, "rb");
    if (!f) {
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    // A document this example will not read in one gulp is one the editor
    // should not be asked to hold either.
    const long kMax = 4 * 1024 * 1024;
    if (size <= 0 || size > kMax) {
        fclose(f);
        return;
    }
    char* buf = (char*)Alloc(nullptr, (int)size + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;
    InputSetValue(&self->source, Str(buf, (int)got));
    Free(nullptr, buf);
    self->previewScroll = 0;
    Notify(cx);
}

static void OnOpenClick(MarkdownApp* self, Ctx* cx, const ClickEvent*) {
    OpenDocument(self, cx);
}

static void OnKey(MarkdownApp* self, Ctx* cx, const KeyEvent* ev) {
    // VK_O, which the key table spells with the letter's ASCII code.
    if (ev->down && ev->vk == 'O' && (ev->ctrl || ev->platform)) {
        OpenDocument(self, cx);
    }
}

// The status bar's `selection-format` button: whether copying the preview
// gives back the rendered text or the markdown behind it.
static void OnToggleSelFormat(MarkdownApp* self, Ctx* cx, const ClickEvent*) {
    self->selFormat = self->selFormat == SelectionFormat::Plain
                          ? SelectionFormat::Source
                          : SelectionFormat::Plain;
    Notify(cx);
}

// The status bar's `table-wrap` button: which layout the preview's tables
// take, the measured one that scrolls or the one that wraps to fit.
static void OnToggleTableWrap(MarkdownApp* self, Ctx* cx, const ClickEvent*) {
    self->tableWrap = !self->tableWrap;
    Notify(cx);
}

static void OnPreviewScroll(MarkdownApp* self, Ctx* cx, const ScrollEvent* ev) {
    self->previewScroll = ev->offsetY;
    Notify(cx);
}

// The two languages the example offers a Run button for.
static const char* const kRunnable[] = {"rust", "python"};

static void OnRunCode(MarkdownApp* self, Ctx* cx, const ClickEvent*,
                      intptr_t which) {
    // `println!("Running {} code: {}", lang, code)` — the example's own
    // placeholder for a terminal, which this tree does not have either.
    Str lang = Str(kRunnable[which == 1 ? 1 : 0]);
    TempStr msg = fmt("Running %s code", lang);
    StrCopyZ(self->lastLink, (int)sizeof(self->lastLink), msg.s);
    Notify(cx);
}

// code_block_actions: a Clipboard over the block's text, and a Run button on
// the two languages the Rust example offers one for.
static El* CodeActions(Ctx* cx, void* data, Str code, Str lang) {
    (void)data;
    Arena* a = cx->a;
    El* row = Div(a)->FlexRow()->Gap(4)->ItemsCenter();
    row->Child(component::Clipboard::New(cx, StrL("copy"))
                   ->Value(StrDup(a, code))
                   ->IntoEl());
    int runnable = -1;
    for (int i = 0; i < 2; i++) {
        Str name = Str(kRunnable[i]);
        if (StrEq(lang, name)) {
            runnable = i;
        }
    }
    if (runnable >= 0) {
        row->Child(component::Button::New(cx, StrL("run-terminal"))
                       ->Icon(IconName::SquareTerminal)
                       ->Ghost()
                       ->WithSize(UiSize::XSmall)
                       ->OnClick(ListenerArg(Listen(cx, &OnRunCode), runnable))
                       ->IntoEl());
    }
    return row;
}

// table_actions: what the shape of the table is, and the table itself on the
// clipboard as GFM. The Rust example puts a CSV / TSV export behind an
// Ellipsis dropdown beside these two; the pair here is the part that needs no
// menu of its own.
static El* TableActions(Ctx* cx, void* data,
                        const component::TableData* table) {
    (void)data;
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)->FlexRow()->W(kFill)->JustifyEnd()->ItemsCenter()->Gap(4);
    row->Child(
        TextEl(a, StrDup(a, fmt("%d × %d", table->rowCount, table->cols)))
            ->Font(12)
            ->Fg(th.mutedFg));
    row->Child(component::Clipboard::New(cx, StrL("copy-table"))
                   ->Value(StrDup(a, table->markdown))
                   ->Tooltip(StrL("Copy as Markdown"))
                   ->IntoEl());
    return row;
}

El* MarkdownApp::Render(MarkdownApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
    }
    // The editor holds the document; the preview reads it back every frame,
    // which is what makes a keystroke on the left redraw the right.
    Str text = InputValue(&self->source);
    cx->win->input = &self->source;

    auto* marks = (TextSpan*)Alloc(a, (int)sizeof(TextSpan) * kMaxMarkers);
    int nMarks = FindMarkers(cx, text, marks, kMaxMarkers);

    // `Editor::new(&state).h(relative(1.))`: the editor fills its panel. A
    // Highlighter takes pixels, so the panel's height is the window's less
    // the status bar under it -- py_1 over a text_sm line, so 26.
    float editorH = WindowSize(cx->win).dipH - 26;
    component::Highlighter* ed =
        component::Highlighter::New(cx, StrL("source"), &self->source);
    ed->H(editorH)->Language(StrL("markdown"))->Decorations(marks, nMarks);
    El* left = Div(a)->FlexCol()->SizeFull()->Child(ed->IntoEl());

    component::TextView* tv = component::TextView::New(cx, text);
    // .plugin(TickerPlugin::new(..)).plugin(UserCardPlugin::new())
    tv->Plugin(StrL("ticker"), &TickerParse, &TickerRender);
    tv->Plugin(StrL("user-card"), &UserCardParse, &UserCardRender);
    MarkdownPlugin inlineMath;
    inlineMath.name = StrL("math-inline");
    inlineMath.parse = &MathInlineParse;
    inlineMath.renderInline = &MathInlineRender;
    tv->Plugin(inlineMath);
    MarkdownPlugin blockMath;
    blockMath.name = StrL("math-block");
    blockMath.parse = &MathBlockParse;
    blockMath.render = &MathBlockRender;
    blockMath.isBlock = true;
    tv->Plugin(blockMath);
    El* preview = tv->TableScroll(!self->tableWrap)
                      ->Selectable()
                      ->SelFormat(self->selFormat)
                      ->OnLink(Listen(cx, &OnLink))
                      ->CodeBlockActions(&CodeActions, self)
                      ->TableActions(&TableActions, self)
                      ->IntoEl();
    // `.p_5().scrollable(true)`: the document scrolls inside its own panel.
    El* right =
        Div(a)
            ->FlexCol()
            ->SizeFull()
            ->ClipY()
            ->ScrollY(self->previewScroll)
            ->ScrollId(HashClickId(StrL("preview")))
            ->OnScroll(Listen(cx, &OnPreviewScroll))
            ->Child(Div(a)->FlexCol()->W(kFill)->PadX(20)->Child(preview));

    El* split = component::Resizable::New(cx, StrL("container"))
                    ->H(kFill)
                    ->Panel(left, 520, 200)
                    ->Grow(right, 200)
                    ->IntoEl();

    component::StatusBar* bar = component::StatusBar::New(cx);
    if (self->lastLink[0]) {
        bar->Left(Str(self->lastLink));
    }
    bar->Right(component::Button::New(cx, StrL("open"))
                   ->Ghost()
                   ->WithSize(UiSize::XSmall)
                   ->Label(StrL("Open..."))
                   ->OnClick(Listen(cx, &OnOpenClick))
                   ->IntoEl());
    bar->Right(component::Button::New(cx, StrL("selection-format"))
                   ->Ghost()
                   ->WithSize(UiSize::XSmall)
                   ->Label(self->selFormat == SelectionFormat::Source
                               ? StrL("Selection: Source")
                               : StrL("Selection: Plain"))
                   ->OnClick(Listen(cx, &OnToggleSelFormat))
                   ->IntoEl());
    bar->Right(component::Button::New(cx, StrL("table-wrap"))
                   ->Ghost()
                   ->WithSize(UiSize::XSmall)
                   ->Label(self->tableWrap ? StrL("Table: Wrap")
                                           : StrL("Table: Scroll"))
                   ->OnClick(Listen(cx, &OnToggleTableWrap))
                   ->IntoEl());

    return Div(a)
        ->FlexCol()
        ->SizeFull()
        ->Bg(th.tokens.background)
        ->Child(Div(a)->Flex1()->W(kFill)->ClipY()->Child(split))
        ->Child(bar->IntoEl());
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    AssetsClear();
    AssetsAddDefaultRoots(StrL("markdown"));
    AssetsAddRoot(StrL("assets/markdown"));
    Entity<MarkdownApp> view = EntityNew<MarkdownApp>(app);
    MarkdownApp* self = view.Get(app);
    // EditorState::new(..).language(Markdown).line_number(true).tab_size(2)
    // .searchable(true).placeholder(..).default_value(EXAMPLE)
    // EditorState is InputKind::Editor — a single-line Input drops the
    // newlines a value hands it, which turns a document into one long line.
    self->source.kind = InputKind::Editor;
    self->source.mode.kind = LayoutModeKind::CodeEditor;
    InputSetPlaceholder(&self->source, StrL("Enter your Markdown here..."));
    self->source.mode.tabSize = 2;
    self->source.mode.lineNumber = true;
    TempStr md = AssetsLoadTextTemp(StrL("test.md"));
    InputSetValue(&self->source, md);
    self->source.focused = true;
    Window* win =
        WindowOpenView(app, StrL("Markdown"), 1200, 900, view.id, WinOpts{});
    // The Open chord, which upstream binds in the story app's own keymap.
    WindowOnKey(win, ListenTo(view, &OnKey));
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
