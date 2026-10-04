#ifndef GPUI_BASE_TEXT_H_
#define GPUI_BASE_TEXT_H_
/* Unstyled markdown / HTML view — crates/base/src/text.

   This is gpui-base's rich text: the parser, the block tree, the renderer and
   the selection, with no dependency on the themed layer. `crates/ui/src/text`
   is now a façade over it and `src/ui/text.h` is that façade here.

   Rust parses with the `markdown` crate into an mdast, folds that into its own
   BlockNode tree (text/node.rs) and renders the tree. This is the same shape
   and the same parser: src/markdown is that crate ported (see
   src/markdown/readme.md), text.cpp folds the mdast it returns into the MdNode
   tree below, and TextView::IntoEl walks it. It runs in the GFM dialect, the
   one markdown_ext.rs asks for, so tables, strikethrough, task lists,
   footnotes and bare-URL autolinks all work.

   Raw HTML is the other half. Rust parses it with html5ever and folds the
   DOM into the same BlockNode tree (text/format/html.rs); src/html5ever is
   that crate's parser surface here and base/text_format.cpp folds its DOM
   into MdNode. An HTML block inside markdown, an inline <b> or <a>, and a
   whole HTML document all render through the one walk below.

   This is the one rich-text renderer in the tree. Rust has exactly one too —
   everything that shows markdown or HTML goes through TextView — so a page
   that needs it should come here rather than grow another parser.

   Colours come from TextViewStyle, not from a theme lookup: a Base
   application renders legibly with `TextViewStyle::Default()` and the themed
   layer installs its own through TextViewDefaults. Syntax highlighting is
   opt-in for the same reason — `CodeBlockHighlighter` is a callback, and
   `src/ui/text.cpp` is the one that fills it in with ui/syntax.h.

   An image — ![alt](src) or <img> — is a run of its own in the flow, drawn
   by gpui/image.h from the asset roots or from a data: URI. What it cannot
   reach it shows as its alt text, and that is most of what a document
   written for the web holds: fetching an http(s) URL needs a socket and a
   TLS stack this tree does not have.

   Selection is the window's: say Selectable() on the text and a drag runs
   from one paragraph into the next, across every element between them
   (base/text_selection.h WindowSelection). */

#include "gpui/gpui.h"
#include "base/motion.h"
#include "base/theme.h"
#include "markdown/markdown.h"

namespace gpui {

struct TextView;

// text/node.rs's public value vocabulary. The renderer stores the compact
// equivalents on MdRun, but callers and plugins can use the source-shaped
// values without depending on that representation.
struct Span {
    int start = 0;
    int end = 0;
};

struct LinkMark {
    Str url = {};
    Str identifier = {};
    Str title = {};
};

struct TextMark {
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    bool underline = false;
    bool code = false;
    Rgba highlight = {};
    LinkMark link = {};
    bool hasHighlight = false;
    bool hasLink = false;

    TextMark& Bold();
    TextMark& Italic();
    TextMark& Strikethrough();
    TextMark& Underline();
    TextMark& Code();
    TextMark& Highlight(Rgba color);
    TextMark& Link(LinkMark value);
    void Merge(const TextMark& other);
};

struct ImageNode {
    Str url = {};
    LinkMark link = {};
    Str title = {};
    Str alt = {};
    float width = 0;
    float height = 0;
    bool hasLink = false;

    Str Title(Arena* a) const;
};

// The markdown crate port keeps a node's unist Position in a side table
// (markdown::NodePositions) rather than on the node, so NodeSource reads it
// from there; a parse that kept none answers an empty Str. Value exposes the
// mdast string fields custom parsers actually consume, and Copy gives their
// results the same parse-arena lifetime as the document.
struct MarkdownParseContext {
    Arena* arena = nullptr;
    Str source = {};
    int offset = 0;
    const markdown::NodePositions* positions = nullptr;

    Str Source() const { return source; }
    int Offset() const { return offset; }
    Str NodeSource(const markdown::Node* node) const;
    Str Value(const markdown::Node* node, markdown::NodeStrKind kind) const;
    Str Copy(Str value) const;
};

// markdown_ext.rs MarkdownNode. Rust's Arc<dyn Any> becomes an opaque POD
// payload owned by the caller; parsed strings and the record itself live in
// the document arena.
struct MarkdownNode {
    Str name = {};
    Str text = {};
    Str markdown = {};
    void* data = nullptr;
    Span span = {};
    bool hasSpan = false;

    static MarkdownNode New(Str name, void* data = nullptr);
    MarkdownNode& Text(Str value);
    MarkdownNode& Markdown(Str value);
    Str ToMarkdown() const;
};

// inline_element.rs. An element is one atomic object in a wrapping text row;
// an explicit baseline is measured down from its top edge.
struct InlineElement {
    El* element = nullptr;
    float baseline = 0;
    bool hasBaseline = false;

    static InlineElement New(El* element) { return {element, 0, false}; }
    InlineElement& WithBaseline(float px) {
        baseline = px;
        hasBaseline = true;
        return *this;
    }
};

struct InlineRenderContext {
    gpui::Style textStyle = {};
    float fontSize = 0;
    float lineHeight = 0;
    float remSize = 16;
};

using MarkdownBlockParserFn = bool (*)(const markdown::Node* node,
                                       const MarkdownParseContext* context,
                                       void* data, MarkdownNode* out);
using MarkdownBlockRenderFn = El* (*)(Ctx * cx, const MarkdownNode* node,
                                      void* data);
using MarkdownInlineRenderFn =
    InlineElement (*)(Ctx* cx, const MarkdownNode* node,
                      const InlineRenderContext* context, void* data);

// MarkdownPlugin's object-safe C++ projection. Function pointers plus an
// opaque payload are the repository-wide replacement for boxed closures.
struct MarkdownPlugin {
    Str name = {};
    MarkdownBlockParserFn parse = nullptr;
    MarkdownBlockRenderFn render = nullptr;
    MarkdownInlineRenderFn renderInline = nullptr;
    void* data = nullptr;
    bool isBlock = false;
};

struct MarkdownBlockParser {
    MarkdownBlockParserFn fn = nullptr;
    void* data = nullptr;
};

struct MarkdownBlockRenderer {
    Str name = {};
    MarkdownBlockRenderFn fn = nullptr;
    void* data = nullptr;
};

struct MarkdownInlineRenderer {
    Str name = {};
    MarkdownBlockRenderFn render = nullptr;
    MarkdownInlineRenderFn renderInline = nullptr;
    void* data = nullptr;
};

// markdown_ext.rs MarkdownExtensions. MDX remains unavailable because the
// pinned markdown crate port excludes MDX itself; Mdx records the request so
// callers can detect that it cannot be honored instead of silently parsing
// the document as a different dialect.
struct MarkdownExtensions {
    ArenaVec<MarkdownBlockParser> blockParsers{};
    ArenaVec<MarkdownBlockRenderer> blockRenderers{};
    ArenaVec<MarkdownBlockParser> inlineParsers{};
    ArenaVec<MarkdownInlineRenderer> inlineRenderers{};
    uint64_t revision = 0;
    // parser_revision: what the caller changes when a parser's captures or a
    // plugin's configuration change. Equal revisions let registrations
    // rebuilt every frame keep the parsed document; renderer-only changes
    // need no new value.
    uint64_t parserRevision = 0;
    bool enableMdx = false;
    bool enableFrontmatter = false;

