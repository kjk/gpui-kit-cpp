/* Ports of the parse tests in crates/ui/src/text — format/markdown.rs and
   format/html.rs both end at a BlockNode tree, and these check the MdNode
   tree ui/text.cpp and ui/html.cpp build in its place. */

#include "Test.h"

using namespace gpui::component;

// The n-th child of `n`, or null.
static MdNode* Child(MdNode* n, int ix) {
    if (!n) {
        return nullptr;
    }
    for (MdNode* c = n->first; c; c = c->next) {
        if (ix-- == 0) {
            return c;
        }
    }
    return nullptr;
}

static int Children(MdNode* n) {
    int count = 0;
    for (MdNode* c = n ? n->first : nullptr; c; c = c->next) {
        count++;
    }
    return count;
}

// Every run of a node concatenated, which is the text it shows.
static Str NodeText(Arena* a, MdNode* n) {
    int len = 0;
    for (MdRun* r = n ? n->runFirst : nullptr; r; r = r->next) {
        len += r->text.len;
    }
    char* buf = (char*)Alloc(a, len + 1);
    int at = 0;
    for (MdRun* r = n ? n->runFirst : nullptr; r; r = r->next) {
        memcpy(buf + at, r->text.s, (size_t)r->text.len);
        at += r->text.len;
    }
    buf[at] = 0;
    return Str(buf, at);
}

static bool TextIs(Arena* a, MdNode* n, const char* want) {
    Str got = NodeText(a, n);
    return base::StrEq(got, want);
}

// The marks on the run covering `needle`, or 0xff when no run holds it.
static uint8_t MarksOf(MdNode* n, const char* needle) {
    int len = (int)strlen(needle);
    for (MdRun* r = n ? n->runFirst : nullptr; r; r = r->next) {
        for (int i = 0; i + len <= r->text.len; i++) {
            if (StrEq(Str(r->text.s + i, len), Str(needle, len))) {
                return r->marks;
            }
        }
    }
    return 0xff;
}

static Str HrefOf(MdNode* n, const char* needle) {
    int len = (int)strlen(needle);
    for (MdRun* r = n ? n->runFirst : nullptr; r; r = r->next) {
        for (int i = 0; i + len <= r->text.len; i++) {
            if (StrEq(Str(r->text.s + i, len), Str(needle, len))) {
                return r->href;
            }
        }
    }
    return {};
}

// ─── markdown ─────────────────────────────────────────────────────────────

static void TestMarkdownBlocks(Arena* a) {
    MdNode* doc = MdParse(a, StrL("# Title\n\nSome *text*.\n\n- one\n- two\n"));
    utassert(Children(doc) == 3);
    MdNode* h = Child(doc, 0);
    utassert(h->kind == MdKind::Heading);
    utassert(h->level == 1);
    utassert(TextIs(a, h, "Title"));
    MdNode* p = Child(doc, 1);
    utassert(p->kind == MdKind::Paragraph);
    utassert(MarksOf(p, "text") == MdItalic);
    MdNode* list = Child(doc, 2);
    utassert(list->kind == MdKind::List);
    utassert(!list->ordered);
    utassert(Children(list) == 2);
    // An item holds blocks: mdast gives even a tight list item a paragraph
    // of its own.
    utassert(TextIs(a, Child(Child(list, 1), 0), "two"));
}

#if GPUI_MARKDOWN_FULL

// GFM task list items: mdast reports the `[x]` as the item's `checked` and
// takes the marker off the text, which is what markdown.rs carries onto the
// BlockNode.
static void TestMarkdownTaskList(Arena* a) {
    MdNode* doc = MdParse(a, StrL("- [x] done\n- [ ] todo\n- plain\n"));
    MdNode* list = Child(doc, 0);
    utassert(list->kind == MdKind::List);
    MdNode* done = Child(list, 0);
    utassert(done->hasCheck && done->checked);
    utassert(TextIs(a, Child(done, 0), "done"));
    MdNode* todo = Child(list, 1);
    utassert(todo->hasCheck && !todo->checked);
    utassert(TextIs(a, Child(todo, 0), "todo"));
    // An item with no checkbox carries neither half of the Option.
    MdNode* plain = Child(list, 2);
    utassert(!plain->hasCheck && !plain->checked);
}

// The delimiter row's colons, which node.rs render_wrap_table aligns each
// column by. mdast reports them once per column, as `Table::align`.
static void TestMarkdownTableAlign(Arena* a) {
    MdNode* doc = MdParse(a, StrL("| a | b | c |\n"
                                  "|:--|:-:|--:|\n"
                                  "| 1 | 2 | 3 |\n"));
    MdNode* table = Child(doc, 0);
    utassert(table->kind == MdKind::Table);
    MdNode* head = Child(table, 0);
    utassert(head->head);
    utassert(Child(head, 0)->align == MdAlignLeft);
    utassert(Child(head, 1)->align == MdAlignCenter);
    utassert(Child(head, 2)->align == MdAlignRight);
    MdNode* body = Child(table, 1);
    utassert(!body->head);
    utassert(Child(body, 2)->align == MdAlignRight);
}

// Inline HTML inside a paragraph: the parser hands the tags over as mdast
// Html nodes and text.cpp turns them into the marks html5ever would have
// produced.
static void TestMarkdownInlineHtml(Arena* a) {
    MdNode* doc = MdParse(
        a, StrL("Plain <b>bold</b> and <a href=\"http://x/\">link</a>.\n"));
    MdNode* p = Child(doc, 0);
    utassert(p->kind == MdKind::Paragraph);
    utassert(MarksOf(p, "Plain") == 0);
    utassert(MarksOf(p, "bold") == MdBold);
    utassert(MarksOf(p, "link") == MdLink);
    utassert(base::StrEq(HrefOf(p, "link"), StrL("http://x/")));
    // The mark ends with the tag: what follows is unmarked again.
    utassert(MarksOf(p, "and") == 0);
}

// A raw HTML block is parsed rather than dropped — Rust hands the same node
// to format::html from markdown_ext.rs.
static void TestMarkdownHtmlBlock(Arena* a) {
    MdNode* doc = MdParse(a, StrL("Before\n\n<div>\n  <p>Inside</p>\n"
                                  "</div>\n\nAfter\n"));
    utassert(Children(doc) == 3);
    utassert(TextIs(a, Child(doc, 0), "Before"));
    MdNode* html = Child(doc, 1);
    utassert(html->kind == MdKind::Html);
    utassert(html->runFirst == nullptr);
    MdNode* div = Child(html, 0);
    utassert(div->kind == MdKind::Group);
    utassert(TextIs(a, Child(div, 0), "Inside"));
    utassert(TextIs(a, Child(doc, 2), "After"));
}

#endif // GPUI_MARKDOWN_FULL

// ─── html ─────────────────────────────────────────────────────────────────

static void TestHtmlBlocks(Arena* a) {
    MdNode* doc = HtmlParse(a, StrL("<html><head><title>t</title></head>"
                                    "<body><h2>Head</h2><p>Body text</p>"
                                    "<script>if (a < b) {}</script>"
                                    "</body></html>"));
    MdNode* body = Child(Child(doc, 0), 0);
    utassert(body->kind == MdKind::Group);
    MdNode* h = Child(body, 0);
    utassert(h->kind == MdKind::Heading);
    utassert(h->level == 2);
    utassert(TextIs(a, h, "Head"));
    MdNode* p = Child(body, 1);
    utassert(TextIs(a, p, "Body text"));
    // <head>, <title>, <script> and <style> take their content with them.
    utassert(Children(body) == 2);
}

