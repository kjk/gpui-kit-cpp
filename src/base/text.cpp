#include "base/text.h"

#include "base/global_state.h"
#include "base/input_keys.h"
#include "base/scrollable_mask.h"
#include "base/text_format.h"
#include "base/text_selection.h"
#include "gpui/image.h"
#include "gpui/keymap.h"
#include "gpui/paint.h"
#include "markdown/markdown.h"

namespace gpui {

void TextViewInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) return;
    bound = KeymapGeneration();
    const char* context = "TextView";
    KeyBinding bindings[] = {
#if GPUI_OS_MAC
        {"cmd-c", input::Copy(), context},
        {"cmd-a", input::SelectAll(), context},
#else
        {"ctrl-c", input::Copy(), context},
        {"ctrl-a", input::SelectAll(), context},
#endif
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

TextMark& TextMark::Bold() {
    bold = true;
    return *this;
}
TextMark& TextMark::Italic() {
    italic = true;
    return *this;
}
TextMark& TextMark::Strikethrough() {
    strikethrough = true;
    return *this;
}
TextMark& TextMark::Underline() {
    underline = true;
    return *this;
}
TextMark& TextMark::Code() {
    code = true;
    return *this;
}
TextMark& TextMark::Highlight(Rgba color) {
    highlight = color;
    hasHighlight = true;
    return *this;
}
TextMark& TextMark::Link(LinkMark value) {
    link = value;
    hasLink = true;
    return *this;
}
void TextMark::Merge(const TextMark& other) {
    bold |= other.bold;
    italic |= other.italic;
    strikethrough |= other.strikethrough;
    underline |= other.underline;
    code |= other.code;
    if (other.hasHighlight) {
        highlight = other.highlight;
        hasHighlight = true;
    }
    if (other.hasLink) {
        link = other.link;
        hasLink = true;
    }
}

Str ImageNode::Title(Arena* a) const {
    return StrDup(a, title.s ? title : alt);
}

Str MarkdownParseContext::Value(const markdown::Node* node,
                                markdown::NodeStrKind kind) const {
    return arena && node ? markdown::NodeGetStr(arena, node, kind) : Str{};
}

Str MarkdownParseContext::Copy(Str value) const {
    return arena ? StrDup(arena, value) : Str{};
}

MarkdownNode MarkdownNode::New(Str value, void* payload) {
    MarkdownNode out;
    out.name = value;
    out.data = payload;
    return out;
}

MarkdownNode& MarkdownNode::Text(Str value) {
    text = value;
    return *this;
}

MarkdownNode& MarkdownNode::Markdown(Str value) {
    markdown = value;
    return *this;
}

Str MarkdownNode::ToMarkdown() const {
    return markdown.len > 0 ? markdown : text;
}

static uint64_t gMarkdownExtensionsRevision = 1;

static uint64_t NextMarkdownExtensionsRevision() {
    uint64_t out = gMarkdownExtensionsRevision++;
    if (gMarkdownExtensionsRevision == 0) {
        gMarkdownExtensionsRevision = 1;
    }
    return out;
}

MarkdownExtensions& MarkdownExtensions::Mdx() {
    enableMdx = true;
    revision = NextMarkdownExtensionsRevision();
    return *this;
}

MarkdownExtensions& MarkdownExtensions::BlockParser(Arena* a,
                                                    MarkdownBlockParserFn fn,
                                                    void* data) {
    if (fn) {
        blockParsers.Append(a, {fn, data});
        revision = NextMarkdownExtensionsRevision();
    }
    return *this;
}

MarkdownExtensions& MarkdownExtensions::BlockRenderer(Arena* a, Str name,
                                                      MarkdownBlockRenderFn fn,
                                                      void* data) {
    if (fn) {
        // HashMap::insert replaces a renderer registered for the same name.
        for (int i = 0; i < blockRenderers.len; i++) {
            if (base::StrEq(blockRenderers[i].name, name)) {
                blockRenderers[i] = {name, fn, data};
                revision = NextMarkdownExtensionsRevision();
                return *this;
            }
        }
        blockRenderers.Append(a, {name, fn, data});
        revision = NextMarkdownExtensionsRevision();
    }
    return *this;
}

MarkdownExtensions& MarkdownExtensions::Plugin(Arena* a,
                                               const MarkdownPlugin& plugin) {
    if (plugin.isBlock && plugin.parse && plugin.render) {
        BlockParser(a, plugin.parse, plugin.data);
        BlockRenderer(a, plugin.name, plugin.render, plugin.data);
    }
    return *this;
}

bool MarkdownExtensions::HasSameParserConfiguration(
    const MarkdownExtensions& other) const {
    if (enableMdx != other.enableMdx ||
        blockParsers.len != other.blockParsers.len ||
        blockRenderers.len != other.blockRenderers.len) {
        return false;
    }
    // Rust compares the renderer map's keys; the order they were registered
    // in is not part of the parser's shape.
    for (int i = 0; i < blockRenderers.len; i++) {
        if (!other.Renderer(blockRenderers[i].name)) {
            return false;
        }
    }
    return true;
}

uint64_t MarkdownExtensions::ParserFingerprint() const {
    uint64_t h = enableMdx ? 0x9e3779b97f4a7c15ull : 0xcbf29ce484222325ull;
    h = h * 1099511628211ull + (uint64_t)blockParsers.len;
    h = h * 1099511628211ull + (uint64_t)blockRenderers.len;
    // Name by name, and order-independent, so a renderer table rebuilt in a
    // different order is still the same parser configuration.
    uint64_t names = 0;
    for (int i = 0; i < blockRenderers.len; i++) {
        uint64_t one = 1469598103934665603ull;
        Str name = blockRenderers[i].name;
        for (int at = 0; at < name.len; at++) {
            one = (one ^ (uint64_t)(uint8_t)name.s[at]) * 1099511628211ull;
        }
        names += one;
    }
    return h * 1099511628211ull + names;
}

const MarkdownBlockRenderer* MarkdownExtensions::Renderer(Str name) const {
    for (int i = 0; i < blockRenderers.len; i++) {
        if (base::StrEq(blockRenderers[i].name, name)) {
            return &blockRenderers[i];
        }
    }
    return nullptr;
}

TextView* TextViewPlugin::Setup(TextView* view) const {
    return setup ? setup(view, data) : view;
}

CodeBlock CodeBlock::FromCode(Str code, Str lang) {
    CodeBlock out;
    out.code = code;
    out.lang = lang;
    return out;
}

TextViewDefaults& TextViewDefaults::WithStyle(const TextViewStyle& value) {
    style = value;
    hasStyle = true;
    return *this;
}

TextViewDefaults& TextViewDefaults::WithCodeBlockHighlighter(
    CodeBlockHighlighterFn fn, void* data) {
    codeBlockHighlighter = fn;
    codeBlockHighlighterData = data;
    return *this;
}

void TextViewDefaults::Install(App* app) const {
    if (TextViewDefaults* slot = AppGlobalEnsure<TextViewDefaults>(app)) {
        *slot = *this;
    }
}

TextViewDefaults TextViewDefaults::Global(const App* app) {
    const TextViewDefaults* installed = AppGlobalGet<TextViewDefaults>(app);
    return installed ? *installed : TextViewDefaults{};
}

TextViewStyle TextViewStyle::FromColors(const ColorTokens& colors,
                                        bool isDarkValue) {
    // Rich text needs a handful of roles the palette does not name directly —
    // a code background, a link colour — so they are mapped here once instead
    // of at every call site.
    TextViewStyle out;
    out.foreground = colors.foreground;
    out.mutedForeground = colors.mutedForeground;
    out.link = colors.primary;
    out.selection = colors.selection;
    out.codeBackground = colors.accent;
    out.border = colors.border;
    out.isDark = isDarkValue;
    return out;
}

TextViewStyle TextViewStyle::FromTheme(const base_theme::Theme& theme) {
    return FromColors(theme.tokens.colors,
                      theme.appearance == base_theme::ThemeAppearance::Dark);
}

TextViewStyle TextViewStyle::Default() {
    return FromColors(ColorTokens::Light(), false);
}

Rgba TextViewStyle::InlineCodeBackground() const {
    if ((inlineCodeFields & StyleFieldBg) && inlineCode.bg.color.a) {
        return inlineCode.bg.color;
    }
    return codeBackground;
}

float TextViewStyle::HeadingSize(uint8_t level) const {
    if (headingFontSize) {
        return headingFontSize(level, headingBaseFontSize, headingFontSizeData);
    }
    switch (level) {
        case 1:
            return headingBaseFontSize * 2.f;
        case 2:
            return headingBaseFontSize * 1.5f;
        case 3:
            return headingBaseFontSize * 1.25f;
        case 4:
            return headingBaseFontSize * 1.125f;
        default:
            return headingBaseFontSize;
    }
}

TextViewStyle& TextViewStyle::WithForeground(Rgba value) {
    foreground = value;
    return *this;
}

TextViewStyle& TextViewStyle::WithMutedForeground(Rgba value) {
    mutedForeground = value;
    return *this;
}

TextViewStyle& TextViewStyle::WithLink(Rgba value) {
    link = value;
    return *this;
}

TextViewStyle& TextViewStyle::WithSelection(Rgba value) {
    selection = value;
    return *this;
}

TextViewStyle& TextViewStyle::WithCodeBackground(Rgba value) {
    codeBackground = value;
    return *this;
}

TextViewStyle& TextViewStyle::WithBorder(Rgba value) {
    border = value;
    return *this;
}

TextViewStyle& TextViewStyle::WithParagraphGap(float gap) {
    paragraphGap = gap;
    return *this;
}

TextViewStyle& TextViewStyle::WithHeadingBaseFontSize(float size) {
    headingBaseFontSize = size;
    return *this;
}

TextViewStyle& TextViewStyle::WithHeadingFontSize(HeadingFontSizeFn fn,
                                                  void* data) {
    headingFontSize = fn;
    headingFontSizeData = data;
    return *this;
}

TextViewStyle& TextViewStyle::WithCodeBlock(const gpui::Style& value,
                                            uint32_t fields) {
    codeBlock = value;
    codeBlockFields = fields;
    return *this;
}

TextViewStyle& TextViewStyle::WithTable(const gpui::Style& value,
                                        uint32_t fields) {
    table = value;
    tableFields = fields;
    return *this;
}

TextViewStyle& TextViewStyle::WithTableHead(const gpui::Style& value,
                                            uint32_t fields) {
    tableHead = value;
    tableHeadFields = fields;
    return *this;
}

TextViewStyle& TextViewStyle::WithTableCell(const gpui::Style& value,
                                            uint32_t fields) {
    tableCell = value;
    tableCellFields = fields;
    return *this;
}

TextViewStyle& TextViewStyle::WithInlineCode(const gpui::Style& value,
                                             uint32_t fields) {
    inlineCode = value;
    inlineCodeFields = fields;
    return *this;
}

TextViewStyle& TextViewStyle::WithDark(bool value) {
    isDark = value;
    return *this;
}

static bool TextRgbaEq(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static bool TextBackgroundEq(const Background& a, const Background& b) {
    return TextRgbaEq(a.color, b.color) &&
           TextRgbaEq(a.from.color, b.from.color) &&
           a.from.percentage == b.from.percentage &&
           TextRgbaEq(a.to.color, b.to.color) &&
           a.to.percentage == b.to.percentage && a.angle == b.angle &&
           a.gradient == b.gradient;
}

static bool TextEdgesEq(Edges a, Edges b) {
    return a == b;
}

static bool StyleFieldsEqual(const gpui::Style& a, const gpui::Style& b,
                             uint32_t fields) {
    if ((fields & StyleFieldBg) && !TextBackgroundEq(a.bg, b.bg)) return false;
    if ((fields & StyleFieldColor) && !TextRgbaEq(a.color, b.color))
        return false;
    if ((fields & StyleFieldBorderColor) &&
        !TextRgbaEq(a.borderColor, b.borderColor))
        return false;
    if ((fields & StyleFieldPad) && !TextEdgesEq(a.pad, b.pad)) return false;
    if ((fields & StyleFieldMargin) && !TextEdgesEq(a.margin, b.margin))
        return false;
    if ((fields & StyleFieldGap) && (a.gapX != b.gapX || a.gapY != b.gapY))
        return false;
    if ((fields & StyleFieldRadius) && a.radius != b.radius) return false;
    if ((fields & StyleFieldBorder) && a.border != b.border) return false;
    if ((fields & StyleFieldBorderT) && a.borderT != b.borderT) return false;
    if ((fields & StyleFieldBorderB) && a.borderB != b.borderB) return false;
    if ((fields & StyleFieldBorderL) && a.borderL != b.borderL) return false;
    if ((fields & StyleFieldBorderR) && a.borderR != b.borderR) return false;
    if ((fields & StyleFieldFontSize) && a.fontSize != b.fontSize) return false;
    if ((fields & StyleFieldWidth) && a.width != b.width) return false;
    if ((fields & StyleFieldHeight) && a.height != b.height) return false;
    if ((fields & StyleFieldOpacity) && a.opacity != b.opacity) return false;
    if ((fields & StyleFieldHoverBg) && !TextBackgroundEq(a.hoverBg, b.hoverBg))
        return false;
    if ((fields & StyleFieldHoverFg) && !TextRgbaEq(a.hoverFg, b.hoverFg))
        return false;
    if ((fields & StyleFieldActiveBg) &&
        !TextBackgroundEq(a.activeBg, b.activeBg))
        return false;
    return true;
}

bool TextViewStyle::Equals(const TextViewStyle& other) const {
    // The six colours are part of the fingerprint: a theme change now moves
    // the style rather than the theme the renderer reads, and the selection
    // revision has to notice.
    if (!TextRgbaEq(foreground, other.foreground) ||
        !TextRgbaEq(mutedForeground, other.mutedForeground) ||
        !TextRgbaEq(link, other.link) ||
        !TextRgbaEq(selection, other.selection) ||
        !TextRgbaEq(codeBackground, other.codeBackground) ||
        !TextRgbaEq(border, other.border)) {
        return false;
    }
    if (paragraphGap != other.paragraphGap ||
        headingBaseFontSize != other.headingBaseFontSize ||
        codeBlockFields != other.codeBlockFields ||
        tableFields != other.tableFields ||
        tableHeadFields != other.tableHeadFields ||
        tableCellFields != other.tableCellFields ||
        inlineCodeFields != other.inlineCodeFields || isDark != other.isDark ||
        !StyleFieldsEqual(codeBlock, other.codeBlock, codeBlockFields) ||
        !StyleFieldsEqual(table, other.table, tableFields) ||
        !StyleFieldsEqual(tableHead, other.tableHead, tableHeadFields) ||
        !StyleFieldsEqual(tableCell, other.tableCell, tableCellFields) ||
        !StyleFieldsEqual(inlineCode, other.inlineCode, inlineCodeFields)) {
        return false;
    }
    if ((headingFontSize == nullptr) != (other.headingFontSize == nullptr)) {
        return false;
    }
    if (!headingFontSize) return true;
    for (uint8_t level = 1; level <= 6; level++) {
        if (HeadingSize(level) != other.HeadingSize(level)) return false;
    }
    return true;
}

TextViewState::~TextViewState() {
    StrFree(text);
}

static Entity<TextViewState> NewTextViewState(App* app, Str text,
                                              TextViewFormat format) {
    Entity<TextViewState> entity;
    if (!app) return entity;
    entity = EntityNewState<TextViewState>(app);
    if (TextViewState* state = entity.Get(app)) {
        state->self = entity.id;
        state->text = StrDup(text);
        state->format = format;
    }
    return entity;
}

Entity<TextViewState> TextViewState::Markdown(App* app, Str text) {
    return NewTextViewState(app, text, TextViewFormat::Markdown);
}

Entity<TextViewState> TextViewState::Html(App* app, Str text) {
    return NewTextViewState(app, text, TextViewFormat::Html);
}

void TextViewState::Changed(App* app, Window* window,
                            bool selectionCompatible) {
    revision++;
    if (!selectionCompatible) {
        selectionRevision++;
        WindowSelectionClear(window);
    }
    if (app && self.IsValid()) NotifyEntity(app, self, window);
}

void TextViewState::SetText(Str value, App* app, Window* window) {
    if (base::StrEq(text, value)) return;
    Str replacement = StrDup(value);
    StrFree(text);
    text = replacement;
    Changed(app, window, false);
}

void TextViewState::PushStr(Str value, App* app, Window* window) {
    if (value.len <= 0) return;
    int oldLen = text.len;
    char* joined = (char*)Alloc(nullptr, oldLen + value.len + 1);
    if (!joined) return;
    if (oldLen > 0) memcpy(joined, text.s, (size_t)oldLen);
    memcpy(joined + oldLen, value.s, (size_t)value.len);
    joined[oldLen + value.len] = 0;
    StrFree(text);
    text = Str(joined, oldLen + value.len);
    Changed(app, window, true);
}

void TextViewState::SetSelectable(bool value, App* app, Window* window) {
    if (selectable == value) return;
    selectable = value;
    if (!value) WindowSelectionClear(window);
    Changed(app, window, true);
}

void TextViewState::SetScrollable(bool value, App* app, Window* window) {
    if (scrollable == value) return;
    scrollable = value;
    Changed(app, window, true);
}

void TextViewState::SetSelectionFormat(gpui::SelectionFormat value, App* app,
                                       Window* window) {
    if (selectionFormat == value) return;
    selectionFormat = value;
    Changed(app, window, true);
}

int TextViewState::SelectedText(Window* window, char* out, int cap) const {
    return WindowSelectionTextForEntity(window, self, out, cap,
                                        format == TextViewFormat::Html
                                            ? gpui::SelectionFormat::Plain
                                            : selectionFormat);
}

bool TextViewState::HasSelection(const Window* window) const {
    return WindowSelectionHasEntity(window, self);
}

void TextViewState::ClearSelection(Window* window, App*) {
    WindowSelectionClear(window);
}

void TextViewState::SelectAll(Window* window, App*) {
    WindowSelectionSelectAll(window, self);
}

void TextViewState::OnAction(TextViewState* state, Ctx* cx,
                             const ActionEvent* event) {
    if (!state || !state->selectable) {
        const_cast<ActionEvent*>(event)->propagate = true;
        return;
    }
    if (event->action == input::Copy()) {
        if (!WindowSelectionCopy(cx->win)) {
            const_cast<ActionEvent*>(event)->propagate = true;
        }
        return;
    }
    if (event->action == input::SelectAll()) {
        state->SelectAll(cx->win, cx->app);
        Notify(cx);
        return;
    }
    const_cast<ActionEvent*>(event)->propagate = true;
}

void TextViewState::OnScroll(TextViewState* state, Ctx* cx,
                             const ScrollEvent* event) {
    state->scrollY = event->offsetY;
    Notify(cx);
}

void TextViewState::OnLineClamp(TextViewState* state, Ctx* cx,
                                const LineClampEvent* event) {
    if (!state || !event || state->clamped == event->clamped) {
        return;
    }
    state->clamped = event->clamped;
    // Rust notifies on a transition so an observer gating an expand button on
    // is_clamped() gets one more frame with the now-settled answer.
    Notify(cx);
}

// What Word and Inline take to mean "name no colour at all", so the run
// inherits the one the container above the view pushed. A transparent text
// colour would draw nothing, so nothing else can want it.
static const Rgba kInheritFg = Rgba{0, 0, 0, 0};

// ─── parse ────────────────────────────────────────────────────────────────
//
// src/markdown is the `markdown` crate, ported: it hands back an mdast, the
// same tree Rust gets from `markdown::to_mdast`. This walk folds that tree
// into the MdNode tree below, which is what crates/ui/src/text/format/
// markdown.rs does with `ast_to_node` and `parse_paragraph`.

namespace md = markdown;

// A link reference definition, kept so `[text][id]` can find its URL.
// crates/ui/src/text/format/markdown.rs puts these in the NodeContext with
// `cx.add_ref`; there is one parse here, so they live with the builder.
struct MdDef {
    Str identifier;
    Str url;
};

struct MdBuild {
    Arena* a = nullptr;
    Str source = {};
    const MarkdownExtensions* extensions = nullptr;
    MdNode* cur = nullptr;
    // The marks in effect, from the enclosing inline nodes.
    uint8_t marks = 0;
    Str href = {};
    ArenaVec<MdDef> defs{};
};

// A node's strings are ArenaStr — an offset into the arena the tree was
// parsed into, which is the builder's own. This reads one back.
// One of a node's strings, or an empty Str when it does not carry that one.
static Str V(MdBuild* b, const md::Node* n, md::NodeStrKind k) {
    return md::NodeGetStr(b->a, n, k);
}

static MdNode* Push(MdBuild* b, MdKind k) {
    MdNode* n = ArenaNew<MdNode>(b->a);
    n->kind = k;
    n->parent = b->cur;
    if (b->cur->last) {
        b->cur->last->next = n;
    } else {
        b->cur->first = n;
    }
    b->cur->last = n;
    b->cur = n;
    return n;
}

static void Pop(MdBuild* b) {
    if (b->cur->parent) {
        b->cur = b->cur->parent;
    }
}

// Appends to the run being built when the marks match and the text is the
// next byte of the source; otherwise starts a new run. Most of a paragraph is
// one uninterrupted stretch of source, so this usually collapses to one run
// pointing straight at the tree's text with nothing copied.
static void AddText(MdBuild* b, Str s) {
    if (s.len <= 0) {
        return;
    }
    MdNode* n = b->cur;
    MdRun* r = n->runLast;
    if (r && !r->imgSrc.s && r->marks == b->marks && r->href.s == b->href.s &&
        r->text.s + r->text.len == s.s) {
        r->text.len += s.len;
        return;
    }
    r = ArenaNew<MdRun>(b->a);
    r->text = s;
    r->marks = b->marks;
    r->href = b->href;
    if (n->runLast) {
        n->runLast->next = r;
    } else {
        n->runFirst = r;
    }
    n->runLast = r;
}

// node.rs InlineNode::image: an image sits in the flow beside the words,
// carrying the marks in force — an image inside a link is a link.
static void AddImage(MdBuild* b, Str src, Str alt, float w, float h) {
    if (src.len <= 0) {
        return;
    }
    MdNode* n = b->cur;
    MdRun* r = ArenaNew<MdRun>(b->a);
    r->imgSrc = src;
    r->text = alt;
    r->imgW = w;
    r->imgH = h;
    r->marks = b->marks;
    r->href = b->href;
    if (n->runLast) {
        n->runLast->next = r;
    } else {
        n->runFirst = r;
    }
    n->runLast = r;
}

// "&amp;" -> "&". Returns the entity unchanged when it is not one the crate's
// table knows. Kept as the public compatibility helper; both parsers decode
// character references before their trees reach the GPUI projection.
Str MdDecodeEntity(Arena* a, Str e) {
    if (e.len < 3 || e.s[0] != '&' || e.s[e.len - 1] != ';') {
        return e;
    }
    Str body((char*)e.s + 1, e.len - 2);
    Str value;
    if (body.len > 1 && body.s[0] == '#') {
        if (body.s[1] == 'x' || body.s[1] == 'X') {
            value =
                md::DecodeNumeric(a, Str((char*)body.s + 2, body.len - 2), 16);
        } else {
            value =
                md::DecodeNumeric(a, Str((char*)body.s + 1, body.len - 1), 10);
        }
    } else {
        value = md::DecodeNamed(a, body);
    }
    return value.s ? value : e;
}

// One inline tag inside a paragraph. The markdown parser hands `<b>` over as
// an mdast Html node and leaves the meaning to us; Rust reaches the same tags
// through html5ever, since markdown.rs sends the node to format::html. A raw
// HTML *block* is a node of its own and is parsed whole, below.
static void MdInlineHtml(MdBuild* b, Str tag) {
    if (b->cur->kind == MdKind::Html) {
        // Inside a raw HTML block every byte is source, tags included.
        AddText(b, tag);
        return;
    }
    HtmlInlineTag t = HtmlParseInlineTag(b->a, tag);
    if (!t.known) {
        // An unknown tag is dropped, the way Rust drops what its own
        // vocabulary does not cover.
        return;
    }
    if (t.isBreak) {
        AddText(b, StrL("\n"));
        return;
    }
    if (t.isImage) {
        AddImage(b, t.src, t.alt, t.width, t.height);
        return;
    }
    if (t.close) {
        b->marks = (uint8_t)(b->marks & ~t.mark);
        if (t.mark & MdLink) {
            b->href = {};
        }
        return;
    }
    b->marks = (uint8_t)(b->marks | t.mark);
    if (t.mark & MdLink) {
        b->href = t.href;
    }
}

// ─── the mdast walk ───────────────────────────────────────────────────────

static void MdInlineNode(MdBuild* b, const md::Node* n);

static void MdInlineChildren(MdBuild* b, const md::Node* n) {
    for (const md::Node* child : md::NodeKids(b->a, n)) {
        MdInlineNode(b, child);
    }
}

// The children of `n` with `mark` added to whatever is already in force,
// which is markdown.rs's merge_children_with_mark.
static void MdMarked(MdBuild* b, const md::Node* n, uint8_t mark) {
    uint8_t saved = b->marks;
    b->marks = (uint8_t)(b->marks | mark);
    MdInlineChildren(b, n);
    b->marks = saved;
}

// The URL a `[text][id]` or `![alt][id]` points at, from the definitions
// collected below. Empty when the definition is missing, which is what Rust's
// LinkMark holds until the reference is resolved.
static Str MdDefUrl(MdBuild* b, Str identifier) {
    for (const MdDef& def : b->defs) {
        if (StrEq(def.identifier, identifier)) {
            return def.url;
        }
    }
    return {};
}

static void MdInlineNode(MdBuild* b, const md::Node* n) {
    switch (n->kind) {
        case md::NodeKind::Text:
            AddText(b, V(b, n, md::NodeStrKind::Value));
            break;
        case md::NodeKind::Emphasis:
            MdMarked(b, n, MdItalic);
            break;
        case md::NodeKind::Strong:
            MdMarked(b, n, MdBold);
            break;
        case md::NodeKind::Delete:
            MdMarked(b, n, MdDel);
            break;
        case md::NodeKind::InlineCode:
        case md::NodeKind::InlineMath: {
            uint8_t saved = b->marks;
            b->marks = (uint8_t)(b->marks | MdCode);
            AddText(b, V(b, n, md::NodeStrKind::Value));
            b->marks = saved;
            break;
        }
        case md::NodeKind::Break:
            // CommonMark hard breaks start a new row of the inline flow.
            AddText(b, StrL("\n"));
            break;
        case md::NodeKind::Link:
        case md::NodeKind::LinkReference: {
            Str saved = b->href;
            b->href = n->kind == md::NodeKind::Link
                          ? V(b, n, md::NodeStrKind::Url)
                          : MdDefUrl(b, V(b, n, md::NodeStrKind::Identifier));
            MdMarked(b, n, MdLink);
            b->href = saved;
            break;
        }
        case md::NodeKind::Image:
            AddImage(b, V(b, n, md::NodeStrKind::Url),
                     V(b, n, md::NodeStrKind::Alt), 0, 0);
            break;
        case md::NodeKind::ImageReference:
            AddImage(b, MdDefUrl(b, V(b, n, md::NodeStrKind::Identifier)),
                     V(b, n, md::NodeStrKind::Alt), 0, 0);
            break;
        case md::NodeKind::FootnoteReference: {
            // markdown.rs renders the call as an italic `[id]`.
            uint8_t saved = b->marks;
            b->marks = (uint8_t)(b->marks | MdItalic);
            AddText(b, StrL("["));
            AddText(b, V(b, n, md::NodeStrKind::Identifier));
            AddText(b, StrL("]"));
            b->marks = saved;
            break;
        }
        case md::NodeKind::Html:
            MdInlineHtml(b, V(b, n, md::NodeStrKind::Value));
            break;
        default:
            // Anything else is not inline content; Rust warns and drops it.
            break;
    }
}

// The inline children of `n` as the runs of the current block.
static void MdInline(MdBuild* b, const md::Node* n) {
    MdInlineChildren(b, n);
}

static void MdBlockNode(MdBuild* b, const md::Node* n);

static void MdBlockChildren(MdBuild* b, const md::Node* n) {
    for (const md::Node* child : md::NodeKids(b->a, n)) {
        MdBlockNode(b, child);
    }
}

// A code block, however the source spelled it: a fence, an indent, math, or
// the document's frontmatter.
static void MdCodeBlock(MdBuild* b, Str value, Str lang) {
    MdNode* n = Push(b, MdKind::Code);
    n->lang = lang;
    AddText(b, value);
    Pop(b);
}

static void MdTable(MdBuild* b, const md::Node* n) {
    MdNode* table = Push(b, MdKind::Table);
    (void)table;
    int32_t rowIndex = 0;
    for (const md::Node* row : md::NodeKids(b->a, n)) {
        int32_t at = rowIndex++;
        if (row->kind != md::NodeKind::TableRow) {
            continue;
        }
        MdNode* r = Push(b, MdKind::Row);
        // mdast has no thead: the first row is the head.
        r->head = at == 0;
        int32_t cellIndex = 0;
        for (const md::Node* cell : md::NodeKids(b->a, row)) {
            if (cell->kind != md::NodeKind::TableCell) {
                continue;
            }
            int32_t column = cellIndex++;
            MdNode* c = Push(b, MdKind::Cell);
            md::ArenaAlign align = md::NodePerKind(b->a, n);
            if (column < md::ArenaAlignCount(b->a, align)) {
                switch (md::ArenaAlignAt(b->a, align, column)) {
                    case md::AlignKind::Left:
                        c->align = MdAlignLeft;
                        break;
                    case md::AlignKind::Center:
                        c->align = MdAlignCenter;
                        break;
                    case md::AlignKind::Right:
                        c->align = MdAlignRight;
                        break;
                    case md::AlignKind::None:
                        c->align = MdAlignDefault;
                        break;
                }
            }
            MdInline(b, cell);
            Pop(b);
        }
        Pop(b);
    }
    Pop(b);
}

static void MdBlockNode(MdBuild* b, const md::Node* n) {
    if (b->extensions) {
        MarkdownParseContext context;
        context.arena = b->a;
        context.source = b->source;
        for (int i = 0; i < b->extensions->blockParsers.len; i++) {
            const MarkdownBlockParser& parser = b->extensions->blockParsers[i];
            MarkdownNode custom;
            if (!parser.fn || !parser.fn(n, &context, parser.data, &custom)) {
                continue;
            }
            MdNode* node = Push(b, MdKind::Custom);
            node->custom = custom;
            node->custom.name = StrDup(b->a, custom.name);
            node->custom.text = StrDup(b->a, custom.text);
            node->custom.markdown = StrDup(b->a, custom.markdown);
            Pop(b);
            return;
        }
    }
    switch (n->kind) {
        case md::NodeKind::Paragraph:
            Push(b, MdKind::Paragraph);
            MdInline(b, n);
            Pop(b);
            break;
        case md::NodeKind::Heading: {
            MdNode* h = Push(b, MdKind::Heading);
            uint32_t depth = md::NodePerKind(b->a, n);
            h->level = depth == 0 ? 1 : (uint8_t)depth;
            MdInline(b, n);
            Pop(b);
            break;
        }
        case md::NodeKind::Blockquote:
            Push(b, MdKind::Quote);
            MdBlockChildren(b, n);
            Pop(b);
            break;
        case md::NodeKind::List: {
            MdNode* l = Push(b, MdKind::List);
            l->ordered = n->Has(md::NodeOrdered);
            l->start =
                n->Has(md::NodeHasStart) ? (int)md::NodePerKind(b->a, n) : 1;
            MdBlockChildren(b, n);
            Pop(b);
            break;
        }
        case md::NodeKind::ListItem: {
            MdNode* item = Push(b, MdKind::Item);
            // markdown.rs carries mdast's `checked` straight onto the
            // BlockNode. to_mdast has already taken the `[x] ` off the front
            // of the item's first paragraph, so nothing here has to.
            item->hasCheck = n->Has(md::NodeHasChecked);
            item->checked = n->Has(md::NodeChecked);
            MdBlockChildren(b, n);
            Pop(b);
            break;
        }
        case md::NodeKind::ThematicBreak:
            Push(b, MdKind::Rule);
            Pop(b);
            break;
        case md::NodeKind::Code:
            MdCodeBlock(b, V(b, n, md::NodeStrKind::Value),
                        V(b, n, md::NodeStrKind::Lang));
            break;
        case md::NodeKind::Math:
            MdCodeBlock(b, V(b, n, md::NodeStrKind::Value), {});
            break;
        case md::NodeKind::Yaml:
            MdCodeBlock(b, V(b, n, md::NodeStrKind::Value), StrL("yml"));
            break;
        case md::NodeKind::Toml:
            MdCodeBlock(b, V(b, n, md::NodeStrKind::Value), StrL("toml"));
            break;
        case md::NodeKind::Table:
            MdTable(b, n);
            break;
        case md::NodeKind::Html: {
            // The raw source of the block; MdExpandHtml below turns it into
            // children, and the node keeps the source for a plugin that
            // matches on the tag.
            Push(b, MdKind::Html);
            Str raw = V(b, n, md::NodeStrKind::Value);
            b->cur->raw = raw;
            AddText(b, raw);
            Pop(b);
            break;
        }
        case md::NodeKind::Break:
            Push(b, MdKind::Paragraph);
            AddText(b, StrL("\n"));
            Pop(b);
            break;
        case md::NodeKind::FootnoteDefinition: {
            // markdown.rs renders the definition as a paragraph opening with
            // an italic `[id]: `.
            Push(b, MdKind::Paragraph);
            uint8_t saved = b->marks;
            b->marks = (uint8_t)(b->marks | MdItalic);
            AddText(b, StrL("["));
            AddText(b, V(b, n, md::NodeStrKind::Identifier));
            AddText(b, StrL("]: "));
            b->marks = saved;
            for (const md::Node* c : md::NodeKids(b->a, n)) {
                // Its children are blocks; their inline content joins the one
                // paragraph, which is what Rust's parse_paragraph does.
                if (md::NodeHasChildren(c->kind)) {
                    MdInline(b, c);
                } else {
                    AddText(b, V(b, c, md::NodeStrKind::Value));
                }
            }
            Pop(b);
            break;
        }
        case md::NodeKind::Definition:
            // Collected before the walk; it renders as nothing.
        default:
            break;
    }
}

// Every link reference definition in the tree, wherever it sits.
static void MdCollectDefs(MdBuild* b, const md::Node* n) {
    if (n->kind == md::NodeKind::Definition) {
        MdDef def;
        def.identifier = V(b, n, md::NodeStrKind::Identifier);
        def.url = V(b, n, md::NodeStrKind::Url);
        b->defs.Append(b->a, def);
        return;
    }
    for (const md::Node* child : md::NodeKids(b->a, n)) {
        MdCollectDefs(b, child);
    }
}

// A raw HTML block arrives as source text on an MdKind::Html node. Turning
// it into children here rather than at render time means the parse cache
// holds the finished tree, and it is the same hand-off Rust makes when
// markdown.rs gives an mdast::Html node to format::html.
static void MdExpandHtml(Arena* a, MdNode* n) {
    for (MdNode* c = n->first; c; c = c->next) {
        MdExpandHtml(a, c);
    }
    if (n->kind != MdKind::Html || !n->runFirst) {
        return;
    }
    int len = 0;
    for (MdRun* r = n->runFirst; r; r = r->next) {
        len += r->text.len;
    }
    char* buf = (char*)Alloc(a, len + 1);
    if (!buf) {
        return;
    }
    int at = 0;
    for (MdRun* r = n->runFirst; r; r = r->next) {
        memcpy(buf + at, r->text.s, (size_t)r->text.len);
        at += r->text.len;
    }
    buf[at] = 0;
    n->runFirst = nullptr;
    n->runLast = nullptr;
    HtmlParseInto(a, n, Str(buf, at));
}

static MdNode* MdParseWithExtensions(Arena* a, Str source,
                                     const MarkdownExtensions* extensions) {
    MdNode* doc = ArenaNew<MdNode>(a);
    doc->kind = MdKind::Doc;
    if (!source.s || source.len <= 0) {
        return doc;
    }

    // The GFM dialect, which is what TextView renders: tables, strikethrough,
    // task lists, footnotes and bare-URL autolinks. `parse_options` in
    // crates/ui/src/text/markdown_ext.rs asks for the same.
    md::Node* root = md::ToMdast(a, source, md::ParseOptions::Gfm());

    MdBuild b;
    b.a = a;
    b.source = source;
    b.extensions = extensions;
    b.cur = doc;
    MdCollectDefs(&b, root);
    MdBlockChildren(&b, root);
    MdExpandHtml(a, doc);
    return doc;
}

MdNode* MdParse(Arena* a, Str source) {
    return MdParseWithExtensions(a, source, nullptr);
}

// ─── parse cache ──────────────────────────────────────────────────────────
//
// The element tree is rebuilt from the frame arena every frame, but the
// markdown behind it hardly ever changes. Re-parsing the story's 13 KB README
// on every render cost 52us and 44 KB of frame arena for a tree identical to
// the last one, so each window keeps a few parsed documents around, keyed on
// the source text.
//
// A slot owns a copy of the source as well as the nodes, because the tree
// points into the source instead of copying it (see AddText). That way a
// caller may hand us a string that only lives for this frame, and comparing
// the copy is what tells us the cached tree is still the right answer.

struct MdCacheSlot {
    Arena* a = nullptr; // owns `source` and `doc`
    Str source = {};
    MdNode* doc = nullptr;
    uint64_t used = 0; // lookup stamp for LRU; 0 == empty
    // Which parser made `doc`. The same bytes are a different tree read as
    // HTML than read as markdown, so it is part of the key.
    bool html = false;
    uint64_t extensionsRevision = 0;
};

// One story page is on screen at a time and a page holds a handful of these.
constexpr int kMdCacheSlots = 8;

struct MdCache {
    MdCacheSlot slots[kMdCacheSlots] = {};
    uint64_t clock = 0;

    ~MdCache() {
        for (int i = 0; i < kMdCacheSlots; i++) {
            if (slots[i].a) {
                ArenaDelete(slots[i].a);
            }
        }
    }
};

// The whole point is to be much cheaper than a parse. StrEq of the story's
// 13 KB README costs 0.2us against 52us to parse it, so there is no hash to
// reject with first: the length check throws out most misses and StrEq stops
// at the first byte that differs.
// The tree for `source`, parsed only when it isn't cached already. Falls back
// to the frame arena when there is no window to hang a cache off, which is
// what the tests and any headless measuring pass see.
static MdNode* MdParseCached(Ctx* cx, Arena* frame, Str source, bool html,
                             const MarkdownExtensions* extensions) {
    // Keyed on the parser's *shape*, not on the revision every rebuilt
    // closure bumps: a render method that builds its plugins again each frame
    // has to reuse the cached document, or the parse notifies, the notify
    // renders and the document never settles.
    uint64_t extensionRevision =
        extensions ? extensions->ParserFingerprint() : 0;
    MdCache* c = nullptr;
    if (cx && cx->win) {
        auto* slot = KeyedState<Entity<MdCache>>(
            cx, (uint32_t)HashClickId(StrL("gpui-md-parse-cache")));
        if (slot) {
            if (!slot->IsValid()) {
                *slot = EntityNewState<MdCache>(cx->app);
            }
            c = slot->Get(cx);
        }
    }
    if (!c) {
        return html ? HtmlParse(frame, source)
                    : MdParseWithExtensions(frame, source, extensions);
    }

    c->clock++;
    MdCacheSlot* lru = &c->slots[0];
    for (int i = 0; i < kMdCacheSlots; i++) {
        MdCacheSlot* s = &c->slots[i];
        if (s->used != 0 && s->html == html &&
            s->extensionsRevision == extensionRevision &&
            base::StrEq(s->source, source)) {
            s->used = c->clock;
            return s->doc;
        }
        if (s->used < lru->used) {
            lru = s;
        }
    }
    // Evict the least recently looked up slot and reuse its arena.
    if (!lru->a) {
        lru->a = ArenaNew();
    } else {
        lru->a->Reset();
    }
    lru->source = StrDup(lru->a, source);
    lru->html = html;
    lru->extensionsRevision = extensionRevision;
    lru->doc = html ? HtmlParse(lru->a, lru->source)
                    : MdParseWithExtensions(lru->a, lru->source, extensions);
    lru->used = c->clock;
    return lru->doc;
}

MdNode* MdParseCachedForTest(Ctx* cx, Arena* frame, Str source,
                             const MarkdownExtensions* extensions) {
    return MdParseCached(cx, frame, source, false, extensions);
}

// ─── render ───────────────────────────────────────────────────────────────

// The fallback handle_link_click takes when no handler was given: cx.open_url.
static void MdOpenHref(char* href) {
    if (href) {
        OpenUrl(Str(href));
    }
}

// node.rs 2258: h1 2.0/BOLD, h2 1.5, h3 1.25, h4 1.125, h5 1.0/SEMIBOLD,
// h6 1.0/MEDIUM.
static float HeadingScale(int level) {
    switch (level) {
        case 1:
            return 2.f;
        case 2:
            return 1.5f;
        case 3:
            return 1.25f;
        case 4:
            return 1.125f;
        default:
            return 1.f;
    }
}

static int HeadingWeight(int level) {
    if (level == 1) {
        return 3;
    }
    if (level >= 6) {
        return 1;
    }
    return 2;
}

static El* ApplyWeight(El* t, int weight) {
    if (weight >= 3) {
        return t->Bold();
    }
    if (weight == 2) {
        return t->Semibold();
    }
    if (weight == 1) {
        return t->Medium();
    }
    return t;
}

// text/utils.rs BULLETS, one per nesting depth.
static Str Bullet(int depth) {
    switch (depth) {
        case 0:
            return StrL("\xE2\x80\xA2 ");
        case 1:
            return StrL("\xE2\x97\xA6 ");
        case 2:
            return StrL("\xE2\x96\xAA ");
        case 3:
            return StrL("\xE2\x80\xA3 ");
        default:
            return StrL("\xE2\x81\x83 ");
    }
}

// text/utils.rs list_item_prefix: 1. at depth 0, A. at depth 1, a. below.
static Str OrderedMarker(Arena* a, int n, int depth) {
    if (depth == 0) {
        return StrDup(a, fmt("%d. ", n));
    }
    // `0.` is a legal CommonMark start, so index from 0 rather than n - 1.
    int ix = n > 0 ? (n - 1) % 26 : 0;
    return StrDup(a, fmt("%c. ", (depth == 1 ? 'A' : 'a') + ix));
}

// âââ SelectionFormat::Source âââ
//
// node.rs reconstructs the Markdown of a selection by walking the BlockNode
// tree it rendered from. The window's selection here knows only the flat list
// of painted runs, so the walk happens as the tree is built: each block hands
// its runs a gpui::SelBlock naming what opens and closes it, and each run a
// gpui::SelSource naming the marks around it. gpui.cpp's CopyTextHitsIn puts
// the pieces back together in document order.

// Several short pieces as one arena string. The affixes are three or four
// parts and none of them wants a builder.
static Str SrcCat(Arena* a, Str p0, Str p1 = {}, Str p2 = {}, Str p3 = {},
                  Str p4 = {}) {
    Str parts[5] = {p0, p1, p2, p3, p4};
    int len = 0;
    for (const Str& p : parts) {
        len += p.len > 0 ? p.len : 0;
    }
    if (len <= 0) {
        return Str{};
    }
    char* buf = (char*)Alloc(a, len + 1);
    if (!buf) {
        return Str{};
    }
    int at = 0;
    for (const Str& p : parts) {
        if (p.len <= 0 || !p.s) {
            continue;
        }
        memcpy(buf + at, p.s, (size_t)p.len);
        at += p.len;
    }
    buf[at] = 0;
    return Str(buf, at);
}

// As many spaces as `s` is bytes wide, which is the indent Rust's
// list_selected_source puts under a marker so an item's later lines line up
// with its text.
static Str SrcIndent(Arena* a, Str s) {
    if (s.len <= 0) {
        return Str{};
    }
    char* buf = (char*)Alloc(a, s.len + 1);
    if (!buf) {
        return Str{};
    }
    memset(buf, ' ', (size_t)s.len);
    buf[s.len] = 0;
    return Str(buf, s.len);
}

// node.rs wrap_with_mark, split into the two halves a run is emitted between.
// The nesting is that function's: code innermost, then italic, bold,
// strikethrough, underline, highlight, and the link outermost.
static Str SrcMarkPre(Arena* a, uint8_t marks) {
    StrBuilder out(a);
    auto put = [&](const char* text) { out.Append(Str(text)); };
    if (marks & MdLink) {
        put("[");
    }
    if (marks & MdHighlight) {
        put("==");
    }
    if (marks & MdUnderline) {
        put("<u>");
    }
    if (marks & MdDel) {
        put("~~");
    }
    if (marks & MdBold) {
        put("**");
    }
    if (marks & MdItalic) {
        put("*");
    }
    if (marks & MdCode) {
        put("`");
    }
    return out.TakeStr();
}

static Str SrcMarkPost(Arena* a, uint8_t marks, Str href) {
    StrBuilder out(a);
    auto put = [&](const char* text) { out.Append(Str(text)); };
    if (marks & MdCode) {
        put("`");
    }
    if (marks & MdItalic) {
        put("*");
    }
    if (marks & MdBold) {
        put("**");
    }
    if (marks & MdDel) {
        put("~~");
    }
    if (marks & MdUnderline) {
        put("</u>");
    }
    if (marks & MdHighlight) {
        put("==");
    }
    Str tail = out.TakeStr();
    if (marks & MdLink) {
        // Rust writes the title too when the link carries one; MdRun keeps
        // only the url, which is what the parse fold kept.
        return SrcCat(a, tail, StrL("]("), href, StrL(")"));
    }
    return tail;
}

const SelBlock* TextView::SrcOpen(Str marker, Str post, bool join) {
    if (!selectable) {
        return nullptr;
    }
    SelBlock* b = ArenaNew<SelBlock>(a);
    if (!b) {
        return nullptr;
    }
    // The marker a list item left for its first block is spent here; every
    // later block of the item takes the plain line prefix instead. A block
    // that continues the previous one's line opens no line and so takes
    // neither.
    Str head = {};
    if (!join) {
        head = srcMarker.len > 0 ? srcMarker : srcLinePre;
        srcMarker = Str{};
    }
    b->pre = SrcCat(a, head, marker);
    b->post = post;
    b->linePre = srcLinePre;
    b->join = join;
    srcBlock = b;
    srcLineStart = true;
    srcRunLast = nullptr;
    return b;
}

void TextView::SrcCell(MdNode* row, MdNode* c, int nCols,
                       const uint8_t* colAlign) {
    if (!selectable) {
        return;
    }
    // table_selected_source pipes each row — `| a | b |` — and puts the
    // alignment row after the header, so the first cell of a row opens the
    // line, every other cell continues it, and the last cell closes it.
    bool first = c == row->first;
    bool last = c->next == nullptr;
    Str post = last ? StrL(" |") : StrL(" ");
    if (last && row->head) {
        // The delimiter row carries the column alignments and has to follow
        // the header row. Rust's ColumnAlign has no default arm: a column
        // that named no alignment is Left, which is `:--`.
        Str line = SrcCat(a, StrL(" |"), StrL("\n"), srcLinePre, StrL("|"));
        int cells = 0;
        for (MdNode* q = row->first; q; q = q->next) {
            cells++;
        }
        for (int i = 0; i < cells; i++) {
            uint8_t al = i < nCols ? colAlign[i] : (uint8_t)MdAlignDefault;
            Str d = al == MdAlignCenter  ? StrL(" :-: |")
                    : al == MdAlignRight ? StrL(" --: |")
                                         : StrL(" :-- |");
            line = SrcCat(a, line, d);
        }
        post = line;
    }
    SrcOpen(StrL("| "), post, !first);
}

El* TextView::SrcMark(El* t, uint8_t marks, Str href) {
    if (!selectable || !t) {
        return t;
    }
    t->SelectionOwner(BaseTextViewStateCurrent(cx->app));
    // One record per mark group: an adjacent run with the same marks and the
    // same href reuses it, and the copier closes a group only when the record
    // changes — so a bold phrase split into word elements copies as
    // `**one two three**` rather than as three wrapped words, which is what
    // reconstruct_markdown gets from walking mark ranges instead of words.
    bool same = srcRunLast && srcRunLast->block == srcBlock &&
                srcRunMarks == marks && srcRunHref.len == href.len &&
                (href.len == 0 || srcRunHref.s == href.s);
    if (!same) {
        SelSource* s = ArenaNew<SelSource>(a);
        if (!s) {
            return t;
        }
        s->pre = SrcMarkPre(a, marks);
        s->post = SrcMarkPost(a, marks, href);
        s->block = srcBlock;
        srcRunLast = s;
        srcRunMarks = marks;
        srcRunHref = href;
    }
    t->SelSrc(srcRunLast, !srcLineStart);
    srcLineStart = false;
    return t;
}

void TextView::SrcBreak() {
    srcLineStart = true;
}

// node.rs image_markdown: `![alt](url)`. Rust writes the title after the url
// when the image carries one; MdRun keeps the url and the alt text, which is
// what the parse fold kept.
El* TextView::SrcImage(El* e, MdRun* r) {
    if (!selectable || !e) {
        return e;
    }
    SelSource* s = ArenaNew<SelSource>(a);
    if (!s) {
        return e;
    }
    s->pre = SrcCat(a, StrL("!["), r->text, StrL("]("), r->imgSrc, StrL(")"));
    s->block = srcBlock;
    // The image has no text of its own — the whole of it is the affix — and
    // the copier knows that from the run being an image element.
    e->Selectable()
        ->SelectionOwner(BaseTextViewStateCurrent(cx->app))
        ->SelSrc(s, !srcLineStart);
    srcLineStart = false;
    // Not a mark group anything can join: the words after the picture open
    // one of their own.
    srcRunLast = nullptr;
    return e;
}

// `<mark>`: html.rs takes yellow(200) — a fixed Tailwind step, not a theme
// color — and leaves the ink alone, which works because the foreground it
// runs against there is dark. Ours is near-white in the dark theme, so the
// ink is pinned to the same near-black the light theme already paints with
// and the highlight reads the same in both.
static const Rgba kMarkBg = {0xfe, 0xf0, 0x8a, 0xff};
static const Rgba kMarkFg = {0x0a, 0x0a, 0x0a, 0xff};

// node.rs puts an img() element in the flow beside the words: its own size
// unless the document gave one, never wider than the space it has, and — for
// an image inside a link — the hand and the click the link's words get.
El* TextView::ImageRun(MdRun* r, float font, Rgba color, bool inFlow) {
    El* e = ImageEl(a, r->imgSrc, r->text)->Font(font)->Fg(color);
    float w = r->imgW;
    float h = r->imgH;
    // inline_flow.rs image_size: a picture in a run of words that names
    // neither dimension is three quarters of the line it sits on, and as wide
    // as its own shape makes it — the badge beside a sentence is a glyph, not
    // a plate. Only the height is named here; the layout takes the width from
    // the picture's aspect, which is the one it knows and this does not.
    if (inFlow && w <= 0 && h <= 0) {
        h = font * kLineHeight * 0.75f;
    }
    // A vector picture knows its own shape, so a document that gave only one
    // dimension gets the other rather than a run of text's line height. A
    // bitmap cannot answer that without being decoded, so this is the SVG
    // case only — the shipped asset, or one fetched, once it has arrived.
    if ((w > 0) != (h > 0)) {
        Size vb = {};
        int opsLen = 0;
        const uint8_t* ops = ImageVectorForSrc(r->imgSrc, &opsLen);
        if (ops && DrawOpsViewBox(ops, opsLen, &vb) && vb.w > 0 && vb.h > 0) {
            if (w > 0) {
                h = w * (vb.h / vb.w);
            } else {
                w = h * (vb.w / vb.h);
            }
        }
    }
    if (w > 0) {
        e->W(w);
    }
    if (h > 0) {
        e->H(h);
    }
    // `img(..).object_fit(Contain).max_w(relative(1.))`: the limit is present
    // even when HTML supplied a width. The README's showcase image says
    // width=1763, for example, but Rust scales it to the text column.
    e->MaxW(kFill);
    if ((r->marks & MdLink) && r->href.len > 0) {
        e->Cursor(CursorKind::Pointer);
        if (onLink.IsValid()) {
            e->OnClick(ListenerArg(onLink, (intptr_t)r->href.s));
        } else {
            e->OnClick(MkFunc0(MdOpenHref, r->href.s));
        }
    }
    return e;
}

// One styled word. Everything a TextMark can say about a run, applied to the
// element that carries it — node.rs 1390 builds the same HighlightStyle.
El* TextView::Word(Str w, float font, Rgba color, uint8_t marks, int weight,
                   Str href) {
    Rgba c = color;
    if (marks & MdLink) {
        // node.rs: `highlight.color = Some(node_cx.style.link())`. It used to
        // be `cx.theme().link`; the style carries the role now.
        c = textViewStyle.link;
    }
    if (marks & MdHighlight) {
        c = kMarkFg;
    }
    El* t = TextEl(a, w)->Font(font);
    if (c.a) {
        t->Fg(c);
    }
    ApplyWeight(t, (marks & MdBold) ? (weight > 2 ? weight : 2) : weight);
    if (marks & MdItalic) {
        t->Italic();
    }
    if (marks & (MdLink | MdUnderline)) {
        // Rust hands the run an `UnderlineStyle { thickness: px(1.) }`, which
        // GPUI draws as a quad under the whole run — the spaces inside it as
        // well. A shaper's own underline stops at the last glyph, so a word
        // that carries a trailing space would leave a gap before the next
        // one and a multi-word link would be underlined word by word. The
        // rule goes on as a span instead, which is the same 1-DIP quad over
        // the run's full advance.
        TextSpan* sp = ArenaNew<TextSpan>(a);
        sp->lo = 0;
        sp->hi = w.len;
        // The rule takes the colour the glyphs will: a run that named none
        // draws in the theme's own foreground, and a span with no alpha is
        // not drawn at all.
        sp->color = c.a ? c : textViewStyle.foreground;
        sp->underline = true;
        t->Underlines(sp, 1);
    }
    if (marks & MdDel) {
        t->Strikethrough();
    }
    if (marks & MdHighlight) {
        t->Bg(kMarkBg);
    } else if (marks & MdCode) {
        // TextViewStyle::inline_code_highlight falls back to the style's own
        // code background, and says nothing else: an inline code span is the
        // paragraph's font at its size, with a background behind it.
        t->Bg(textViewStyle.InlineCodeBackground());
        if (textViewStyle.inlineCodeFields) {
            t->Refine(textViewStyle.inlineCode, textViewStyle.inlineCodeFields);
        }
    }
    if (selectable) {
        t->Selectable();
    }
    SrcMark(t, marks, href);
    if ((marks & MdLink) && href.len > 0) {
        // handle_link_click: the handler if one was given, the desktop's
        // browser otherwise. The href is NUL-terminated in the arena the
        // parse lives in, so the handler gets a `const char*` it can read for
        // the length of the call — the same rule every hit-test payload
        // follows.
        t->Cursor(CursorKind::Pointer);
        if (onLink.IsValid()) {
            t->OnClick(ListenerArg(onLink, (intptr_t)href.s));
        } else {
            t->OnClick(MkFunc0(MdOpenHref, href.s));
        }
    }
    return t;
}

static bool IsPlainRun(MdRun* r) {
    if (!r || r->next || r->marks != 0 || r->imgSrc.len > 0) {
        return false;
    }
    for (int i = 0; i < r->text.len; i++) {
        if (r->text.s[i] == '\n') {
            return false;
        }
    }
    return true;
}

// A cell's text-align. Rust gives the flow itself the alignment (node.rs
// render_wrap_table); here the row of words is a flex line, so the line is
// what justifies. A left-aligned or default flow still fills the cell, which
// is what lets a long one wrap.
static El* AlignRow(El* row, uint8_t align) {
    if (align == MdAlignCenter) {
        return row->JustifyCenter();
    }
    if (align == MdAlignRight) {
        return row->JustifyEnd();
    }
    return row;
}

El* TextView::Inline(MdNode* n, float font, Rgba color, int weight,
                     uint8_t align) {
    // The common case is one unmarked stretch of source. Keeping it as a
    // single TextEl lets the text engine break the line on its own metrics,
    // which is both better looking and cheaper than a row of word elements.
    if (IsPlainRun(n->runFirst)) {
        El* t = TextEl(a, n->runFirst->text)->Font(font)->Wrap();
        if (color.a) {
            t->Fg(color);
        }
        ApplyWeight(t, weight);
        if (selectable) {
            t->Selectable();
            SrcMark(t, 0);
        }
        if (align == MdAlignCenter || align == MdAlignRight) {
            // The text shrink-wraps so the box around it can push it over.
            return AlignRow(Div(a)->FlexRow()->W(kFill), align)
                ->Child(t)
                ->ReportLineSpan(font * kLineHeight);
        }
        return t->W(kFill)->ReportLineSpan(font * kLineHeight);
    }
    // Otherwise the flow is a column of wrapping rows — a hard break ends a
    // row — and each row is a run of styled words. Every word carries its own
    // trailing space rather than the row carrying a gap: a gap would put a
    // space between an emphasis run and the punctuation after it ("**bold**:"
    // reads as "bold :") and would loosen wrapped-line leading.
    // should_render_inline_flow: a paragraph that mixes words with a picture
    // lays the picture out as part of the line. One that is a picture and
    // nothing else is a block, and keeps the picture's own size.
    bool inFlow = false;
    for (MdRun* r = n->runFirst; r && !inFlow; r = r->next) {
        inFlow = r->imgSrc.len <= 0 && r->text.len > 0;
    }
    El* col = Div(a)->FlexCol()->W(kFill);
    El* row = AlignRow(Div(a)->FlexRow()->FlexWrap()->W(kFill), align);
    int wordCap = 1;
    for (MdRun* r = n->runFirst; r; r = r->next) {
        if (r->text.len > 0) {
            wordCap += r->text.len;
        }
    }
    char* word = (char*)Alloc(a, wordCap);
    if (!word) {
        return col;
    }
    int len = 0;
    uint8_t marks = 0;
    Str href = {};
    auto flush = [&]() {
        if (len <= 0) {
            return;
        }
        row->Child(
            Word(StrDup(a, Str(word, len)), font, color, marks, weight, href));
        len = 0;
    };
    for (MdRun* r = n->runFirst; r; r = r->next) {
        flush();
        marks = r->marks;
        href = r->href;
        if (r->imgSrc.len > 0) {
            row->Child(SrcImage(ImageRun(r, font, color, inFlow), r));
            continue;
        }
        for (int i = 0; i < r->text.len; i++) {
            char c = r->text.s[i];
            if (c == '\n') {
                flush();
                SrcBreak();
                col->Child(row);
                row = AlignRow(Div(a)->FlexRow()->FlexWrap()->W(kFill), align);
                continue;
            }
            word[len++] = c;
            if (c == ' ') {
                flush();
            }
        }
    }
    flush();
    col->Child(row);
    return col->ReportLineSpan(font * kLineHeight);
}

static int RunsLen(MdNode* n) {
    int len = 0;
    for (MdRun* r = n->runFirst; r; r = r->next) {
        len += r->text.len;
    }
    return len;
}

// The text of one cell, its runs joined. A cell that holds a pipe or a
// backslash has to escape it, or the row it is written into stops parsing
// where the pipe is — which is what `Table::to_markdown` does upstream.
static Str TableCellText(Arena* a, MdNode* c) {
    int len = 0;
    for (MdRun* r = c->runFirst; r; r = r->next) {
        for (int i = 0; i < r->text.len; i++) {
            char ch = r->text.s[i];
            len += (ch == '|' || ch == '\\') ? 2 : 1;
        }
    }
    char* buf = (char*)Alloc(a, len + 1);
    if (!buf) {
        return {};
    }
    int at = 0;
    for (MdRun* r = c->runFirst; r; r = r->next) {
        for (int i = 0; i < r->text.len; i++) {
            char ch = r->text.s[i];
            if (ch == '|' || ch == '\\') {
                buf[at++] = '\\';
            }
            // A newline inside a cell would end the row; GFM has no way to
            // carry one, so it becomes a space the way a soft break does.
            buf[at++] = (ch == '\n' || ch == '\r') ? ' ' : ch;
        }
    }
    buf[at] = 0;
    return Str(buf, at);
}

// `Table::to_markdown`: the table as GFM, outer pipes and all, with the
// delimiter row carrying each column's alignment. BlockNode::to_markdown used
// to join cells straight out of the paragraph writer — which trails a blank
// line — and emit no outer pipes, so a single-column table did not round-trip;
// this is the fixed shape.
static Str TableToMarkdown(Arena* a, MdNode* n, const Str* cells, int cols,
                           int rows, const uint8_t* align) {
    (void)n;
    StrBuilder out;
    for (int r = 0; r < rows; r++) {
        out.Append(StrL("|"));
        for (int c = 0; c < cols; c++) {
            out.Append(StrL(" "));
            out.Append(cells[r * cols + c]);
            out.Append(StrL(" |"));
        }
        out.Append(StrL("\n"));
        if (r != 0) {
            continue;
        }
        // The delimiter row, right under the header.
        out.Append(StrL("|"));
        for (int c = 0; c < cols; c++) {
            switch (align[c]) {
                case MdAlignLeft:
                    out.Append(StrL(" :--- |"));
                    break;
                case MdAlignCenter:
                    out.Append(StrL(" :---: |"));
                    break;
                case MdAlignRight:
                    out.Append(StrL(" ---: |"));
                    break;
                default:
                    out.Append(StrL(" --- |"));
                    break;
            }
        }
        out.Append(StrL("\n"));
    }
    return StrDup(a, out.TakeStr());
}

static void TableDimensions(MdNode* table, int* rowsOut, int* colsOut) {
    int rows = 0;
    int cols = 0;
    for (MdNode* r = table ? table->first : nullptr; r; r = r->next) {
        rows++;
        int rowCols = 0;
        for (MdNode* c = r->first; c; c = c->next) {
            rowCols++;
        }
        if (rowCols > cols) {
            cols = rowCols;
        }
    }
    if (rowsOut) {
        *rowsOut = rows;
    }
    if (colsOut) {
        *colsOut = cols;
    }
}

// Arena::Alloc takes an int byte count. Reject only a shape that cannot be
// represented by that allocator; this is an overflow guard, not a content
// limit like the old 32-column/8,192-cell tables.
template <typename T>
static T* TextArenaArray(Arena* a, int count) {
    if (count <= 0 || count > 0x7fffffff / (int)sizeof(T)) {
        return nullptr;
    }
    T* out = (T*)Alloc(a, count * (int)sizeof(T));
    if (out) {
        for (int i = 0; i < count; i++) {
            new (out + i) T();
        }
    }
    return out;
}

static int TableCellCount(int rows, int cols) {
    if (rows <= 0 || cols <= 0 || rows > 0x7fffffff / cols) {
        return 0;
    }
    return rows * cols;
}

Str MdTableToMarkdown(Arena* a, MdNode* table) {
    if (!table) {
        return {};
    }
    int rows = 0;
    int cols = 0;
    TableDimensions(table, &rows, &cols);
    int cellCount = TableCellCount(rows, cols);
    if (cellCount <= 0) {
        return {};
    }
    uint8_t* align = TextArenaArray<uint8_t>(a, cols);
    Str* cells = TextArenaArray<Str>(a, cellCount);
    if (!align || !cells) {
        return {};
    }
    for (MdNode* r = table->first; r; r = r->next) {
        int ix = 0;
        for (MdNode* c = r->first; c; c = c->next, ix++) {
            if (align[ix] == MdAlignDefault) {
                align[ix] = c->align;
            }
        }
    }
    int ri = 0;
    for (MdNode* r = table->first; r; r = r->next, ri++) {
        int ix = 0;
        for (MdNode* c = r->first; c && ix < cols; c = c->next, ix++) {
            cells[ri * cols + ix] = TableCellText(a, c);
        }
    }
    return TableToMarkdown(a, table, cells, cols, rows, align);
}

// The actions row a caller hangs under a table, from the table it is under.
El* TextView::TableActionsRow(MdNode* n, int nCols, const uint8_t* colAlign) {
    if (!tableActions || nCols <= 0) {
        return nullptr;
    }
    int rows = 0;
    TableDimensions(n, &rows, nullptr);
    int cellCount = TableCellCount(rows, nCols);
    if (cellCount <= 0) {
        return nullptr;
    }
    Str* cells = TextArenaArray<Str>(a, cellCount);
    if (!cells) {
        return nullptr;
    }
    int ri = 0;
    for (MdNode* r = n->first; r; r = r->next, ri++) {
        int ix = 0;
        for (MdNode* c = r->first; c && ix < nCols; c = c->next, ix++) {
            cells[ri * nCols + ix] = TableCellText(a, c);
        }
    }
    TableData data;
    data.cols = nCols;
    data.header = cells;
    // Everything under the header row, which is what `rows` is upstream.
    data.rows = rows > 1 ? cells + nCols : nullptr;
    data.rowCount = rows > 1 ? rows - 1 : 0;
    data.markdown = TableToMarkdown(a, n, cells, nCols, rows, colAlign);
    return tableActions(cx, tableActionsData, &data);
}

El* TextView::CodeBlock(MdNode* n) {
    // node.rs takes the corner radius off the Base theme's own tokens; the
    // colours are the style's.
    float radius = base_theme::Theme::Global(cx->app).tokens.radius.md;
    // code_block.selected_source wraps the selected code in a fence carrying
    // the block's language, so it round-trips as Markdown rather than pasting
    // as bare text. The opening fence ends its own line, so the prefix the
    // block sits under has to start the first code line again.
    SrcOpen(SrcCat(a, StrL("```"), n->lang, StrL("\n"), srcLinePre),
            SrcCat(a, StrL("\n"), srcLinePre, StrL("```")));
    El* box = Div(a)->FlexCol()->W(kFill)->Pad(12)->Radius(radius)->Bg(
        textViewStyle.codeBackground);
    if (textViewStyle.codeBlockFields) {
        box->Refine(textViewStyle.codeBlock, textViewStyle.codeBlockFields);
    }
    // The runs are verbatim text with embedded newlines. They go into one
    // TextEl rather than one per line: the text engine then lays every line
    // out against the same metrics, so a line that needs a font fallback (box
    // drawing, CJK) cannot set its own leading and make the block ragged.
    // Nothing wraps, so a long line clips the way a <pre> does.
    int len = RunsLen(n);
    while (len > 0 && n->runLast && n->runLast->text.len > 0 &&
           n->runLast->text.s[n->runLast->text.len - 1] == '\n') {
        // The fence's own trailing newline would paint an empty last line.
        n->runLast->text.len--;
        len--;
    }
    char* buf = (char*)Alloc(a, len + 1);
    if (!buf) {
        return box;
    }
    int at = 0;
    for (MdRun* r = n->runFirst; r; r = r->next) {
        memcpy(buf + at, r->text.s, (size_t)r->text.len);
        at += r->text.len;
    }
    buf[at] = 0;
    // Syntax highlighting is opt-in: the view's own highlighter, then the one
    // TextViewDefaults installed, then none at all. Base ships no language
    // support, which is what let the renderer come down here.
    CodeBlockHighlighterFn highlighter = codeHighlighter;
    void* highlighterData = codeHighlighterData;
    if (!highlighter) {
        TextViewDefaults defaults = TextViewDefaults::Global(cx->app);
        highlighter = defaults.codeBlockHighlighter;
        highlighterData = defaults.codeBlockHighlighterData;
    }
    ArenaVec<CodeHighlight> spans{};
    if (highlighter) {
        gpui::CodeBlock block =
            gpui::CodeBlock::FromCode(Str(buf, at), n->lang);
        highlighter(highlighterData, &block, a, &spans);
    }
    if (spans.len > 0) {
        box->Child(CodeLines(Str(buf, at), spans));
    } else {
        El* t = TextEl(a, Str(buf, at))
                    ->Font(codeFont)
                    ->Fg(textViewStyle.foreground)
                    ->Mono();
        if (selectable) {
            t->Selectable();
            SrcMark(t, 0);
        }
        box->Child(t->ReportLineSpan(codeFont * kLineHeight));
    }
    if (codeActions) {
        // `div().id("actions").absolute().top_2().right_2().bg(muted)
        // .rounded(radius)`, over the block it belongs to.
        El* actions = codeActions(cx, codeActionsData, Str(buf, at), n->lang);
        if (actions) {
            box->Child(Div(a)
                           ->Absolute()
                           ->Top(8)
                           ->Right(8)
                           ->Radius(radius)
                           ->Bg(textViewStyle.codeBackground)
                           ->Child(actions));
        }
    }
    return box;
}

// The highlighted form: one row per line, one element per run of a color.
// This is where the single-TextEl argument above gives way — a line has to
// be several elements to be several colors — so the rows carry the line box
// themselves and every element in them is the same mono face at the same
// size, which keeps the lines from setting their own leading.
El* TextView::CodeLines(Str code, const ArenaVec<CodeHighlight>& spans) {
    const int count = spans.len;
    El* col = Div(a)->FlexCol()->W(kFill);
    float lineH = codeFont * kLineHeight;
    El* row = Div(a)->FlexRow()->H(lineH);
    // The run being gathered: adjacent tokens of one color are one element,
    // which keeps a line of code down to a handful.
    char* piece = (char*)Alloc(a, code.len + 1);
    if (!piece) {
        return col->ReportLineSpan(lineH);
    }
    int len = 0;
    Rgba color = textViewStyle.foreground;
    auto flush = [&]() {
        if (len <= 0) {
            return;
        }
        El* t = TextEl(a, StrDup(a, Str(piece, len)))
                    ->Font(codeFont)
                    ->Fg(color)
                    ->Mono();
        if (selectable) {
            t->Selectable();
            SrcMark(t, 0);
        }
        row->Child(t);
        len = 0;
    };

    // The highlighter answers byte ranges; everything a range does not cover
    // keeps the body colour. A range that runs past the end of the code, or
    // backwards, is dropped rather than clamped — `highlighted_styles`
    // filters `range.start <= range.end && range.end <= code_len`.
    int at = 0;
    int next = 0;
    while (at < code.len) {
        while (next < count &&
               (spans[next].end <= at || spans[next].start > spans[next].end ||
                spans[next].end > code.len)) {
            next++;
        }
        int stop = code.len;
        Rgba c = textViewStyle.foreground;
        if (next < count) {
            if (spans[next].start > at) {
                stop = spans[next].start;
            } else {
                stop = spans[next].end;
                c = spans[next].color;
            }
        }
        for (int i = at; i < stop; i++) {
            char ch = code.s[i];
            if (ch == '\n') {
                flush();
                SrcBreak();
                col->Child(row);
                row = Div(a)->FlexRow()->H(lineH);
                continue;
            }
            if (ch == '\r') {
                continue;
            }
            if (c.a != color.a || c.r != color.r || c.g != color.g ||
                c.b != color.b) {
                flush();
                color = c;
            }
            piece[len++] = ch;
        }
        if (stop <= at) {
            // An empty or already-consumed range would stall the walk.
            next++;
            continue;
        }
        at = stop;
    }
    flush();
    col->Child(row);
    return col->ReportLineSpan(lineH);
}

// node.rs render_scroll_table, which is what `style.table` opts a table into
// with overflow-x: scroll. The columns are as wide as the widest text in
// them — measured, not counted, since a character count is a poor guess on a
// proportional font — and they grow to fill a frame wider than the content.
// A narrower frame squeezes them, their text wrapping, until each is down to
// its floor; below that the table keeps the floors and scrolls, so nothing
// it holds is ever out of reach.
// The offset one scrolling table is at. Rust keeps a ScrollHandle in keyed
// state under the table's own span; the key here is the view and which table
// in it this is, which is the same thing said with what this tree has.
struct MdTableScroll {
    float x = 0;
};

static void OnMdTableScroll(MdTableScroll* st, Ctx* cx, const ScrollEvent* ev) {
    st->x = ev->offsetX;
    Notify(cx);
}

El* TextView::ScrollTable(MdNode* n) {
    // px_2 either side, the border every column but the last draws, and the
    // track's own border on both sides.
    const float kCellPad = 16.f;
    const float kCellMin = 48.f;
    const float kCellBorder = 1.f;
    const float kTableBorder = 2.f;
    // A column stops shrinking at about the width its text would wrap to two
    // lines at, held between the two bounds so a moderate column can still
    // wrap and one huge column cannot push the scrolling threshold up on its
    // own.
    const float kWrapLines = 2.f;
    const float kWrapMin = 160.f;
    const float kWrapMax = 480.f;

    // node.rs paints the frame from the Base theme's surface and the style's
    // border; the radius arrives through `style.table()`, which is what the
    // themed façade fills in.
    Rgba surface = base_theme::Theme::Global(cx->app).tokens.colors.surface;
    PaintCtx* paint = cx->win ? &cx->win->paint : nullptr;
    int rows = 0;
    int nCols = 0;
    TableDimensions(n, &rows, &nCols);
    if (rows <= 0 || nCols <= 0) {
        return Div(a);
    }
    float* colW = TextArenaArray<float>(a, nCols);
    uint8_t* colAlign = TextArenaArray<uint8_t>(a, nCols);
    if (!colW || !colAlign) {
        return Div(a);
    }
    for (MdNode* r = n->first; r; r = r->next) {
        int ix = 0;
        for (MdNode* c = r->first; c; c = c->next, ix++) {
            if (colAlign[ix] == MdAlignDefault) {
                colAlign[ix] = c->align;
            }
            if (colW[ix] < kCellMin) {
                colW[ix] = kCellMin;
            }
            float w = 0;
            if (paint) {
                for (MdRun* run = c->runFirst; run; run = run->next) {
                    // Unwrapped, so what comes back is the run's own width.
                    Size sz =
                        MeasureText(paint, run->text, baseFont, 0, false, 0);
                    w += sz.w;
                }
            } else {
                w = (float)RunsLen(c) * baseFont * 0.5f;
            }
            w += kCellPad + (ix + 1 < nCols ? kCellBorder : 0);
            if (w > colW[ix]) {
                colW[ix] = w;
            }
        }
    }
    float minTotal = kTableBorder;
    float* colMin = TextArenaArray<float>(a, nCols);
    if (!colMin) {
        return Div(a);
    }
    for (int i = 0; i < nCols; i++) {
        float floorW = colW[i] / kWrapLines;
        if (floorW < kWrapMin) {
            floorW = kWrapMin;
        }
        if (floorW > kWrapMax) {
            floorW = kWrapMax;
        }
        colMin[i] = floorW < colW[i] ? floorW : colW[i];
        minTotal += colMin[i];
    }

    El* track =
        Div(a)->FlexCol()->W(kFill)->MinW(minTotal)->Bg(surface)->Border(
            1, textViewStyle.border);
    if (textViewStyle.tableFields) {
        track->Refine(textViewStyle.table, textViewStyle.tableFields);
    }
    for (MdNode* r = n->first; r; r = r->next) {
        El* row = Div(a)->FlexRow()->W(kFill);
        if (r->next) {
            row->BorderB(1, textViewStyle.border);
        }
        if (r->head) {
            row->Bg(textViewStyle.codeBackground)->Fg(textViewStyle.foreground);
            if (textViewStyle.tableHeadFields) {
                row->Refine(textViewStyle.tableHead, textViewStyle
                                                         .tableHeadFields);
            }
        }
        int ix = 0;
        for (MdNode* c = r->first; c; c = c->next, ix++) {
            int col = ix < nCols ? ix : nCols - 1;
            // The measured width is the basis and what the growth is shared
            // out in proportion to, and the floor is where the squeezing
            // stops and the track starts to be wider than its frame.
            El* cell = Div(a)
                           ->Basis(colW[col])
                           ->Grow(colW[col])
                           ->MinW(colMin[col])
                           ->ClipX()
                           ->PadX(8)
                           ->PadY(4);
            if (textViewStyle.tableCellFields) {
                cell->Refine(textViewStyle.tableCell, textViewStyle
                                                          .tableCellFields);
            }
            if (c->next) {
                cell->BorderR(1, textViewStyle.border);
            }
            uint8_t align = c->align;
            if (align == MdAlignDefault) {
                align = colAlign[col];
            }
            SrcCell(r, c, nCols, colAlign);
            cell->Child(Inline(c, baseFont, r->head ? kInheritFg : BlockFg(), 0,
                               align));
            row->Child(cell);
        }
        track->Child(row);
    }
    // The viewport: it clips and scrolls sideways, and the frame is on the
    // track inside it so it wraps the table rather than the box it slides in.
    // The offset is the table's own, so two tables in a document scroll
    // apart.
    // Which table in this view it is — the parse is rebuilt every frame, so
    // the node's address is not a name that lasts, and its position in the
    // document is.
    tableIx++;
    uint32_t name =
        (uint32_t)(cx->self.index + 1) * 1000003u + (uint32_t)tableIx;
    uint32_t key = KeyedKey(name, (uint32_t)HashClickId(StrL("md-table")));
    Entity<MdTableScroll> ent = KeyedEntity<MdTableScroll>(cx, key);
    MdTableScroll* st = ent.Get(cx->app);
    El* scroller = Div(a)
                       ->W(kFill)
                       ->ClipX()
                       ->ScrollX(st ? st->x : 0)
                       ->ScrollId((int)key)
                       ->OnScroll(ListenTo(ent, &OnMdTableScroll))
                       ->Child(track);
    // horizontal_scroll_area: the viewport clips and the mask over it takes
    // the wheel, so the gesture locks to the axis it started on and a
    // vertical-dominant trackpad swipe belongs to the enclosing TextView
    // rather than to this table. #2881 had replaced it with a plain
    // overflow-x scroller and lost that; #2890 put it back.
    scroller = HorizontalScrollArea(cx, StrL("md-table"), scroller);
    El* actions = TableActionsRow(n, nCols, colAlign);
    if (!actions) {
        return scroller;
    }
    // A small gap, so the buttons' hover backgrounds stay clear of the
    // table's border. Where they sit along the row is the caller's business.
    return Div(a)->FlexCol()->W(kFill)->Gap(4)->Child(scroller)->Child(actions);
}

// node.rs render_wrap_table proportions the columns by content length and
// lets them shrink to fit, with a floor per column. Same here: the widths are
// fractions of the table, TableColumnWidth is the floor, and a table whose
// floors do not fit is clipped rather than scrolled — this tree has no
// horizontal scroll area.
//
// Column alignment is the delimiter row's (`|:--:|`) or, for an HTML table,
// the cell's align attribute. A body cell with none of its own takes the
// header cell's, which is how a markdown table says it once.
El* TextView::Table(MdNode* n) {
    enum : uint8_t {
        // node.rs MAX_LENGTH: one long cell must not starve the rest.
        kMaxLen = 150
    };
    Rgba surface = base_theme::Theme::Global(cx->app).tokens.colors.surface;
    int rows = 0;
    int nCols = 0;
    TableDimensions(n, &rows, &nCols);
    if (rows <= 0 || nCols <= 0) {
        return Div(a);
    }
    int* colLen = TextArenaArray<int>(a, nCols);
    uint8_t* colAlign = TextArenaArray<uint8_t>(a, nCols);
    if (!colLen || !colAlign) {
        return Div(a);
    }
    for (MdNode* r = n->first; r; r = r->next) {
        int ix = 0;
        for (MdNode* c = r->first; c; c = c->next, ix++) {
            if (colAlign[ix] == MdAlignDefault) {
                colAlign[ix] = c->align;
            }
            int len = RunsLen(c);
            if (len > kMaxLen) {
                len = kMaxLen;
            }
            if (len > colLen[ix]) {
                colLen[ix] = len;
            }
        }
    }
    float total = 0;
    for (int i = 0; i < nCols; i++) {
        // An empty column still needs room for its border and padding.
        if (colLen[i] < 4) {
            colLen[i] = 4;
        }
        total += (float)colLen[i];
    }
    if (total <= 0) {
        return Div(a);
    }

    El* table = Div(a)->FlexCol()->W(kFill)->Bg(surface)->Border(
        1, textViewStyle.border);
    if (textViewStyle.tableFields) {
        table->Refine(textViewStyle.table, textViewStyle.tableFields);
    }
    for (MdNode* r = n->first; r; r = r->next) {
        El* row = Div(a)->FlexRow()->W(kFill);
        if (r->next) {
            row->BorderB(1, textViewStyle.border);
        }
        if (r->head) {
            row->Bg(textViewStyle.codeBackground)->Fg(textViewStyle.foreground);
            if (textViewStyle.tableHeadFields) {
                row->Refine(textViewStyle.tableHead, textViewStyle
                                                         .tableHeadFields);
            }
        }
        int ix = 0;
        for (MdNode* c = r->first; c; c = c->next, ix++) {
            float frac = ix < nCols ? (float)colLen[ix] / total : 1.f / total;
            El* cell = Div(a)->WFrac(frac)->MinW(tableColW)->PadX(8)->PadY(4);
            if (textViewStyle.tableCellFields) {
                cell->Refine(textViewStyle.tableCell, textViewStyle
                                                          .tableCellFields);
            }
            if (c->next) {
                cell->BorderR(1, textViewStyle.border);
            }
            uint8_t align = c->align;
            if (align == MdAlignDefault && ix < nCols) {
                align = colAlign[ix];
            }
            SrcCell(r, c, nCols, colAlign);
            cell->Child(Inline(c, baseFont, r->head ? kInheritFg : BlockFg(), 0,
                               align));
            row->Child(cell);
        }
        table->Child(row);
    }
    El* actions = TableActionsRow(n, nCols, colAlign);
    if (!actions) {
        return table;
    }
    return Div(a)->FlexCol()->W(kFill)->Gap(4)->Child(table)->Child(actions);
}

Rgba TextView::BlockFg() const {
    // Unset is not `theme.foreground`: node.rs names no colour on a paragraph
    // or a heading, so the run takes whatever the container above the view
    // pushed — an alert's variant colour, a blockquote's grey. A transparent
    // colour is what says "inherit" to Word and Inline below.
    return blockFgSet ? blockFg : kInheritFg;
}

// render_list_item_row: a task list item draws a checkbox where the bullet or
// the number would go — `rems(0.875)` square, square-cornered, the style's own
// foreground as its border, filled with a tick when the item is ticked. The
// box is centred in a cell as tall as the line rather than nudged down by a
// fixed `rems(0.4)`, which is what keeps it on the first line at any text
// size, and `mr_1p5` sits in front of it.
static El* TaskBox(Arena* a, const TextViewStyle& style, float lineHeight,
                   bool on) {
    El* box = Div(a)
                  ->Flex()
                  ->W(14)
                  ->H(14)
                  ->Shrink0()
                  ->ItemsCenter()
                  ->JustifyCenter()
                  ->Border(1, style.foreground);
    if (on) {
        // Rust embeds two one-path SVGs and picks by `style.is_dark()`,
        // because gpui-base has no icon set. This tree's icon byte code is in
        // gpui, below Base, so the same glyph is drawn from there; the colour
        // is the one that reads against a foreground-filled box.
        Rgba tick = style.isDark ? RgbaHex(0x000000) : RgbaHex(0xffffff);
        box->Bg(style.foreground)
            ->Child(IconEl(a, IconName::Check, 10)->Fg(tick));
    }
    // There are no margins in this tree, so the row's `mr_1p5` is padding on
    // the cell that holds the box.
    return Div(a)
        ->Shrink0()
        ->H(lineHeight)
        ->Flex()
        ->ItemsCenter()
        ->PadR(6)
        ->Child(box);
}

El* TextView::Item(MdNode* n, Str marker, int depth) {
    El* content = Div(a)->FlexCol()->Flex1()->MinW(0)->ClipX();
    // list_selected_source: the item's first line carries the Markdown
    // marker, and every line under it is indented by the marker's width so
    // continuations and nested lists line up with the item's text. A task
    // item's `[x] ` rides on the first line with the marker and is not part
    // of that indent, which is Rust indenting by `marker.len()` alone.
    Str savedPre = srcLinePre;
    Str md = srcItemMarker;
    Str pad = srcItemPad.len > 0 ? srcItemPad : md;
    srcItemMarker = Str{};
    srcItemPad = Str{};
    srcMarker = SrcCat(a, srcMarker.len > 0 ? srcMarker : srcLinePre, md);
    srcLinePre = SrcCat(a, srcLinePre, SrcIndent(a, pad));
    // An item's blocks are below; runs sit on the item itself only when
    // something built the tree by hand, since mdast gives even a tight list
    // item a paragraph of its own.
    if (n->runFirst) {
        SrcOpen({}, {});
        content->Child(Inline(n, baseFont, BlockFg(), 0));
    }
    // Everything under a task item is rendered with `todo: checked.is_some()`,
    // which is what takes the bullets off a plain list nested inside one.
    bool savedTodo = inTodo;
    inTodo = n->hasCheck;
    Blocks(content, n, depth, true);
    inTodo = savedTodo;
    // An item with nothing selectable in it never spent its marker.
    srcMarker = Str{};
    srcLinePre = savedPre;
    El* row = Div(a)->FlexRow()->W(kFill)->ItemsStart();
    if (n->hasCheck) {
        row->Child(
            TaskBox(a, textViewStyle, baseFont * kLineHeight, n->checked));
    } else if (marker.len > 0) {
        // list_item_prefix is a plain string child: it takes the color the
        // list inherits, so a bullet inside a red alert is red.
        row->Child(TextEl(a, marker)->Font(baseFont)->Shrink0());
    }
    return row->Child(content);
}

El* TextView::Blocks(El* into, MdNode* n, int depth, bool inList) {
    for (MdNode* c = n->first; c; c = c->next) {
        El* e = Block(c, depth, inList, c->next == nullptr);
        if (e) {
            into->Child(e);
        }
    }
    return into;
}

// The text a plugin's parser is given: an HTML block's raw source, and every
// other block's runs laid end to end.
Str TextView::BlockText(MdNode* n) {
    if (n->kind == MdKind::Html && n->raw.len > 0) {
        return n->raw;
    }
    int len = 0;
    for (MdRun* r = n->runFirst; r; r = r->next) {
        len += r->text.len;
    }
    if (len <= 0) {
        return Str{};
    }
    if (!n->runFirst->next) {
        return n->runFirst->text;
    }
    char* buf = (char*)Alloc(a, len + 1);
    if (!buf) {
        return Str{};
    }
    int at = 0;
    for (MdRun* r = n->runFirst; r; r = r->next) {
        memcpy(buf + at, r->text.s, (size_t)r->text.len);
        at += r->text.len;
    }
    buf[at] = 0;
    return Str(buf, at);
}

// Every registered plugin is offered the block, in the order they were added.
El* TextView::PluginBlock(MdNode* n) {
    if (plugins.len <= 0) {
        return nullptr;
    }
    Str text = BlockText(n);
    for (int i = 0; i < plugins.len; i++) {
        MdPluginNode node;
        node.name = plugins[i].name;
        if (!plugins[i].parse(cx, n, text, plugins[i].data, &node)) {
            continue;
        }
        if (El* el = plugins[i].render(cx, &node, plugins[i].data)) {
            return el;
        }
    }
    return nullptr;
}

El* TextView::Block(MdNode* n, int depth, bool inList, bool isLast) {
    if (n->kind == MdKind::Custom) {
        float pad = (inList || isLast) ? 0.f : paragraphGap;
        const struct MarkdownBlockRenderer* renderer =
            markdownExtensions.Renderer(n->custom.name);
        El* content = renderer && renderer->fn
                          ? renderer->fn(cx, &n->custom, renderer->data)
                          : nullptr;
        if (!content && n->custom.text.s) {
            SrcOpen({}, {});
            content = TextEl(a, n->custom.text)->Font(baseFont)->Wrap();
            if (selectable) {
                content->Selectable();
                SrcMark(content, 0);
            }
        }
        return content ? Div(a)->W(kFill)->PadB(pad)->Child(content) : nullptr;
    }
    // A plugin's block stands in for whatever markdown made of it, and takes
    // the paragraph gap the block it replaced would have carried.
    if (El* claimed = PluginBlock(n)) {
        float pad = (inList || isLast) ? 0.f : paragraphGap;
        return Div(a)->W(kFill)->PadB(pad)->Child(claimed);
    }
    // node.rs render_block: every block but the last one in its container
    // carries the paragraph gap below it, and a block inside a list item
    // carries none.
    float mb = (inList || isLast) ? 0.f : paragraphGap;
    switch (n->kind) {
        case MdKind::Paragraph:
            SrcOpen({}, {});
            return Div(a)->W(kFill)->PadB(mb)->Child(
                Inline(n, baseFont, BlockFg(), 0));
        case MdKind::Heading: {
            float font = textViewStyle.headingFontSize
                             ? textViewStyle.HeadingSize(n->level)
                             : headingFont * HeadingScale(n->level);
            // node.rs prefixes the heading marker in source mode so a
            // selected heading round-trips as `## Title`.
            char hashes[8] = {};
            int nh = n->level > 0 && n->level < 8 ? n->level : 1;
            for (int i = 0; i < nh; i++) {
                hashes[i] = '#';
            }
            SrcOpen(SrcCat(a, Str(hashes, nh), StrL(" ")), {});
            // Headings use their own 0.3rem bottom padding, not the gap.
            return Div(a)->W(kFill)->PadB(5)->Child(
                Inline(n, font, BlockFg(), HeadingWeight(n->level)));
        }
        case MdKind::Rule:
            return Div(a)->W(kFill)->PadB(mb)->Child(
                Div(a)->H(2)->W(kFill)->Bg(textViewStyle.border));
        case MdKind::Quote: {
            El* inner = Div(a)
                            ->FlexCol()
                            ->W(kFill)
                            ->Fg(textViewStyle.mutedForeground)
                            ->BorderL(3, textViewStyle.border)
                            ->PadX(16);
            // text_color(muted_foreground) on the quote, and nothing inside
            // it naming a colour of its own: the paragraphs and headings it
            // holds inherit the grey rather than painting themselves black.
            Rgba savedFg = blockFg;
            bool savedSet = blockFgSet;
            blockFg = textViewStyle.mutedForeground;
            blockFgSet = true;
            // node.rs prefixes every line of a selected blockquote with
            // `> ` so it round-trips; nested quotes stack the prefix.
            Str savedPre = srcLinePre;
            srcLinePre = SrcCat(a, srcLinePre, StrL("> "));
            Blocks(inner, n, depth, false);
            srcLinePre = savedPre;
            blockFg = savedFg;
            blockFgSet = savedSet;
            return Div(a)->W(kFill)->PadB(mb)->Child(inner);
        }
        case MdKind::List: {
            El* list = Div(a)->FlexCol()->W(kFill)->MinW(0)->PadB(mb);
            int ix = n->start;
            for (MdNode* c = n->first; c; c = c->next) {
                if (c->kind != MdKind::Item) {
                    continue;
                }
                Str marker =
                    n->ordered ? OrderedMarker(a, ix, depth) : Bullet(depth);
                // list_selected_source restores the Markdown marker rather
                // than the bullet glyph the item is drawn with, and puts the
                // task list's checkbox after it.
                Str src = n->ordered ? OrderedMarker(a, ix, 0) : StrL("- ");
                srcItemPad = src;
                if (c->hasCheck) {
                    // The checkbox stands where the prefix would: `when(!todo
                    // && checked.is_none())` is what draws one at all.
                    src = SrcCat(a, src,
                                 c->checked ? StrL("[x] ") : StrL("[ ] "));
                    marker = Str{};
                } else if (inTodo) {
                    marker = Str{};
                }
                srcItemMarker = src;
                list->Child(Item(c, marker, depth + 1));
                ix++;
            }
            return list;
        }
        case MdKind::Code:
            return Div(a)->W(kFill)->PadB(mb)->Child(CodeBlock(n));
        case MdKind::Table:
            return Div(a)->W(kFill)->PadB(mb)->Child(
                tableScroll ? ScrollTable(n) : Table(n));
        case MdKind::Item: {
            // Only reached for a stray list item; treat it as its contents.
            El* box = Div(a)->FlexCol()->W(kFill)->PadB(mb);
            if (n->runFirst) {
                SrcOpen({}, {});
                box->Child(Inline(n, baseFont, BlockFg(), 0));
            }
            return Blocks(box, n, depth, inList);
        }
        case MdKind::Html:
        case MdKind::Group: {
            // BlockNode::Root: the children in the parent's flow, no box.
            // The gap goes on the group rather than on its last child, so a
            // <div> of paragraphs sits the same distance from what follows it
            // as a paragraph would.
            if (!n->first) {
                return nullptr;
            }
            El* box = Div(a)->FlexCol()->W(kFill)->PadB(mb);
            return Blocks(box, n, depth, inList);
        }
        case MdKind::Doc:
        case MdKind::Row:
        case MdKind::Cell:
        case MdKind::Custom:
            return nullptr;
    }
    return nullptr;
}

El* TextView::IntoEl() {
    // The style this view renders with: its own, then the application's
    // TextViewDefaults, then the Base theme's palette. A Base application
    // that installs neither still gets a readable document.
    if (!textViewStyleSet) {
        TextViewDefaults defaults = TextViewDefaults::Global(cx->app);
        TextViewStyle resolved =
            defaults.hasStyle
                ? defaults.style
                : TextViewStyle::FromTheme(base_theme::Theme::Global(cx->app));
        // Font sizes named on the builder outlive the style swap: they are
        // the caller's, not the palette's.
        resolved.headingBaseFontSize = textViewStyle.headingBaseFontSize;
        resolved.paragraphGap = textViewStyle.paragraphGap;
        textViewStyle = resolved;
    }
    if (!state.IsValid()) {
        uint32_t kind = (uint32_t)HashClickId(
            html ? StrL("TextViewStateHtml") : StrL("TextViewStateMarkdown"));
        uint32_t key = KeyedKey(KeyedName(cx, source), kind);
        state = cx->win ? KeyedEntity<TextViewState>(cx, key)
                        : EntityNewState<TextViewState>(cx->app);
        if (TextViewState* managed = state.Get(cx)) {
            if (!managed->self.IsValid()) managed->self = state.id;
            if (!base::StrEq(managed->text, source)) {
                StrFree(managed->text);
                managed->text = StrDup(source);
                managed->revision++;
                managed->selectionRevision++;
            }
            managed->format =
                html ? TextViewFormat::Html : TextViewFormat::Markdown;
        }
    }
    TextViewState* managed = state.Get(cx);
    if (managed) {
        source = managed->text;
        html = managed->format == TextViewFormat::Html;
        managed->selectable = selectable;
        managed->selectionFormat = selFormat;
        managed->scrollable = scrollable;
        managed->maxLines = scrollable ? -1 : maxLines;
        // A style that moved — a theme change, most often — invalidates the
        // selection layout the last frame published.
        if (!managed->textViewStyle.Equals(textViewStyle)) {
            managed->selectionRevision++;
        }
        managed->textViewStyle = textViewStyle;
    }

    BaseTextViewStatePush(cx->app, state.id);
    MdNode* doc = MdParseCached(cx, a, source, html,
                                html ? nullptr : &markdownExtensions);
    // Rust keeps selection_format on the view's own state and reconstructs
    // the source when the copy asks for it. The copy is the window's here,
    // so the view says what format it wants and the runs it builds below
    // carry the Markdown that format needs.
    if (selectable) {
        WindowSelectionSetFormat(
            cx->win, html ? gpui::SelectionFormat::Plain : selFormat);
    }
    // `.text_color(text_view_style.foreground())` on the root: every block
    // below names no colour of its own, so this is what they inherit.
    El* root = Div(a)->FlexCol()->W(kFill);
    if (textViewStyle.foreground.a) {
        root->Fg(textViewStyle.foreground);
    }
    El* element = Blocks(root, doc, 0, false);
    BaseTextViewStatePop(cx->app);

    // max_lines is fit-content only: cap at body-text leading, keep the full
    // subtree laid out underneath, then let paint snap the mask to a whole
    // Inline boundary. A zero count intentionally produces an empty box.
    if (!scrollable && maxLines >= 0) {
        float cap = baseFont * kLineHeight * (float)maxLines;
        element->LineClamp(
            cap, state.IsValid() ? ListenTo(state, &TextViewState::OnLineClamp)
                                 : Listener{});
    }

    if (scrollable && cx->win && managed) {
        uint32_t name = (uint32_t)(state.id.index + 1) * 1000003u +
                        (uint32_t)(state.id.gen + 1);
        uint32_t key =
            KeyedKey(name, (uint32_t)HashClickId(StrL("TextViewScrollState")));
        element = Div(a)
                      ->FlexCol()
                      ->W(kFill)
                      ->H(kFill)
                      ->ClipY()
                      ->ScrollY(managed->scrollY)
                      ->ScrollId((int)key)
                      ->OnScroll(ListenTo(state, &TextViewState::OnScroll))
                      ->Child(element);
    }
    if (outerStyleFields) {
        element->Refine(outerStyle, outerStyleFields);
    }
    if (selectable && state.IsValid()) {
        TextViewInitKeys();
        int focus = HashClickId(
            StrDup(a, fmt("text-view-%d-%u", state.id.index, state.id.gen)));
        Listener onAction = ListenTo(state, &TextViewState::OnAction);
        element->KeyContext(StrL("TextView"))
            ->FocusId(focus)
            ->FocusOnPress()
            ->OnAction(input::Copy(), onAction)
            ->OnAction(input::SelectAll(), onAction);
    }
    return element;
}

// ─── builder ──────────────────────────────────────────────────────────────

TextView* TextView::New(Ctx* cx, Str source) {
    Arena* a = cx->a;
    TextView* t = ArenaNew<TextView>(a);
    t->a = a;
    t->cx = cx;
    t->source = source;
    // Left unresolved: IntoEl picks the caller's style, the application's
    // defaults or the Base palette, in that order.
    t->textViewStyle = TextViewStyle::Default();
    return t;
}

Text Text::FromStr(Str value) {
    Text out;
    out.string = value;
    return out;
}

Text Text::FromView(TextView* value) {
    Text out;
    out.view = value;
    return out;
}

Text Text::Style(const TextViewStyle& style) const {
    Text out = *this;
    if (out.view) {
        out.view->Style(style);
    }
    return out;
}

Str Text::GetText(const App* app) const {
    if (!view) {
        return string;
    }
    if (view->state.IsValid()) {
        if (TextViewState* managed = view->state.Get((App*)app)) {
            return managed->Source();
        }
    }
    return view->source;
}

El* Text::IntoEl(Ctx* cx) const {
    if (view) {
        return view->IntoEl();
    }
    return TextEl(cx->a, string);
}

TextView* MarkdownView(Ctx* cx, Str source) {
    return TextView::New(cx, source);
}

TextView* HtmlView(Ctx* cx, Str source) {
    return TextView::NewHtml(cx, source);
}

TextView* TextView::NewHtml(Ctx* cx, Str source) {
    TextView* t = TextView::New(cx, source);
    t->html = true;
    return t;
}

TextView* TextView::New(Ctx* cx, Entity<TextViewState> managed) {
    TextView* t = TextView::New(cx, Str{});
    t->state = managed;
    return t;
}

TextView* TextView::TableActions(TableActionsFn fn, void* data) {
    tableActions = fn;
    tableActionsData = data;
    return this;
}

TextView* TextView::CodeBlockActions(CodeBlockActionsFn fn, void* data) {
    codeActions = fn;
    codeActionsData = data;
    return this;
}

TextView* TextView::Plugin(Str name, MdPluginParseFn parse,
                           MdPluginRenderFn render, void* data) {
    if (!parse || !render) {
        return this;
    }
    plugins.Append(a, MdPlugin{name, parse, render, data});
    return this;
}

TextView* TextView::OnLink(Listener fn) {
    onLink = fn;
    return this;
}

TextView* TextView::Font(float px) {
    baseFont = px;
    return this;
}

TextView* TextView::HeadingFont(float px) {
    headingFont = px;
    textViewStyle.headingBaseFontSize = px;
    return this;
}

TextView* TextView::Style(const TextViewStyle& value) {
    textViewStyle = value;
    textViewStyleSet = true;
    headingFont = value.headingBaseFontSize;
    paragraphGap = value.paragraphGap;
    return this;
}

TextView* TextView::CodeBlockHighlighter(CodeBlockHighlighterFn fn,
                                         void* data) {
    codeHighlighter = fn;
    codeHighlighterData = data;
    return this;
}

TextView* TextView::Refine(const gpui::Style& value, uint32_t fields) {
    StyleApplyFields(&outerStyle, value, fields);
    outerStyleFields |= fields;
    return this;
}

TextView* TextView::Selectable(bool on) {
    selectable = on;
    return this;
}

TextView* TextView::SelFormat(gpui::SelectionFormat fmt) {
    selFormat = fmt;
    return this;
}

TextView* TextView::TableColumnWidth(float px) {
    tableColW = px;
    return this;
}

TextView* TextView::TableScroll(bool on) {
    tableScroll = on;
    return this;
}

TextView* TextView::Scrollable(bool on) {
    scrollable = on;
    return this;
}

TextView* TextView::MaxLines(int count) {
    maxLines = count >= 0 ? count : 0;
    return this;
}

TextView* TextView::ParagraphGap(float px) {
    paragraphGap = px;
    textViewStyle.paragraphGap = px;
    return this;
}

TextView* TextView::MarkdownExtensionsSet(
    const MarkdownExtensions& extensions) {
    markdownExtensions.enableMdx = extensions.enableMdx;
    markdownExtensions.revision = extensions.revision;
    for (int i = 0; i < extensions.blockParsers.len; i++) {
        markdownExtensions.blockParsers.Append(a, extensions.blockParsers[i]);
    }
    for (int i = 0; i < extensions.blockRenderers.len; i++) {
        markdownExtensions.blockRenderers
            .Append(a, extensions.blockRenderers[i]);
    }
    return this;
}

TextView* TextView::MarkdownBlockParser(MarkdownBlockParserFn parser,
                                        void* data) {
    markdownExtensions.BlockParser(a, parser, data);
    return this;
}

TextView* TextView::MarkdownBlockRenderer(Str name,
                                          MarkdownBlockRenderFn renderer,
                                          void* data) {
    markdownExtensions.BlockRenderer(a, name, renderer, data);
    return this;
}

TextView* TextView::Plugin(const MarkdownPlugin& plugin) {
    markdownExtensions.Plugin(a, plugin);
    return this;
}

TextView* TextView::Plugin(const TextViewPlugin& plugin) {
    return plugin.Setup(this);
}

} // namespace gpui