    MarkdownExtensions& ParserRevision(uint64_t value);
    MarkdownExtensions& Mdx();
    MarkdownExtensions& Frontmatter();
    MarkdownExtensions& BlockParser(Arena* a, MarkdownBlockParserFn fn,
                                    void* data = nullptr);
    MarkdownExtensions& BlockRenderer(Arena* a, Str name,
                                      MarkdownBlockRenderFn fn,
                                      void* data = nullptr);
    MarkdownExtensions& Plugin(Arena* a, const MarkdownPlugin& plugin);
    const MarkdownBlockRenderer* Renderer(Str name) const;
    const MarkdownInlineRenderer* InlineRenderer(Str name) const;
    // `has_same_parser_configuration`: whether replacing these handles can
    // change the parsed tree. A render method commonly rebuilds equivalent
    // plugin closures every frame — their revisions all differ, but the
    // parser's shape does not, and reparsing on each would never settle.
    bool HasSameParserConfiguration(const MarkdownExtensions& other) const;
    // That shape as one number, which is what the parse cache is keyed on.
    uint64_t ParserFingerprint() const;
};

// text_view.rs TextViewPlugin. This is deliberately a setup operation over
// the frame builder, just as the Rust trait consumes and returns TextView.
using TextViewSetupFn = TextView* (*)(TextView * view, void* data);
struct TextViewPlugin {
    TextViewSetupFn setup = nullptr;
    void* data = nullptr;

    TextView* Setup(TextView* view) const;
};

// text/node.rs TextMark.
enum MdMark : uint8_t {
    MdBold = 1 << 0,
    MdItalic = 1 << 1,
    MdCode = 1 << 2,
    MdDel = 1 << 3,
    MdUnderline = 1 << 4,
    MdLink = 1 << 5,
    // <mark>: TextMark::highlight, painted with the theme's yellow behind it.
    // Rust reads a color off the tag; this takes the default one.
    MdHighlight = 1 << 6,
};

// One styled piece of a paragraph: node.rs's (text, TextMark) pair, or —
// when `imgSrc` is set — node.rs's InlineNode::image, an ImageNode sitting in
// the flow with the words. `text` is then the alt text, which is what paints
// if the source will not decode (gpui/image.h says when that is).
struct MdRun {
    Str text = {};
    // node.rs InlineNode::source_segments: the run's rendered bytes paired
    // with the Markdown bytes they came from, in the run's own offsets. None
    // for a run the parse could not place (inline HTML, a footnote
    // definition's label), which selected_source_range reads as unmapped.
    const SourceSegment* segments = nullptr;
    int segmentCount = 0;
    // ImageNode::span, for an image run: the whole `![alt](url)`.
    Span imgSpan = {};
    bool hasImgSpan = false;
    // LinkMark::url, when marks has MdLink.
    Str href = {};
    // ImageNode::url. An image run carries no other text.
    Str imgSrc = {};
    // ImageNode::width / height, when the document gave them. 0 is "its own".
    float imgW = 0;
    float imgH = 0;
    MarkdownNode custom = {};
    bool hasCustom = false;
    MdRun* next = nullptr;
    uint8_t marks = 0;
};

// text/node.rs BlockNode. Table rows and cells are blocks here rather than
// the separate TableRow / TableCell structs Rust uses; the tree walk is the
// same either way.
enum class MdKind : uint8_t {
    Doc,
    Paragraph,
    Heading,
    Quote,
    List,
    Item,
    Code,
    Table,
    Row,
    Cell,
    Rule,
    // A raw HTML block inside markdown. Its children are what
    // base/text_format.cpp
    // made of the raw text; Rust reaches the same place through
    // markdown_ext.rs handing the html node to format::html.
    Html,
    // An HTML container that is not a block of its own — div, section, li's
    // wrapper, <figure>. BlockNode::Root in Rust: it contributes its
    // children and no box.
    Group,
    // markdown_ext.rs BlockNode::Custom. Parsed during mdast conversion and
    // rendered later through the extension registry.
    Custom,
};

// mdast's AlignKind, repeated so text_format.cpp — which has no mdast of its
// own — can say the same thing.
enum MdAlign : uint8_t {
    MdAlignDefault = 0,
    MdAlignLeft = 1,
    MdAlignCenter = 2,
    MdAlignRight = 3,
};

struct MdNode {
    MdKind kind = MdKind::Doc;
    MdNode* parent = nullptr;
    MdNode* first = nullptr;
    MdNode* last = nullptr;
    MdNode* next = nullptr;
    // Inline content, for Paragraph, Heading, Cell and Code.
    MdRun* runFirst = nullptr;
    MdRun* runLast = nullptr;
    // Code: the fence's info string, e.g. "cpp".
    Str lang = {};
    // Html: the block's raw source, kept after MdExpandHtml has turned it
    // into children — a plugin matches on the tag it names, which is what
    // Rust's `Node::Html(raw)` arm reads.
    Str raw = {};
    // List: the first ordered number.
    int start = 1;
    // Heading: 1..6.
    uint8_t level = 0;
    // Cell: MD_ALIGN.
    uint8_t align = 0;
    bool ordered = false;
    // Row: this row is the table head.
    bool head = false;
    // Item: the GFM task list checkbox — whether the item carries one at all,
    // and whether it is ticked. BlockNode::ListItem's `Option<bool> checked`,
    // as the pair mdast keeps it as. text_format.cpp leaves both unset, which
    // is what format/html.rs does.
    bool hasCheck = false;
    bool checked = false;
    // The TextLeafKey of this node's text, for a paragraph, heading, code
    // block or table cell whose block the parse placed: the owning block's
    // source start (the table's, for a cell) and the cell ordinal. -1 when
    // it has none, which leaves the text out of every range highlight.
    int leafStart = -1;
    int leafOrdinal = 0;
    // A top-level block's source start (BlockNode::span), what an append
    // parses again from; -1 for a block whose source is not known.
    int blockStart = -1;
    MarkdownNode custom = {};
};

// Compatibility name from the earlier render-time plugin seam.
using MdPluginNode = MarkdownNode;

// MarkdownPlugin::parse. `node` is the block, `text` its flattened text —
// the raw source for an HTML block, which is what a tag plugin matches on.
// True when the plugin claims the block.
using MdPluginParseFn = bool (*)(Ctx* cx, MdNode* node, Str text, void* data,
                                 MdPluginNode* out);
// MarkdownPlugin::render, for a block its own parser claimed.
using MdPluginRenderFn = El* (*)(Ctx * cx, const MdPluginNode* node,
                                 void* data);

// One registered extension: `markdown(..).plugin(TickerPlugin::new(..))`.
struct MdPlugin {
    Str name = {};
    MdPluginParseFn parse = nullptr;
    MdPluginRenderFn render = nullptr;
    void* data = nullptr;
};

// text_view.rs CodeBlockActionsFn: what the caller hangs in the corner of a
// fenced block. Upstream's markdown example puts a Clipboard there and a Run
// button on the languages it knows. `code` is the block's text and `lang` its
// info string, both good for the length of the call.
using CodeBlockActionsFn = El* (*)(Ctx * cx, void* data, Str code, Str lang);

// text/node.rs TableData: the snapshot a table hands to `table_actions`, so a
// caller never needs the node types. The cells are the table's own text, row
// by row — `header` is the first row and `rows` is everything under it — and
// `markdown` is the table re-serialized as GFM, which is what a copy button
// puts on the clipboard.
struct TableData {
    // Row-major, `cols` per row. The header row is `header[0..cols]`.
    const Str* header = nullptr;
    const Str* rows = nullptr;
    int cols = 0;
    int rowCount = 0;
    Str markdown = {};