static void TestHtmlInlineMarks(Arena* a) {
    MdNode* doc =
        HtmlParse(a, StrL("<p>a <b>b</b> <i>i</i> <code>c</code> <s>s</s> "
                          "<mark>m</mark> <a href='/go'>go</a></p>"));
    MdNode* p = Child(doc, 0);
    utassert(p->kind == MdKind::Paragraph);
    utassert(MarksOf(p, "b") == MdBold);
    utassert(MarksOf(p, "i") == MdItalic);
    utassert(MarksOf(p, "c") == MdCode);
    utassert(MarksOf(p, "s") == MdDel);
    utassert(MarksOf(p, "m") == MdHighlight);
    utassert(MarksOf(p, "go") == MdLink);
    utassert(base::StrEq(HrefOf(p, "go"), StrL("/go")));
}

// Nested marks: html.rs merges the child's marks with the parent's, so the
// inner text carries both.
static void TestHtmlNestedMarks(Arena* a) {
    MdNode* doc = HtmlParse(a, StrL("<p><b>bold <i>both</i></b></p>"));
    MdNode* p = Child(doc, 0);
    utassert(MarksOf(p, "bold") == MdBold);
    utassert(MarksOf(p, "both") == (MdBold | MdItalic));
}

// Whitespace between block elements is layout, not content; inside a
// paragraph a run of it collapses to one space.
static void TestHtmlWhitespace(Arena* a) {
    MdNode* doc = HtmlParse(a, StrL("<div>\n   <p>one\n   two</p>\n</div>"));
    MdNode* div = Child(doc, 0);
    utassert(Children(div) == 1);
    utassert(TextIs(a, Child(div, 0), "one two"));
}

static void TestHtmlEntities(Arena* a) {
    MdNode* doc = HtmlParse(a, StrL("<p>a &amp; b &lt;c&gt; &#65; &nope;</p>"));
    utassert(TextIs(a, Child(doc, 0), "a & b <c> A &nope;"));

    // The longest HTML5 entity name is well beyond the former twelve-byte
    // lexer window.
    MdNode* longName =
        HtmlParse(a, StrL("<p>&CounterClockwiseContourIntegral;</p>"));
    utassert(TextIs(a, Child(longName, 0), "\xE2\x88\xB3"));
}

static void TestHtmlNestingHasNoPortLimit(Arena* a) {
    StrBuilder source;
    for (int i = 0; i < 100; i++) {
        source.Append(StrL("<div>"));
    }
    source.Append(StrL("<p>deep</p>"));
    for (int i = 0; i < 100; i++) {
        source.Append(StrL("</div>"));
    }
    Str html = source.TakeStr();
    MdNode* node = HtmlParse(a, html);
    for (int i = 0; i < 100; i++) {
        node = Child(node, 0);
        utassert(node && node->kind == MdKind::Group);
    }
    utassert(TextIs(a, Child(node, 0), "deep"));
    StrFree(html);
}

static void TestHtmlList(Arena* a) {
    MdNode* doc =
        HtmlParse(a, StrL("<ol start=\"3\"><li>one</li><li>two</li></ol>"));
    MdNode* list = Child(doc, 0);
    utassert(list->kind == MdKind::List);
    utassert(list->ordered);
    utassert(list->start == 3);
    utassert(Children(list) == 2);
    utassert(TextIs(a, Child(list, 0), "one"));
}

// <pre> keeps its text as typed, and the <code class> inside it names the
// language the way a fenced block's info string does.
static void TestHtmlPre(Arena* a) {
    MdNode* doc =
        HtmlParse(a, StrL("<pre><code class=\"language-cpp\">int a;\n  int b;\n"
                          "</code></pre>"));
    MdNode* code = Child(doc, 0);
    utassert(code->kind == MdKind::Code);
    utassert(base::StrEq(code->lang, StrL("cpp")));
    utassert(TextIs(a, code, "int a;\n  int b;\n"));
}

static void TestHtmlTable(Arena* a) {
    MdNode* doc = HtmlParse(
        a, StrL("<table><thead><tr><th>h</th></tr></thead>"
                "<tbody><tr><td align=\"center\">c</td>"
                "<td style=\"text-align: right\">r</td></tr></tbody></table>"));
    MdNode* table = Child(doc, 0);
    utassert(table->kind == MdKind::Table);
    utassert(Children(table) == 2);
    MdNode* head = Child(table, 0);
    utassert(head->head);
    MdNode* row = Child(table, 1);
    utassert(!row->head);
    utassert(Child(row, 0)->align == MdAlignCenter);
    utassert(Child(row, 1)->align == MdAlignRight);
}

// An <img> has no loader behind it, so it contributes its alt text — the same
// place a markdown ![alt](url) lands.
static void TestHtmlImageAlt(Arena* a) {
    MdNode* doc = HtmlParse(a, StrL("<p>see <img src=\"a.png\" alt=\"a cat\">"
                                    " here</p>"));
    utassert(TextIs(a, Child(doc, 0), "see a cat here"));
}

// A tag left open is closed by its ancestor, and a stray close tag that
// matches nothing is ignored.
static void TestHtmlUnbalanced(Arena* a) {
    MdNode* doc = HtmlParse(a, StrL("<div><p>one<b>two</div></i><p>three</p>"));
    MdNode* div = Child(doc, 0);
    utassert(div->kind == MdKind::Group);
    utassert(TextIs(a, Child(div, 0), "onetwo"));
    utassert(TextIs(a, Child(doc, 1), "three"));
}

static void TestHtmlComments(Arena* a) {
    MdNode* doc =
        HtmlParse(a, StrL("<!doctype html><!-- <p>hidden</p> --><p>shown</p>"));
    utassert(Children(doc) == 1);
    utassert(TextIs(a, Child(doc, 0), "shown"));
}

// <br> is a hard break inside the flow, which the renderer starts a new row
// on; the tree carries it as a newline in the run.
static void TestHtmlBreak(Arena* a) {
    MdNode* doc = HtmlParse(a, StrL("<p>one<br>two</p>"));
    utassert(TextIs(a, Child(doc, 0), "one\ntwo"));
}

static void TestMarkdownSoftBreak(Arena* a) {
    const char* sources[] = {"this sentence\ncontinues as a soft wrap",
                             "this sentence\r\ncontinues as a soft wrap",
                             "this sentence\rcontinues as a soft wrap"};
    for (const char* source : sources) {
        MdNode* doc = MdParse(a, Str(source));
        utassert(
            TextIs(a, Child(doc, 0), "this sentence continues as a soft wrap"));
    }
    MdNode* doc = MdParse(a, StrL("a\nb  \nc"));
    utassert(TextIs(a, Child(doc, 0), "a b\nc"));
}

static void TestMarkdownHardBreak(Arena* a) {
    const char* sources[] = {"Owner: Jane  \nPersona: assistant",
                             "Owner: Jane\\\nPersona: assistant"};
    for (const char* source : sources) {
        MdNode* doc = MdParse(a, Str(source));
        utassert(TextIs(a, Child(doc, 0), "Owner: Jane\nPersona: assistant"));
    }
}

// The run covering `needle`, or the first image run when `needle` is null.
static MdRun* ImageRunOf(MdNode* n) {
    for (MdRun* r = n ? n->runFirst : nullptr; r; r = r->next) {
        if (r->imgSrc.len > 0) {
            return r;
        }
    }
    return nullptr;
}

// node.rs InlineNode::image: a markdown image is a run of its own, carrying
// the source and the alt text beside the words.
static void TestMarkdownImage(Arena* a) {
    MdNode* doc = MdParse(a, StrL("see ![a cat](cat.png) here\n"));
    MdNode* p = Child(doc, 0);
    MdRun* img = ImageRunOf(p);
    utassert(img != nullptr);
    utassert(base::StrEq(img->imgSrc, StrL("cat.png")));
    utassert(base::StrEq(img->text, StrL("a cat")));
    // The words around it are still their own runs, in order.
    utassert(TextIs(a, p, "see a cat here"));
    // An image inside a link is a link, the way ImageNode::link is.
    MdNode* linked = MdParse(a, StrL("[![alt](c.png)](https://x/)\n"));
    MdRun* r = ImageRunOf(Child(linked, 0));
    utassert(r != nullptr);
    utassert((r->marks & MdLink) != 0);
    utassert(base::StrEq(r->href, StrL("https://x/")));
}

// html.rs attr_width_height: the size the tag gives, in pixels. A percentage
// is not a size this layout can use, so it reads as none.
static void TestHtmlImage(Arena* a) {
    MdNode* doc =
        HtmlParse(a, StrL("<p>a <img src=\"x.png\" alt=\"alt\" width=\"60\" "
                          "height=\"40\"> b</p>"));
    MdRun* img = ImageRunOf(Child(doc, 0));
    utassert(img != nullptr);
    utassert(base::StrEq(img->imgSrc, StrL("x.png")));
    utassert(base::StrEq(img->text, StrL("alt")));
    utassert(img->imgW == 60 && img->imgH == 40);

    // The README showcase uses this shape: a remote bitmap with a width and
    // no height. It remains an image run and lets the decoded aspect supply
    // the missing dimension.
    MdNode* widthOnly =
        HtmlParse(a, StrL("<img width=\"1763\" alt=\"Image\" "
                          "src=\"https://example.com/showcase.png\">"));
    MdRun* widthRun = ImageRunOf(Child(widthOnly, 0));
    utassert(widthRun != nullptr);
    utassert(widthRun->imgW == 1763 && widthRun->imgH == 0);
    utassert(base::StrEq(widthRun->imgSrc,
                         StrL("https://example.com/showcase.png")));

    MdNode* pct =
        HtmlParse(a, StrL("<img src=\"y.png\" style=\"width: 50%\">"));
    MdRun* r = ImageRunOf(Child(pct, 0));
    utassert(r != nullptr);
    utassert(r->imgW == 0);

    // html.rs drops an image with no src; so does this.
    MdNode* nosrc = HtmlParse(a, StrL("<p><img alt=\"x\"></p>"));
    utassert(ImageRunOf(Child(nosrc, 0)) == nullptr);
}

// gpui/image.h: local paths use assets and URLs remain network resources.
static bool TestImageAssetLoad(void*, Str, Vec<uint8_t>*) {
    return false;
}

static bool TestImageAssetExists(void*, Str path) {
    return base::StrEq(path, StrL("story/logo.svg"));
}

static void TestImageSrc() {
    utassert(ImageSrcIsLocal(StrL("logo.png")));
    utassert(ImageSrcIsLocal(StrL("icons/logo.png")));
    utassert(ImageSrcIsLocal(StrL("data:image/png;base64,iVBORw0KGgo=")));
    utassert(!ImageSrcIsLocal(StrL("https://example.com/a.png")));
    utassert(!ImageSrcIsLocal(StrL("http://example.com/a.png")));
    utassert(!ImageSrcIsLocal(StrL("")));

    AssetsClear();
    int sourceMarker = 1;
    int source = AssetsAddSource(&sourceMarker, TestImageAssetLoad,
                                 TestImageAssetExists);
    utassert(source != 0);
    Arena* a = ArenaNew();
    utassert(base::StrEq(ImageAssetFor(a, StrL("story/logo.svg")),
                         StrL("story/logo.svg")));
    // Rust sends a URI to its HTTP client. A coincidentally matching asset
    // basename must not replace it.
    utassert(!ImageAssetFor(a, StrL("https://example.com/logo.svg")));
    ArenaDelete(a);
    ImageCacheClear();
    AssetsClear();
    AssetsAddDefaultRoots({});
}

// ─── SelectionFormat::Source ──────────────────────────────────────────────
//
// Ports of node.rs's own reconstruct tests — reconstruct_markdown_wraps_
// marked_runs, reconstruct_markdown_emits_unmarked_text_verbatim, and the
// selected_source cases for headings, blockquotes, code blocks and tables.
// Rust rebuilds the markdown by walking the BlockNode tree; here the walk
// happens as the tree is built and each painted run carries its piece of it,
// so what these drive is the other end: CopyTextHitsIn putting the pieces
// back together over a frame's registered runs.

// A frame's text registrations, built by hand the way a paint pass builds
// them.
struct SrcDoc {
    PaintCtx ctx;

    void Run(const char* text, const SelSource* src, bool join) {
        TextHit h;
        h.bounds = {0, 0, 100, 20};
        h.text = Str((char*)text);
        h.font = 14;
        h.maxW = 100;
        h.docOff = ctx.textDocLen;
        h.src = src;
        h.join = join;
        VecAppend(ctx.texts, h);
        // The gap of one between runs, which is what the copier's document
        // order leaves room for.
        ctx.textDocLen += h.text.len + 1;
    }

    // An inline image: a run with no text of its own, holding one place in
    // the document order.
    void Image(const SelSource* src, bool join) {
        TextHit h;
        h.bounds = {0, 0, 20, 20};
        h.font = 14;
        h.docOff = ctx.textDocLen;
        h.src = src;
        h.join = join;
        h.atom = true;
        VecAppend(ctx.texts, h);
        ctx.textDocLen += 1;
    }
};

// The whole document, copied in `fmt`.
static TempStr SrcCopyTemp(SrcDoc* d, SelectionFormat fmt) {
    TempStr buf = AllocStrTemp(511);
    // One short of the gap after the last run, so nothing reaches past it.
    int n = CopyTextHitsIn(&d->ctx, 0, d->ctx.textDocLen - 1, -1, buf.s,
                           buf.len + 1, fmt);
    buf.len = n;
    return buf;
}

static TempStr SrcRangeCopyTemp(SrcDoc* d, int start, int end) {
    TempStr buf = AllocStrTemp(511);
    buf.len = CopyTextHitsIn(&d->ctx, start, end, -1, buf.s, buf.len + 1,
                             SelectionFormat::Source);
    return buf;
}

// A mark group split over several word elements wraps once, not per word —
// which is what reconstruct_markdown gets from walking mark ranges rather
// than words.
static void TestSourceMarks() {
    SelBlock para = {};
    SelSource bold = {StrL("**"), StrL("**"), &para};
    SelSource plain = {{}, {}, &para};
    SrcDoc d;
    d.Run("one ", &bold, false);
    d.Run("two ", &bold, true);
    d.Run("three", &plain, true);
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Source),
                         StrL("**one two **three")));
    // The same runs in Plain are the text as rendered, on one line: a
    // paragraph is one InlineState.text in Rust however it is copied.
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Plain),
                         StrL("one two three")));
}

// reconstruct_markdown: a partial selection inside a marked run still wraps
// the slice.
static void TestSourcePartialMark() {
    SelBlock para = {};
    SelSource bold = {StrL("**"), StrL("**"), &para};
    SrcDoc d;
    d.Run("bold", &bold, false);
    utassert(base::StrEq(SrcRangeCopyTemp(&d, 1, 3), StrL("**ol**")));
}