    Str Cell(int row, int col) const {
        if (row < 0 || col < 0 || col >= cols || row >= rowCount) {
            return {};
        }
        return rows[row * cols + col];
    }
};

// text_view.rs TableActionsFn: what the caller hangs under a Markdown table —
// a copy or a download button, say. Answers null to add nothing.
using TableActionsFn = El* (*)(Ctx * cx, void* data, const TableData* table);

// text_view.rs ImageSourceFn: the source every document image is drawn and
// measured from, given the URL the document wrote.
using ImageSourceFn = gpui::ImageSource (*)(Str uri, void* data);

// text/style.rs `with_heading`: the refinement a heading of `level` (1-6)
// takes over its built-in size, weight and spacing. Writes the style and
// answers the StyleField mask it names; naming nothing leaves the heading as
// it is.
using HeadingStyleFn = uint32_t (*)(uint8_t level, gpui::Style* style,
                                    void* data);

// text/style.rs TextViewStyle. A Style plus its named-field mask is this
// tree's StyleRefinement representation; the five refinements therefore
// preserve the same "only fields the caller named win" behavior.
//
// The six colours are what crossing the gpui-base seam added: rich text used
// to read `cx.theme()` at every paint, so a Base application without the
// themed layer drew nothing legible. Rust made the fields private behind
// `with_*` builders and accessors of the same name; the C++ struct keeps them
// public — a POD carrying its own defaults is this tree's convention — and
// the builders are spelled the way Rust spells them.
struct TextViewStyle {
    // The body text colour, and the secondary one a blockquote greys to.
    Rgba foreground = {};
    Rgba mutedForeground = {};
    // The colour of a link's words, and the wash behind selected text.
    Rgba link = {};
    Rgba selection = {};
    // Behind a fenced block, an inline code span and a table's header row.
    Rgba codeBackground = {};
    // Rules, table borders and the bar down the side of a blockquote.
    Rgba border = {};
    // rems(1.), held as DIPs at a 16 px rem: the view resolves it against
    // the window's rem size.
    float paragraphGap = 16;
    // Unset, every level's refinement is empty.
    HeadingStyleFn heading = nullptr;
    void* headingData = nullptr;
    gpui::Style codeBlock = {};
    uint32_t codeBlockFields = 0;
    gpui::Style table = {};
    uint32_t tableFields = 0;
    gpui::Style tableHead = {};
    uint32_t tableHeadFields = 0;
    gpui::Style tableCell = {};
    uint32_t tableCellFields = 0;
    gpui::Style inlineCode = {};
    uint32_t inlineCodeFields = 0;
    bool isDark = false;

    // `TextViewStyle::default()` is a complete, readable style rather than an
    // empty customization bag: the light ColorTokens palette.
    static TextViewStyle Default();
    // `from_theme`: the Base semantic tokens, with the two roles rich text
    // needs that the palette does not name mapped here once.
    static TextViewStyle FromTheme(const base_theme::Theme& theme);
    static TextViewStyle FromColors(const ColorTokens& colors, bool isDark);
    // `heading(level)`: the refinement for a heading at `level`, written to
    // `out`; answers the fields it names (0, empty, by default).
    uint32_t Heading(uint8_t level, gpui::Style* out) const;
    // The style inline code paints with, falling back to the code background
    // when the caller named none — `inline_code_highlight`.
    Rgba InlineCodeBackground() const;
    TextViewStyle& WithForeground(Rgba color);
    TextViewStyle& WithMutedForeground(Rgba color);
    TextViewStyle& WithLink(Rgba color);
    TextViewStyle& WithSelection(Rgba color);
    TextViewStyle& WithCodeBackground(Rgba color);
    TextViewStyle& WithBorder(Rgba color);
    TextViewStyle& WithParagraphGap(float gap);
    // with_heading: install the level-aware refinement. `data` must outlive
    // the style.
    TextViewStyle& WithHeading(HeadingStyleFn fn, void* data = nullptr);
    TextViewStyle& WithCodeBlock(const gpui::Style& style, uint32_t fields);
    TextViewStyle& WithTable(const gpui::Style& style, uint32_t fields);
    TextViewStyle& WithTableHead(const gpui::Style& style, uint32_t fields);
    TextViewStyle& WithTableCell(const gpui::Style& style, uint32_t fields);
    TextViewStyle& WithInlineCode(const gpui::Style& style, uint32_t fields);
    TextViewStyle& WithDark(bool value);
    bool Equals(const TextViewStyle& other) const;
};

// text/node.rs CodeBlock: one fenced block, as a highlighter sees it.
// `from_code` builds one that is not tied to a parsed document, which is what
// anyone writing a highlighter needs to exercise it against.
struct CodeBlock {
    Str code = {};
    Str lang = {};

    static CodeBlock FromCode(Str code, Str lang = {});
    Str Code() const { return code; }
    Str Lang() const { return lang; }
};

// One highlighted stretch of a code block, in bytes of CodeBlock::Code.
// Rust hands back `Vec<(Range<usize>, HighlightStyle)>`; the only field the
// renderer reads out of that HighlightStyle is the colour.
struct CodeHighlight {
    int start = 0;
    int end = 0;
    Rgba color = {};
};

// text_view.rs CodeBlockHighlighterFn. Ranges outside the code are dropped,
// as they are there. Without one, code is unhighlighted — Base keeps no
// language support of its own, which is what made the move possible.
using CodeBlockHighlighterFn = void (*)(void* data, const CodeBlock* block,
                                        Arena* a, ArenaVec<CodeHighlight>* out);

// text_view.rs TextViewDefaults: the style and the highlighter every TextView
// starts from, installed once for the application. `src/ui/theme.cpp` is what
// installs them here, the way Rust's `Theme::change` does.
struct TextViewDefaults {
    TextViewStyle style = {};
    bool hasStyle = false;
    CodeBlockHighlighterFn codeBlockHighlighter = nullptr;
    void* codeBlockHighlighterData = nullptr;

    static TextViewDefaults New() { return {}; }
    TextViewDefaults& WithStyle(const TextViewStyle& value);
    TextViewDefaults& WithCodeBlockHighlighter(CodeBlockHighlighterFn fn,
                                               void* data = nullptr);
    void Install(App* app) const;
    static TextViewDefaults Global(const App* app);
    bool HasCodeBlockHighlighter() const {
        return codeBlockHighlighter != nullptr;
    }
};

enum class TextViewFormat : uint8_t {
    Markdown,
    Html
};

// stream_fade.rs motion policy. Durations use this port's established
// millisecond convention.
struct TextViewMotion {
    float streamFadeMs = 0;
    float streamFadeStaggerMs = 0;
    Easing streamFadeEasing = Easing::EaseOut();

    TextViewMotion WithStreamFade(float ms) const {
        TextViewMotion out = *this;
        out.streamFadeMs = std::max(0.f, ms);
        return out;
    }
    TextViewMotion WithStreamFadeStagger(float ms) const {
        TextViewMotion out = *this;
        out.streamFadeStaggerMs = std::max(0.f, ms);
        return out;
    }
    TextViewMotion WithStreamFadeEasing(Easing easing) const {
        TextViewMotion out = *this;
        out.streamFadeEasing = easing;
        return out;
    }
    // stagger_step: the start offset between consecutive words of an update
    // `words` long — the stagger as asked, or nothing. Staggering only reads
    // as words arriving one after another while the whole update lights up
    // within one stream fade; once the last word would start later than
    // that, the update was not typed, so it fades as one chunk.
    float StaggerStepMs(int words) const {
        if (words < 2) {
            return 0;
        }
        if (streamFadeStaggerMs * (float)(words - 1) > streamFadeMs) {
            return 0;
        }
        return streamFadeStaggerMs;
    }
};

// stream_fade.rs TextLeafKey: one run of rendered text across re-parses — the
// source start of the block that owns it, plus the cell ordinal inside a
// table (0 for the block's own text, the cell's index + 1 for a cell). Keys
// order as their leaves appear in the document. MdNode::leafStart and
// leafOrdinal carry it on the node the leaf's text is on.
struct TextLeafKey {
    int blockStart = 0;
    int ordinal = 0;