// reconstruct_markdown_emits_unmarked_text_verbatim, and a link's tail.
static void TestSourceCodeAndLink() {
    SelBlock para = {};
    SelSource plain = {{}, {}, &para};
    SelSource code = {StrL("`"), StrL("`"), &para};
    SelSource link = {StrL("["), StrL("](https://x.dev)"), &para};
    SrcDoc d;
    d.Run("a ", &plain, false);
    d.Run("b", &code, true);
    d.Run(" c ", &plain, true);
    d.Run("home", &link, true);
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Source),
                         StrL("a `b` c [home](https://x.dev)")));
}

// A selected heading round-trips with its marker, and the paragraph under it
// starts a line of its own.
static void TestSourceHeading() {
    SelBlock head = {StrL("## "), {}, {}, false};
    SelBlock para = {};
    SelSource h = {{}, {}, &head};
    SelSource p = {{}, {}, &para};
    SrcDoc d;
    d.Run("Title", &h, false);
    d.Run("body", &p, false);
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Source),
                         StrL("## Title\nbody")));
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Plain),
                         StrL("Title\nbody")));
}

// Every line of a blockquote carries its prefix, including the ones inside a
// run that holds its own line breaks.
static void TestSourceBlockquote() {
    SelBlock q1 = {StrL("> "), {}, StrL("> "), false};
    SelBlock q2 = {StrL("> "), {}, StrL("> "), false};
    SelSource a = {{}, {}, &q1};
    SelSource b = {{}, {}, &q2};
    SrcDoc d;
    d.Run("first", &a, false);
    d.Run("second\nthird", &b, false);
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Source),
                         StrL("> first\n> second\n> third")));
}

// code_block.selected_source: the code comes back fenced, with the block's
// language on the opening fence.
static void TestSourceCodeBlock() {
    SelBlock fence = {StrL("```rust\n"), StrL("\n```"), {}, false};
    SelSource tok = {{}, {}, &fence};
    SrcDoc d;
    d.Run("let x", &tok, false);
    d.Run(" = 1;", &tok, true);
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Source),
                         StrL("```rust\nlet x = 1;\n```")));
}

// table_selected_source: the row is piped and the alignment row follows the
// header. In Plain the cells of a row are joined with a space.
static void TestSourceTable() {
    SelBlock h0 = {StrL("| "), StrL(" "), {}, false};
    SelBlock h1 = {StrL("| "), StrL(" |\n| :-- | :-: |"), {}, true};
    SelBlock b0 = {StrL("| "), StrL(" "), {}, false};
    SelBlock b1 = {StrL("| "), StrL(" |"), {}, true};
    SelSource s0 = {{}, {}, &h0};
    SelSource s1 = {{}, {}, &h1};
    SelSource s2 = {{}, {}, &b0};
    SelSource s3 = {{}, {}, &b1};
    SrcDoc d;
    d.Run("Name", &s0, false);
    d.Run("Qty", &s1, false);
    d.Run("Nut", &s2, false);
    d.Run("3", &s3, false);
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Source),
                         StrL("| Name | Qty |\n| :-- | :-: |\n| Nut | 3 |")));
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Plain),
                         StrL("Name Qty\nNut 3")));
}

// A list item's marker is the markdown one, not the bullet glyph it draws
// with, and the lines under it are indented by the marker's width.
static void TestSourceList() {
    SelBlock item = {StrL("- "), {}, StrL("  "), false};
    SelBlock nested = {StrL("  - "), {}, StrL("    "), false};
    SelSource a = {{}, {}, &item};
    SelSource b = {{}, {}, &nested};
    SrcDoc d;
    d.Run("first", &a, false);
    d.Run("under", &b, false);
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Source),
                         StrL("- first\n  - under")));
}

// A task list item: list_selected_source puts the checkbox after the marker
// and indents the lines under it by the marker alone, so the `[x] ` stays on
// the first line.
static void TestSourceTaskList() {
    SelBlock done = {StrL("- [x] "), {}, StrL("  "), false};
    SelBlock todo = {StrL("- [ ] "), {}, StrL("  "), false};
    SelSource a = {{}, {}, &done};
    SelSource b = {{}, {}, &todo};
    SrcDoc d;
    d.Run("shipped", &a, false);
    d.Run("pending", &b, false);
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Source),
                         StrL("- [x] shipped\n- [ ] pending")));
    // The rendered text is the item's words: the checkbox is drawn, not
    // written.
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Plain),
                         StrL("shipped\npending")));
}

// node.rs selected_source: an inline image is emitted when the selection runs
// into it — the run before it selected to its end, the run after it from its
// beginning — and copies as nothing in Plain, since Paragraph::text lays the
// children's text end to end and an image child has none.
static void TestSourceImage() {
    SelBlock para = {};
    SelSource plain = {{}, {}, &para};
    SelSource img = {StrL("![alt](a.png)"), {}, &para};
    SrcDoc d;
    d.Run("see ", &plain, false);
    d.Image(&img, true);
    d.Run(" now", &plain, true);
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Source),
                         StrL("see ![alt](a.png) now")));
    utassert(
        base::StrEq(SrcCopyTemp(&d, SelectionFormat::Plain), StrL("see  now")));
    // Stopping at the end of the run before it still reaches it: the run
    // after has nothing selected in it, which is the trailing case.
    utassert(
        base::StrEq(SrcRangeCopyTemp(&d, 0, 4), StrL("see ![alt](a.png)")));
    // Stopping short of that end does not.
    utassert(base::StrEq(SrcRangeCopyTemp(&d, 0, 2), StrL("se")));
    // Nor does a selection that starts after the picture.
    utassert(base::StrEq(SrcRangeCopyTemp(&d, 6, 10), StrL(" now")));
}

// The two ends of the same rule: a paragraph that begins or ends with an
// image has no run on that side, and that counts as reaching it.
static void TestSourceImageAtTheEnds() {
    SelBlock para = {};
    SelSource plain = {{}, {}, &para};
    SelSource img = {StrL("![alt](a.png)"), {}, &para};
    // Leading: selecting the words after the picture takes the picture.
    SrcDoc lead;
    lead.Image(&img, false);
    lead.Run(" now", &plain, true);
    utassert(
        base::StrEq(SrcRangeCopyTemp(&lead, 1, 5), StrL("![alt](a.png) now")));
    // Trailing: selecting the words before it does too.
    SrcDoc tail;
    tail.Run("see ", &plain, false);
    tail.Image(&img, true);
    utassert(
        base::StrEq(SrcRangeCopyTemp(&tail, 0, 4), StrL("see ![alt](a.png)")));
    // A picture with no words either side is the whole paragraph, and Rust
    // emits nothing for such a paragraph: it is the document walk that takes
    // it when what encloses it is selected. Here that is the selection having
    // run past the place it sits in.
    SelBlock next = {};
    SelSource below = {{}, {}, &next};
    SrcDoc lone;
    lone.Image(&img, false);
    lone.Run("after", &below, false);
    utassert(base::StrEq(SrcCopyTemp(&lone, SelectionFormat::Source),
                         StrL("![alt](a.png)\nafter")));
    // The paragraph below it on its own leaves it behind.
    utassert(base::StrEq(SrcRangeCopyTemp(&lone, 1, 6), StrL("after")));
}

// A run that names no source — everything outside a TextView — copies as its
// own text in both formats, one run per line, which is what the copier did
// before there was a second format at all.
static void TestSourceIgnoresPlainRuns() {
    SrcDoc d;
    d.Run("hello", nullptr, false);
    d.Run("world", nullptr, false);
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Source),
                         StrL("hello\nworld")));
    utassert(base::StrEq(SrcCopyTemp(&d, SelectionFormat::Plain),
                         StrL("hello\nworld")));
}