    static TextLeafKey Block(int start) { return TextLeafKey{start, 0}; }
    static TextLeafKey TableCell(int start, int ordinal) {
        return TextLeafKey{start, ordinal + 1};
    }
    int BlockStart() const { return blockStart; }
    // The index of the table cell the leaf is, among all the cells of its
    // table, or -1 when it is not a cell (Rust's `None`).
    int CellIx() const { return ordinal - 1; }
    // The same leaf in its block moved to start at `start`.
    TextLeafKey MovedTo(int start) const { return TextLeafKey{start, ordinal}; }
};
inline bool operator==(TextLeafKey a, TextLeafKey b) {
    return a.blockStart == b.blockStart && a.ordinal == b.ordinal;
}
inline bool operator<(TextLeafKey a, TextLeafKey b) {
    return a.blockStart != b.blockStart ? a.blockStart < b.blockStart
                                        : a.ordinal < b.ordinal;
}

// stream_fade.rs FadeSegment: rendered bytes [range) of one leaf that an
// update added, fading in from `startedAt` (MotionNow seconds, which may lie
// ahead for a staggered word).
struct StreamFadeSegment {
    TextLeafKey key = {};
    Span range = {};
    double startedAt = 0;
};

// StreamFadeFrame's entries: the fade_out a leaf's rendered bytes [range)
// paint with this frame, 1 transparent and 0 opaque.
struct StreamFadeRange {
    TextLeafKey key = {};
    Span range = {};
    float fadeOut = 0;
};

// stream_fade.rs fade_units: `start..end` of a leaf's rendered `text` split
// into the units that fade one after another — a word with the whitespace
// after it, or one CJK character, since CJK text has no spaces to reveal it
// by. Appended to `out`.
void StreamFadeUnits(Str text, int start, int end, Vec<Span>* out);

// range_highlight.rs RangeHighlight: a background painted behind one range
// of a RenderedText. It is painted under the text and under the selection,
// and never changes layout. Where highlights overlap, the later one paints
// over the earlier. Colours are Rgba in this tree, where Rust takes
// `impl Into<Hsla>`.
struct RangeHighlight {
    Span range = {};
    Rgba background = {0, 0, 0, 0};

    // A highlight over `range`, in byte offsets of a RenderedText.
    static RangeHighlight New(Span range, Rgba background) {
        RangeHighlight out;
        out.range = range;
        out.background = background;
        return out;
    }
    static RangeHighlight New(Span range, Hsla background) {
        return New(range, HslaToRgba(background));
    }
    Span Range() const { return range; }
    Rgba Background() const { return background; }
};

// range_highlight.rs RangeHighlightError, with `None` standing for Rust's
// `Ok(())`: SetRangeHighlights answers one of these rather than a Result.
enum class RangeHighlightErrorKind : uint8_t {
    None,
    // The view renders HTML, which records no source positions to address
    // its text by.
    Unsupported,
    // The highlight at `index` is reversed, out of bounds, or not on a
    // character boundary.
    InvalidRange,
};

// Why setting range highlights was rejected. Existing highlights stay
// unchanged.
struct RangeHighlightError {
    RangeHighlightErrorKind kind = RangeHighlightErrorKind::None;
    int index = 0;

    static RangeHighlightError Unsupported() {
        return RangeHighlightError{RangeHighlightErrorKind::Unsupported, 0};
    }
    static RangeHighlightError InvalidRange(int index) {
        return RangeHighlightError{RangeHighlightErrorKind::InvalidRange,
                                   index};
    }
    bool IsOk() const { return kind == RangeHighlightErrorKind::None; }
    // `impl Display`, in `a`.
    Str Display(Arena* a) const;
};
inline bool operator==(RangeHighlightError a, RangeHighlightError b) {
    return a.kind == b.kind && a.index == b.index;
}

// range_highlight.rs RenderedText: a snapshot of the text a TextViewState
// renders, as of one parse of its content. Offsets into it are UTF-8 byte
// offsets. It is the string plain copy produces: `hello **world**` renders
// as `hello world`, escapes are resolved, and heading and list markers are
// left out. Blocks end with a newline and table cells are joined with a
// space; those separators belong to no block, so no highlight paints them.
//
// Two snapshots are equal when they come from the same view and the same
// parse, which tells an observer whether the content changed. Rust's
// snapshot holds the parsed document and builds its text on first read; the
// text here is the view's own copy, built when the parse lands, so `text`,
// `source` and RangeForSource are only good until the view's content is
// parsed again — compare snapshots after that, but read a fresh one.
struct RenderedIndex;
struct RenderedText {
    EntityId owner = {};
    uint64_t revision = 0;
    Str text = {};
    Str source = {};
    const RenderedIndex* index = nullptr;

    Str AsStr() const { return text; }
    int Len() const { return len(text); }
    bool IsEmpty() const { return len(text) == 0; }

    // The source this text was rendered from, whose byte ranges
    // RangeForSource takes.
    //
    // It comes from the same parse as the text, so it trails the text last
    // given to the view until that text's parse lands. Before converting a
    // range, check that it indexes this source: while text is streamed in,
    // this source is a prefix of the text the application holds, and after
    // SetText it may be different text.
    Str Source() const { return source; }

    // The range of this text rendered from `range`, a UTF-8 byte range of
    // Source().
    //
    // This converts a range of the Markdown source, such as one
    // SelectedSourceRange returned or one an application stored with its own
    // data, into the range a RangeHighlight takes. A character is rendered
    // from `range` when any of the source it was rendered from lies in it:
    // all of `&amp;` for `&`, the backslash and the `*` of an escaped `*`,
    // the whole source of an inline object for its text. Source that renders
    // nothing, such as emphasis delimiters, heading and list markers, code
    // fences, table pipes and link destinations, adds nothing, so `**bold**`
    // and `bold` give the same range.
    //
    // The result is the smallest range holding every character rendered from
    // `range`. When it spans blocks, it also holds the separators between
    // them, which a highlight leaves unpainted. Text the parser recorded no
    // source position for, such as the text of inline HTML, is only included
    // when it lies between characters that are.
    //
    // Converting the source range a selection of this text reports gives the
    // selected range back, widened only to whole characters where several
    // share their source, like the text of an inline object. The separators
    // between blocks are rendered from no source, so one at either end of
    // the selection is left out: Select All gives back everything but the
    // line break after the last block.
    //
    // False when `range` is empty, reversed, out of bounds, or not on a
    // character boundary, or when nothing is rendered from it. HTML views
    // record no source positions, so they always answer false.
    bool RangeForSource(Span range, Span* out) const;
};
inline bool operator==(const RenderedText& a, const RenderedText& b) {
    return a.owner == b.owner && a.revision == b.revision;
}
inline bool operator!=(const RenderedText& a, const RenderedText& b) {
    return !(a == b);
}

// One background of a leaf, in the leaf's rendered byte space.
struct RangeBackground {
    Span range = {};
    Rgba color = {0, 0, 0, 0};
};

// range_highlight.rs RangeHighlightFrame: the highlights each leaf paints,
// resolved once when they change so rendering only looks up its leaf.
struct RangeHighlightFrame {
    struct Leaf {
        TextLeafKey key = {};
        int first = 0;
        int count = 0;
    };
    // Sorted by key. A leaf's backgrounds keep the order the application
    // gave them in, so a later one paints over an earlier one.
    Vec<Leaf> leaves;
    Vec<RangeBackground> backgrounds;

    // The backgrounds of leaf `key`, in its rendered byte space.
    const RangeBackground* Backgrounds(TextLeafKey key, int* count) const;
};

// range_highlight.rs RenderedIndex: the rendered text of one parse and where
// each text leaf sits in it. Opaque; text.cpp builds it.

// The rendered text of `doc` and its leaves, as RenderedIndex::new builds
// them from a parsed document; `source` is what `doc` was parsed from. The
// caller frees it with RenderedIndexFree.
RenderedIndex* RenderedIndexNew(const MdNode* doc, Str source);
void RenderedIndexFree(RenderedIndex* index);
Str RenderedIndexText(const RenderedIndex* index);
Str RenderedIndexSource(const RenderedIndex* index);
bool RenderedIndexRangeForSource(const RenderedIndex* index, Span source,
                                 Span* out);
// The index's leaves and its source map, in the order of the text. Exposed
// for the tests that check the two mappings against each other.
int RenderedIndexLeafCount(const RenderedIndex* index);
Span RenderedIndexLeafRange(const RenderedIndex* index, int ix);
const SourceSegment* RenderedIndexSourceMap(const RenderedIndex* index,
                                            int* count);
// RangeHighlightFrame::new: validates `highlights` against `index` and
// resolves them to leaves. `*out` is null when they paint nothing.
RangeHighlightError RangeHighlightFrameNew(const RenderedIndex* index,
                                           const RangeHighlight* highlights,
                                           int count,
                                           RangeHighlightFrame** out);
// RangeHighlightFrame::remap: the highlights that still describe `next`, the
// parse after `prev`. Null when none do. `tailOnly` is an append that kept
// every block before `tailStart` as it was.
RangeHighlightFrame* RangeHighlightFrameRemap(const RangeHighlightFrame* frame,
                                              const RenderedIndex* prev,
                                              const RenderedIndex* next,
                                              bool tailOnly = false,
                                              int tailStart = -1);
void RangeHighlightFrameFree(RangeHighlightFrame* frame);

// range_highlight.rs PendingReveal: a range TextViewState::RevealRange is
// scrolling into view. The frame marks the text that lays out the start of
// the range (or, for text outside every leaf, its whole top-level block) to
// report where it was painted, and the next frame reads the report: done
// once the line is visible, a scroll or an on_reveal otherwise.
struct TextViewReveal {
    bool pending = false;
    // RevealTarget::Block: a whole top-level block, for text that belongs
    // to no leaf. RevealTarget::Line otherwise.
    bool block = false;
    TextLeafKey key = {};
    int offset = 0;
    int blockIx = 0;
    // When it was asked for, in TimeNow seconds, and how many frames its
    // line was painted without being visible.
    double requestedAt = 0;
    int attempts = 0;
    // Where the line (or block) was painted last frame, in window
    // coordinates; empty when it was not.
    Bounds line = {};
    // Where the view itself was painted last frame, which picks the scroll
    // boxes around it whose viewport a line has to be inside.
    Bounds view = {};
};

// TextView::on_reveal's payload: the line of a pending reveal, in window
// coordinates, after a frame in which it was painted but not visible.
struct TextViewRevealEvent {
    Bounds line = {};
};

// range_highlight.rs locate: where `range` starts, as a reveal target.
// False when it is not a range of the index's text, or there is none.
bool RenderedIndexLocate(const RenderedIndex* index, Span range,
                         TextViewReveal* out);

// state.rs TextViewState. Parsing remains synchronous behind the existing
// per-window LRU because this runtime has no cancellable Task<T>; ownership,
// mutation revisions, selection and managed-view identity are retained.
struct TextViewParse;
struct TextViewParseJob;
struct TextViewBaselineAck;

struct TextViewState {
    EntityId self = {};
    Str text = {};
    TextViewFormat format = TextViewFormat::Markdown;
    // The source a stateless TextView element handed over last frame. When
    // the same allocation and length arrive again and retained text still
    // has that length, no byte comparison is needed.
    const char* elementTextPtr = nullptr;
    int elementTextLen = 0;
    TextViewStyle textViewStyle = {};
    uint64_t revision = 0;
    uint64_t selectionRevision = 0;
    float scrollY = 0;
    bool selectable = false;
    bool scrollable = false;
    // TextView::max_lines. -1 is unset; a scrollable view ignores the cap.
    int maxLines = -1;
    // Whether the last painted frame overflowed that cap.
    bool clamped = false;
    gpui::SelectionFormat selectionFormat = gpui::SelectionFormat::Plain;
    TextViewMotion motion = {};
    // StreamFadeTracker::segments: what recent updates added, per leaf, and
    // when each piece starts fading in. An append waiting for the next parse
    // is streamFadePending; streamFadeReplace says it replaced the text.
    Vec<StreamFadeSegment> fadeSegments;
    // state.rs `select_all`: the selection SelectAll made, which
    // selected_source_range answers with the whole source for as long as the
    // window's selection is still that one. -1 when there is none.
    int selectAllAnchor = -1;
    int selectAllCursor = -1;
    bool streamFadePending = false;
    bool streamFadeReplace = false;
    // state.rs fade_tick: the pending repaint of a streamed fade, a
    // WindowSetTimeout handle, or 0.
    int fadeTick = 0;
    // state.rs rendered_index / committed_revision / range_highlights: the
    // index of the parse that landed last, which ReconcileRangeHighlights
    // builds as it lands. `renderedRevision` counts the parses that landed.
    RenderedIndex* renderedIndex = nullptr;
    uint64_t renderedRevision = 0;
    RangeHighlightFrame* rangeHighlights = nullptr;
    // state.rs pending_reveal.
    TextViewReveal reveal = {};
    // state.rs parsed_content and its background parser. What renders is
    // `parsed`, the last parse that landed, which owns the source its nodes
    // point into. A replacement of at most kMaxSyncFullReplaceBytes parses
    // at once; a larger one, and an append, parse in `parseFlight` while the
    // view keeps rendering `parsed`. An append parses the last block again
    // with the new text and keeps the blocks before it. A parser plugin
    // needs the UI thread and a Ctx, so a view that has one parses as it
    // renders. updateRevision counts text updates, fullUpdateRevision is the
    // last to replace the text, committedRevision the one `parsed` is of.
    TextViewParse* parsed = nullptr;
    TextViewParseJob* parseFlight = nullptr;
    // Parses that are no longer current, newest first, kept while the
    // runs last painted may still point into them (TextViewParseRetire).
    TextViewParse* retiredParses[4] = {};
    // A parse to start once the one in flight lands.
    bool parseQueued = false;
    // The background parser has not yet taken in a small replacement that
    // parsed at once, so an append before it does merges into one full parse
    // (UpdateOptions::merge over a BaselineAck).
    bool baselinePending = false;
    // The ack on its way back for baselinePending, which this state lets go
    // of when it is dropped first, so it lands on nothing.
    TextViewBaselineAck* baselineAck = nullptr;
    uint64_t updateRevision = 0;
    uint64_t fullUpdateRevision = 0;
    uint64_t committedRevision = 0;
    // What the view last parsed with: its parser fingerprint, the two flags
    // a parse off the UI thread needs, and whether it has a parser plugin.
    uint64_t parserFingerprint = 0;
    bool parserFrontmatter = false;
    bool parserMdx = false;
    bool parserPlugins = false;