#if GPUI_MARKDOWN_FULL

// `Table::to_markdown`, which gpui-kit b1e78a51 fixed on its way to the
// table_actions hook: it used to join cells straight out of the paragraph
// writer -- which trails a blank line -- and emit no outer pipes, so a
// single-column table did not round-trip as GFM.
static void TestTableToMarkdown(Arena* a) {
    MdNode* doc = MdParse(a, StrL("| a | b |\n| --- | ---: |\n| 1 | 2 |\n"));
    MdNode* table = Child(doc, 0);
    utassert(table && table->kind == MdKind::Table);
    utassert(base::StrEq(MdTableToMarkdown(a, table),
                         StrL("| a | b |\n| --- | ---: |\n| 1 | 2 |\n")));

    // One column, which is the shape that was not valid GFM before.
    MdNode* one = Child(MdParse(a, StrL("| only |\n| --- |\n| x |\n")), 0);
    utassert(one && one->kind == MdKind::Table);
    Str md1 = MdTableToMarkdown(a, one);
    utassert(base::StrEq(md1, StrL("| only |\n| --- |\n| x |\n")));
    // And it parses back to the table it came from.
    MdNode* again = Child(MdParse(a, md1), 0);
    utassert(again && again->kind == MdKind::Table);
    utassert(Children(again) == Children(one));

    // Alignment rides in the delimiter row, column by column.
    MdNode* aligned = Child(
        MdParse(a, StrL("| l | c | r |\n| :-- | :-: | --: |\n| 1 | 2 | 3 |\n")),
        0);
    utassert(base::StrEq(
        MdTableToMarkdown(a, aligned),
        StrL("| l | c | r |\n| :--- | :---: | ---: |\n| 1 | 2 | 3 |\n")));
}

#endif // GPUI_MARKDOWN_FULL

static int CountByte(Str s, char needle) {
    int n = 0;
    for (int i = 0; i < s.len; i++) {
        n += s.s[i] == needle ? 1 : 0;
    }
    return n;
}

static int gTableActionCols = 0;
static int gTableActionRows = 0;

static El* CaptureLargeTable(Ctx* cx, void* data, const TableData* table) {
    (void)cx;
    (void)data;
    gTableActionCols = table->cols;
    gTableActionRows = table->rowCount;
    return nullptr;
}

static bool NeverClaimPlugin(Ctx* cx, MdNode* node, Str text, void* data,
                             MdPluginNode* out) {
    (void)cx;
    (void)node;
    (void)text;
    (void)data;
    (void)out;
    return false;
}

static El* NeverRenderPlugin(Ctx* cx, const MdPluginNode* node, void* data) {
    (void)cx;
    (void)node;
    (void)data;
    return nullptr;
}

static int ElementTextBytes(El* e) {
    int n = e ? e->text.len : 0;
    for (El* child = e ? e->first : nullptr; child; child = child->next) {
        n += ElementTextBytes(child);
    }
    return n;
}

static Str HtmlTableSource(int rows, int cols) {
    StrBuilder source;
    source.Append(StrL("<table>"));
    for (int r = 0; r < rows; r++) {
        source.Append(StrL("<tr>"));
        for (int c = 0; c < cols; c++) {
            source.Append(r == 0 ? StrL("<th>x</th>") : StrL("<td>x</td>"));
        }
        source.Append(StrL("</tr>"));
    }
    source.Append(StrL("</table>"));
    return source.TakeStr();
}

static void TestTextCollectionsGrowWithTheDocument(Arena* a) {
    Ctx cx = {};
    App app;
    cx.app = &app;
    cx.a = a;

    TextView* plugins = TextView::New(&cx, StrL("plain"));
    for (int i = 0; i < 20; i++) {
        plugins->Plugin(StrL("test"), &NeverClaimPlugin, &NeverRenderPlugin,
                        (void*)(intptr_t)(i + 1));
    }
    utassert(plugins->plugins.len == 20);
    utassert(plugins->plugins[19].data == (void*)(intptr_t)20);

    // A marked word and a highlighted code token both used 512-byte scratch
    // arrays. Render enough bytes to cross those arrays and count the text in
    // the resulting element tree.
    StrBuilder markedSource;
    markedSource.Append(StrL("<p><b>"));
    for (int i = 0; i < 700; i++) {
        markedSource.Append(StrL("x"));
    }
    markedSource.Append(StrL("</b></p>"));
    Str marked = markedSource.TakeStr();
    El* markedEl = TextView::NewHtml(&cx, marked)->IntoEl();
    utassert(ElementTextBytes(markedEl) == 700);
    StrFree(marked);

    StrBuilder codeSource;
    codeSource.Append(StrL("<pre><code class='language-cpp'>"));
    for (int i = 0; i < 700; i++) {
        codeSource.Append(StrL("x"));
    }
    codeSource.Append(StrL("</code></pre>"));
    Str code = codeSource.TakeStr();
    El* codeEl = TextView::NewHtml(&cx, code)->IntoEl();
    utassert(ElementTextBytes(codeEl) == 700);
    StrFree(code);

    // Forty columns cross the old column cap; seventy rows also cross the
    // old table-actions cell cap. Serialization and rendering see all of it.
    Str tableSource = HtmlTableSource(70, 40);
    MdNode* table = Child(HtmlParse(a, tableSource), 0);
    utassert(table && table->kind == MdKind::Table);
    utassert(Children(Child(table, 0)) == 40);
    Str markdown = MdTableToMarkdown(a, table);
    utassert(CountByte(markdown, '|') == 41 * 71);

    gTableActionCols = 0;
    gTableActionRows = 0;
    TextView::NewHtml(&cx, tableSource)
        ->TableActions(&CaptureLargeTable)
        ->IntoEl();
    utassert(gTableActionCols == 40);
    utassert(gTableActionRows == 69);
    StrFree(tableSource);
}

static float HeadingIdentity(uint8_t, float base, void*) {
    return base;
}

static float HeadingDouble(uint8_t, float base, void*) {
    return base * 2;
}

static void TestSourceShapedTextValues(Arena* a) {
    TextMark mark;
    mark.Bold().Italic().Code().Underline();
    TextMark more;
    LinkMark link;
    link.url = StrL("https://example.com");
    more.Strikethrough().Highlight(Rgba8(1, 2, 3, 255)).Link(link);
    mark.Merge(more);
    utassert(mark.bold && mark.italic && mark.code && mark.underline);
    utassert(mark.strikethrough && mark.hasHighlight && mark.hasLink);
    utassert(base::StrEq(mark.link.url, StrL("https://example.com")));

    ImageNode image;
    image.alt = StrL("fallback");
    utassert(base::StrEq(image.Title(a), StrL("fallback")));
    image.title = StrL("title");
    utassert(base::StrEq(image.Title(a), StrL("title")));

    MarkdownNode node = MarkdownNode::New(StrL("demo"), &image);
    node.Text(StrL("plain")).Markdown(StrL("**plain**"));
    utassert(base::StrEq(node.ToMarkdown(), StrL("**plain**")));
    node.markdown = {};
    utassert(base::StrEq(node.ToMarkdown(), StrL("plain")));

    TextViewStyle base = TextViewStyle::Default();
    TextViewStyle same = TextViewStyle::Default();
    utassert(base.Equals(same));
    same.WithHeadingFontSize(&HeadingIdentity);
    utassert(!base.Equals(same));
    base.WithHeadingFontSize(&HeadingIdentity);
    utassert(base.Equals(same));
    same.WithHeadingFontSize(&HeadingDouble);
    utassert(!base.Equals(same));
    utassert(same.HeadingSize(2) == 28);

    TextViewStyle styledA = TextViewStyle::Default();
    TextViewStyle styledB = TextViewStyle::Default();
    gpui::Style codeA;
    gpui::Style codeB;
    codeA.bg = Rgb(10, 20, 30);
    codeB.bg = codeA.bg;
    codeB.fontSize = 99;
    styledA.WithCodeBlock(codeA, StyleFieldBg);
    styledB.WithCodeBlock(codeB, StyleFieldBg);
    utassert(styledA.Equals(styledB));
    codeB.bg = Rgb(30, 20, 10);
    styledB.WithCodeBlock(codeB, StyleFieldBg);
    utassert(!styledA.Equals(styledB));

    gpui::Style head;
    head.color = Rgb(1, 2, 3);
    styledA = TextViewStyle::Default();
    styledB = TextViewStyle::Default();
    styledA.WithTableHead(head, StyleFieldColor);
    utassert(!styledA.Equals(styledB));
    styledB.WithTableHead(head, StyleFieldColor);
    utassert(styledA.Equals(styledB));
}