    ~TextViewState();
    static Entity<TextViewState> Markdown(App* app, Str text);
    static Entity<TextViewState> Html(App* app, Str text);
    // state.rs source(): the text of the parse that landed last, which is
    // what renders. Text set since then is not in it until its parse lands;
    // `text` is the latest.
    Str Source() const;
    // parsed_content.document.blocks.len(): how many top-level blocks the
    // parse that landed has.
    int ParsedBlockCount() const;
    void SetText(Str value, App* app, Window* window = nullptr);
    void PushStr(Str value, App* app, Window* window = nullptr);
    void SetSelectable(bool value, App* app, Window* window = nullptr);
    void SetScrollable(bool value, App* app, Window* window = nullptr);
    TextViewState& Motion(TextViewMotion value) {
        motion = value;
        return *this;
    }
    void SetMotion(TextViewMotion value, App* app, Window* window = nullptr);
    bool IsClamped() const { return clamped; }
    void SetSelectionFormat(gpui::SelectionFormat value, App* app,
                            Window* window = nullptr);
    int SelectedText(Window* window, char* out, int cap) const;
    // state.rs selected_source_range: the byte range of the Markdown source
    // behind the rendered selection, so identical text maps to the
    // occurrence actually selected. One contiguous range, taking in any
    // delimiters between its ends. False for an HTML view and for a
    // selection with no exact mapping. A selection over the whole view (what
    // select_all makes) is the whole source.
    bool SelectedSourceRange(const Window* window, Span* out) const;
    bool HasSelection(const Window* window) const;
    void ClearSelection(Window* window, App* app);
    void SelectAll(Window* window, App* app);
    // state.rs rendered_text: the text this view renders, which RangeHighlight
    // ranges index — the string plain copy produces, as of the last parse
    // that landed. Text set since then is not in it until the view renders.
    gpui::RenderedText RenderedText() const;
    // state.rs set_range_highlights: replace the range highlights, whose
    // ranges index the current rendered text. Compute them from the current
    // RenderedText() and set them in the same update. A range crossing blocks
    // paints in both, skipping their separator; text outside every block
    // (separators, custom blocks, HTML blocks, inline objects) is left
    // unpainted. Any invalid range rejects the set. Highlights follow
    // unchanged text through updates; after a table edit, cells in and after
    // the edited row lose theirs because cells are known by position.
    RangeHighlightError SetRangeHighlights(const RangeHighlight* highlights,
                                           int count, App* app,
                                           Window* window = nullptr);
    // state.rs clear_range_highlights.
    void ClearRangeHighlights(App* app, Window* window = nullptr);
    // state.rs reveal_range: scroll the line `range` starts on into view,
    // `range` indexing the current rendered text as SetRangeHighlights'
    // ranges do. A scrollable view scrolls itself; any other container
    // follows through TextView::OnReveal. An empty range reveals the line of
    // its position; a range in text outside every leaf reveals its whole
    // block. Only the latest reveal is carried out; it follows the content as
    // highlights do, and is dropped when its text changes, when the view
    // clamps its lines, or when it cannot be shown within a second. Best
    // effort: success means the request was taken, not that the view has
    // scrolled.
    RangeHighlightError RevealRange(Span range, App* app,
                                    Window* window = nullptr);
    // state.rs reconcile_range_highlights: `doc`, parsed from `source`, has
    // landed. Rebuilds the rendered index and carries the highlights and a
    // pending reveal over to it. `tailOnly` is an append whose parse kept
    // every block that starts before `tailStart` as it was, which are then
    // kept without comparing them. `now` is when the parse landed, for the
    // stream fade it records.
    void ReconcileRangeHighlights(const MdNode* doc, Str source, double now,
                                  bool tailOnly = false, int tailStart = -1);
    // increment_update's parse: parse the text as it now is -- at once for a
    // small replacement, or on the background executor -- with the parser
    // the view last rendered with, or with `extensions` when the view is
    // rendering and passes its own. Rendering asks for it when the text has
    // no parse yet or the parser changed.
    // `now` parses on this thread whatever the size: a view rendered with no
    // window has a fresh state each time, which a later parse never reaches.
    void StartParse(App* app, Window* window,
                    const MarkdownExtensions* extensions = nullptr,
                    bool now = false);
    // The parse a TextViewParseJob made, landing on the UI thread.
    static void ParseLanded(TextViewParseJob* job);
    // StreamFadeTracker::record: what `next` renders that `prev` did not,
    // for the update noted since the last parse, as segments that start
    // fading at `now` — word by word when the motion staggers.
    void RecordStreamFade(const RenderedIndex* prev, const RenderedIndex* next,
                          double now);
    // StreamFadeTracker::frame: every unfinished segment sampled at `now`
    // into `out` (arena-owned), the finished ones dropped. Returns how many.
    int StreamFadeFrame(Arena* a, double now, StreamFadeRange** out);
    static void OnAction(TextViewState* self, Ctx* cx,
                         const ActionEvent* event);
    static void OnScroll(TextViewState* self, Ctx* cx,
                         const ScrollEvent* event);
    static void OnLineClamp(TextViewState* self, Ctx* cx,
                            const LineClampEvent* event);
    static void OnFadeTick(TextViewState* self, Ctx* cx,
                           const TickEvent* event);

  private:
    void Changed(App* app, Window* window, bool selectionCompatible);
};

// text_view.rs RequestLayoutState. Layout and prepaint are fused into El in
// this runtime, so `element` is the requested subtree rather than AnyElement
// — the complete C++ mapping of that GPUI type.
struct TextViewLayoutState {
    Entity<TextViewState> state = {};
    El* element = nullptr;
};

// `Table::to_markdown`: a table node written back out as GFM — outer pipes,
// a delimiter row carrying each column's alignment, and cells escaped so a
// pipe inside one does not end the row. What `TableData::markdown` holds.
Str MdTableToMarkdown(Arena* a, MdNode* table);

// text/utils.rs ordered_list_ordinal: the ordinal of an ordered list's item
// `ix`, counted from the list's `start` (MdNode::start, 1 when the source
// named none — Rust's `None`).
int OrderedListOrdinal(int start, int ix);

// text/utils.rs list_item_prefix: an ordered item's marker is its ordinal at
// depth 0, a letter at depth 1 (A.) and below (a.) indexed from the ordinal,
// and the ordinal again for a nested list starting at 0, which no letter
// stands for; an unordered one takes the depth's bullet. The string is in
// `a`, or static.
Str ListItemPrefix(Arena* a, int ix, int start, bool ordered, int depth);

// The payload OnLinkWithContext supplies to its listener. It is owned by the
// current frame arena and valid only for that call.
struct TextViewLinkBinding {
    int64_t context = 0;
    const char* href = nullptr;
};

struct TextView {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str source = {};
    Entity<TextViewState> state = {};
    // Body text size. In Rust this is whatever the TextView inherits, which
    // is theme.font_size — 16 — and is separate from the heading base below.
    float baseFont = 16;
    // TextViewStyle::heading_base_font_size. Heading sizes are multiples of
    // it: node.rs 2258 has h1 2.0, h2 1.5, h3 1.25, h4 1.125, h5 and h6 1.0.
    float headingFont = 14;
    // theme.mono_font_size — fenced code blocks. Inline code follows Rust's
    // relative 0.875 scale so it stays proportional inside headings too.
    float codeFont = 13;
    // TextViewStyle::paragraph_gap, rems(1.), at a 16 px rem like the
    // style's.
    float paragraphGap = 16;
    // Whether the text can be dragged over. Rust's TextView is selectable
    // through its own selection machinery; here it is El::Selectable. Every
    // constructor turns it on — `.selectable(true)` was the common case and
    // is now the default.
    bool selectable = true;
    // Whether `source` is HTML rather than markdown — TextView::html().
    bool html = false;
    // text_view.rs link_click_handler.
    Listener onLink;
    int64_t onLinkContext = 0;
    bool onLinkHasContext = false;
    CodeBlockActionsFn codeActions = nullptr;
    // text_view.rs code_block_highlighter. Unset falls back to the one
    // TextViewDefaults installed, and then to no highlighting at all.
    CodeBlockHighlighterFn codeHighlighter = nullptr;
    void* codeHighlighterData = nullptr;
    TableActionsFn tableActions = nullptr;
    void* tableActionsData = nullptr;
    void* codeActionsData = nullptr;
    // text_view.rs image_source.
    ImageSourceFn imageSource = nullptr;
    void* imageSourceData = nullptr;
    // Rust stores plugins in a Vec and offers them in registration order.
    ArenaVec<MdPlugin> plugins{};
    // node.rs min_w_16: the floor a table column shrinks to. Above the floor
    // a column's width is a fraction of the table, proportional to the length
    // of its content, the way render_wrap_table distributes the space. Zero
    // is min_w_16 itself, four of the window's rems.
    float tableColW = 0;
    // TextViewStyle::table with overflow-x: scroll. A table laid out this way
    // takes its column widths from the measured text rather than from a
    // character count, and scrolls sideways once the columns are down to
    // their floors — node.rs render_scroll_table, which is what the markdown
    // example defaults to.
    bool tableScroll = false;
    // TextView::scrollable: a vertically scrolling document viewport. The
    // current runtime lays all blocks rather than virtualizing them through
    // gpui::list, but preserves the state and interaction contract.
    bool scrollable = false;
    // TextView::max_lines. -1 is absent; zero is a valid empty cap.
    int maxLines = -1;
    // How many scrolling tables have been built this frame, which is what
    // names each one's scroll offset.
    int tableIx = 0;
    // TextView::selection_format. Rust keeps it on the view's own state and
    // the document reconstructs the source when the copy asks for it; the
    // selection here is the window's, so the view pushes the format onto it
    // and every run carries the Markdown around it (gpui::SelSource).
    gpui::SelectionFormat selFormat = gpui::SelectionFormat::Plain;
    TextViewStyle textViewStyle = {};
    // Whether `Style()` named one. Unset lets IntoEl fall back to the
    // application's TextViewDefaults and then to the Base palette, which is
    // Rust's `Option<TextViewStyle>`.
    bool textViewStyleSet = false;
    MarkdownExtensions markdownExtensions = {};
    gpui::Style outerStyle = {};
    uint32_t outerStyleFields = 0;
    TextViewMotion motion = {};
    bool motionSet = false;
    // How deep Blocks is, so it knows the top-level blocks a whole-block
    // reveal counts.
    int blockDepth = 0;
    // NodeContext::stream_fade: this frame's fading ranges, per leaf.
    const StreamFadeRange* streamFades = nullptr;
    int nStreamFades = 0;
    // NodeContext::range_highlights: the view state's resolved highlights,
    // for this frame. Null when there are none.
    const RangeHighlightFrame* rangeHighlights = nullptr;
    // NodeContext::reveal: the pending reveal this frame marks, and where the
    // marked text or block reports itself. Null when there is none.
    const TextViewReveal* revealTarget = nullptr;
    Bounds* revealOut = nullptr;
    // text_view.rs reveal_handler.
    Listener onReveal;