static void TestHtmlMinifier(Arena* a) {
    Minifier minifier;
    utassert(base::StrEq(
        minifier.WriteCollapseWhitespace(a, StrL(" x   \n  \t y  ")),
        StrL(" x y ")));
    minifier.precedingWhitespace = true;
    utassert(base::StrEq(minifier.WriteCollapseWhitespace(a, StrL("   x")),
                         StrL("x")));

    minifier = {};
    minifier.OmitDoctype();
    Str minified =
        minifier.Minify(a, StrL("<!doctype html><p> a   b </p><!-- gone -->"
                                "<pre> x   y </pre>"));
    utassert(base::StrEq(minified, StrL("<p> a b </p><pre> x   y </pre>")));
    minifier = {};
    minifier.PreserveComments();
    utassert(base::StrEq(minifier.Minify(a, StrL("<p>x</p><!-- keep -->")),
                         StrL("<p>x</p><!-- keep -->")));
}

static int gParseTimePluginCalls = 0;
static int gParseTimeRenderCalls = 0;

static bool ParseCodeNode(const markdown::Node* source,
                          const MarkdownParseContext* context, void*,
                          MarkdownNode* out) {
    gParseTimePluginCalls++;
    if (source->kind != markdown::NodeKind::Code) return false;
    *out = MarkdownNode::New(context->Copy(StrL("code-card")));
    out->Text(context->Value(source, markdown::NodeStrKind::Value));
    out->Markdown(context->Copy(StrL("```demo\nclaimed\n```")));
    return true;
}

static El* RenderCodeNode(Ctx* cx, const MarkdownNode* node, void*) {
    gParseTimeRenderCalls++;
    return TextEl(cx->a, node->text);
}

static bool FindSelectionOwner(El* element, EntityId owner) {
    if (!element) return false;
    if (element->kind == ElKind::Text && element->selectionOwner == owner) {
        return true;
    }
    for (El* child = element->first; child; child = child->next) {
        if (FindSelectionOwner(child, owner)) return true;
    }
    return false;
}

static bool SameTextViewColor(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static El* FindMarkdownTableFrame(El* element, Rgba border) {
    if (!element) return nullptr;
    if (element->style.border == 1 &&
        SameTextViewColor(element->style.borderColor, border)) {
        return element;
    }
    for (El* child = element->first; child; child = child->next) {
        if (El* found = FindMarkdownTableFrame(child, border)) return found;
    }
    return nullptr;
}

static void TestMarkdownTableThemeTokens() {
    App app;
    ThemeSet(&app, ThemeMode::Dark);
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    const Theme& th = ThemeNow(&app);
    // Rich text reads no theme any more: the themed palette reaches it as the
    // TextViewDefaults ThemeSet installs, and the table's header pair and
    // row rules arrive as style refinements on top of the Base defaults.
    TextViewDefaults installed = TextViewDefaults::Global(&app);
    utassert(installed.hasStyle);
    utassert(installed.HasCodeBlockHighlighter());
    utassert(installed.style.tableHeadFields != 0);
    Str source = StrL("| head | other |\n|---|---|\n| body | value |\n");

    for (int scroll = 0; scroll < 2; scroll++) {
        El* rendered =
            TextView::New(&cx, source)->TableScroll(scroll != 0)->IntoEl();
        El* table = FindMarkdownTableFrame(rendered, th.border);
        utassert(table && table->style.hasBg);
        utassert(table && SameTextViewColor(table->style.bg.color,
                                            th.tokens.tableBg.color));
        El* head = table ? table->first : nullptr;
        utassert(head && head->style.hasBg && head->style.hasColor);
        // Base paints the header from the style's own code background and
        // foreground; the themed pair arrives on top of it as the
        // `table_head` refinement, which is what the façade fills in.
        utassert(head && SameTextViewColor(head->style.bg.color,
                                           installed.style.codeBackground));
        utassert(head && SameTextViewColor(head->style.color, installed.style
                                                                  .foreground));
        utassert(head && (head->StyleStates()->refineSet & StyleFieldBg) &&
                 SameTextViewColor(head->StyleStates()->refine.bg.color,
                                   th.tokens.tableHead.color));
        utassert(head && (head->StyleStates()->refineSet & StyleFieldColor) &&
                 SameTextViewColor(head->StyleStates()->refine.color,
                                   th.tableHeadFg));
        // Row rules and column rules are the style's border now, not the
        // table theme's own row border.
        utassert(head && head->style.borderB == 1 &&
                 SameTextViewColor(head->style.borderColor, th.border));
        El* firstCell = head ? head->first : nullptr;
        utassert(firstCell && firstCell->style.borderR == 1 &&
                 SameTextViewColor(firstCell->style.borderColor, th.border));
    }

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

static int CountReportedLineSpans(El* e) {
    if (!e) return 0;
    int count = e->lineSpan ? 1 : 0;
    for (El* child = e->first; child; child = child->next) {
        count += CountReportedLineSpans(child);
    }
    return count;
}

static float TextViewSubtreeBottom(El* e) {
    if (!e) return 0;
    float bottom = e->y + e->h;
    for (El* child = e->first; child; child = child->next) {
        float childBottom = TextViewSubtreeBottom(child);
        if (childBottom > bottom) bottom = childBottom;
    }
    return bottom;
}

static void TestTextViewMaxLines() {
    LineSpan spans[] = {
        {0, 60, 20},
        {68, 128, 20},
    };
    float clip = 0;
    utassert(LineSafeClipBottom(spans, 2, 100, 400, &clip));
    utassert(fabsf(clip - 88) < 0.01f);
    utassert(!LineSafeClipBottom(spans, 2, 88, 400, &clip));
    utassert(LineSafeClipBottom(spans, 2, 64, 400, &clip));
    utassert(fabsf(clip - 60) < 0.01f);
    utassert(!LineSafeClipBottom(spans, 1, 200, 400, &clip));
    utassert(!LineSafeClipBottom(spans, 2, 130, 128, &clip));

    LineSpan heading[] = {{70, 98, 28}};
    utassert(!LineSafeClipBottom(heading, 1, 96, 400, &clip));
    LineSpan rows[] = {{100, 126, 26}, {135, 161, 26}};
    utassert(LineSafeClipBottom(rows, 2, 148, 400, &clip));
    utassert(fabsf(clip - 126) < 0.01f);

    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    Entity<TextViewState> state = TextViewState::Markdown(
        &app, StrL("first\n\nsecond\n\nthird\n\nfourth"));
    El* clamped = TextView::New(&cx, state)->MaxLines(2)->IntoEl();
    TextViewState* managed = state.Get(&app);
    utassert(clamped && clamped->lineClamp);
    utassert(clamped && clamped->style.overflowX == Overflow::Hidden &&
             clamped->style.overflowY == Overflow::Hidden);
    utassert(clamped &&
             fabsf(clamped->lineClampCap - 2 * 16.f * kLineHeight) < 0.01f);
    utassert(CountReportedLineSpans(clamped) == 4);
    utassert(managed && managed->maxLines == 2 && !managed->IsClamped());
    win->paint.app = &app;
    win->paint.window = win;
    const Theme& th = ThemeNow(&app);
    LayoutEl(&win->paint, clamped, 0, 0, 200, 500, th.fontSize, th.foreground);
    utassert(clamped->h <= clamped->lineClampCap + 0.01f);
    utassert(TextViewSubtreeBottom(clamped) > clamped->y + clamped->h + 1.f);

    El* scrolling =
        TextView::New(&cx, state)->MaxLines(2)->Scrollable()->IntoEl();
    utassert(scrolling && !scrolling->lineClamp);
    utassert(managed && managed->maxLines == -1);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

static void TestTextViewKeys() {
    KeymapClear();
    TextViewInitKeys();

    uint32_t context = KeyContextOf(StrL("TextView"));
    KeyChord chord = {};
    utassert(KeyChordParse(StrL("secondary-c"), &chord));
    utassert(KeymapMatch(chord, &context, 1).action == input::Copy());
    utassert(KeyChordParse(StrL("secondary-a"), &chord));
    utassert(KeymapMatch(chord, &context, 1).action == input::SelectAll());

    uint32_t other = KeyContextOf(StrL("Other"));
    utassert(KeymapMatch(chord, &other, 1).action == 0);
    KeymapClear();
}

static void TestManagedTextViewAndParseTimePlugins(Arena* a) {
    App app;
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Entity<TextViewState> state =
        TextViewState::Markdown(&app, StrL("```cpp\nclaimed\n```"));
    TextViewState* managed = state.Get(&app);
    utassert(managed &&
             base::StrEq(managed->Source(), StrL("```cpp\nclaimed\n```")));
    uint64_t revision = managed->revision;
    managed->PushStr(StrL("\n"), &app);
    utassert(managed->revision == revision + 1);
    utassert(managed->selectionRevision == 0);
    managed->SetText(StrL("```cpp\nclaimed\n```"), &app);
    utassert(managed->selectionRevision == 1);

    gParseTimePluginCalls = 0;
    gParseTimeRenderCalls = 0;
    El* element =
        TextView::New(&cx, state)
            ->Selectable()
            ->MarkdownBlockParser(&ParseCodeNode)
            ->MarkdownBlockRenderer(StrL("code-card"), &RenderCodeNode)
            ->IntoEl();
    utassert(gParseTimePluginCalls > 0);
    utassert(gParseTimeRenderCalls == 1);
    utassert(ElementTextBytes(element) == 7);
    El* owned = TextView::New(&cx, state)->Selectable()->IntoEl();
    utassert(FindSelectionOwner(owned, state.id));
    utassert(!UiTextViewStateCurrent(&app).IsValid());

    Window window;
    window.app = &app;
    TextHit first;
    first.text = StrL("alpha");
    first.docOff = 0;
    first.owner = state.id;
    first.bounds = {0, 0, 40, 20};
    VecAppend(window.paint.texts, first);
    TextHit other;
    other.text = StrL("other");
    other.docOff = 6;
    other.owner = EntityId{100, 1};
    other.bounds = {0, 20, 40, 20};
    VecAppend(window.paint.texts, other);
    TextHit last;
    last.text = StrL("omega");
    last.docOff = 12;
    last.owner = state.id;
    last.bounds = {0, 40, 40, 20};
    VecAppend(window.paint.texts, last);
    window.paint.textDocLen = 18;

    managed->SelectAll(&window, &app);
    utassert(managed->HasSelection(&window));
    TempStr selected = AllocStrTemp(63);
    int n = managed->SelectedText(&window, selected.s, selected.len + 1);
    utassert(base::StrEq(Str(selected.s, n), StrL("alpha\nomega")));
    managed->ClearSelection(&window, &app);
    utassert(!managed->HasSelection(&window));

    WindowSelectionFree(&window);
    VecReset(window.paint.texts);
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// text/style.rs: default_style_is_readable_without_an_application_theme,
// from_theme_maps_base_semantic_tokens, inline_code_falls_back_to_the_code_
// background and heading_font_size_resolves_through_the_installed_callback.
static float HeadingByLevel(uint8_t level, float base, void*) {
    return base * (7.f - (float)level);
}

static void TestTextViewStyleIsReadableWithoutATheme() {
    TextViewStyle style = TextViewStyle::Default();
    utassert(style.foreground.a == 255);
    utassert(style.link.a == 255);
    utassert(style.selection.a > 0);
    utassert(style.codeBackground.a > 0);
    utassert(style.border.a > 0);
    utassert(style.InlineCodeBackground().a > 0);
    utassert(style.codeBlockFields == 0);
    // The default is the light palette, not a bag of zeroes.
    utassert(
        SameTextViewColor(style.foreground, ColorTokens::Light().foreground));

    base_theme::Theme theme;
    theme.appearance = base_theme::ThemeAppearance::Dark;
    theme.tokens.colors.foreground = RgbaHex(0x112233);
    theme.tokens.colors.mutedForeground = RgbaHex(0x445566);
    theme.tokens.colors.primary = RgbaHex(0x3366ff);
    theme.tokens.colors.accent = RgbaHex(0xddeeff);
    theme.tokens.colors.border = RgbaHex(0x778899);
    theme.tokens.colors.selection = RgbaHex(0x55a0fc);
    TextViewStyle themed = TextViewStyle::FromTheme(theme);
    utassert(SameTextViewColor(themed.foreground, RgbaHex(0x112233)));
    utassert(SameTextViewColor(themed.mutedForeground, RgbaHex(0x445566)));
    utassert(SameTextViewColor(themed.link, RgbaHex(0x3366ff)));
    utassert(SameTextViewColor(themed.selection, RgbaHex(0x55a0fc)));
    utassert(SameTextViewColor(themed.codeBackground, RgbaHex(0xddeeff)));
    utassert(SameTextViewColor(themed.border, RgbaHex(0x778899)));
    utassert(themed.isDark);

    // inline_code_highlight falls back to the code background.
    TextViewStyle fallback = TextViewStyle::Default()
                                 .WithCodeBackground(RgbaHex(0x123456));
    utassert(
        SameTextViewColor(fallback.InlineCodeBackground(), RgbaHex(0x123456)));
    gpui::Style named = {};
    named.bg = Background(RgbaHex(0x654321));
    fallback.WithInlineCode(named, StyleFieldBg);
    utassert(
        SameTextViewColor(fallback.InlineCodeBackground(), RgbaHex(0x654321)));

    // heading_font_size resolves through the installed callback.
    TextViewStyle heading = TextViewStyle::Default();
    utassert(!heading.HasHeadingFontSize());
    heading.WithHeadingFontSize(&HeadingByLevel);
    utassert(heading.HasHeadingFontSize());
    utassert(heading.HeadingSize(1) == 14.f * 6.f);
    utassert(heading.HeadingSize(6) == 14.f);
}

// text_view.rs: text_view_constructors_are_selectable_by_default and
// syntax_highlighting_is_opt_in.
static int gTestHighlighterCalls = 0;

static void TestHighlighter(void* data, const CodeBlock* block, Arena* a,
                            ArenaVec<CodeHighlight>* out) {
    (void)data;
    gTestHighlighterCalls++;
    CodeHighlight span;
    span.start = 0;
    span.end = block->Code().len;
    span.color = RgbaHex(0x3366ff);
    out->Append(a, span);
}

static void TestTextViewDefaultsAndOptInHighlighting() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};

    // Every constructor is selectable now; `.selectable(false)` opts out.
    utassert(TextView::New(&cx, StrL("text"))->selectable);
    utassert(TextView::NewHtml(&cx, StrL("<p>text</p>"))->selectable);
    utassert(!TextView::New(&cx, StrL("text"))->Selectable(false)->selectable);
    // The two free constructors are the same two calls.
    utassert(MarkdownView(&cx, StrL("# hi"))->selectable);
    utassert(HtmlView(&cx, StrL("<p>hi</p>"))->html);

    // Without a highlighter a fenced block is one plain run; with one, the
    // colours it answers reach the painted words.
    Str source = StrL("```rust\nfn main() {}\n```");
    gTestHighlighterCalls = 0;
    El* plain = TextView::New(&cx, source)->IntoEl();
    utassert(plain && gTestHighlighterCalls == 0);

    El* highlighted = TextView::New(&cx, source)
                          ->CodeBlockHighlighter(&TestHighlighter)
                          ->IntoEl();
    utassert(highlighted && gTestHighlighterCalls == 1);

    // TextViewDefaults installs one for every view that names none.
    TextViewDefaults::New()
        .WithCodeBlockHighlighter(&TestHighlighter)
        .Install(&app);
    utassert(TextViewDefaults::Global(&app).HasCodeBlockHighlighter());
    gTestHighlighterCalls = 0;
    El* fromDefaults = TextView::New(&cx, source)->IntoEl();
    utassert(fromDefaults && gTestHighlighterCalls == 1);

    // An installed style is what an unstyled view renders with, so a Base
    // application's link and selection colours are its own.
    TextViewStyle style = TextViewStyle::Default()
                              .WithLink(RgbaHex(0x55aaff))
                              .WithSelection(RgbaHex(0x335577));
    TextViewDefaults::New().WithStyle(style).Install(&app);
    Entity<TextViewState> state =
        TextViewState::Markdown(&app, StrL("[link](url)"));
    TextView::New(&cx, state)->IntoEl();
    TextViewState* managed = state.Get(&app);
    utassert(managed &&
             SameTextViewColor(managed->textViewStyle.link, RgbaHex(0x55aaff)));
    utassert(managed && SameTextViewColor(managed->textViewStyle.selection,
                                          RgbaHex(0x335577)));

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// markdown_ext.rs has_same_parser_configuration, and the render loop it
// exists to stop: a view that rebuilds equivalent plugins every frame must
// reuse the parsed document — `stateless_markdown_with_rebuilt_parser_
// settles`.
static bool NeverClaims(const markdown::Node*, const MarkdownParseContext*,
                        void*, MarkdownNode*) {
    return false;
}

static El* RenderNothing(Ctx*, const MarkdownNode*, void*) {
    return nullptr;
}

static void TestMarkdownExtensionsParserConfiguration(Arena* a) {
    MarkdownExtensions first;
    first.BlockParser(a, &NeverClaims);
    first.BlockRenderer(a, StrL("card"), &RenderNothing);
    MarkdownExtensions second;
    second.BlockParser(a, &NeverClaims);
    second.BlockRenderer(a, StrL("card"), &RenderNothing);

    // Rebuilt handles: different revisions, the same parser.
    utassert(first.revision != second.revision);
    utassert(first.HasSameParserConfiguration(second));
    utassert(first.ParserFingerprint() == second.ParserFingerprint());

    MarkdownExtensions renamed;
    renamed.BlockParser(a, &NeverClaims);
    renamed.BlockRenderer(a, StrL("ticker"), &RenderNothing);
    utassert(!first.HasSameParserConfiguration(renamed));
    utassert(first.ParserFingerprint() != renamed.ParserFingerprint());

    MarkdownExtensions extra;
    extra.BlockParser(a, &NeverClaims);
    utassert(!first.HasSameParserConfiguration(extra));

    MarkdownExtensions mdx;
    mdx.BlockParser(a, &NeverClaims);
    mdx.BlockRenderer(a, StrL("card"), &RenderNothing);
    mdx.Mdx();
    utassert(!first.HasSameParserConfiguration(mdx));
}

static void TestStatelessMarkdownSettles() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    Str source = StrL("# Heading\n\nA paragraph with `code` in it.\n");

    // Two frames, each building its plugin table again. The parse cache is
    // keyed on the parser's shape, so the second frame reuses the first
    // frame's document instead of reparsing and notifying forever.
    MdNode* first = nullptr;
    MdNode* second = nullptr;
    for (int frame = 0; frame < 2; frame++) {
        TextView* view =
            TextView::New(&cx, source)
                ->MarkdownBlockParser(&NeverClaims)
                ->MarkdownBlockRenderer(StrL("card"), &RenderNothing);
        view->IntoEl();
        MdNode* doc =
            MdParseCachedForTest(&cx, a, source, &view->markdownExtensions);
        if (frame == 0) {
            first = doc;
        } else {
            second = doc;
        }
    }
    utassert(first && first == second);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

void TestTextView() {
    TestSuite("TextView");
    Arena* a = ArenaNew();
    TestMarkdownBlocks(a);
#if GPUI_MARKDOWN_FULL
    TestMarkdownTableAlign(a);
    TestTableToMarkdown(a);
    TestMarkdownInlineHtml(a);
    TestMarkdownHtmlBlock(a);
#endif
    TestMarkdownImage(a);
    TestHtmlBlocks(a);
    TestHtmlInlineMarks(a);
    TestHtmlNestedMarks(a);
    TestHtmlWhitespace(a);
    TestHtmlEntities(a);
    TestHtmlNestingHasNoPortLimit(a);
    TestHtmlList(a);
    TestHtmlPre(a);
    TestHtmlTable(a);
    TestHtmlImageAlt(a);
    TestHtmlUnbalanced(a);
    TestHtmlComments(a);
    TestHtmlBreak(a);
    TestMarkdownHardBreak(a);
    TestMarkdownSoftBreak(a);
    TestHtmlImage(a);
    TestImageSrc();
    TestSourceMarks();
    TestSourcePartialMark();
    TestSourceCodeAndLink();
    TestSourceHeading();
    TestSourceBlockquote();
    TestSourceCodeBlock();
    TestSourceTable();
    TestSourceList();
    TestSourceTaskList();
    TestSourceImage();
    TestSourceImageAtTheEnds();
    TestSourceIgnoresPlainRuns();
    TestTextCollectionsGrowWithTheDocument(a);
    TestSourceShapedTextValues(a);
    TestHtmlMinifier(a);
    TestTextViewKeys();
    TestTextViewStyleIsReadableWithoutATheme();
    TestTextViewDefaultsAndOptInHighlighting();
    TestMarkdownExtensionsParserConfiguration(a);
    TestStatelessMarkdownSettles();
    TestManagedTextViewAndParseTimePlugins(a);
    TestMarkdownTableThemeTokens();
    TestTextViewMaxLines();
#if GPUI_MARKDOWN_FULL
    TestMarkdownTaskList(a);
#endif
    ArenaDelete(a);
}