    // text_view.rs TextView::markdown / TextView::html.
    static TextView* New(Ctx* cx, Str source);
    static TextView* NewHtml(Ctx* cx, Str source);
    // TextView::new(&state), for caller-managed streaming/mutable content.
    static TextView* New(Ctx* cx, Entity<TextViewState> state);
    TextView* Font(float px);
    TextView* HeadingFont(float px);
    TextView* Style(const TextViewStyle& style);
    TextView* Refine(const gpui::Style& style, uint32_t fields);
    TextView* Selectable(bool on = true);
    // `.selection_format(..)`: whether a copy of the selection is the text as
    // rendered or the Markdown it was rendered from. Only meaningful on a
    // Selectable() view.
    TextView* SelFormat(gpui::SelectionFormat fmt);
    TextView* TableColumnWidth(float px);
    TextView* TableScroll(bool on = true);
    TextView* Scrollable(bool on = true);
    // Clamp fit-content rendering to this many body-text lines. The runtime
    // snaps the mask to whole descendant Inline lines; ignored by Scrollable.
    TextView* MaxLines(int count);
    TextView* ParagraphGap(float px);
    TextView* Motion(TextViewMotion value);
    // Component compat's stream_fade(true): Claude-like 280 ms ease-out with
    // a 10 ms word stagger.
    TextView* StreamFade(bool value = true);
    // text_view::LinkClickHandlerFn. The handler's int64_t is the link's
    // href as a NUL-terminated `const char*`; it points into the parse the
    // frame was built from and is good for the length of the call, which is
    // the same rule every other hit-test payload follows. Without a handler
    // a link opens in the desktop's browser, which is what Rust's
    // handle_link_click falls back to (cx.open_url).
    TextView* OnLink(Listener fn);
    // text_view.rs on_reveal: scroll a container that does not follow
    // scroll requests to the line of TextViewState::RevealRange. After a
    // frame in which the line was painted but not visible, the listener gets
    // a TextViewRevealEvent with its bounds in window coordinates.
    TextView* OnReveal(Listener fn);
    // Shell has to retain both its callback route and the href TextView
    // supplies. WithContext wraps those two values in a frame-arena pair and
    // hands its address to the listener.
    TextView* OnLinkWithContext(Listener fn, int64_t context);
    // code_block_actions(..): the row is absolutely placed at the block's
    // top right, over a muted plate, exactly where node.rs puts it.
    TextView* CodeBlockActions(CodeBlockActionsFn fn, void* data = nullptr);
    // `.code_block_highlighter(..)`: opt-in syntax highlighting. The ranges
    // it answers are bytes of the block's own code; anything outside is
    // dropped rather than clamped.
    TextView* CodeBlockHighlighter(CodeBlockHighlighterFn fn,
                                   void* data = nullptr);
    // Rendered below every Markdown table, both layouts, with a small gap so
    // the buttons' hover backgrounds stay clear of the table border.
    TextView* TableActions(TableActionsFn fn, void* data = nullptr);
    // `.image_source(..)`: overrides the source of every document image,
    // embedded data URLs included, for drawing and for intrinsic-size
    // measurement alike. The answer is authoritative: a load that is pending
    // or failed never falls back to the document's URL. Without it an image
    // takes the ordinary URI, asset and data-URL path.
    TextView* ImageSource(ImageSourceFn fn, void* data = nullptr);
    // `.plugin(..)`: a parser and a renderer for blocks this view knows how
    // to draw and markdown does not. They are offered every block in the
    // order they were added, and the first that claims one renders it.
    TextView* Plugin(Str name, MdPluginParseFn parse, MdPluginRenderFn render,
                     void* data = nullptr);
    TextView* MarkdownExtensionsSet(const MarkdownExtensions& extensions);
    TextView* MarkdownBlockParser(MarkdownBlockParserFn parser,
                                  void* data = nullptr);
    TextView* MarkdownBlockRenderer(Str name, MarkdownBlockRenderFn renderer,
                                    void* data = nullptr);
    TextView* Plugin(const MarkdownPlugin& plugin);
    TextView* Plugin(const TextViewPlugin& plugin);
    El* IntoEl();

  private:
    // node.rs names no text colour on a paragraph, a heading or a table cell:
    // each takes whatever the container above it pushed, which is how a
    // blockquote greys everything inside it in one line. This is that
    // inherited colour, unset meaning the theme's plain foreground.
    Rgba blockFg = {};
    bool blockFgSet = false;
    Rgba BlockFg() const;

    // ─── SelectionFormat::Source ──────────────────────────────────────────
    //
    // node.rs rebuilds a selection's Markdown by walking the BlockNode tree
    // (`text_by_kind`, `list_selected_source`, `table_selected_source`); the
    // window's selection here knows only the flat list of painted runs, so
    // the walk happens as the tree is built and each run is handed the piece
    // of that reconstruction it is responsible for. These four fields are the
    // walk's state.
    //
    // The line prefix the block being built sits under — one `> ` per
    // enclosing blockquote, and the indent under each enclosing list marker.
    Str srcLinePre = {};
    // What the next block's first line carries in front of that prefix: a
    // list item's `- ` or `1. `, spent by the first block of the item.
    Str srcMarker = {};
    // The Markdown marker the list being built hands its next item, which is
    // not the bullet glyph the item is drawn with.
    Str srcItemMarker = {};
    // The part of that marker the item's later lines are indented by:
    // list_selected_source indents by the marker alone, so a task item's
    // `[x] ` sits on the first line and not under the ones below it.
    Str srcItemPad = {};
    // Whether the item being built sits inside a task list item.
    // render_list_item_row draws no bullet or number for a row whose
    // enclosing item was a checkbox (`options.todo`), which is how a plain
    // list nested under a todo reads as part of it.
    bool inTodo = false;
    // The block runs are being built for, shared by every run in it, and
    // whether the next run starts a line rather than continuing one.
    const SelBlock* srcBlock = nullptr;
    bool srcLineStart = true;
    // The mark group open right now, so an adjacent run that carries the same
    // marks shares its record and the copier wraps the phrase once.
    const SelSource* srcRunLast = nullptr;
    uint8_t srcRunMarks = 0;
    Str srcRunHref = {};
    // Open a block: `marker` goes in front of its first line, `post` closes
    // it, and `join` says it continues the previous block's line (a table
    // cell). Answers null when the view is not selectable, since nothing
    // then paints a run that could be copied.
    const SelBlock* SrcOpen(Str marker, Str post, bool join = false);
    // One table cell: `| ` in front, ` |` or a separator after, and the
    // alignment row behind the last cell of the header.
    void SrcCell(MdNode* row, MdNode* c, int nCols, const uint8_t* colAlign);
    // Hand `t` the Markdown around it — wrap_with_mark's affixes — and
    // whether it continues the run before it. Answers `t`.
    El* SrcMark(El* t, uint8_t marks, Str href = {});
    // The next run starts a line of its own: a hard break, a new code line.
    void SrcBreak();
    // Hand an inline image element its `![alt](url)` — node.rs
    // image_markdown — as a run of its own with no text in it. Answers `e`.
    El* SrcImage(El* e, MdRun* r);
    // Hangs the run's source segments on `t`, whose text starts `offset`
    // bytes into the run.
    El* SrcMap(El* t, const SourceSegment* segments, int count, int offset,
               bool atomic = false);
    Listener LinkListener(Str href);
    // The text a plugin's parser sees, and the block a plugin claimed.
    Str BlockText(MdNode* n);
    El* PluginBlock(MdNode* n);
    // node.rs render_scroll_table: the same table, measured and scrolling.
    El* ScrollTable(MdNode* n);
    // node.rs render_block. `depth` is the list nesting level, `inList` and
    // `isLast` decide whether the block carries a paragraph gap below it.
    El* Block(MdNode* n, int depth, bool inList, bool isLast);
    El* Blocks(El* into, MdNode* n, int depth, bool inList);
    El* Item(MdNode* n, Str marker, int depth);
    El* Table(MdNode* n);
    // The `table_actions` row, built from the table it goes under. Null when
    // no hook is set.
    El* TableActionsRow(MdNode* n, int nCols, const uint8_t* colAlign);
    El* CodeBlock(MdNode* n);
    // The highlighted form of a code block: the installed highlighter says
    // which stretches take which colour and this paints them.
    El* CodeLines(Str code, const ArenaVec<CodeHighlight>& spans,
                  const SourceSegment* segments, int segmentCount,
                  const MdNode* leaf = nullptr);
    // An image run: node.rs putting an img() element in the middle of the
    // inline flow.
    El* ImageRun(MdRun* r, float font, Rgba color, bool inFlow);
    // One styled word of a flow, with its marks applied and — for a link —
    // the click that opens it.
    El* Word(Str w, float font, Rgba color, uint8_t marks, int weight,
             Str href);
    // The inline flow of a block, as a column of wrapping rows — a hard break
    // starts a new row. `weight` is 0 normal, 1 medium, 2 semibold, 3 bold.
    El* Inline(MdNode* n, float font, Rgba color, int weight,
               uint8_t align = MdAlignDefault);
    // Inline::range_backgrounds: the backgrounds of `leaf` — a node carrying
    // a TextLeafKey — over its bytes [lo, hi), rebased to lo, as washes on
    // `t`. `t` is left alone when none land on it.
    // With `markOver`, `t` is a `<mark>` word whose background moves into
    // the washes after the highlights, so it paints over them.
    El* RangeWashes(El* t, const MdNode* leaf, int lo, int hi,
                    bool markOver = false);
    // TextViewState::reveal_frame and the TextView prepaint that reads the
    // reveal's progress: settle last frame's report (done once visible; a
    // scroll of a scrollable view or an on_reveal otherwise), drop a reveal
    // that is clamped, expired or out of attempts, and mark this frame's.
    void RevealFrame(TextViewState* managed);
    // Inline::reveal: whether the pending reveal starts in `leaf`'s text,
    // and at which offset of it.
    bool RevealIn(const MdNode* leaf, int* offset) const;
    // Inline::request_reveal: have `t`, the text element showing `leaf`'s
    // bytes [lo, lo + len(t text)), report where the reveal's offset —
    // `offset` in leaf bytes — was painted.
    void RevealMark(El* t, int lo, int offset);
};

// Parses `source` into a block tree allocated from `a`. Exposed for tests.
MdNode* MdParse(Arena* a, Str source);

// node.rs SourceRangeSelection: what one piece of a selection maps to. An
// unmapped piece poisons the whole selection, since a guessed range would be
// worse than none.
struct SourceRangeSelection {
    enum Kind : uint8_t {
        Unselected,
        Mapped,
        Unmapped,
    };
    Kind kind = Unselected;
    Span range = {};

    static SourceRangeSelection MappedRange(int start, int end);
    void Merge(const SourceRangeSelection& other);
    bool IntoRange(Span* out) const;
};

// node.rs source_range_for_segments: the source bytes behind the rendered
// bytes [start, end), or false when the segments leave part of it uncovered.
bool SourceRangeForSegments(const SourceSegment* segments, int count, int start,
                            int end, Span* out);

// format/markdown.rs source_char_offset's index: every character of the
// source from the first miss on, sorted by (character, offset). Built on the
// first miss, so a node whose characters are all found never allocates it.
struct SourceCharPos {
    uint32_t key = 0;
    int offset = 0;
};
struct SourceCharIndex {
    Vec<SourceCharPos> pos;
    bool built = false;
};

// format/markdown.rs source_char_offset: the first source offset at or past
// `rawCursor` holding the character `ch` (`cl` bytes), or -1. Exposed so the
// tests reach it the way Rust's do.
int SourceCharOffset(Str raw, int rawCursor, const char* ch, int cl,
                     SourceCharIndex* positions);

// format/markdown.rs aligned_source_segments: map each character of
// `rendered` back into `raw`, a node's source that starts at `sourceOffset`,
// appending compacted segments to `out`.
void AlignedSourceSegments(Arena* a, Str raw, Str rendered, int sourceOffset,
                           bool decodeEntities, bool decodeEscapes,
                           Vec<SourceSegment>& out);

// Paragraph::selected_source_range / CodeBlock::selected_source_range for a
// selection [start, end) of `n`'s rendered text — its runs' text, an image
// run contributing none. An image is taken in when the selection reaches it
// from either side. Exposed for tests; the view maps the painted runs with
// TextHitsSourceRange.
SourceRangeSelection MdSelectedSourceRange(const MdNode* n, int start, int end);

// The same mapping over a frame's painted runs: every run `owner` painted
// in `scope` that the selection [selA, selB) reaches, merged.
SourceRangeSelection TextHitsSourceRange(const PaintCtx* ctx, int selA,
                                         int selB, int scope, EntityId owner);

// The window's cached parse of `source`, which is what a TextView renders
// from. Exposed so a test can ask whether two frames of a view that rebuilt
// its plugin table share one parsed document, the way Rust's
// `stateless_markdown_with_rebuilt_parser_settles` counts renders.
MdNode* MdParseCachedForTest(Ctx* cx, Arena* frame, Str source,
                             const MarkdownExtensions* extensions);

// state.rs::init: Copy and SelectAll in the TextView key context.
void TextViewInitKeys();

// text/mod.rs `enum Text`: either a plain string or a rich TextView, which is
// what a component takes when its caption may be either. Rust's payload enum
// is two nullable fields here; `view` wins when both are set, the way the
// `TextView` arm does.
struct Text {
    Str string = {};
    TextView* view = nullptr;

    static Text FromStr(Str value);
    static Text FromView(TextView* value);
    // `Text::style`: does nothing to a plain string.
    Text Style(const TextViewStyle& style) const;
    // `get_text`: the source behind it, for a caller that wants the words
    // rather than the element.
    Str GetText(const App* app) const;
    // RenderOnce: the string as a text element, or the view as its own.
    El* IntoEl(Ctx* cx) const;
};

// "&amp;" -> "&", for the entities that show up in prose. Returns the text
// unchanged when it is not an entity we know. Shared with text_format.cpp.
Str MdDecodeEntity(Arena* a, Str e);

// text/mod.rs `markdown(source)` / `html(source)`, the two free constructors
// whose element id is the call site. `Ctx` already carries that identity
// here, so they are the same two calls TextView::New spells out.
TextView* MarkdownView(Ctx* cx, Str source);
TextView* HtmlView(Ctx* cx, Str source);

} // namespace gpui
#endif // GPUI_BASE_TEXT_H_
