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
    int total = 0;
    for (MdRun* r = n ? n->runFirst : nullptr; r; r = r->next) {
        total += len(r->text);
    }
    char* buf = (char*)Alloc(a, total + 1);
    int at = 0;
    for (MdRun* r = n ? n->runFirst : nullptr; r; r = r->next) {
        memcpy(buf + at, r->text.s, (size_t)len(r->text));
        at += len(r->text);
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
    int needleLen = (int)strlen(needle);
    for (MdRun* r = n ? n->runFirst : nullptr; r; r = r->next) {
        for (int i = 0; i + needleLen <= len(r->text); i++) {
            if (StrEq(Str(r->text.s + i, needleLen), Str(needle, needleLen))) {
                return r->marks;
            }
        }
    }
    return 0xff;
}

static Str HrefOf(MdNode* n, const char* needle) {
    int needleLen = (int)strlen(needle);
    for (MdRun* r = n ? n->runFirst : nullptr; r; r = r->next) {
        for (int i = 0; i + needleLen <= len(r->text); i++) {
            if (StrEq(Str(r->text.s + i, needleLen), Str(needle, needleLen))) {
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

static bool PrefixIs(Arena* a, int ix, int start, bool ordered, int depth,
                     const char* want) {
    return base::StrEq(ListItemPrefix(a, ix, start, ordered, depth), Str(want));
}

// text/utils.rs: test_list_item_prefix. A start of 1 is Rust's Some(1) and
// what an unordered list's None comes to here.
static void TestListItemPrefix(Arena* a) {
    utassert(PrefixIs(a, 0, 1, true, 0, "1. "));
    utassert(PrefixIs(a, 1, 1, true, 0, "2. "));
    utassert(PrefixIs(a, 2, 1, true, 0, "3. "));
    utassert(PrefixIs(a, 10, 1, true, 0, "11. "));
    utassert(PrefixIs(a, 0, 3, true, 0, "3. "));
    utassert(PrefixIs(a, 1, 3, true, 0, "4. "));
    utassert(PrefixIs(a, 0, 1, true, 1, "A. "));
    utassert(PrefixIs(a, 1, 1, true, 1, "B. "));
    utassert(PrefixIs(a, 0, 4, true, 1, "D. "));
    utassert(PrefixIs(a, 1, 4, true, 1, "E. "));
    utassert(PrefixIs(a, 0, 1, true, 2, "a. "));
    utassert(PrefixIs(a, 1, 1, true, 2, "b. "));
    utassert(PrefixIs(a, 6, 1, true, 2, "g. "));
    utassert(PrefixIs(a, 0, 0, true, 1, "0. "));
    utassert(PrefixIs(a, 1, 0, true, 1, "1. "));
    utassert(PrefixIs(a, 0, 1, false, 0, "\xE2\x80\xA2 "));
    utassert(PrefixIs(a, 0, 1, false, 1, "\xE2\x97\xA6 "));
    utassert(PrefixIs(a, 0, 1, false, 2, "\xE2\x96\xAA "));
    utassert(PrefixIs(a, 0, 1, false, 3, "\xE2\x80\xA3 "));
    utassert(PrefixIs(a, 0, 1, false, 4, "\xE2\x81\x83 "));
}

// text_view.rs: ordered_markdown_list_start_reaches_layout_marker, from the
// parsed start to the marker an item is drawn with (the list's depth is the
// one Blocks hands ListItemPrefix). state.rs:
// streamed_ordered_list_continuation_preserves_start — an append reparses
// the whole text here, so the continued list keeps its start. node.rs:
// ordered_list_selected_source_preserves_start — the Source marker is the
// ordinal whatever the depth.
static void TestOrderedListStarts(Arena* a) {
    MdNode* one = Child(MdParse(a, StrL("1. one\n2. two")), 0);
    utassert(one->ordered && one->start == 1);
    utassert(PrefixIs(a, 1, one->start, true, 0, "2. "));
    MdNode* html =
        Child(HtmlParse(a, StrL("<ol><li>one</li><li>two</li></ol>")), 0);
    utassert(html->ordered && html->start == 1);

    MdNode* three = Child(MdParse(a, StrL("3. hello\n4. world")), 0);
    utassert(three->start == 3 && Children(three) == 2);
    utassert(PrefixIs(a, 0, three->start, true, 0, "3. "));
    utassert(PrefixIs(a, 1, three->start, true, 0, "4. "));

    MdNode* outer =
        Child(MdParse(a, StrL("1. outer\n\n   4. nested\n   5. again")), 0);
    MdNode* nested = Child(Child(outer, 0), 1);
    utassert(nested && nested->kind == MdKind::List && nested->start == 4);
    utassert(PrefixIs(a, 0, nested->start, true, 1, "D. "));
    utassert(PrefixIs(a, 1, nested->start, true, 1, "E. "));
    utassert(OrderedListOrdinal(nested->start, 1) == 5);

    MdNode* zero = Child(
        Child(Child(MdParse(a, StrL("1. outer\n\n   0. zero")), 0), 0), 1);
    utassert(zero && zero->start == 0);
    utassert(PrefixIs(a, 0, zero->start, true, 1, "0. "));

    MdNode* streamed = Child(MdParse(a, StrL("3. three\n4. four")), 0);
    utassert(streamed->start == 3 && Children(streamed) == 2);
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
        if (len(r->imgSrc) > 0) {
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
        ctx.textDocLen += len(h.text) + 1;
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
                           len(buf) + 1, fmt);
    buf.len = n;
    return buf;
}

static TempStr SrcRangeCopyTemp(SrcDoc* d, int start, int end) {
    TempStr buf = AllocStrTemp(511);
    buf.len = CopyTextHitsIn(&d->ctx, start, end, -1, buf.s, len(buf) + 1,
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
    for (int i = 0; i < len(s); i++) {
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
    int n = e ? len(e->text) : 0;
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

// with_heading(|_| StyleRefinement::default().text_size(px(14.))), and the
// same at 28.
static uint32_t Heading14(uint8_t, gpui::Style* out, void*) {
    out->fontSize = 14;
    return StyleFieldFontSize;
}

static uint32_t Heading28(uint8_t, gpui::Style* out, void*) {
    out->fontSize = 28;
    return StyleFieldFontSize;
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
    // selection_layout_fingerprint_covers_callback_table_and_theme_fields and
    // cloning_preserves_the_same_heading_callback_fingerprint: a heading
    // callback is compared by what it answers.
    same.WithHeading(&Heading14);
    utassert(!base.Equals(same));
    base.WithHeading(&Heading14);
    utassert(base.Equals(same));
    TextViewStyle copy = same;
    utassert(copy.Equals(same));
    same.WithHeading(&Heading28);
    utassert(!base.Equals(same));

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

static void TestEqualBlockCountReplacementRemeasures() {
    App app;
    app.paint = PaintAppNew();
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    win->paint.pa = app.paint;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    Entity<TextViewState> state = TextViewState::Markdown(&app, StrL(""));
    LayoutCache* layout = LayoutCacheNew();
    float heights[2] = {};
    for (int pass = 0; pass < 2; pass++) {
        StrBuilder source(a);
        for (int block = 0; block < 24; block++) {
            if (block) source.Append(StrL("\n\n"));
            source.Append(fmt("Block %d: ", block + 1));
            for (int word = 0; word < (pass ? 60 : 1); word++) {
                source.Append(StrL("lorem ipsum dolor "));
            }
        }
        state.Get(&app)->SetText(source.TakeStr(), &app, win);
        El* view = TextView::New(&cx, state)->IntoEl();
        LayoutEl(&win->paint, view, 0, 0, 400, 200, 16, Rgba{}, layout);
        heights[pass] = TextViewSubtreeBottom(view);
    }
    utassert(heights[0] > 0);
    utassert(heights[1] > heights[0] * 5);
    LayoutCacheFree(layout);
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
    PaintAppFree(app.paint);
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
    int n = managed->SelectedText(&window, selected.s, len(selected) + 1);
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
// background and heading_refinement_defaults_empty_and_resolves_by_level.
static uint32_t HeadingByLevel(uint8_t level, gpui::Style* out, void*) {
    if (level == 1) {
        // pt(rems(1.)).pb(rems(0.5))
        out->pad.top = 16;
        out->pad.bottom = 8;
    } else {
        out->pad.bottom = 4;
    }
    return StyleFieldPad;
}

// component text/mod.rs: legacy_heading_configuration_maps_to_base_heading_
// refinements.
static float LegacyHeadingByLevel(uint8_t level, float base, void*) {
    return base * (float)level;
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

    // The heading refinement is empty by default and resolves by level.
    TextViewStyle heading = TextViewStyle::Default();
    gpui::Style resolved;
    utassert(heading.Heading(1, &resolved) == 0);
    TextViewStyle byLevel = TextViewStyle::Default();
    byLevel.WithHeading(&HeadingByLevel);
    utassert(byLevel.Heading(1, &resolved) == StyleFieldPad);
    utassert(resolved.pad.top == 16 && resolved.pad.bottom == 8);
    utassert(byLevel.Heading(2, &resolved) == StyleFieldPad);
    utassert(resolved.pad.top == 0 && resolved.pad.bottom == 4);
    utassert(!byLevel.Equals(heading));

    // The legacy base size and resolver become a text-size refinement.
    component::TextViewHeadingCompat legacy;
    legacy.headingBaseFontSize = 10;
    legacy.headingFontSize = &LegacyHeadingByLevel;
    TextViewStyle compat = TextViewStyle::Default();
    compat.WithHeading(&component::TextViewHeadingCompatRefine, &legacy);
    utassert(compat.Heading(2, &resolved) == StyleFieldFontSize);
    utassert(resolved.fontSize == 20.f);
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

// text_view.rs request_layout: `.text_color(text_view_style.foreground())`
// on the root, then `.refine_style(&self.style)`. The view names its own
// colour, so a parent's `text_color` stops at it — a muted description
// around a markdown TextView still draws the body foreground — and only a
// colour refined onto the view itself wins.
static void TestTextViewRootNamesItsForeground() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};

    TextViewStyle style = TextViewStyle::Default()
                              .WithForeground(RgbaHex(0x112233));
    El* view = TextView::New(&cx, StrL("plain words"))
                   ->Selectable(false)
                   ->Style(style)
                   ->IntoEl();
    utassert(view && view->style.hasColor &&
             SameTextViewColor(view->style.color, RgbaHex(0x112233)));

    gpui::Style muted = {};
    muted.color = RgbaHex(0x445566);
    muted.hasColor = true;
    El* refined = TextView::New(&cx, StrL("plain words"))
                      ->Selectable(false)
                      ->Style(style)
                      ->Refine(muted, StyleFieldColor)
                      ->IntoEl();
    // The refinement lands on the root over the foreground, and PrepareEl
    // applies it before the colour cascades.
    utassert(refined && refined->style.hasColor &&
             SameTextViewColor(refined->style.color, RgbaHex(0x112233)));
    utassert(refined && refined->StyleStates() &&
             (refined->StyleStates()->refineSet & StyleFieldColor) &&
             SameTextViewColor(refined->StyleStates()->refine.color,
                               RgbaHex(0x445566)));

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

static int gInlineParses = 0;
static int gInlineRenders = 0;
static bool gInlineInheritedBold = false;
static bool gInlineInheritedLink = false;

static bool ParseInlineMath(const markdown::Node* source,
                            const MarkdownParseContext* context, void*,
                            MarkdownNode* out) {
    if (source->kind != markdown::NodeKind::InlineMath) return false;
    gInlineParses++;
    utassert(
        StrEq(context->Value(source, markdown::NodeStrKind::Value), StrL("x")));
    *out = MarkdownNode::New(context->Copy(StrL("formula")))
               .Text(context->Copy(StrL("formula")))
               .Markdown(context->Copy(StrL("$x$")));
    return true;
}

static InlineElement RenderInlineMath(Ctx* cx, const MarkdownNode* node,
                                      const InlineRenderContext* context,
                                      void*) {
    gInlineRenders++;
    gInlineInheritedBold = context->textStyle.fontSemibold || context->textStyle
                                                                  .fontBold;
    gInlineInheritedLink = context->textStyle.underline;
    return InlineElement::New(TextEl(cx->a, node->text)->Id(StrL("formula")))
        .WithBaseline(context->fontSize * 0.75f);
}

static MdRun* FirstRunOfKind(MdNode* node, MdKind kind) {
    if (!node) return nullptr;
    if (node->kind == kind) return node->runFirst;
    for (MdNode* child = node->first; child; child = child->next) {
        if (MdRun* run = FirstRunOfKind(child, kind)) return run;
    }
    return nullptr;
}

static El* FindTextViewElement(El* element, const char* id) {
    if (!element) return nullptr;
    if (element->id.s && StrEqI(element->id, id)) return element;
    for (El* child = element->first; child; child = child->next) {
        if (El* found = FindTextViewElement(child, id)) return found;
    }
    return nullptr;
}

static void TestMarkdownInlinePlugin() {
    App app;
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};

    MarkdownPlugin plugin;
    plugin.name = StrL("formula");
    plugin.parse = &ParseInlineMath;
    plugin.renderInline = &RenderInlineMath;
    MarkdownExtensions extensions;
    extensions.Plugin(a, plugin);

    gInlineParses = 0;
    gInlineRenders = 0;
    MdNode* doc = MdParseCachedForTest(
        &cx, a, StrL("before **[$x$](https://example.com)** after"),
        &extensions);
    MdRun* custom = FirstRunOfKind(doc, MdKind::Paragraph);
    while (custom && !custom->hasCustom) custom = custom->next;
    utassert(custom && custom->hasCustom);
    utassert(custom && StrEq(custom->text, StrL("formula")));
    utassert(custom && StrEq(custom->custom.ToMarkdown(), StrL("$x$")));
    utassert(custom && (custom->marks & MdBold) && (custom->marks & MdLink));
    utassert(custom && StrEq(custom->href, StrL("https://example.com")));

    El* root =
        TextView::New(&cx, StrL("before **[$x$](https://example.com)** after"))
            ->MarkdownExtensionsSet(extensions)
            ->IntoEl();
    utassert(FindTextViewElement(root, "formula") != nullptr);
    utassert(gInlineParses >= 1 && gInlineRenders == 1);
    utassert(gInlineInheritedBold && gInlineInheritedLink);

    // Math syntax is enabled even without a plugin; an unclaimed expression
    // remains the literal text the author typed and code spans stay code.
    MdNode* plain = MdParse(a, StrL("spent $5 and $10, but `$code$` stayed"));
    MdRun* run = FirstRunOfKind(plain, MdKind::Paragraph);
    StrBuilder text;
    for (; run; run = run->next) text.Append(run->text);
    Str flattened = text.TakeStr();
    utassert(StrEq(flattened, StrL("spent $5 and $10, but $code$ stayed")));
    StrFree(flattened);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// The paragraph's text, and the stretches of it that carry `mark`: runs
// next to each other that both carry it are one stretch, which is what one
// mdast node's mark range is in markdown.rs's tests.
static Str ParagraphText(MdNode* doc, uint8_t mark, Str* runs, int cap,
                         int* count) {
    MdRun* run = FirstRunOfKind(doc, MdKind::Paragraph);
    StrBuilder text;
    StrBuilder stretch;
    bool open = false;
    *count = 0;
    for (; run; run = run->next) {
        text.Append(run->text);
        bool marked = (run->marks & mark) != 0;
        if (marked) {
            stretch.Append(run->text);
            open = true;
        } else if (open) {
            if (*count < cap) runs[(*count)++] = stretch.TakeStr();
            open = false;
        }
    }
    if (open && *count < cap) runs[(*count)++] = stretch.TakeStr();
    return text.TakeStr();
}

static void FreeRuns(Str* runs, int count) {
    for (int i = 0; i < count; i++) StrFree(runs[i]);
}

// markdown.rs: unclaimed_inline_math_parses_its_span_as_prose. Two dollar
// amounts pair up into an unclaimed math span; what lies between them is
// ordinary prose, so inline HTML there still pairs with tags outside the span
// and emphasis inside it renders.
static void UnclaimedInlineMathParsesItsSpanAsProse() {
    Arena* a = ArenaNew();
    MdNode* doc = MdParse(a, StrL("EPS of <strong>$1.56</strong> beat the "
                                  "<strong>$1.50</strong> consensus, *up $2*"));
    Str bold[4];
    int nBold = 0;
    Str text = ParagraphText(doc, MdBold, bold, 4, &nBold);
    utassert(StrEq(text, StrL("EPS of $1.56 beat the $1.50 consensus, up $2")));
    utassert(nBold == 2 && StrEq(bold[0], StrL("$1.56")) &&
             StrEq(bold[1], StrL("$1.50")));
    Str italic[4];
    int nItalic = 0;
    Str again = ParagraphText(doc, MdItalic, italic, 4, &nItalic);
    utassert(nItalic == 1 && StrEq(italic[0], StrL("up $2")));
    FreeRuns(bold, nBold);
    FreeRuns(italic, nItalic);
    StrFree(text);
    StrFree(again);
    ArenaDelete(a);
}

static bool ParseBracketedMath(const markdown::Node* source,
                               const MarkdownParseContext* context, void*,
                               MarkdownNode* out) {
    if (source->kind != markdown::NodeKind::InlineMath) return false;
    Str value = context->Value(source, markdown::NodeStrKind::Value);
    StrBuilder text(context->arena);
    text.AppendChar('[');
    text.Append(value);
    text.AppendChar(']');
    *out = MarkdownNode::New(context->Copy(StrL("formula")))
               .Text(text.TakeStr());
    return true;
}

// markdown.rs: claimed_inline_math_survives_prose_flattening.
static void ClaimedInlineMathSurvivesProseFlattening() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    MarkdownPlugin plugin;
    plugin.name = StrL("formula");
    plugin.parse = &ParseBracketedMath;
    // An inline plugin registers with a renderer; the parse is what counts.
    plugin.renderInline = &RenderInlineMath;
    MarkdownExtensions extensions;
    extensions.Plugin(a, plugin);
    MdNode* doc = MdParseCachedForTest(
        &cx, a, StrL("area $x^2$ costs $5 and $10"), &extensions);
    Str none[1];
    int n = 0;
    Str text = ParagraphText(doc, 0, none, 1, &n);
    utassert(StrEq(text, StrL("area [x^2] costs [5 and ]10")));
    StrFree(text);
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// markdown.rs: inline_html_formatting_tags_pair_across_siblings. A tag with
// no partner is dropped, as it is when Rust parses it alone.
static void InlineHtmlFormattingTagsPairAcrossSiblings() {
    Arena* a = ArenaNew();
    MdNode* doc =
        MdParse(a, StrL("a <strong>b *c* <em>d</em></strong> e <b>f</b> "
                        "<i>g</i> <del>h</del> <br> <strong>unclosed"));
    Str bold[4];
    int nBold = 0;
    Str text = ParagraphText(doc, MdBold, bold, 4, &nBold);
    utassert(StrEq(text, StrL("a b c d e f g h \n unclosed")));
    utassert(nBold == 2 && StrEq(bold[0], StrL("b c d")) &&
             StrEq(bold[1], StrL("f")));
    Str italic[4];
    int nItalic = 0;
    Str t2 = ParagraphText(doc, MdItalic, italic, 4, &nItalic);
    utassert(nItalic == 3 && StrEq(italic[0], StrL("c")) &&
             StrEq(italic[1], StrL("d")) && StrEq(italic[2], StrL("g")));
    Str struck[2];
    int nStruck = 0;
    Str t3 = ParagraphText(doc, MdDel, struck, 2, &nStruck);
    utassert(nStruck == 1 && StrEq(struck[0], StrL("h")));
    FreeRuns(bold, nBold);
    FreeRuns(italic, nItalic);
    FreeRuns(struck, nStruck);
    StrFree(text);
    StrFree(t2);
    StrFree(t3);
    ArenaDelete(a);
}

static uint32_t HeadingOnePadded(uint8_t level, gpui::Style* out, void*) {
    if (level != 1) return 0;
    out->pad.bottom = 32; // pb(rems(2.))
    return StyleFieldPad;
}

static bool HasBottomPad(El* e, float pad) {
    if (!e) return false;
    if (e->style.pad.bottom == pad) return true;
    for (El* c = e->first; c; c = c->next) {
        if (HasBottomPad(c, pad)) return true;
    }
    return false;
}

// text_view.rs: heading_refinement_changes_rendered_heading_geometry. The
// level-1 refinement reaches an h1's box, and an h2 keeps its default 0.3rem
// bottom padding.
static void HeadingRefinementChangesRenderedHeadingGeometry() {
    App app;
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    TextViewStyle custom = TextViewStyle::Default();
    custom.WithHeading(&HeadingOnePadded);
    El* defaultH1 = TextView::New(&cx, StrL("# Heading"))->IntoEl();
    El* customH1 =
        TextView::New(&cx, StrL("# Heading"))->Style(custom)->IntoEl();
    El* customH2 =
        TextView::New(&cx, StrL("## Heading"))->Style(custom)->IntoEl();
    utassert(!HasBottomPad(defaultH1, 32.f) && HasBottomPad(defaultH1, 5.f));
    utassert(HasBottomPad(customH1, 32.f));
    utassert(!HasBottomPad(customH2, 32.f) && HasBottomPad(customH2, 5.f));
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// inline_flow.rs: an_unchanged_flow_is_not_laid_out_again and
// a_flow_is_laid_out_again_when_its_text_changes. A paragraph with code spans
// is a flex-wrap row of words here, and the window's layout cache is what
// keeps it: a frame that changes nothing makes, restyles and remeasures no
// node, and new text does. (Rust's at-another-width case is taffy relaying
// out the kept nodes, which the stats do not count.)
static El* InlineCodeAnswer(Ctx* cx, Str source) {
    return TextView::New(cx, source)->IntoEl();
}

static void AnUnchangedFlowIsNotLaidOutAgain() {
    App app;
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    LayoutCache* lc = LayoutCacheNew();
    Str source = StrL("Call `parse` then `render` on the `TextView`.");
    for (int frame = 0; frame < 3; frame++) {
        a->Reset();
        LayoutEl(nullptr, InlineCodeAnswer(&cx, source), 0, 0, 400, 300, 14,
                 Rgba{}, lc);
    }
    LayoutCacheStats same = LayoutCacheLastStats(lc);
    utassert(same.made == 0 && same.restyled == 0 && same.remeasured == 0);
    a->Reset();
    LayoutEl(nullptr,
             InlineCodeAnswer(&cx, StrL("Call `parse` then `paint` on the "
                                        "`TextView` twice.")),
             0, 0, 400, 300, 14, Rgba{}, lc);
    LayoutCacheStats changed = LayoutCacheLastStats(lc);
    utassert(changed.made > 0 || changed.restyled > 0 ||
             changed.remeasured > 0);
    LayoutCacheFree(lc);
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// text_view.rs inline_code_line_is_as_tall_as_a_plain_line and
// inline_flow.rs inline_code_line_is_as_tall_as_a_plain_line_when_glyphs_
// overflow_it (#3162). A line with a code span is a row of words here and a
// plain line one text run; the smaller mono run joins the row inside the
// body's line box, so both come out the same height at every scale and
// zoom, and a list with one code item is evenly spaced.
static void InlineCodeLineIsAsTallAsAPlainLine() {
    App app;
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    const float dpis[2] = {96.f * 1.6f, 96.f * 2.f};
    const float zooms[2] = {1.f, 1.25f};
    for (float dpi : dpis) {
        for (float zoom : zooms) {
            a->Reset();
            win->paint.dpi = dpi;
            float font = 16.f * zoom;
            // Rust zooms with the root's rem size; the view's base font is
            // the same knob here.
            El* plain = TextView::New(&cx, StrL("plain body words"))
                            ->Font(font)
                            ->IntoEl();
            El* code = TextView::New(&cx, StrL("plain `code` words"))
                           ->Font(font)
                           ->IntoEl();
            El* root = Div(a)->W(600)->FlexCol()->Child(plain)->Child(code);
            LayoutEl(&win->paint, root, 0, 0, 600, 400, font, Rgba{});
            utassertnear(code->h, plain->h);
        }
    }
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
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

static void TestMarkdownFrontmatter() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    MarkdownExtensions extensions;
    extensions.Frontmatter().Plugin(a, FrontmatterPlugin::New());
    MdNode* doc =
        MdParseCachedForTest(&cx, a,
                             StrL("---\ntitle: GPUI\nsummary: >-\n  Small "
                                  "native UI\n  toolkit\n---\n\n# Hello\n"),
                             &extensions);
    utassert(Children(doc) == 2);
    MdNode* frontmatter = Child(doc, 0);
    utassert(frontmatter && frontmatter->kind == MdKind::Custom);
    utassert(StrEq(frontmatter->custom.name, StrL("frontmatter")));
    utassert(StrEq(frontmatter->custom.text,
                   StrL("title: GPUI\nsummary: Small native UI toolkit")));
    utassert(TextIs(a, Child(doc, 1), "Hello"));

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
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
    uint64_t settledRevision = 0;
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
            TextViewState* state = view->state.Get(&app);
            settledRevision = state ? state->revision : 0;
        } else {
            second = doc;
            TextViewState* state = view->state.Get(&app);
            utassert(state && state->revision == settledRevision);
            utassert(state->elementTextPtr == source.s);
        }
    }
    utassert(first && first == second);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// state.rs set_text_extending_markdown_appends_and_keeps_selection: Markdown
// that extends the current text is appended and keeps the selection; text
// that is not an extension replaces it. Rust also checks its
// full_update_revision; this state has no background parse to restart, so
// the selection revision is the whole of it.
static void SetTextExtendingMarkdownAppendsAndKeepsSelection() {
    App app;
    Entity<TextViewState> entity = TextViewState::Markdown(&app, StrL("hello"));
    TextViewState* state = entity.Get(&app);
    uint64_t initial = state->selectionRevision;

    state->SetText(StrL("hello world"), &app);
    utassert(base::StrEq(state->Source(), StrL("hello world")));
    utassert(state->selectionRevision == initial);

    state->SetText(StrL("hello"), &app);
    utassert(base::StrEq(state->Source(), StrL("hello")));
    utassert(state->selectionRevision != initial);
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// state.rs set_text_streaming_markdown_matches_a_full_parse: chunks that
// continue a heading, a paragraph, an inline code span, a list and a fenced
// code block, past the size Rust parses synchronously, stream in as appends
// and leave the same source a full set would. The document is parsed from
// the source when it renders, so the same source is the same parse.
static void SetTextStreamingMarkdownMatchesAFullParse() {
    App app;
    Entity<TextViewState> streamed =
        TextViewState::Markdown(&app, StrL("# Title"));
    TextViewState* state = streamed.Get(&app);
    uint64_t selection = state->selectionRevision;
    Arena* a = ArenaNew();
    StrBuilder text(a);
    text.Append(StrL("# Title"));
    // MAX_SYNC_FULL_REPLACE_BYTES / 5 + 1 words.
    StrBuilder filler(a);
    for (int i = 0; i < 4096 / 5 + 1; i++) {
        filler.Append(StrL("word "));
    }
    const Str chunks[] = {
        StrL("\n\nfirst para"), StrL("graph with `co"),
        StrL("de`\n\n- one\n"), StrL("- two\n\n```rust\nfn main() {"),
        StrL("}\n```\n\n"),     Str(filler.els, filler.len),
        StrL("\n\nlast")};
    for (const Str& chunk : chunks) {
        text.Append(chunk);
        state->SetText(Str(text.els, text.len), &app);
    }
    utassert(state->selectionRevision == selection);
    utassert(base::StrEq(state->Source(), Str(text.els, text.len)));
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(a);
}

// state.rs set_text_extending_html_parses_it_again: HTML blocks carry no
// source spans, so an extension of HTML is a replacement.
static void SetTextExtendingHtmlParsesItAgain() {
    App app;
    Entity<TextViewState> streamed =
        TextViewState::Html(&app, StrL("<ul><li>a</li>"));
    TextViewState* state = streamed.Get(&app);
    uint64_t selection = state->selectionRevision;
    state->SetText(StrL("<ul><li>a</li><li>b</li></ul>"), &app);
    utassert(state->selectionRevision != selection);
    utassert(
        base::StrEq(state->Source(), StrL("<ul><li>a</li><li>b</li></ul>")));
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// The first text run under `e` whose text holds `needle`.
static El* StreamTextEl(El* e, Str needle) {
    if (!e) {
        return nullptr;
    }
    if (e->kind == ElKind::Text && base::StrContains(e->text, needle)) {
        return e;
    }
    for (El* c = e->first; c; c = c->next) {
        if (El* t = StreamTextEl(c, needle)) {
            return t;
        }
    }
    return nullptr;
}

// stream_fade.rs fade_units_are_words_with_their_trailing_space and
// fade_units_split_cjk_by_character.
static void StreamFadeUnitsAreWordsOrCjkCharacters() {
    Vec<Span> units;
    StreamFadeUnits(StrL("hello one two  three"), 5, 20, &units);
    utassert(len(units) == 3);
    utassert(units[0].start == 5 && units[0].end == 10);
    utassert(units[1].start == 10 && units[1].end == 15);
    utassert(units[2].start == 15 && units[2].end == 20);
    // A unit that is only whitespace joins the word after it.
    VecClear(units);
    StreamFadeUnits(StrL("a  b"), 1, 4, &units);
    utassert(len(units) == 1 && units[0].start == 1 && units[0].end == 4);
    VecClear(units);
    StreamFadeUnits(StrL("abc"), 3, 3, &units);
    utassert(len(units) == 0);

    VecClear(units);
    StreamFadeUnits(StrL("你好，世界 ok"), 0, 18, &units);
    const int want[][2] = {{0, 3}, {3, 6}, {6, 9}, {9, 12}, {12, 16}, {16, 18}};
    utassert(len(units) == 6);
    for (int i = 0; i < 6 && i < len(units); i++) {
        utassert(units[i].start == want[i][0] && units[i].end == want[i][1]);
    }
    // Latin before CJK starts a unit at the script change.
    VecClear(units);
    StreamFadeUnits(StrL("ab中"), 0, 5, &units);
    utassert(len(units) == 2 && units[0].end == 2 && units[1].start == 2);
}

// inline.rs fade_highlights_tests: a fade takes its share of the colour and
// the background of whatever it lands on, and nothing outside it.
static void FadesLayerOverHighlightsInsideTheirRange() {
    Arena* a = ArenaNew();
    Rgba base = Rgba8(0, 0, 0, 255);
    TextSpan code;
    code.lo = 0;
    code.hi = 4;
    code.color = Rgba8(200, 0, 0, 255);
    code.bg = Rgba8(0, 0, 200, 200);
    TextFade half = {2, 6, 0.5f};
    const TextSpan* out = nullptr;
    int n = TextFadeSpans(a, 8, base, &code, 1, &half, 1, &out);
    utassert(n == 3);
    if (n == 3) {
        // Untouched highlight, faded highlight, faded plain text.
        utassert(out[0].lo == 0 && out[0].hi == 2 && out[0].color.a == 255 &&
                 out[0].bg.a == 200);
        utassert(out[1].lo == 2 && out[1].hi == 4 && out[1].color.r == 200 &&
                 out[1].color.a == 128 && out[1].bg.a == 100);
        utassert(out[2].lo == 4 && out[2].hi == 6 && out[2].color.a == 128 &&
                 out[2].color.r == 0 && out[2].bg.a == 0);
    }
    // No fades leave the highlights untouched: the same array comes back.
    utassert(TextFadeSpans(a, 8, base, &code, 1, nullptr, 0, &out) == 1 &&
             out == &code);
    ArenaDelete(a);
}

// A staggered update fades word by word: each word starts a step after the
// one before it, so mid-fade the earlier words are further along.
static void StreamedWordsFadeInOneAfterAnother() {
    bool wasReduced = MotionReduced();
    MotionSetReduced(false);
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    win->frameNow = 100.0;
    Entity<TextViewState> entity = TextViewState::Markdown(&app, StrL("hello"));
    TextViewState* state = entity.Get(&app);
    state->SetMotion(TextViewMotion{}
                         .WithStreamFade(600.f)
                         .WithStreamFadeStagger(100.f)
                         .WithStreamFadeEasing(Easing::Linear()),
                     &app, win);
    TextView::New(&cx, entity)->IntoEl();

    state->PushStr(StrL(" one two  three"), &app, win);
    TextView::New(&cx, entity)->IntoEl();
    utassert(len(state->fadeSegments) == 3);
    if (len(state->fadeSegments) == 3) {
        utassert(state->fadeSegments[0].range.start == 5);
        utassertnear((float)(state->fadeSegments[1].startedAt -
                             state->fadeSegments[0].startedAt),
                     0.1f);
        utassertnear((float)(state->fadeSegments[2].startedAt -
                             state->fadeSegments[0].startedAt),
                     0.2f);
    }

    // 150 ms in: the first word is a quarter lit, the second a twelfth, the
    // third has not started.
    win->frameNow = 100.15;
    a->Reset();
    El* view = TextView::New(&cx, entity)->IntoEl();
    El* run = StreamTextEl(view, StrL("three"));
    int n = 0;
    const TextFade* fades = run ? run->FadesGet(&n) : nullptr;
    utassert(fades && n == 3);
    if (fades && n == 3) {
        utassertnear(fades[0].fadeOut, 0.75f);
        utassertnear(fades[1].fadeOut, 1.f - 50.f / 600.f);
        utassertnear(fades[2].fadeOut, 1.f);
        utassert(fades[0].lo == 5 && fades[2].hi == 20);
    }

    // Once the first word is lit it stops fading; the others carry on.
    win->frameNow = 100.65;
    a->Reset();
    view = TextView::New(&cx, entity)->IntoEl();
    run = StreamTextEl(view, StrL("three"));
    fades = run ? run->FadesGet(&n) : nullptr;
    utassert(fades && n == 2 && fades[0].lo == 10);
    win->frameNow = 101.0;
    a->Reset();
    TextView::New(&cx, entity)->IntoEl();
    utassert(len(state->fadeSegments) == 0);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
    MotionSetReduced(wasReduced);
}

static void TestStreamFadeTracksRenderedAppends() {
    bool wasReduced = MotionReduced();
    MotionSetReduced(false);
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    Entity<TextViewState> entity = TextViewState::Markdown(&app, StrL("hello"));
    TextViewState* state = entity.Get(&app);
    state->SetMotion(TextViewMotion{}.WithStreamFade(10000.f), &app, win);

    TextView::New(&cx, entity)->IntoEl();
    utassert(len(state->fadeSegments) == 0);

    // The appended bytes of the leaf fade; the text before them does not,
    // and nothing fades by opacity on a whole block any more.
    state->PushStr(StrL(" world"), &app, win);
    El* faded = TextView::New(&cx, entity)->IntoEl();
    utassert(len(state->fadeSegments) == 1);
    utassert(state->fadeSegments[0].range.start == 5 &&
             state->fadeSegments[0].range.end == 11);
    utassert(faded && faded->first && faded->first->style.opacity == 1.f);
    El* run = StreamTextEl(faded, StrL("world"));
    int n = 0;
    const TextFade* fades = run ? run->FadesGet(&n) : nullptr;
    utassert(fades && n == 1 && fades[0].lo == 5 && fades[0].hi == 11);
    utassert(fades && fades[0].fadeOut > 0.99f);

    state->SetText(StrL("replacement"), &app, win);
    TextView::New(&cx, entity)->IntoEl();
    utassert(len(state->fadeSegments) == 0);

    TextView* compat = TextView::New(&cx, entity)->StreamFade();
    utassert(compat->motionSet && compat->motion.streamFadeMs == 280.f);
    utassert(compat->motion.streamFadeStaggerMs == 10.f);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
    MotionSetReduced(wasReduced);
}

// state.rs: a_fade_repaints_on_a_timer_until_nothing_fades. Rust advances
// TestAppContext's clock; the tick here is the window timer the frame arms,
// fired by hand.
static void AFadeRepaintsOnATimerUntilNothingFades() {
    bool wasReduced = MotionReduced();
    MotionSetReduced(false);
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    Entity<TextViewState> entity = TextViewState::Markdown(&app, StrL("hello"));
    TextViewState* state = entity.Get(&app);
    state->SetMotion(TextViewMotion{}.WithStreamFade(10000.f), &app, win);
    TextView::New(&cx, entity)->IntoEl();
    utassert(state->fadeTick == 0 && len(win->timers) == 0);

    state->PushStr(StrL(" world"), &app, win);
    TextView::New(&cx, entity)->IntoEl();
    utassert(state->fadeTick != 0 && len(win->timers) == 1);
    utassert(win->timers[0].ms == 33);
    // A frame painted before the tick arms no second one.
    TextView::New(&cx, entity)->IntoEl();
    utassert(len(win->timers) == 1);

    // Each tick repaints once, and that frame schedules the next tick.
    for (int tick = 0; tick < 3; tick++) {
        WindowCancelTimer(win, state->fadeTick);
        TextViewState::OnFadeTick(state, &cx, nullptr);
        utassert(state->fadeTick == 0);
        TextView::New(&cx, entity)->IntoEl();
        utassert(state->fadeTick != 0 && len(win->timers) == 1);
    }

    // Replacing the text drops the fade, so the ticks stop.
    state->SetText(StrL("other"), &app, win);
    WindowCancelTimer(win, state->fadeTick);
    TextViewState::OnFadeTick(state, &cx, nullptr);
    TextView::New(&cx, entity)->IntoEl();
    utassert(state->fadeTick == 0 && len(win->timers) == 0);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
    MotionSetReduced(wasReduced);
}

// stream_fade.rs: stagger_holds_while_the_update_lights_up_within_one_fade
// and an_update_too_large_to_light_up_in_one_fade_fades_as_one_chunk.
static void TestStreamFadeStaggerStep() {
    TextViewMotion motion =
        TextViewMotion{}.WithStreamFade(600.f).WithStreamFadeStagger(100.f);
    utassert(motion.StaggerStepMs(1) == 0.f);
    utassert(motion.StaggerStepMs(3) == 100.f);
    // The 7th word starts at 600 ms, exactly one fade in -- still the
    // stagger as asked.
    utassert(motion.StaggerStepMs(7) == 100.f);
    // An 8th word would start past the fade: that is a sweep, not typing.
    utassert(motion.StaggerStepMs(8) == 0.f);
    utassert(motion.StaggerStepMs(200) == 0.f);
    // Without a stagger nothing changes -- every update was already one
    // chunk.
    TextViewMotion plain = TextViewMotion{}.WithStreamFade(600.f);
    utassert(plain.StaggerStepMs(200) == 0.f);
}

// ─── selected_source_range
// ────────────────────────────────────────────────
//
// Ports of format/markdown.rs's selected_source_range_* and
// source_segments_* tests and state.rs's selected_source_range_* ones. Rust
// sets a rendered selection on the paragraph's inline state; here
// MdSelectedSourceRange takes the same selection of the node's rendered
// text. The two MDX cases (selected_source_range_maps_mdx_*) are not
// ported: MDX is not (src/markdown/readme.md).

#if GPUI_MARKDOWN_FULL
// first_paragraph / first_code_block.
static const MdNode* FirstOfKind(const MdNode* n, MdKind kind) {
    if (!n) {
        return nullptr;
    }
    if (n->kind == kind ||
        (kind == MdKind::Paragraph && n->kind == MdKind::Heading)) {
        return n;
    }
    if (n->kind != MdKind::Doc && n->kind != MdKind::Quote &&
        n->kind != MdKind::List && n->kind != MdKind::Item &&
        n->kind != MdKind::Group) {
        return nullptr;
    }
    for (const MdNode* c = n->first; c; c = c->next) {
        if (const MdNode* found = FirstOfKind(c, kind)) {
            return found;
        }
    }
    return nullptr;
}

// selected_rendered_range / selected_code_range: the source range behind
// [start, end) of the first paragraph (or code block) of `source`, as
// "start..end", or "None".
static TempStr SourceRangeTemp(const char* source, int start, int end,
                               MdKind kind = MdKind::Paragraph) {
    Arena* a = ArenaNew();
    MdNode* doc = MdParse(a, Str((char*)source));
    const MdNode* n = FirstOfKind(doc, kind);
    Span span;
    TempStr out = n && MdSelectedSourceRange(n, start, end).IntoRange(&span)
                      ? fmt("%d..%d", span.start, span.end)
                      : TempStr("None");
    ArenaDelete(a);
    return out;
}

static bool RangeIs(const char* source, int start, int end, const char* want,
                    MdKind kind = MdKind::Paragraph) {
    return base::StrEq(SourceRangeTemp(source, start, end, kind), want);
}

static bool CodeRangeIs(const char* source, int start, int end,
                        const char* want) {
    return RangeIs(source, start, end, want, MdKind::Code);
}

static bool HasSegment(const MdRun* r, int rs, int re, int ss, int se) {
    for (int i = 0; r && i < r->segmentCount; i++) {
        const SourceSegment& s = r->segments[i];
        if (s.renderedStart == rs && s.renderedEnd == re &&
            s.sourceStart == ss && s.sourceEnd == se) {
            return true;
        }
    }
    return false;
}

// selected_source_range_uses_the_selected_identical_styled_occurrence,
// selected_source_range_maps_partial_styled_text,
// selected_source_range_crosses_style_boundaries,
// selected_source_range_maps_inline_code_in_merged_styled_node.
static void TestSourceRangeStyled() {
    utassert(RangeIs("**same** then **same**", 10, 14, "16..20"));
    utassert(RangeIs("**same** then **same**", 11, 13, "17..19"));
    utassert(RangeIs("left **bold** right", 2, 12, "2..16"));
    utassert(RangeIs("**left `code` right**", 5, 9, "8..12"));
}

// source_segments_compact_contiguous_one_to_one_mappings,
// source_segments_keep_non_linear_mappings_atomic.
static void TestSourceSegmentsCompact() {
    Arena* a = ArenaNew();
    MdNode* doc = MdParse(a, StrL("plain text"));
    const MdRun* r = FirstOfKind(doc, MdKind::Paragraph)->runFirst;
    utassert(r && r->segmentCount == 1 && HasSegment(r, 0, 10, 0, 10));
    utassert(RangeIs("plain text", 2, 7, "2..7"));

    doc = MdParse(a, StrL("a\\* &amp; b"));
    r = FirstOfKind(doc, MdKind::Paragraph)->runFirst;
    // "ordinary characters should be compacted into runs"
    utassert(r && r->segmentCount < len(r->text));
    utassert(HasSegment(r, 1, 2, 1, 3));
    utassert(HasSegment(r, 3, 4, 4, 9));
    utassert(RangeIs("a\\* &amp; b", 1, 2, "1..3"));
    utassert(RangeIs("a\\* &amp; b", 3, 4, "4..9"));
    ArenaDelete(a);
}

// `pre` + `unit` repeated `n` times + `post`, NUL-terminated in `a`.
static Str Repeat(Arena* a, const char* pre, const char* unit, int n,
                  const char* post) {
    int lp = (int)strlen(pre), lu = (int)strlen(unit), lq = (int)strlen(post);
    int total = lp + lu * n + lq;
    char* buf = (char*)Alloc(a, total + 1);
    memcpy(buf, pre, (size_t)lp);
    for (int i = 0; i < n; i++) {
        memcpy(buf + lp + i * lu, unit, (size_t)lu);
    }
    memcpy(buf + lp + lu * n, post, (size_t)lq);
    buf[total] = 0;
    return Str(buf, total);
}

// The segments `AlignedSourceSegments` produced, against `want` rows of
// {renderedStart, renderedEnd, sourceStart, sourceEnd}.
static bool SegmentsAre(const Vec<SourceSegment>& got, const int (*want)[4],
                        int n) {
    if (got.len != n) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        const SourceSegment& s = got[i];
        if (s.renderedStart != want[i][0] || s.renderedEnd != want[i][1] ||
            s.sourceStart != want[i][2] || s.sourceEnd != want[i][3]) {
            return false;
        }
    }
    return true;
}

// source_alignment_compacts_long_whitespace_runs: a long run of blanks is
// one segment, and the compaction as it goes never holds one per character.
static void TestSourceAlignmentCompactsLongWhitespaceRuns() {
    Arena* a = ArenaNew();
    Str raws[3] = {
        Repeat(a, "", "a ", 16384, ""),
        Repeat(a, "a", " ", 32768, "b"),
        Repeat(a, "a", " \t", 16384, "b\n"),
    };
    for (Str raw : raws) {
        Vec<SourceSegment> segments;
        AlignedSourceSegments(a, raw, raw, 7, true, segments);
        const int want[1][4] = {{0, len(raw), 7, 7 + len(raw)}};
        utassert(SegmentsAre(segments, want, 1));
        // "compaction must not retain a per-character allocation"
        utassert(segments.cap < 64);
    }
    ArenaDelete(a);
}

// source_alignment_preserves_multiline_code_and_final_newline.
static void TestSourceAlignmentPreservesMultilineCodeAndFinalNewline() {
    Arena* a = ArenaNew();
    Str raw = Repeat(a, "", "x\n", 16384, "");
    Str rendered(raw.s, len(raw) - 1);
    Vec<SourceSegment> segments;
    AlignedSourceSegments(a, raw, rendered, 4, false, segments);
    const int want[1][4] = {{0, len(rendered), 4, 4 + len(rendered)}};
    utassert(SegmentsAre(segments, want, 1));

    Vec<SourceSegment> quote;
    AlignedSourceSegments(a, StrL("a\n> \n"), StrL("a\n\n"), 0, false, quote);
    const int wantQuote[2][4] = {{0, 2, 0, 2}, {2, 3, 2, 5}};
    utassert(SegmentsAre(quote, wantQuote, 2));
    ArenaDelete(a);
}

// source_alignment_keeps_soft_breaks_and_entities_atomic.
static void TestSourceAlignmentKeepsSoftBreaksAndEntitiesAtomic() {
    Arena* a = ArenaNew();
    Vec<SourceSegment> soft;
    AlignedSourceSegments(a, StrL("a \r\nb"), StrL("a b"), 9, true, soft);
    const int wantSoft[3][4] = {{0, 1, 9, 10}, {1, 2, 11, 13}, {2, 3, 13, 14}};
    utassert(SegmentsAre(soft, wantSoft, 3));

    Str entity = StrL("&NotEqualTilde;");
    // U+2242 U+0338.
    Str decoded = StrL("\xE2\x89\x82\xCC\xB8");
    Vec<SourceSegment> atomic;
    AlignedSourceSegments(a, entity, decoded, 3, true, atomic);
    const int wantAtomic[1][4] = {{0, len(decoded), 3, 3 + len(entity)}};
    utassert(SegmentsAre(atomic, wantAtomic, 1));
    ArenaDelete(a);
}

// source_alignment_resumes_after_unmapped_characters.
static void TestSourceAlignmentResumesAfterUnmappedCharacters() {
    Arena* a = ArenaNew();
    Str raw = Repeat(a, "", "abc", 4096, "");
    const char* replacement = "\xEF\xBF\xBD"; // U+FFFD
    SourceCharIndex positions;
    utassert(SourceCharOffset(raw, 0, "b", 1, &positions) == 1);
    // "successful scans need no index"
    utassert(!positions.built);
    utassert(SourceCharOffset(raw, 0, replacement, 3, &positions) == -1);
    // "failed scans must not be repeated"
    utassert(positions.built);
    utassert(SourceCharOffset(raw, 2, "b", 1, &positions) == 4);
    utassert(SourceCharOffset(raw, len(raw), "a", 1, &positions) == -1);

    Str missing = Repeat(a, "", replacement, 4096, "");
    int m = len(missing);
    Vec<SourceSegment> resumed;
    AlignedSourceSegments(a, raw, Repeat(a, "", replacement, 4096, "abc"), 5,
                          true, resumed);
    const int wantResumed[1][4] = {{m, m + 3, 5, 8}};
    utassert(SegmentsAre(resumed, wantResumed, 1));

    Vec<SourceSegment> entity;
    AlignedSourceSegments(a, StrL("&amp;z"),
                          Repeat(a, "", replacement, 4096, "&z"), 0, true,
                          entity);
    const int wantEntity[2][4] = {{m, m + 1, 0, 5}, {m + 1, m + 2, 5, 6}};
    utassert(SegmentsAre(entity, wantEntity, 2));
    ArenaDelete(a);
}

// source_alignment_parses_long_text_and_code_without_selection.
static void TestSourceAlignmentParsesLongTextAndCode() {
    Arena* a = ArenaNew();
    Str source = Repeat(a, "", "a ", 4096, "end");
    MdNode* doc = MdParse(a, source);
    const MdNode* paragraph = FirstOfKind(doc, MdKind::Paragraph);
    utassert(paragraph && paragraph->runFirst && !paragraph->runFirst->next);
    const MdRun* r = paragraph->runFirst;
    utassert(r && base::StrEq(r->text, source));
    utassert(r && r->segmentCount == 1 && r->segments[0].sourceStart == 0 &&
             r->segments[0].sourceEnd == len(source));
    utassert(RangeIs(source.s, len(source) - 3, len(source),
                     fmt("%d..%d", len(source) - 3, len(source)).s));

    Str body = Repeat(a, "", "x\n", 4096, "");
    Str code = Repeat(a, "```\n", "x\n", 4096, "```");
    doc = MdParse(a, code);
    utassert(FirstOfKind(doc, MdKind::Code) != nullptr);
    utassert(CodeRangeIs(code.s, len(body) - 2, len(body) - 1,
                         fmt("%d..%d", len(body) + 2, len(body) + 3).s));
    ArenaDelete(a);
}

// selected_source_range_maps_inline_code_delimiters_and_boundaries,
// selected_source_range_maps_footnote_reference_syntax.
static void TestSourceRangeInlineCodeAndFootnote() {
    utassert(RangeIs("`code` x", 0, 4, "1..5"));
    utassert(RangeIs("`code` x", 5, 6, "7..8"));
    utassert(RangeIs("`code` x", 3, 6, "4..8"));
    utassert(RangeIs("`` code ` value ``", 0, 4, "3..7"));
    utassert(RangeIs("`` code ` value ``", 5, 6, "8..9"));
    utassert(RangeIs("`&amp;`", 0, 5, "1..6"));

    const char* note = "before[^note] after\n\n[^note]: body";
    utassert(RangeIs(note, 0, 6, "0..6"));
    utassert(RangeIs(note, 6, 12, "6..13"));
    utassert(RangeIs(note, 13, 18, "14..19"));
    utassert(RangeIs(note, 4, 15, "4..16"));
}

// selected_source_range_maps_math_block_body,
// selected_source_range_maps_fenced_code_body_after_matching_info_string,
// selected_source_range_excludes_closing_fence_candidate,
// selected_source_range_maps_indented_code_content,
// selected_source_range_maps_multiline_indented_code,
// selected_source_range_maps_fenced_code_nested_in_a_list,
// selected_source_range_maps_fenced_code_nested_in_a_blockquote,
// selected_source_range_maps_fenced_code_with_blank_lines_and_repeated_text.
static void TestSourceRangeCodeBlocks() {
    utassert(CodeRangeIs("$$\nx + y\n$$", 0, 5, "3..8"));
    utassert(CodeRangeIs("$$\nx + y\n$$", 2, 3, "5..6"));
    utassert(CodeRangeIs("```rust\nrust\n```", 0, 4, "8..12"));
    utassert(CodeRangeIs("````text\n```\n````", 0, 3, "9..12"));
    utassert(CodeRangeIs("    rust", 0, 4, "4..8"));
    const char* indented = "    one\n    two\n    three";
    utassert(CodeRangeIs(indented, 0, 3, "4..7"));
    utassert(CodeRangeIs(indented, 4, 7, "12..15"));
    utassert(CodeRangeIs(indented, 0, 13, "4..25"));
    const char* list = "- ```rust\n  one\n  two\n  ```";
    utassert(CodeRangeIs(list, 0, 3, "12..15"));
    utassert(CodeRangeIs(list, 4, 7, "18..21"));
    utassert(CodeRangeIs(list, 0, 7, "12..21"));
    const char* quote = "> ```\n> one\n> two\n> ```";
    utassert(CodeRangeIs(quote, 0, 3, "8..11"));
    utassert(CodeRangeIs(quote, 4, 7, "14..17"));
    utassert(CodeRangeIs(quote, 0, 7, "8..17"));
    const char* blank = "```\nsame\n\nsame\n```";
    utassert(CodeRangeIs(blank, 0, 4, "4..8"));
    utassert(CodeRangeIs(blank, 6, 10, "10..14"));
    utassert(CodeRangeIs(blank, 0, 10, "4..14"));
}

// selected_source_range_maps_the_whole_markdown_escape,
// selected_source_range_maps_after_an_escaped_backslash,
// selected_source_range_does_not_borrow_an_escape_from_the_previous_node,
// selected_source_range_does_not_shift_a_hard_break_after_an_escape.
static void TestSourceRangeEscapes() {
    utassert(RangeIs("\\*", 0, 1, "0..2"));
    utassert(RangeIs("a\\\\b", 1, 2, "1..3"));
    utassert(RangeIs("a\\\\b", 2, 3, "3..4"));
    utassert(RangeIs("a\\\\b", 1, 3, "1..4"));
    utassert(RangeIs("\\\\\\\\b", 2, 3, "4..5"));
    utassert(RangeIs("a\\\\$x$", 2, 3, "3..4"));
    utassert(RangeIs("a\\\\$x$", 1, 3, "1..4"));
    utassert(RangeIs("a\\\\  \nb", 2, 3, "3..6"));
    utassert(RangeIs("a\\\\  \nb", 1, 3, "1..6"));
}

// selected_source_range_includes_a_trailing_inline_image, _a_leading_,
// _an_enclosed_, excludes_an_unreached_inline_image and
// includes_consecutive_inline_images. An image has no rendered text of its
// own, so "before " is 0..7 and " after" follows it.
static void TestSourceRangeImages() {
    utassert(RangeIs("before ![alt](image.png)", 0, 7, "0..24"));
    utassert(RangeIs("![alt](image.png) after", 0, 6, "0..23"));
    const char* enclosed = "before ![alt](image.png) after";
    utassert(RangeIs(enclosed, 0, 13, "0..30"));
    utassert(RangeIs(enclosed, 0, 3, "0..3"));
    // " after" selected from its third byte: "fter", after the image.
    utassert(RangeIs(enclosed, 9, 13, "26..30"));
    utassert(
        RangeIs("![first](one.png)![second](two.png) after", 0, 6, "0..41"));
}

// selected_source_range_maps_decoded_entity_to_its_source_syntax,
// selected_source_range_maps_around_named_and_numeric_entities,
// selected_source_range_rejects_mapped_block_plus_unmappable_entity.
static void TestSourceRangeEntities() {
    Arena* a = ArenaNew();
    MdNode* doc = MdParse(a, StrL("A &amp; B"));
    utassert(TextIs(a, (MdNode*)FirstOfKind(doc, MdKind::Paragraph), "A & B"));
    utassert(RangeIs("A &amp; B", 2, 3, "2..7"));

    const char* refs = "Copyright &copy; &#x1F600; &#169; 2024";
    utassert(RangeIs(refs, 0, 9, "0..9"));
    utassert(RangeIs(refs, 10, 12, "10..16"));
    utassert(RangeIs(refs, 13, 17, "17..26"));
    utassert(RangeIs(refs, 18, 20, "27..33"));
    utassert(RangeIs(refs, 21, 25, "34..38"));
    utassert(RangeIs(refs, 8, 22, "8..35"));

    // Two paragraphs: the document's range is the merge of each one's.
    doc = MdParse(a, StrL("mapped\n\nA &amp; B"));
    const MdNode* mapped = Child(doc, 0);
    const MdNode* entity = Child(doc, 1);
    SourceRangeSelection selected = MdSelectedSourceRange(mapped, 0, 6);
    selected.Merge(MdSelectedSourceRange(entity, 2, 2));
    Span span;
    utassert(selected.IntoRange(&span) && span.start == 0 && span.end == 6);
    selected.Merge(MdSelectedSourceRange(entity, 2, 3));
    utassert(selected.IntoRange(&span) && span.start == 0 && span.end == 15);
    ArenaDelete(a);
}

// selected_source_range_maps_soft_breaks_with_source_prefixes,
// selected_source_range_maps_soft_breaks_with_trailing_spaces_and_crlf.
static void TestSourceRangeSoftBreaks() {
    utassert(RangeIs("a\n   b", 0, 1, "0..1"));
    utassert(RangeIs("a\n   b", 2, 3, "5..6"));
    utassert(RangeIs("a\n   b", 0, 3, "0..6"));
    utassert(RangeIs("> a\n> b", 0, 1, "2..3"));
    utassert(RangeIs("> a\n> b", 2, 3, "6..7"));
    utassert(RangeIs("> a\n> b", 0, 3, "2..7"));
    utassert(RangeIs("- a\n  b", 0, 1, "2..3"));
    utassert(RangeIs("- a\n  b", 2, 3, "6..7"));
    utassert(RangeIs("- a\n  b", 0, 3, "2..7"));

    utassert(RangeIs("a \nb", 0, 1, "0..1"));
    utassert(RangeIs("a \nb", 1, 2, "2..3"));
    utassert(RangeIs("a \nb", 2, 3, "3..4"));
    utassert(RangeIs("a \nb", 0, 3, "0..4"));
    utassert(RangeIs("a \r\nb", 1, 2, "2..4"));
    utassert(RangeIs("a \r\nb", 2, 3, "4..5"));
    utassert(RangeIs("a\r\nb", 0, 1, "0..1"));
    utassert(RangeIs("a\r\nb", 2, 3, "3..4"));
    utassert(RangeIs("a\r\nb", 0, 3, "0..4"));
}

// state.rs selected_source_range_keeps_global_offsets_after_incremental_
// tail_parse. The C++ parse is always of the whole source, so the appended
// paragraph's offsets are the document's without a tail offset to add.
static void TestSourceRangeAfterAppend() {
    Arena* a = ArenaNew();
    MdNode* doc = MdParse(a, StrL("first\n\nsecond\n\n**\xC3\xA9"
                                  "cho**"));
    utassert(Children(doc) == 3);
    Span span;
    utassert(MdSelectedSourceRange(Child(doc, 2), 0, 5).IntoRange(&span) &&
             span.start == 17 && span.end == 22);
    ArenaDelete(a);
}

// The view's own path: the runs a frame painted, each carrying the source
// map of the run it came from, and a selection over them. A run painted as
// two word elements maps each from its own offset into the run.
static void TestSourceRangeOverPaintedRuns() {
    Arena* a = ArenaNew();
    MdNode* doc = MdParse(a, StrL("**same** then **same**"));
    const MdNode* p = FirstOfKind(doc, MdKind::Paragraph);
    EntityId owner = EntityId{7, 1};
    PaintCtx ctx;
    SelSourceMap maps[8];
    int nMaps = 0;
    for (const MdRun* r = p->runFirst; r; r = r->next) {
        // " then " as two words, the way Inline splits a run.
        int starts[2] = {0, len(r->text)};
        int nWords = 1;
        if (len(r->text) == 6) {
            starts[1] = 1;
            nWords = 2;
        }
        for (int w = 0; w < nWords; w++) {
            int end = w + 1 < nWords ? starts[w + 1] : len(r->text);
            SelSourceMap& m = maps[nMaps++];
            m.segments = r->segments;
            m.count = r->segmentCount;
            m.offset = starts[w];
            TextHit h;
            h.text = Str((char*)r->text.s + starts[w], end - starts[w]);
            h.docOff = ctx.textDocLen;
            h.owner = owner;
            h.map = &m;
            VecAppend(ctx.texts, h);
            ctx.textDocLen += len(h.text) + 1;
        }
    }
    // Runs: "same" 0..4, " " 5..6, "then " 7..12, "same" 13..17.
    Span span;
    utassert(TextHitsSourceRange(&ctx, 13, 17, 0, owner).IntoRange(&span) &&
             span.start == 16 && span.end == 20);
    utassert(TextHitsSourceRange(&ctx, 8, 10, 0, owner).IntoRange(&span) &&
             span.start == 10 && span.end == 12);
    // Another view's runs have no part in it.
    utassert(TextHitsSourceRange(&ctx, 13, 17, 0, EntityId{8, 1})
                 .kind == SourceRangeSelection::Unselected);
    VecReset(ctx.texts);
    ArenaDelete(a);
}

// selected_source_range_returns_full_markdown_source_for_select_all,
// selected_source_range_returns_none_for_html.
static void TestSourceRangeSelectAllAndHtml() {
    App app;
    Entity<TextViewState> md =
        TextViewState::Markdown(&app, StrL("**quick** value"));
    Entity<TextViewState> html =
        TextViewState::Html(&app, StrL("<b>quick</b>"));
    Window window;
    window.app = &app;
    TextHit first;
    first.text = StrL("quick");
    first.owner = md.id;
    first.bounds = {0, 0, 40, 20};
    VecAppend(window.paint.texts, first);
    TextHit other;
    other.text = StrL("quick");
    other.docOff = 6;
    other.owner = html.id;
    other.bounds = {0, 20, 40, 20};
    VecAppend(window.paint.texts, other);
    window.paint.textDocLen = 12;

    Span span;
    md.Get(&app)->SelectAll(&window, &app);
    utassert(md.Get(&app)->SelectedSourceRange(&window, &span) &&
             span.start == 0 && span.end == 15);
    html.Get(&app)->SelectAll(&window, &app);
    utassert(!html.Get(&app)->SelectedSourceRange(&window, &span));

    WindowSelectionFree(&window);
    VecReset(window.paint.texts);
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

#endif

// The tree's parser is always the full one; a dist built with the mini
// parser has no tables to highlight.
#if !GPUI_MARKDOWN_MINI
// ─── range highlights ─────────────────────────────────────────────────────
//
// range_highlight.rs and state.rs `mod range_highlights`. Rust's tests run a
// parse through TestAppContext and read the resolved frame; the parse lands
// here when the view renders, so each fixture renders once after every
// change, where Rust runs until parked.
//
// inline.rs's glyph_boxes / range_boxes tests (rows_align_the_way_gpui_paints_
// them, range_boxes_*, a_highlight_starting_a_wrapped_row_paints_only_that_
// row, highlights_after_a_hard_line_break_start_on_its_row,
// highlights_follow_centered_and_right_aligned_rows) check Rust's own paint
// geometry. The washes here go through PaintTextRange, the painter the
// selection uses, so they wrap and align the way the selection does.

// range_highlight.rs: range_highlight_requires_a_background.
static void RangeHighlightRequiresABackground() {
    Hsla color = HslaNew(0.15f, 1.f, 0.5f, 0.4f);
    RangeHighlight highlight = RangeHighlight::New(Span{2, 5}, color);
    utassert(highlight.Range().start == 2 && highlight.Range().end == 5);
    Rgba want = HslaToRgba(color);
    Rgba got = highlight.Background();
    utassert(got.r == want.r && got.g == want.g && got.b == want.b &&
             got.a == want.a);
}

struct RhView {
    App app;
    Window* win = nullptr;
    Arena* a = nullptr;
    Ctx cx = {};
    Entity<gpui::TextViewState> state = {};
    const MarkdownExtensions* extensions = nullptr;
};

static void RhRender(RhView* v) {
    gpui::TextView* view = gpui::TextView::New(&v->cx, v->state);
    if (v->extensions) {
        view->MarkdownExtensionsSet(*v->extensions);
    }
    view->IntoEl();
}

static void RhOpen(RhView* v, const char* markdown, bool html = false) {
    v->win = new Window();
    v->win->app = &v->app;
    v->a = ArenaNew();
    v->cx = Ctx{&v->app, v->win, v->a, {}};
    v->state = html ? gpui::TextViewState::Html(&v->app, Str(markdown))
                    : gpui::TextViewState::Markdown(&v->app, Str(markdown));
    RhRender(v);
}

static void RhClose(RhView* v) {
    WindowKeyedFree(v->win);
    ArenaDelete(v->a);
    delete v->win;
    EntityDropAll(&v->app);
    AppGlobalClear(&v->app);
}

static gpui::TextViewState* RhState(RhView* v) {
    return v->state.Get(&v->app);
}

// Highlights `ranges` of the current rendered text.
static RangeHighlightError RhSet(RhView* v, const Span* ranges, int count) {
    RangeHighlight highlights[64];
    for (int i = 0; i < count; i++) {
        highlights[i] =
            RangeHighlight::New(ranges[i], HslaNew(0.15f, 1.f, 0.5f, 0.4f));
    }
    return RhState(v)->SetRangeHighlights(highlights, count, &v->app, v->win);
}

// Whether leaf `key` paints exactly `want`, in its own byte space.
static bool RhPainted(RhView* v, gpui::TextLeafKey key, const Span* want,
                      int count) {
    const gpui::RangeHighlightFrame* frame = RhState(v)->rangeHighlights;
    int n = 0;
    const gpui::RangeBackground* bgs =
        frame ? frame->Backgrounds(key, &n) : nullptr;
    if (n != count) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (bgs[i].range.start != want[i].start ||
            bgs[i].range.end != want[i].end) {
            return false;
        }
    }
    return true;
}

static bool RhUnpainted(RhView* v, gpui::TextLeafKey key) {
    return RhPainted(v, key, nullptr, 0);
}

static int RhFind(Str hay, const char* needle, bool last = false) {
    Str n = Str(needle);
    int found = -1;
    for (int i = 0; i + len(n) <= len(hay); i++) {
        if (memcmp(hay.s + i, n.s, (size_t)len(n)) == 0) {
            found = i;
            if (!last) break;
        }
    }
    return found;
}

using gpui::TextLeafKey;

// rendered_text_is_the_plain_copy_text.
static void RenderedTextIsThePlainCopyText() {
    RhView v;
    RhOpen(&v,
           "# Title\n\nhello **world** \\*\n\n- item\n\n| a | b |\n|---|---|\n"
           "| c | d |\n\n```\nlet x\n```");
    utassert(StrEq(RhState(&v)->RenderedText().AsStr(),
                   StrL("Title\nhello world *\nitem\na b\nc d\n\nlet x\n")));
    RhClose(&v);
}

// a_match_across_marks_paints_in_its_paragraph.
static void AMatchAcrossMarksPaintsInItsParagraph() {
    RhView v;
    RhOpen(&v, "hello **world**");
    Span range = {0, 11};
    utassert(RhSet(&v, &range, 1).IsOk());
    utassert(RhPainted(&v, TextLeafKey::Block(0), &range, 1));
    RhClose(&v);
}

// repeated_text_maps_to_the_occurrence_addressed.
static void RepeatedTextMapsToTheOccurrenceAddressed() {
    RhView v;
    RhOpen(&v, "foo\n\nfoo");
    int second = RhFind(RhState(&v)->RenderedText().AsStr(), "foo", true);
    Span range = {second, second + 3};
    utassert(RhSet(&v, &range, 1).IsOk());
    utassert(RhUnpainted(&v, TextLeafKey::Block(0)));
    Span want = {0, 3};
    utassert(RhPainted(&v, TextLeafKey::Block(5), &want, 1));
    RhClose(&v);
}

// a_range_across_blocks_skips_the_separator.
static void ARangeAcrossBlocksSkipsTheSeparator() {
    RhView v;
    RhOpen(&v, "ab\n\ncd");
    // "ab\ncd\n": 1..4 is "b\nc".
    Span range = {1, 4};
    utassert(RhSet(&v, &range, 1).IsOk());
    Span first = {1, 2};
    Span second = {0, 1};
    utassert(RhPainted(&v, TextLeafKey::Block(0), &first, 1));
    utassert(RhPainted(&v, TextLeafKey::Block(4), &second, 1));
    RhClose(&v);
}

// table_cells_and_code_blocks_are_leaves.
static void TableCellsAndCodeBlocksAreLeaves() {
    RhView v;
    const char* source = "| a | b |\n|---|---|\n| c | d |\n\n```\nlet x\n```";
    RhOpen(&v, source);
    // "a b\nc d\n\nlet x\n"
    Span ranges[] = {{0, 3}, {6, 7}, {9, 12}};
    utassert(RhSet(&v, ranges, 3).IsOk());
    Span one = {0, 1};
    utassert(RhPainted(&v, TextLeafKey::TableCell(0, 0), &one, 1));
    utassert(RhPainted(&v, TextLeafKey::TableCell(0, 1), &one, 1));
    utassert(RhPainted(&v, TextLeafKey::TableCell(0, 3), &one, 1));
    int codeStart = RhFind(Str(source), "```");
    Span code = {0, 3};
    utassert(RhPainted(&v, TextLeafKey::Block(codeStart), &code, 1));
    RhClose(&v);
}

// invalid_ranges_reject_the_whole_set.
static void InvalidRangesRejectTheWholeSet() {
    RhView v;
    // "中文\n\nab" renders "中文\nab\n".
    RhOpen(&v, "\xE4\xB8\xAD\xE6\x96\x87\n\nab");
    Span first = {0, 3};
    utassert(RhSet(&v, &first, 1).IsOk());
    Span invalid[] = {{6, 3}, {0, 1}, {0, 100}};
    for (Span range : invalid) {
        Span set[] = {{0, 3}, range};
        utassert(RhSet(&v, set, 2) == RangeHighlightError::InvalidRange(1));
    }
    // The rejected sets left the earlier highlight in place.
    utassert(RhPainted(&v, TextLeafKey::Block(0), &first, 1));
    RhClose(&v);
}

// text_outside_every_block_is_left_unpainted.
static void TextOutsideEveryBlockIsLeftUnpainted() {
    RhView v;
    const char* source =
        "foo one\n\n<div>foo two</div>\n\n| foo | x |\n|---|---|\n\nfoo "
        "three";
    RhOpen(&v, source);
    Str text = RhState(&v)->RenderedText().AsStr();
    // Every "foo" and " ", an empty range, and the separator after the
    // first block: the HTML block's text and the separators are not any
    // block's text, so they paint nothing, but nothing fails.
    Span ranges[64];
    int n = 0;
    for (int i = 0; i + 3 <= len(text); i++) {
        if (memcmp(text.s + i, "foo", 3) == 0) ranges[n++] = Span{i, i + 3};
    }
    for (int i = 0; i < len(text); i++) {
        if (text.s[i] == ' ') ranges[n++] = Span{i, i + 1};
    }
    ranges[n++] = Span{3, 3};
    ranges[n++] = Span{7, 8};
    utassert(RhSet(&v, ranges, n).IsOk());
    Span fooSpace[] = {{0, 3}, {3, 4}};
    utassert(RhPainted(&v, TextLeafKey::Block(0), fooSpace, 2));
    int table = RhFind(Str(source), "| foo");
    utassert(RhPainted(&v, TextLeafKey::TableCell(table, 0), fooSpace, 1));
    int last = RhFind(Str(source), "foo three");
    utassert(RhPainted(&v, TextLeafKey::Block(last), fooSpace, 2));
    RhClose(&v);
}

// a_later_highlight_paints_over_an_earlier_one.
static void ALaterHighlightPaintsOverAnEarlierOne() {
    RhView v;
    RhOpen(&v, "abcdef");
    Span ranges[] = {{0, 6}, {2, 3}};
    utassert(RhSet(&v, ranges, 2).IsOk());
    utassert(RhPainted(&v, TextLeafKey::Block(0), ranges, 2));
    RhClose(&v);
}

// html_text_is_unsupported.
static void HtmlTextIsUnsupported() {
    RhView v;
    RhOpen(&v, "<p>one</p>", true);
    Span range = {0, 3};
    utassert(RhSet(&v, &range, 1) == RangeHighlightError::Unsupported());
    RhClose(&v);
}

static bool ParseFormula(const markdown::Node* source,
                         const MarkdownParseContext* context, void*,
                         MarkdownNode* out) {
    if (source->kind != markdown::NodeKind::InlineMath) return false;
    *out = MarkdownNode::New(context->Copy(StrL("formula")))
               .Text(context->Copy(
                   context->Value(source, markdown::NodeStrKind::Value)));
    return true;
}

// inline_objects_are_skipped.
static void InlineObjectsAreSkipped() {
    RhView v;
    RhOpen(&v, "x $a$ y");
    Arena* ext = ArenaNew();
    MarkdownPlugin plugin;
    plugin.name = StrL("formula");
    plugin.parse = &ParseFormula;
    plugin.renderInline = &RenderInlineMath;
    MarkdownExtensions extensions;
    extensions.Plugin(ext, plugin);
    v.extensions = &extensions;
    RhRender(&v);
    // "x a y\n", where "a" is the formula.
    utassert(StrEq(RhState(&v)->RenderedText().AsStr(), StrL("x a y\n")));
    Span object = {2, 3};
    utassert(RhSet(&v, &object, 1).IsOk());
    utassert(RhUnpainted(&v, TextLeafKey::Block(0)));
    Span all = {0, 5};
    utassert(RhSet(&v, &all, 1).IsOk());
    Span around[] = {{0, 2}, {3, 5}};
    utassert(RhPainted(&v, TextLeafKey::Block(0), around, 2));
    RhClose(&v);
    ArenaDelete(ext);
}

// push_str_keeps_earlier_blocks_and_clips_the_changed_tail.
static void PushStrKeepsEarlierBlocksAndClipsTheChangedTail() {
    RhView v;
    RhOpen(&v, "first\n\na **b");
    // "first\na **b\n"
    Span ranges[] = {{0, 5}, {6, 11}};
    utassert(RhSet(&v, ranges, 2).IsOk());
    // Closing the emphasis renders the tail as "a bc".
    RhState(&v)->PushStr(StrL("c**"), &v.app, v.win);
    RhRender(&v);
    Span first = {0, 5};
    Span tail = {0, 2};
    utassert(RhPainted(&v, TextLeafKey::Block(0), &first, 1));
    utassert(RhPainted(&v, TextLeafKey::Block(7), &tail, 1));
    RhClose(&v);
}

// push_str_keeps_a_block_whose_text_is_unchanged.
static void PushStrKeepsABlockWhoseTextIsUnchanged() {
    RhView v;
    RhOpen(&v, "first\n\nsecond");
    Span range = {6, 12};
    utassert(RhSet(&v, &range, 1).IsOk());
    // A setext underline turns the paragraph into a heading at the same
    // source start, rendering the same text.
    RhState(&v)->PushStr(StrL("\n==="), &v.app, v.win);
    RhRender(&v);
    Span want = {0, 6};
    utassert(RhPainted(&v, TextLeafKey::Block(7), &want, 1));
    RhClose(&v);
}

// push_str_drops_highlights_of_a_leaf_that_is_gone.
static void PushStrDropsHighlightsOfALeafThatIsGone() {
    RhView v;
    RhOpen(&v, "first\n\n| a |");
    Span ranges[] = {{0, 5}, {6, 11}};
    utassert(RhSet(&v, ranges, 2).IsOk());
    // A delimiter row turns the paragraph into a table, whose text lives in
    // cells instead.
    RhState(&v)->PushStr(StrL("\n|---|"), &v.app, v.win);
    RhRender(&v);
    utassert(RhPainted(&v, TextLeafKey::Block(0), ranges, 1));
    utassert(RhUnpainted(&v, TextLeafKey::Block(7)));
    utassert(RhUnpainted(&v, TextLeafKey::TableCell(7, 0)));
    RhClose(&v);
}

// replacing_text_drops_highlights_only_where_it_changed.
static void ReplacingTextDropsHighlightsOnlyWhereItChanged() {
    RhView v;
    RhOpen(&v, "first\n\nsecond");
    Span ranges[] = {{0, 5}, {6, 12}};
    utassert(RhSet(&v, ranges, 2).IsOk());
    RhState(&v)->SetText(StrL("first\n\nchanged"), &v.app, v.win);
    RhRender(&v);
    utassert(RhPainted(&v, TextLeafKey::Block(0), ranges, 1));
    utassert(RhUnpainted(&v, TextLeafKey::Block(7)));

    // Rust replaces with MAX_SYNC_FULL_REPLACE_BYTES + 1 bytes to go
    // through its background parse; every parse here is the same one.
    char large[513];
    memset(large, 'x', sizeof(large) - 1);
    large[sizeof(large) - 1] = 0;
    RhState(&v)->SetText(Str(large), &v.app, v.win);
    RhRender(&v);
    utassert(RhState(&v)->rangeHighlights == nullptr);
    RhClose(&v);
}

// a_highlight_follows_its_block_past_an_earlier_edit.
static void AHighlightFollowsItsBlockPastAnEarlierEdit() {
    {
        RhView v;
        RhOpen(&v, "foo\n\nbar\n\nbar");
        // "foo\nbar\nbar\n": the first "bar", in the block at source 5.
        Span range = {4, 7};
        utassert(RhSet(&v, &range, 1).IsOk());
        // Deleting "foo" moves that "bar" to 0, and the second one to 5.
        RhState(&v)->SetText(StrL("bar\n\nbar"), &v.app, v.win);
        RhRender(&v);
        Span want = {0, 3};
        utassert(RhPainted(&v, TextLeafKey::Block(0), &want, 1));
        utassert(RhUnpainted(&v, TextLeafKey::Block(5)));
        RhClose(&v);
    }
    {
        RhView v;
        RhOpen(&v, "ERROR a\n\nERROR b\n\nERROR c");
        Span range = {8, 15};
        utassert(RhSet(&v, &range, 1).IsOk());
        RhState(&v)->SetText(StrL("ERROR b\n\nERROR c"), &v.app, v.win);
        RhRender(&v);
        Span want = {0, 7};
        utassert(RhPainted(&v, TextLeafKey::Block(0), &want, 1));
        utassert(RhUnpainted(&v, TextLeafKey::Block(9)));
        RhClose(&v);
    }
}

// appending_a_copy_of_the_last_block_keeps_the_highlight_on_it.
static void AppendingACopyOfTheLastBlockKeepsTheHighlightOnIt() {
    RhView v;
    RhOpen(&v, "a\n\nfoo");
    // "a\nfoo\n"
    Span range = {2, 5};
    utassert(RhSet(&v, &range, 1).IsOk());
    RhState(&v)->SetText(StrL("a\n\nfoo\n\nfoo"), &v.app, v.win);
    RhRender(&v);
    Span want = {0, 3};
    utassert(RhPainted(&v, TextLeafKey::Block(3), &want, 1));
    utassert(RhUnpainted(&v, TextLeafKey::Block(8)));
    RhClose(&v);
}

// deleting_a_table_row_drops_highlights_in_the_rows_it_moves.
static void DeletingATableRowDropsHighlightsInTheRowsItMoves() {
    const char* table = "| a | b |\n|---|---|\n| x | 1 |\n| x | 2 |";
    const char* withoutMiddleRow = "| a | b |\n|---|---|\n| x | 2 |";
    // "a b\nx 1\nx 2\n\n": the last row's cells, then the middle row's "x".
    Span lastRow[] = {{0, 1}, {8, 9}, {10, 11}};
    Span middle[] = {{0, 1}, {4, 5}};
    for (int round = 0; round < 2; round++) {
        RhView v;
        RhOpen(&v, table);
        utassert(round == 0 ? RhSet(&v, lastRow, 3).IsOk()
                            : RhSet(&v, middle, 2).IsOk());
        RhState(&v)->SetText(Str(withoutMiddleRow), &v.app, v.win);
        RhRender(&v);
        Span one = {0, 1};
        utassert(RhPainted(&v, TextLeafKey::TableCell(0, 0), &one, 1));
        for (int cell = 1; cell < 4; cell++) {
            utassert(RhUnpainted(&v, TextLeafKey::TableCell(0, cell)));
        }
        RhClose(&v);
    }
}

// range_highlight.rs long_and_wide_tables_remap_highlights_by_whole_rows
// (#3247). Every cell highlighted, then the last cell of the first data row
// edited: even the unchanged cells earlier in that row lose their highlights,
// as do the rows after it, and only the header keeps its own. Rust's fix
// collects each row's source end once instead of rescanning the table per
// cell; here the rendered index already records it on every cell leaf as the
// row is built, and a leaf is found by binary search, so the remap is linear
// either way. The row-index tests (table_row_index_keeps_empty_cells_in_
// their_row, table_row_index_finds_nested_tables_without_crossing_between_
// them, append_remapping_does_not_build_a_table_row_index) are about that
// Rust structure, which this tree does not have.
static void LongAndWideTablesRemapHighlightsByWholeRows() {
    const int shapes[][2] = {{4096, 1}, {2, 1024}, {64, 16}};
    Rgba color = HslaToRgba(HslaNew(0.15f, 1.f, 0.5f, 0.4f));
    for (const auto& shape : shapes) {
        int rows = shape[0];
        int columns = shape[1];
        StrBuilder row;
        StrBuilder separator;
        row.Append(StrL("|"));
        separator.Append(StrL("|"));
        for (int c = 0; c < columns; c++) {
            row.Append(StrL(" x |"));
            separator.Append(StrL("---|"));
        }
        row.AppendChar('\n');
        separator.AppendChar('\n');
        Str rowText = row.TakeStr();
        Str separatorText = separator.TakeStr();
        StrBuilder source;
        source.Append(rowText);
        source.Append(separatorText);
        for (int r = 0; r < rows; r++) {
            source.Append(rowText);
        }
        Str old = source.TakeStr();
        RhView v;
        RhOpen(&v, old.s);
        Str text = RhState(&v)->RenderedText().AsStr();
        Vec<RangeHighlight> highlights;
        for (int i = 0; i < len(text); i++) {
            if (text.s[i] == 'x') {
                VecAppend(highlights,
                          RangeHighlight::New(Span{i, i + 1}, color));
            }
        }
        utassert(len(highlights) == (rows + 1) * columns);
        utassert(RhState(&v)
                     ->SetRangeHighlights(highlights.els, len(highlights),
                                          &v.app, v.win)
                     .IsOk());
        utassert(len(RhState(&v)->rangeHighlights->leaves) ==
                 (rows + 1) * columns);
        Str changed = StrDup(old);
        int edit = len(rowText) + len(separatorText) + len(rowText) - 1;
        while (changed.s[edit] != 'x') {
            edit--;
        }
        ((char*)changed.s)[edit] = 'y';
        RhState(&v)->SetText(changed, &v.app, v.win);
        RhRender(&v);
        const gpui::RangeHighlightFrame* kept = RhState(&v)->rangeHighlights;
        utassert(kept && len(kept->leaves) == columns);
        Span one = {0, 1};
        for (int c = 0; c < columns; c++) {
            utassert(RhPainted(&v, TextLeafKey::TableCell(0, c), &one, 1));
        }
        StrFree(changed);
        StrFree(old);
        StrFree(rowText);
        StrFree(separatorText);
        RhClose(&v);
    }
}

// streaming_table_rows_keeps_the_highlights_of_earlier_rows.
static void StreamingTableRowsKeepsTheHighlightsOfEarlierRows() {
    RhView v;
    RhOpen(&v, "| a | b |\n|---|---|\n| x | 1 |");
    // "a b\nx 1\n\n"
    Span ranges[] = {{0, 1}, {4, 5}};
    utassert(RhSet(&v, ranges, 2).IsOk());
    RhState(&v)->SetText(StrL("| a | b |\n|---|---|\n| x | 1 |\n| y | 2 |"),
                         &v.app, v.win);
    RhRender(&v);
    Span one = {0, 1};
    utassert(RhPainted(&v, TextLeafKey::TableCell(0, 0), &one, 1));
    utassert(RhPainted(&v, TextLeafKey::TableCell(0, 2), &one, 1));
    RhClose(&v);
}

// an_edit_in_an_earlier_block_keeps_later_highlights.
static void AnEditInAnEarlierBlockKeepsLaterHighlights() {
    RhView v;
    RhOpen(&v, "one\n\ntwo");
    Span ranges[] = {{0, 3}, {4, 7}};
    utassert(RhSet(&v, ranges, 2).IsOk());
    RhState(&v)->SetText(StrL("one!\n\ntwo"), &v.app, v.win);
    RhRender(&v);
    Span want = {0, 3};
    utassert(RhPainted(&v, TextLeafKey::Block(0), &want, 1));
    utassert(RhPainted(&v, TextLeafKey::Block(6), &want, 1));
    RhClose(&v);
}

// streaming_through_set_text_keeps_highlights.
static void StreamingThroughSetTextKeepsHighlights() {
    RhView v;
    RhOpen(&v, "first\n\nsec");
    // "first\nsec\n"
    Span ranges[] = {{0, 5}, {6, 9}};
    utassert(RhSet(&v, ranges, 2).IsOk());
    RhState(&v)->SetText(StrL("first\n\nsecond"), &v.app, v.win);
    RhRender(&v);
    Span sec = {0, 3};
    utassert(RhPainted(&v, TextLeafKey::Block(0), ranges, 1));
    utassert(RhPainted(&v, TextLeafKey::Block(7), &sec, 1));
    RhClose(&v);
}

// a_full_parse_merged_with_an_append_compares_every_block, and
// an_append_after_a_full_update_compares_every_block: this runtime parses
// the whole document every time, so an append compares every block too.
static void AnAppendComparesEveryBlock() {
    RhView v;
    RhOpen(&v, "");
    RhState(&v)->SetText(StrL("[foo] and some text\n\nmore"), &v.app, v.win);
    RhRender(&v);
    int some = RhFind(RhState(&v)->RenderedText().AsStr(), "some");
    Span range = {some, some + 4};
    utassert(RhSet(&v, &range, 1).IsOk());
    // The definition turns the earlier `[foo]` into the link text `foo`.
    RhState(&v)->PushStr(StrL("\n\n[foo]: https://example.com"), &v.app, v.win);
    RhRender(&v);
    utassert(StrStartsWith(RhState(&v)->RenderedText().AsStr(),
                           StrL("foo and some")));
    utassert(RhUnpainted(&v, TextLeafKey::Block(0)));
    RhClose(&v);
}

// a_full_update_before_an_append_drops_replaced_highlights.
static void AFullUpdateBeforeAnAppendDropsReplacedHighlights() {
    RhView v;
    RhOpen(&v, "first");
    Span range = {0, 5};
    utassert(RhSet(&v, &range, 1).IsOk());
    RhState(&v)->SetText(StrL("xxxxxxxx"), &v.app, v.win);
    RhState(&v)->PushStr(StrL(" tail"), &v.app, v.win);
    RhRender(&v);
    utassert(RhState(&v)->rangeHighlights == nullptr);
    RhClose(&v);
}

// reparsing_unchanged_text_keeps_highlights, and the RenderedText identity
// the markdown example compares: a parse that rendered the same source with
// other extensions is a new snapshot, one that did not re-parse is not.
static void ReparsingUnchangedTextKeepsHighlights() {
    RhView v;
    RhOpen(&v, "first");
    Span range = {0, 5};
    utassert(RhSet(&v, &range, 1).IsOk());
    gpui::RenderedText before = RhState(&v)->RenderedText();
    RhRender(&v);
    utassert(RhState(&v)->RenderedText() == before);
    Arena* ext = ArenaNew();
    MarkdownPlugin plugin;
    plugin.name = StrL("formula");
    plugin.parse = &ParseFormula;
    plugin.renderInline = &RenderInlineMath;
    MarkdownExtensions extensions;
    extensions.Plugin(ext, plugin);
    v.extensions = &extensions;
    RhRender(&v);
    utassert(RhState(&v)->RenderedText() != before);
    utassert(RhPainted(&v, TextLeafKey::Block(0), &range, 1));
    RhClose(&v);
    ArenaDelete(ext);
}

static int RhWashedElements(El* e) {
    if (!e) return 0;
    int n = e->nWashes > 0 ? 1 : 0;
    for (El* c = e->first; c; c = c->next) n += RhWashedElements(c);
    return n;
}

// highlights_paint_across_inline_code_tables_and_code_blocks: every
// character, part of the inline code and the whole text highlighted, and the
// view still builds, with the washes on its text elements.
static void HighlightsPaintAcrossInlineCodeTablesAndCodeBlocks() {
    RhView v;
    RhOpen(&v,
           "wrapping text with `inline code` and a [link](https://x.y) that "
           "wraps\n\n| a | b |\n|---|---|\n| c | d |\n\n```\nlet x = 1;\n```");
    Str text = RhState(&v)->RenderedText().AsStr();
    int code = RhFind(text, "code");
    Vec<RangeHighlight> highlights;
    Rgba color = HslaToRgba(HslaNew(0.15f, 1.f, 0.5f, 0.4f));
    VecAppend(highlights, RangeHighlight::New(Span{code, code + 2}, color));
    for (int i = 0; i < len(text); i++) {
        char c = text.s[i];
        if (c != ' ' && c != '\n') {
            VecAppend(highlights, RangeHighlight::New(Span{i, i + 1}, color));
        }
    }
    VecAppend(highlights, RangeHighlight::New(Span{0, len(text)}, color));
    utassert(
        RhState(&v)
            ->SetRangeHighlights(highlights.els, len(highlights), &v.app, v.win)
            .IsOk());
    El* root = gpui::TextView::New(&v.cx, v.state)->Selectable()->IntoEl();
    utassert(RhWashedElements(root) > 0);
    int n = 0;
    const gpui::RangeBackground* bgs =
        RhState(&v)->rangeHighlights->Backgrounds(TextLeafKey::Block(0), &n);
    utassert(n > 0 && bgs[0].range.start == code &&
             bgs[0].range.end == code + 2);
    RhClose(&v);
}

// clear_range_highlights_removes_them.
static void ClearRangeHighlightsRemovesThem() {
    RhView v;
    RhOpen(&v, "first");
    Span range = {0, 5};
    utassert(RhSet(&v, &range, 1).IsOk());
    RhState(&v)->ClearRangeHighlights(&v.app, v.win);
    utassert(RhState(&v)->rangeHighlights == nullptr);
    RhClose(&v);
}

// ─── reveal_range ─────────────────────────────────────────────────────────
//
// state.rs `mod reveal_range` and range_highlight.rs. Rust drives a window
// through TestAppContext and reads where lists scrolled; the frame here marks
// the text the reveal starts in, paint reports where that text landed, and
// the next frame reads the report. The tests hand the report in themselves.

static Span RhRangeOf(RhView* v, const char* needle) {
    Str text = RhState(v)->RenderedText().AsStr();
    int start = RhFind(text, needle);
    return Span{start, start + (int)strlen(needle)};
}

// The element the frame marked to report the reveal's line, or null.
static El* RevealMarked(El* e, const Bounds* out) {
    if (!e) return nullptr;
    if (e->rangeOut == out || e->boundsOut == out) return e;
    for (El* c = e->first; c; c = c->next) {
        if (El* found = RevealMarked(c, out)) return found;
    }
    return nullptr;
}

static El* RhRenderScrollable(RhView* v) {
    return gpui::TextView::New(&v->cx, v->state)->Scrollable()->IntoEl();
}

// The scroll box a scrollable view puts its document in, the way
// text.cpp names it.
static int RhScrollKey(RhView* v) {
    uint32_t name = (uint32_t)(v->state.id.index + 1) * 1000003u +
                    (uint32_t)(v->state.id.gen + 1);
    return (int)KeyedKey(name,
                         (uint32_t)HashClickId(StrL("TextViewScrollState")));
}

// range_highlight.rs locate, and
// a_position_in_an_inline_object_moves_onto_text: a range starts on its first
// leaf text; one covering none on the line of the text at or before its start
// in its block; one in text outside every leaf on its whole block; one inside
// an inline object on the text after it.
static void ARevealStartsOnTheLineOfItsRange() {
    RhView v;
    RhOpen(&v, "hello **world**\n\n<div>html text</div>\n\nlast");
    gpui::TextViewState* s = RhState(&v);
    Span world = RhRangeOf(&v, "world");
    utassert(s->RevealRange(world, &v.app, v.win).IsOk());
    utassert(s->reveal.pending && !s->reveal.block);
    utassert(s->reveal.key == TextLeafKey::Block(0) && s->reveal.offset == 6);
    // The separator after the first block: the line of its last character.
    Span after = {11, 11};
    utassert(s->RevealRange(after, &v.app, v.win).IsOk());
    utassert(!s->reveal.block && s->reveal.offset == 10);
    // The HTML block's text belongs to no leaf.
    utassert(s->RevealRange(RhRangeOf(&v, "html text"), &v.app, v.win).IsOk());
    utassert(s->reveal.block && s->reveal.blockIx == 1);
    RhClose(&v);

    RhView objects;
    RhOpen(&objects, "x $a$ y");
    Arena* ext = ArenaNew();
    MarkdownPlugin plugin;
    plugin.name = StrL("formula");
    plugin.parse = &ParseFormula;
    plugin.renderInline = &RenderInlineMath;
    MarkdownExtensions extensions;
    extensions.Plugin(ext, plugin);
    objects.extensions = &extensions;
    RhRender(&objects);
    // "x a y\n", where "a" is the formula: onto the text after it.
    gpui::TextViewState* o = RhState(&objects);
    utassert(o->RevealRange(Span{2, 2}, &objects.app, objects.win).IsOk());
    utassert(!o->reveal.block && o->reveal.offset == 3);
    RhClose(&objects);
    ArenaDelete(ext);
}

// malformed_ranges_and_html_views_are_rejected and
// an_empty_view_has_nothing_to_reveal.
static void MalformedRangesAndHtmlViewsAreRejected() {
    RhView v;
    RhOpen(&v, "first\n\nsecond");
    gpui::TextViewState* s = RhState(&v);
    utassert(s->RevealRange(Span{5, 3}, &v.app, v.win) ==
             RangeHighlightError::InvalidRange(0));
    utassert(s->RevealRange(Span{0, 100}, &v.app, v.win) ==
             RangeHighlightError::InvalidRange(0));
    utassert(s->RevealRange(Span{0, 5}, &v.app, v.win).IsOk());
    RhClose(&v);

    RhView html;
    RhOpen(&html, "<p>one</p>", true);
    utassert(RhState(&html)->RevealRange(Span{0, 3}, &html.app, html.win) ==
             RangeHighlightError::Unsupported());
    RhClose(&html);

    RhView empty;
    RhOpen(&empty, "");
    utassert(
        RhState(&empty)->RevealRange(Span{0, 0}, &empty.app, empty.win).IsOk());
    utassert(!RhState(&empty)->reveal.pending);
    RhClose(&empty);
}

// a_scrollable_view_scrolls_to_a_line_inside_a_long_paragraph and
// revealing_a_visible_line_does_not_scroll: the frame marks the word the
// range starts in; a report below the viewport scrolls it just into view,
// and one inside it ends the reveal.
static void AScrollableViewScrollsToItsLine() {
    RhView v;
    RhOpen(&v, "w0 w1 w2 `c3` w4 w5");
    gpui::TextViewState* s = RhState(&v);
    utassert(s->RevealRange(RhRangeOf(&v, "w4"), &v.app, v.win).IsOk());
    El* root = RhRenderScrollable(&v);
    El* marked = RevealMarked(root, &s->reveal.line);
    utassert(marked && StrStartsWith(marked->text, StrL("w4")));
    utassert(marked->rangeOutLo == 0 && marked->rangeOutHi == 1);

    ScrollRect viewport;
    viewport.id = RhScrollKey(&v);
    viewport.bounds = {0, 0, 200, 100};
    viewport.contentH = 5000;
    VecAppend(v.win->prevScrolls, viewport);
    // Painted below the viewport: the least scroll that shows it.
    s->reveal.line = {10, 2000, 20, 20};
    RhRenderScrollable(&v);
    utassertnear(s->scrollY, 1920.f);
    utassert(s->reveal.pending && s->reveal.attempts == 1);
    // Painted inside it: shown, and nothing moves.
    s->reveal.line = {10, 80, 20, 20};
    RhRenderScrollable(&v);
    utassert(!s->reveal.pending);
    utassertnear(s->scrollY, 1920.f);
    VecReset(v.win->prevScrolls);
    RhClose(&v);
}

static int gRevealCalls = 0;
static Bounds gRevealLine = {};
struct RevealProbe {
    static void OnReveal(RevealProbe*, Ctx*,
                         const gpui::TextViewRevealEvent* ev) {
        gRevealCalls++;
        gRevealLine = ev->line;
    }
};

// on_reveal_scrolls_a_container_that_ignores_scroll_requests: a fit-content
// view hands a hidden line to on_reveal, judged against the scroll box
// around it.
static void OnRevealHearsAHiddenLine() {
    RhView v;
    RhOpen(&v, "one\n\ntwo");
    gpui::TextViewState* s = RhState(&v);
    Entity<RevealProbe> probe = EntityNewState<RevealProbe>(&v.app);
    gRevealCalls = 0;
    utassert(s->RevealRange(RhRangeOf(&v, "two"), &v.app, v.win).IsOk());
    gpui::TextView::New(&v.cx, v.state)
        ->OnReveal(ListenTo(probe, &RevealProbe::OnReveal))
        ->IntoEl();
    ScrollRect around;
    around.bounds = {0, 0, 300, 100};
    around.contentH = 400;
    VecAppend(v.win->prevScrolls, around);
    s->reveal.view = {10, 0, 200, 400};
    s->reveal.line = {10, 300, 20, 20};
    gpui::TextView::New(&v.cx, v.state)
        ->OnReveal(ListenTo(probe, &RevealProbe::OnReveal))
        ->IntoEl();
    utassert(gRevealCalls == 1 && gRevealLine.y == 300.f);
    utassert(s->reveal.pending);
    VecReset(v.win->prevScrolls);
    RhClose(&v);
}

// a_reveal_that_cannot_be_shown_gives_up, a_reveal_not_carried_out_in_time_
// is_dropped and a_clamped_view_does_not_reveal.
static void ARevealGivesUp() {
    RhView v;
    RhOpen(&v, "one\n\ntwo");
    gpui::TextViewState* s = RhState(&v);
    // Hidden frame after frame, with nothing to scroll.
    utassert(s->RevealRange(RhRangeOf(&v, "two"), &v.app, v.win).IsOk());
    for (int i = 0; i < 10 && s->reveal.pending; i++) {
        RhRender(&v);
        s->reveal.line = {0, 5000, 10, 10};
    }
    utassert(!s->reveal.pending);
    // Not carried out within a second.
    utassert(s->RevealRange(RhRangeOf(&v, "two"), &v.app, v.win).IsOk());
    s->reveal.requestedAt -= 2.0;
    RhRender(&v);
    utassert(!s->reveal.pending);
    // A view clamped to its first lines does not reveal.
    utassert(s->RevealRange(RhRangeOf(&v, "two"), &v.app, v.win).IsOk());
    gpui::TextView::New(&v.cx, v.state)->MaxLines(1)->IntoEl();
    utassert(!s->reveal.pending);
    RhClose(&v);
}

// a_reveal_follows_its_text_past_an_edit_before_it and
// a_reveal_is_dropped_when_its_text_before_it_changes.
static void ARevealFollowsItsText() {
    RhView v;
    RhOpen(&v, "first words then the target\n\nlast");
    gpui::TextViewState* s = RhState(&v);
    // A paragraph inserted above moves it down one block.
    utassert(s->RevealRange(RhRangeOf(&v, "target"), &v.app, v.win).IsOk());
    s->SetText(StrL("inserted\n\nfirst words then the target\n\nlast"), &v.app,
               v.win);
    RhRender(&v);
    utassert(s->reveal.pending && s->reveal.key == TextLeafKey::Block(10));
    // Appending to its paragraph keeps it.
    s->SetText(StrL("inserted\n\nfirst words then the target and more\n\nlast"),
               &v.app, v.win);
    RhRender(&v);
    utassert(s->reveal.pending);
    // An edit before it in its paragraph drops it.
    s->SetText(StrL("inserted\n\nfirst WORDS then the target and more\n\nlast"),
               &v.app, v.win);
    RhRender(&v);
    utassert(!s->reveal.pending);
    RhClose(&v);
}

#endif

// state.rs an_append_adding_blocks_keeps_the_scroll_position (#3261):
// streaming a new paragraph into a scrolled view leaves the scroll where it
// was. Upstream had reset its ListState on every append; the scroll box here
// keeps its offset on the state, and an append does not touch it.
static void AnAppendAddingBlocksKeepsTheScrollPosition() {
    RhView v;
    RhOpen(&v, "one\n\ntwo\n\nthree");
    gpui::TextViewState* s = RhState(&v);
    RhRenderScrollable(&v);
    s->scrollY = 120;
    s->PushStr(StrL("\n\nfour"), &v.app, v.win);
    RhRenderScrollable(&v);
    utassertnear(s->scrollY, 120.f);
    RhClose(&v);
}

// ─── text/state.rs window tests ───────────────────────────────────────────
//
// state.rs mod tests drive a TextViewState through TestAppContext: a parse
// lands when the background executor runs, and run_until_parked is what
// waits for it. The parse here is synchronous inside TextView::IntoEl
// (port-status.md "TextView range highlights land with the render"), so each
// state is opened in a test-platform window (gpui/test_app.h) whose root
// renders it, and a parse lands with the frame the update's flush draws.

#if GPUI_MARKDOWN_FULL

// Where the view sits: state.rs reveal_range's Container, less the
// application `list` this tree has no request_autoscroll for.
enum class TswContainer : uint8_t {
    // The whole window, fit-content: what a state with no view is drawn in.
    Window,
    // A 200×100 scrollable view.
    Scrollable,
    // A 200×100 scrolling div around a fit-content view that follows
    // reveals through OnReveal.
    Div,
    // The same, with the view clamped to two lines.
    Clamped,
    // A 200×100 box that scrolls nothing.
    Fixed,
    // A stateless TextView::markdown(source) over `source`, whose keyed
    // state is what the test reads.
    Element,
};

struct TswRoot {
    Entity<gpui::TextViewState> state = {};
    TswContainer container = TswContainer::Window;
    const MarkdownExtensions* extensions = nullptr;
    // Container::Div's ScrollHandle, positive down, and the scroll box last
    // frame painted.
    float divScrollY = 0;
    Bounds divBounds = {};
    // Container::Element's source string.
    Str source = {};
    // Last frame's scroll box of a scrollable view and the document inside it,
    // for scroll_top. Frame-arena memory, read only between frames.
    El* scroller = nullptr;

    // Container::Div's on_reveal: the scroll handle moved the least that
    // shows the line.
    static void OnReveal(TswRoot* self, Ctx*,
                         const gpui::TextViewRevealEvent* ev) {
        Bounds viewport = self->divBounds;
        Bounds line = ev->line;
        if (line.y + line.h > viewport.y + viewport.h) {
            self->divScrollY += line.y + line.h - (viewport.y + viewport.h);
        } else if (line.y < viewport.y) {
            self->divScrollY -= viewport.y - line.y;
        }
    }

    static El* Render(TswRoot* self, Ctx* cx) {
        Arena* a = cx->a;
        self->scroller = nullptr;
        if (self->container == TswContainer::Element) {
            gpui::TextView* view = gpui::TextView::New(cx, self->source);
            El* e = view->IntoEl();
            self->state = view->state;
            return Div(a)->SizeFull()->Child(e);
        }
        gpui::TextView* view = gpui::TextView::New(cx, self->state);
        // The element hands its selection format to the state every frame,
        // as Rust's does; Rust's tests render no element, so the state's own
        // format stands, and the element here passes it through.
        if (gpui::TextViewState* s = self->state.Get(cx)) {
            view->SelFormat(s->selectionFormat);
        }
        if (self->extensions) {
            view->MarkdownExtensionsSet(*self->extensions);
        }
        if (self->container == TswContainer::Window) {
            return Div(a)->SizeFull()->Child(view->IntoEl());
        }
        El* frame = Div(a)->W(200)->H(100);
        switch (self->container) {
            case TswContainer::Scrollable: {
                El* e = view->Scrollable()->IntoEl();
                self->scroller = e;
                return frame->Child(e);
            }
            case TswContainer::Div:
            case TswContainer::Clamped: {
                if (self->container == TswContainer::Clamped) {
                    view->MaxLines(2);
                }
                view->OnReveal(Listen(cx, &TswRoot::OnReveal));
                // The view first: on_reveal runs while it is built, and the
                // offset it set is the one this frame lays out with, as the
                // handle Rust's set_offset moves is read at layout.
                El* text = view->IntoEl();
                El* scroll = Div(a)
                                 ->PathId(StrL("scroll"))
                                 ->SizeFull()
                                 ->ScrollY(self->divScrollY)
                                 ->BoundsOut(&self->divBounds)
                                 ->Child(text);
                return frame->Child(scroll);
            }
            default:
                return frame->Child(view->IntoEl());
        }
    }
};

struct TswView {
    App* app = nullptr;
    Window* win = nullptr;
    Entity<TswRoot> root = {};
    bool wasReduced = false;

    TswRoot* Root() const { return root.Get(app); }
    gpui::TextViewState* State() const { return Root()->state.Get(app); }
};

static TswView TswOpen(const char* text, TswContainer container,
                       bool html = false) {
    TswView v;
    v.wasReduced = MotionReduced();
    MotionSetReduced(false);
    v.app = TestAppNew();
    v.root = EntityNew<TswRoot>(v.app);
    TswRoot* root = v.Root();
    root->container = container;
    if (container == TswContainer::Element) {
        root->source = Str(text);
    } else {
        root->state = html ? gpui::TextViewState::Html(v.app, Str(text))
                           : gpui::TextViewState::Markdown(v.app, Str(text));
    }
    v.win = TestWindowOpen(v.app, v.root);
    TestRunUntilParked(v.app);
    return v;
}

static void TswClose(TswView* v) {
    TestAppFree(v->app);
    MotionSetReduced(v->wasReduced);
    *v = TswView{};
}

// The end of a `state.update(cx, ..)`: the state notified, and the flush
// draws the frame the parse lands in.
static void TswFlush(const TswView& v) {
    AppInvalidate(v.win);
    TestFlushEffects(v.app);
}

static void TswPushStr(const TswView& v, const char* text) {
    v.State()->PushStr(Str(text), v.app, v.win);
    TswFlush(v);
}

static void TswSetText(const TswView& v, Str text) {
    v.State()->SetText(text, v.app, v.win);
    TswFlush(v);
}

static Str TswJoin(Arena* a, const char* format, int count, const char* sep) {
    StrBuilder sb(a);
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            sb.Append(Str(sep));
        }
        sb.Append(Str(fmt(format, i)));
    }
    return sb.TakeStr();
}

// reveal_range's paragraphs(count) and words(count).
static Str TswParagraphs(Arena* a, int count) {
    return TswJoin(a, "paragraph %d", count, "\n\n");
}

static Str TswWords(Arena* a, int count) {
    return TswJoin(a, "w%d", count, " ");
}

// ─── stream_fade ──────────────────────────────────────────────────────────

// stream_fade's FADE: long enough that the first frame is sampled before
// the fade gets anywhere.
static const float kTswFadeMs = 10000.f;

// fading_state: a view whose motion fades streamed text in over FADE,
// linearly.
static TswView TswFadingState(const char* markdown) {
    TswView v = TswOpen(markdown, TswContainer::Window);
    v.State()->SetMotion(TextViewMotion{}
                             .WithStreamFade(kTswFadeMs)
                             .WithStreamFadeEasing(Easing::Linear()),
                         v.app, v.win);
    TswFlush(v);
    TestRunUntilParked(v.app);
    return v;
}

// fades: the ranges leaf `key` fades over right now, every one of them
// still nearly transparent; false when nothing is fading in it.
static bool TswFades(const TswView& v, TextLeafKey key, Span* out, int* n,
                     double at = -1) {
    Arena* a = ArenaNew();
    gpui::StreamFadeRange* ranges = nullptr;
    int count = v.State()
                    ->StreamFadeFrame(a, at < 0 ? TestClockNow() : at, &ranges);
    *n = 0;
    for (int i = 0; i < count; i++) {
        if (!(ranges[i].key == key)) {
            continue;
        }
        // "a fade sampled right after it starts is still transparent"
        utassert(ranges[i].fadeOut > 0.9f);
        out[(*n)++] = ranges[i].range;
    }
    ArenaDelete(a);
    return *n > 0;
}

static bool TswFadesAre(const TswView& v, TextLeafKey key, const Span* want,
                        int count) {
    Span got[16];
    int n = 0;
    TswFades(v, key, got, &n);
    if (n != count) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (got[i].start != want[i].start || got[i].end != want[i].end) {
            return false;
        }
    }
    return true;
}

static bool TswNothingFades(const TswView& v, double at = -1) {
    Arena* a = ArenaNew();
    gpui::StreamFadeRange* ranges = nullptr;
    int count = v.State()
                    ->StreamFadeFrame(a, at < 0 ? TestClockNow() : at, &ranges);
    ArenaDelete(a);
    return count == 0;
}

// state.rs push_str_fades_only_the_appended_text
static void PushStrFadesOnlyTheAppendedText() {
    TswView v = TswFadingState("hello");
    TswPushStr(v, " world");
    TestRunUntilParked(v.app);
    Span want = {5, 11};
    utassert(TswFadesAre(v, TextLeafKey::Block(0), &want, 1));

    double later = TestClockNow() + kTswFadeMs / 1000.0 + 1.0;
    utassert(TswNothingFades(v, later));
    utassert(TswNothingFades(v));
    TswClose(&v);
}

// state.rs set_text_extending_the_text_fades_like_push_str
static void SetTextExtendingTheTextFadesLikePushStr() {
    TswView v = TswFadingState("hello");
    TswSetText(v, StrL("hello world"));
    TestRunUntilParked(v.app);
    Span want = {5, 11};
    utassert(TswFadesAre(v, TextLeafKey::Block(0), &want, 1));
    TswClose(&v);
}

// state.rs set_text_replacing_the_text_shows_it_at_once
static void SetTextReplacingTheTextShowsItAtOnce() {
    TswView v = TswFadingState("hello");
    TswPushStr(v, " world");
    TestRunUntilParked(v.app);
    Span got[4];
    int n = 0;
    utassert(TswFades(v, TextLeafKey::Block(0), got, &n));

    TswSetText(v, StrL("other"));
    TestRunUntilParked(v.app);
    utassert(!TswFades(v, TextLeafKey::Block(0), got, &n));
    TswClose(&v);
}

// state.rs completed_markup_refades_from_the_divergence
static void CompletedMarkupRefadesFromTheDivergence() {
    // The paragraph renders `text` (trailing space trimmed), then
    // `text **bo` literally.
    TswView v = TswFadingState("text ");
    TswPushStr(v, "**bo");
    TestRunUntilParked(v.app);
    Span first = {4, 9};
    utassert(TswFadesAre(v, TextLeafKey::Block(0), &first, 1));

    // `text **bold**` renders `text bold`: the space keeps its fade, the
    // glyphs from byte 5 on changed and fade again as one run.
    TswPushStr(v, "ld**");
    TestRunUntilParked(v.app);
    Span both[] = {{4, 5}, {5, 9}};
    utassert(TswFadesAre(v, TextLeafKey::Block(0), both, 2));
    TswClose(&v);
}

// state.rs without_stagger_an_update_fades_as_one_chunk
static void WithoutStaggerAnUpdateFadesAsOneChunk() {
    TswView v = TswFadingState("hello");
    TswPushStr(v, " one two three");
    TestRunUntilParked(v.app);
    Span want = {5, 19};
    utassert(TswFadesAre(v, TextLeafKey::Block(0), &want, 1));
    TswClose(&v);
}

// state.rs words_of_one_update_start_one_after_another
static void WordsOfOneUpdateStartOneAfterAnother() {
    const float stagger = 100.f;
    TswView v = TswFadingState("hello");
    v.State()->SetMotion(TextViewMotion{}
                             .WithStreamFade(kTswFadeMs)
                             .WithStreamFadeStagger(stagger)
                             .WithStreamFadeEasing(Easing::Linear()),
                         v.app, v.win);
    TswPushStr(v, " one two three");
    TestRunUntilParked(v.app);

    Arena* a = ArenaNew();
    gpui::StreamFadeRange* ranges = nullptr;
    int n = v.State()->StreamFadeFrame(
        a, TestClockNow() + 3.0 * stagger / 1000.0, &ranges);
    // "words still fading", "paragraph fades"
    utassert(n == 3);
    if (n == 3) {
        const int want[3][2] = {{5, 10}, {10, 14}, {14, 19}};
        for (int i = 0; i < 3; i++) {
            utassert(ranges[i].key == TextLeafKey::Block(0));
            utassert(ranges[i].range.start == want[i][0] &&
                     ranges[i].range.end == want[i][1]);
        }
        // A later word has faded less, so it is still more transparent.
        utassert(ranges[0].fadeOut < ranges[1].fadeOut &&
                 ranges[1].fadeOut < ranges[2].fadeOut);
    }
    ArenaDelete(a);
    TswClose(&v);
}

// state.rs a_new_paragraph_fades_as_a_whole
static void ANewParagraphFadesAsAWhole() {
    TswView v = TswFadingState("first");
    TswPushStr(v, "\n\nsecond");
    TestRunUntilParked(v.app);
    Span got[4];
    int n = 0;
    utassert(!TswFades(v, TextLeafKey::Block(0), got, &n));
    Span want = {0, 6};
    utassert(TswFadesAre(v, TextLeafKey::Block(7), &want, 1));
    TswClose(&v);
}

// state.rs code_block_text_fades_by_block
static void CodeBlockTextFadesByBlock() {
    TswView v = TswFadingState("```rs\nlet");
    TswPushStr(v, " x");
    TestRunUntilParked(v.app);
    Span want = {3, 5};
    utassert(TswFadesAre(v, TextLeafKey::Block(0), &want, 1));
    TswClose(&v);
}

// state.rs table_cells_fade_by_ordinal
static void TableCellsFadeByOrdinal() {
    TswView v = TswFadingState("| a | b |\n|---|---|\n| c | d");
    TswPushStr(v, "e |");
    TestRunUntilParked(v.app);
    Span got[4];
    int n = 0;
    utassert(!TswFades(v, TextLeafKey::TableCell(0, 2), got, &n));
    Span want = {1, 2};
    utassert(TswFadesAre(v, TextLeafKey::TableCell(0, 3), &want, 1));
    TswClose(&v);
}

// state.rs zero_duration_records_nothing
static void ZeroDurationRecordsNothing() {
    TswView v = TswOpen("hello", TswContainer::Window);
    TswPushStr(v, " world");
    TestRunUntilParked(v.app);
    utassert(TswNothingFades(v));
    TswClose(&v);
}

// state.rs reduced_motion_drops_the_fade
static void ReducedMotionDropsTheFade() {
    TswView v = TswFadingState("hello");
    TswPushStr(v, " world");
    TestRunUntilParked(v.app);
    // Rust passes `reduce_motion` to frame(); here it is the process-wide
    // setting the frame reads.
    MotionSetReduced(true);
    utassert(TswNothingFades(v));
    MotionSetReduced(false);
    utassert(TswNothingFades(v));
    TswClose(&v);
}

// state.rs a_fade_repaints_on_a_timer_until_nothing_fades. Rust counts the
// state's notifies; each one here draws a frame, so the frames are counted.
static void AFadeRepaintsOnATimerUntilNothingFadesInAWindow() {
    // STREAM_FADE_TICK.
    const double tick = 33;
    TswView v = TswFadingState("hello");
    TswPushStr(v, " world");
    TestRunUntilParked(v.app);
    TestDraw(v.win);
    uint64_t before = v.win->frameSeq;

    TestAdvanceClock(v.app, tick / 2);
    TestRunUntilParked(v.app);
    utassert(v.win->frameSeq == before);

    // Each tick repaints once, and that frame schedules the next tick.
    for (uint64_t ticks = 1; ticks <= 3; ticks++) {
        TestAdvanceClock(v.app, tick);
        TestRunUntilParked(v.app);
        utassert(v.win->frameSeq == before + ticks);
    }

    // Replacing the text drops the fade, so the ticks stop.
    TswSetText(v, StrL("other"));
    TestRunUntilParked(v.app);
    TestAdvanceClock(v.app, tick);
    TestRunUntilParked(v.app);
    uint64_t after = v.win->frameSeq;
    TestAdvanceClock(v.app, tick * 10);
    TestRunUntilParked(v.app);
    utassert(v.win->frameSeq == after);
    utassert(v.State()->fadeTick == 0);
    TswClose(&v);
}

// ─── parsing ──────────────────────────────────────────────────────────────

// state.rs small_full_replace_parses_before_background_executor_runs and
// large_markdown_and_html_full_replacements_wait_for_background_executor:
// not ported — every parse here is the synchronous one inside
// TextView::IntoEl, so there is no MAX_SYNC_FULL_REPLACE_BYTES and no
// background parse to wait for (port-status.md "TextView range highlights
// land with the render").

static bool ParseFormulaText(const markdown::Node* source,
                             const MarkdownParseContext* context, void*,
                             MarkdownNode* out) {
    if (source->kind != markdown::NodeKind::InlineMath) return false;
    *out = MarkdownNode::New(context->Copy(StrL("formula")))
               .Text(context->Copy(
                   context->Value(source, markdown::NodeStrKind::Value)));
    return true;
}

// state.rs inline_source_ranges_follow_streamed_tail_reparsing
static void InlineSourceRangesFollowStreamedTailReparsing() {
    // "中文 $a$\n\n尾 $x"
    TswView v = TswOpen("\xE4\xB8\xAD\xE6\x96\x87 $a$\n\n\xE5\xB0\xBE $x",
                        TswContainer::Window);
    Arena* ext = ArenaNew();
    MarkdownPlugin plugin;
    plugin.name = StrL("test");
    plugin.parse = &ParseFormulaText;
    plugin.renderInline = &RenderInlineMath;
    MarkdownExtensions extensions;
    extensions.Plugin(ext, plugin);
    v.Root()->extensions = &extensions;
    TswFlush(v);
    TestRunUntilParked(v.app);
    TswPushStr(v, "^2$ \xE5\x90\x8E");
    TestRunUntilParked(v.app);

    const char* source =
        "\xE4\xB8\xAD\xE6\x96\x87 $a$\n\n\xE5\xB0\xBE $x^2$ \xE5\x90\x8E";
    utassert(StrEq(v.State()->Source(), Str(source)));
    Arena* a = ArenaNew();
    Ctx cx = {v.app, v.win, a, {}};
    MdNode* doc =
        MdParseCachedForTest(&cx, a, v.State()->Source(), &extensions);
    const MarkdownNode* objects[4] = {};
    int n = 0;
    for (MdNode* block = doc ? doc->first : nullptr; block;
         block = block->next) {
        utassert(block->kind == MdKind::Paragraph);
        for (MdRun* r = block->runFirst; r; r = r->next) {
            if (r->hasCustom && n < 4) {
                objects[n++] = &r->custom;
            }
        }
    }
    utassert(n == 2);
    const char* markdowns[] = {"$a$", "$x^2$"};
    for (int i = 0; i < n && i < 2; i++) {
        int start = (int)(strstr(source, markdowns[i]) - source);
        int end = start + (int)strlen(markdowns[i]);
        utassert(objects[i]->hasSpan && objects[i]->span.start == start &&
                 objects[i]->span.end == end);
        utassert(StrEq(objects[i]->markdown, Str(markdowns[i])));
    }
    ArenaDelete(a);
    TswClose(&v);
    ArenaDelete(ext);
}

// state.rs async_full_replace_then_push_str_preserves_complete_source. Rust
// replaces with MAX_SYNC_FULL_REPLACE_BYTES + 1 bytes to go through its
// background parse; the same size goes through this tree's only parse.
static void AsyncFullReplaceThenPushStrPreservesCompleteSource() {
    TswView v = TswOpen("old", TswContainer::Window);
    Arena* a = ArenaNew();
    StrBuilder replacement(a);
    for (int i = 0; i < 512 + 1; i++) {
        replacement.AppendChar('x');
    }
    Str big = replacement.TakeStr();
    StrBuilder expected(a);
    expected.Append(big);
    expected.Append(StrL(" tail"));
    Str want = expected.TakeStr();
    v.State()->SetText(big, v.app, v.win);
    v.State()->PushStr(StrL(" tail"), v.app, v.win);
    TswFlush(v);
    TestRunUntilParked(v.app);
    utassert(StrEq(v.State()->text, want));
    utassert(StrEq(v.State()->Source(), want));
    ArenaDelete(a);
    TswClose(&v);
}

// state.rs html_push_str_keeps_earlier_blocks
static void HtmlPushStrKeepsEarlierBlocks() {
    TswView v = TswOpen("<p>first</p>", TswContainer::Window, true);
    TswPushStr(v, "<p>second</p>");
    TestRunUntilParked(v.app);
    utassert(StrEq(v.State()->Source(), StrL("<p>first</p><p>second</p>")));
    Str text = v.State()->RenderedText().AsStr();
    // "lost the first block", "lost the appended block"
    utassert(base::StrContains(text, StrL("first")));
    utassert(base::StrContains(text, StrL("second")));
    TswClose(&v);
}

// state.rs element_text_of_the_same_string_is_not_compared_again. Rust calls
// set_element_text on a state of its own; here the stateless element is what
// hands its string to the keyed state, so the element is rendered with it.
static void ElementTextOfTheSameStringIsNotComparedAgain() {
    static const char kHello[] = "hello";
    TswView v = TswOpen(kHello, TswContainer::Element);
    uint64_t parsed = v.State() ? v.State()->revision : 0;
    utassert(parsed > 0);

    // The same allocation again, and equal bytes in another allocation,
    // both leave the parsed text alone.
    TswFlush(v);
    utassert(v.State()->revision == parsed);
    char equal[] = "hello";
    v.Root()->source = Str(equal);
    TswFlush(v);
    utassert(v.State()->revision == parsed);

    // Once the state's text moved on, the element's string is set again.
    v.State()->PushStr(StrL(" world"), v.app, v.win);
    TswFlush(v);
    utassert(StrEq(v.State()->text, StrL("hello")));
    TswClose(&v);
}

// state.rs set_text_then_push_str_appends_to_replaced_content
static void SetTextThenPushStrAppendsToReplacedContent() {
    TswView v = TswOpen("old", TswContainer::Window);
    v.State()->SetText(Str{}, v.app, v.win);
    v.State()->PushStr(StrL("new"), v.app, v.win);
    v.State()->PushStr(StrL(" text"), v.app, v.win);
    TswFlush(v);
    TestRunUntilParked(v.app);
    utassert(StrEq(v.State()->text, StrL("new text")));
    utassert(StrEq(v.State()->Source(), StrL("new text")));

    TswSetText(v, Str{});
    TestRunUntilParked(v.app);
    utassert(len(v.State()->text) == 0);
    utassert(len(v.State()->Source()) == 0);
    TswClose(&v);
}

static bool TswSelectedTextIs(const TswView& v, const char* want) {
    char buf[256];
    int n = v.State()->SelectedText(v.win, buf, (int)sizeof(buf));
    if (n < 0 || n >= (int)sizeof(buf)) {
        return false;
    }
    Str got = StrTrimAscii(Str(buf, n));
    return StrEq(got, Str(want));
}

// state.rs full_parse_coalesced_with_append_preserves_new_select_all: not
// ported — Rust's select_all is a flag that means the whole view whatever it
// holds; here it is the selection SelectAll made over the runs painted then
// (port-status.md "`selected_source_range` reads the window's painted
// runs"), so text that lands after it is not in it.

// state.rs set_text_extending_after_a_parse_error_parses_it_again: not
// ported — a parse here cannot fail, so there is no parsed_error, and with no
// background parse there is no full_update_revision to restart (port-status.md
// "TextView range highlights land with the render").

// state.rs select_all_returns_rendered_text
static void SelectAllReturnsRenderedText() {
    TswView v = TswOpen("**quick** value", TswContainer::Window);
    v.State()->SelectAll(v.win, v.app);
    TswFlush(v);
    utassert(v.State()->HasSelection(v.win));
    utassert(TswSelectedTextIs(v, "quick value"));

    v.State()->ClearSelection(v.win, v.app);
    TswFlush(v);
    utassert(!v.State()->HasSelection(v.win));
    char buf[16];
    utassert(v.State()->SelectedText(v.win, buf, (int)sizeof(buf)) == 0);
    TswClose(&v);
}

// state.rs select_all_in_source_format_returns_source
static void SelectAllInSourceFormatReturnsSource() {
    const char* markdown = "**quick** value";
    TswView v = TswOpen(markdown, TswContainer::Window);
    v.State()->SelectAll(v.win, v.app);
    TswFlush(v);
    // The default (plain) mode strips the markup.
    utassert(TswSelectedTextIs(v, "quick value"));

    v.State()->SetSelectionFormat(gpui::SelectionFormat::Source, v.app, v.win);
    TswFlush(v);
    // Source mode yields the whole source verbatim.
    utassert(TswSelectedTextIs(v, markdown));
    TswClose(&v);
}

struct TswMention {
    const char* label = nullptr;
};

// TestInlinePlugin("test").parse_with(|node| Text => mention(label)).
static bool ParseMention(const markdown::Node* source,
                         const MarkdownParseContext* context, void* data,
                         MarkdownNode* out) {
    if (source->kind != markdown::NodeKind::Text) return false;
    const TswMention* mention = (const TswMention*)data;
    *out = MarkdownNode::New(context->Copy(StrL("mention")))
               .Text(context->Copy(Str(mention->label)));
    return true;
}

// state.rs parser_revision_reparses_same_name_inline_configuration
static void ParserRevisionReparsesSameNameInlineConfiguration() {
    TswView v = TswOpen("@member", TswContainer::Window);
    TswMention mentions[2] = {{"Alice"}, {"Bob"}};
    Arena* ext = ArenaNew();
    MarkdownExtensions extensions[2];
    for (int i = 0; i < 2; i++) {
        MarkdownPlugin plugin;
        plugin.name = StrL("test");
        plugin.parse = &ParseMention;
        plugin.renderInline = &RenderInlineMath;
        plugin.data = &mentions[i];
        extensions[i].ParserRevision((uint64_t)(i + 1)).Plugin(ext, plugin);
        v.Root()->extensions = &extensions[i];
        TswFlush(v);
        TestRunUntilParked(v.app);

        Arena* a = ArenaNew();
        Ctx cx = {v.app, v.win, a, {}};
        MdNode* doc =
            MdParseCachedForTest(&cx, a, v.State()->Source(), &extensions[i]);
        MdNode* paragraph = doc ? doc->first : nullptr;
        utassert(paragraph && paragraph->kind == MdKind::Paragraph);
        MdRun* first = paragraph ? paragraph->runFirst : nullptr;
        utassert(first && first->hasCustom &&
                 StrEq(first->custom.text, Str(mentions[i].label)));
        ArenaDelete(a);
    }
    TswClose(&v);
    ArenaDelete(ext);
}

// block_parser(|node| Paragraph [Text "$symbol"] => ticker(symbol)).
static bool ParseTicker(const markdown::Node* source,
                        const MarkdownParseContext* context, void*,
                        MarkdownNode* out) {
    if (source->kind != markdown::NodeKind::Paragraph) return false;
    if (markdown::NodeChildCount(context->arena, source) != 1) return false;
    const markdown::Node* text = markdown::NodeChild(context->arena, source, 0);
    if (!text || text->kind != markdown::NodeKind::Text) {
        return false;
    }
    Str value = context->Value(text, markdown::NodeStrKind::Value);
    if (len(value) == 0 || value.s[0] != '$') return false;
    Str symbol = context->Copy(Str(value.s + 1, len(value) - 1));
    Str* data = (Str*)Alloc(context->arena, (int)sizeof(Str));
    if (!data) return false;
    *data = symbol;
    *out = MarkdownNode::New(context->Copy(StrL("ticker")), data)
               .Text(context->Copy(value))
               .Markdown(context->Copy(context->NodeSource(source)));
    return true;
}

// state.rs set_markdown_extensions_reparses_existing_text
static void SetMarkdownExtensionsReparsesExistingText() {
    TswView v = TswOpen("$TSLA.US", TswContainer::Window);
    Arena* ext = ArenaNew();
    MarkdownExtensions extensions;
    extensions.BlockParser(ext, &ParseTicker);
    v.Root()->extensions = &extensions;
    TswFlush(v);
    TestRunUntilParked(v.app);

    Arena* a = ArenaNew();
    Ctx cx = {v.app, v.win, a, {}};
    MdNode* doc =
        MdParseCachedForTest(&cx, a, v.State()->Source(), &extensions);
    MdNode* node = doc ? doc->first : nullptr;
    // "expected custom markdown node"
    utassert(node && node->kind == MdKind::Custom);
    if (node && node->kind == MdKind::Custom) {
        utassert(StrEq(node->custom.name, StrL("ticker")));
        const Str* symbol = (const Str*)node->custom.data;
        utassert(symbol && StrEq(*symbol, StrL("TSLA.US")));
    }
    ArenaDelete(a);
    TswClose(&v);
    ArenaDelete(ext);
}

// ─── reveal_range ─────────────────────────────────────────────────────────

// Where a scrollable view is scrolled to, as gpui::ListOffset: the block at
// the top of the viewport and how far into it the viewport starts. A block
// runs to where the next one starts, gap included, the way a list item does.
struct TswScrollTop {
    int itemIx = 0;
    float offsetInItem = 0;
};

static TswScrollTop TswScrollTopOf(const TswView& v) {
    TswScrollTop top;
    El* scroller = v.Root()->scroller;
    El* doc = scroller ? scroller->first : nullptr;
    float scroll = v.State()->scrollY;
    int ix = 0;
    for (El* b = doc ? doc->first : nullptr; b; b = b->next, ix++) {
        float start = b->y - doc->y;
        if (!b->next || scroll < b->next->y - doc->y) {
            top.itemIx = ix;
            top.offsetInItem = scroll - start;
            break;
        }
    }
    return top;
}

static int TswBlockCount(const TswView& v) {
    El* scroller = v.Root()->scroller;
    El* doc = scroller ? scroller->first : nullptr;
    int n = 0;
    for (El* b = doc ? doc->first : nullptr; b; b = b->next) {
        n++;
    }
    return n;
}

static bool TswPending(const TswView& v) {
    return v.State()->reveal.pending;
}

// request: reveal [start, end) of the current rendered text, before
// anything is drawn.
static void TswRequestRange(const TswView& v, Span range) {
    utassert(v.State()->RevealRange(range, v.app, v.win).IsOk());
}

static Span TswFind(const TswView& v, const char* needle) {
    Str text = v.State()->RenderedText().AsStr();
    const char* at = strstr(text.s, needle);
    utassert(at != nullptr);
    int start = at ? (int)(at - text.s) : 0;
    return Span{start, start + (int)strlen(needle)};
}

// reveal: reveal the first occurrence of `needle` and draw a few frames.
static void TswDraws(const TswView& v) {
    for (int i = 0; i < 3; i++) {
        TestDraw(v.win);
    }
}

static void TswReveal(const TswView& v, const char* needle) {
    TswRequestRange(v, TswFind(v, needle));
    TswDraws(v);
}

static bool TswUnmoved(const TswView& v, TswScrollTop top) {
    TswScrollTop now = TswScrollTopOf(v);
    return !TswPending(v) && now.itemIx == top.itemIx &&
           now.offsetInItem == top.offsetInItem;
}

// state.rs a_scrollable_view_scrolls_to_an_offscreen_block
static void AScrollableViewScrollsToAnOffscreenBlock() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswParagraphs(a, 200).s, TswContainer::Scrollable);
    TswReveal(v, "paragraph 150");
    utassert(!TswPending(v));
    TswScrollTop top = TswScrollTopOf(v);
    utassert(top.itemIx >= 140 && top.itemIx <= 150);

    TswReveal(v, "paragraph 3");
    utassert(!TswPending(v));
    utassert(TswScrollTopOf(v).itemIx <= 3);
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs an_append_adding_blocks_keeps_the_scroll_position
static void AnAppendAddingBlocksKeepsTheScrollPositionInAWindow() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswParagraphs(a, 200).s, TswContainer::Scrollable);
    TswReveal(v, "paragraph 100");
    TswScrollTop top = TswScrollTopOf(v);
    utassert(top.itemIx > 0);

    TswPushStr(v, "\n\nparagraph 200");
    TestRunUntilParked(v.app);
    TestDraw(v.win);
    utassert(TswBlockCount(v) == 201);
    utassert(TswUnmoved(v, top));
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs a_scrollable_view_scrolls_to_a_line_inside_a_long_paragraph
static void AScrollableViewScrollsToALineInsideALongParagraph() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswWords(a, 400).s, TswContainer::Scrollable);
    TswReveal(v, "w390");
    utassert(!TswPending(v));
    TswScrollTop top = TswScrollTopOf(v);
    utassert(top.itemIx == 0);
    utassert(top.offsetInItem > 100.f);
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs revealing_a_visible_line_does_not_scroll
static void RevealingAVisibleLineDoesNotScroll() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswWords(a, 400).s, TswContainer::Scrollable);
    TswReveal(v, "w200");
    TswScrollTop top = TswScrollTopOf(v);
    utassert(top.offsetInItem > 0.f);
    const char* words[] = {"w199", "w198", "w197", "w196"};
    for (const char* word : words) {
        TswReveal(v, word);
        utassert(TswUnmoved(v, top));
    }
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs an_enclosing_list_scrolls_to_a_line_of_a_fit_content_view: not
// ported — there is no request_autoscroll, so an application list around a
// fit-content view does not follow a reveal by itself (port-status.md
// "`reveal_range` reads back last frame's paint"); the OnReveal route is
// on_reveal_scrolls_a_container_that_ignores_scroll_requests below.

// state.rs on_reveal_scrolls_a_container_that_ignores_scroll_requests
static void OnRevealScrollsAContainerThatIgnoresScrollRequests() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswWords(a, 400).s, TswContainer::Div);
    TswReveal(v, "w390");
    utassert(!TswPending(v));
    // Rust's handle offset is negative-down: offset().y < -100.
    utassert(v.Root()->divScrollY > 100.f);
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs a_reveal_that_cannot_be_shown_gives_up
static void ARevealThatCannotBeShownGivesUp() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswWords(a, 4000).s, TswContainer::Fixed);
    TswReveal(v, "w3990");
    utassert(TswPending(v));
    for (int i = 0; i < 10; i++) {
        TestDraw(v.win);
    }
    utassert(!TswPending(v));
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs a_reveal_not_carried_out_in_time_is_dropped
static void ARevealNotCarriedOutInTimeIsDropped() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswParagraphs(a, 200).s, TswContainer::Scrollable);
    TswRequestRange(v, TswFind(v, "paragraph 150"));
    // The clock moves inside the update, before its flush draws the frame
    // the request asked for: the harness's advance runs until parked and
    // would draw it first, so the invalidation is held back across it.
    uint64_t held = v.win->invalidations;
    v.win->invalidations = 0;
    TestAdvanceClock(v.app, 2000);
    v.win->invalidations += held;
    TestFlushEffects(v.app);
    TestDraw(v.win);
    utassert(!TswPending(v));
    utassert(TswScrollTopOf(v).itemIx == 0);
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs an_empty_range_reveals_its_line: not ported — it reveals w391 as
// a position on the line w390 was just scrolled onto, which is where Rust's
// font breaks the 200-pixel lines; with this platform's font w391 starts the
// next line, below the viewport, so the reveal scrolls by that line. The same
// rule, a position on a visible line, is the next test's end of the text.

// state.rs a_position_after_the_last_character_reveals_its_line
static void APositionAfterTheLastCharacterRevealsItsLine() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswWords(a, 400).s, TswContainer::Scrollable);
    TswReveal(v, "w399");
    TswScrollTop top = TswScrollTopOf(v);
    utassert(top.offsetInItem > 1000.f);
    // The end of the text, on the visible last line.
    Span last = TswFind(v, "w399");
    TswRequestRange(v, Span{last.end, last.end});
    TswDraws(v);
    utassert(TswUnmoved(v, top));
    TswClose(&v);
    ArenaDelete(a);
}

// reveal_at: reveal `range` of the current text and draw a few frames.
static void TswRevealAt(const TswView& v, Span range) {
    TswRequestRange(v, range);
    TswDraws(v);
}

// state.rs the_end_of_the_text_and_its_separators_reveal_the_last_line
static void TheEndOfTheTextAndItsSeparatorsRevealTheLastLine() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswWords(a, 400).s, TswContainer::Scrollable);
    TswReveal(v, "w399");
    TswScrollTop top = TswScrollTopOf(v);
    utassert(top.offsetInItem > 1000.f);
    // The end of the text, after the separator that ends the block.
    int n = v.State()->RenderedText().Len();
    TswRevealAt(v, Span{n, n});
    utassert(TswUnmoved(v, top));
    // Only that separator.
    TswRevealAt(v, Span{n - 1, n});
    utassert(TswUnmoved(v, top));

    StrBuilder code(a);
    code.Append(StrL("```\n"));
    code.Append(TswJoin(a, "line %d", 200, "\n"));
    code.Append(StrL("\n```"));
    TswSetText(v, code.TakeStr());
    TestRunUntilParked(v.app);
    TswReveal(v, "line 199");
    top = TswScrollTopOf(v);
    utassert(top.offsetInItem > 1000.f);
    n = v.State()->RenderedText().Len();
    TswRevealAt(v, Span{n, n});
    utassert(TswUnmoved(v, top));
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs the_end_of_a_block_above_reveals_its_last_line
static void TheEndOfABlockAboveRevealsItsLastLine() {
    Arena* a = ArenaNew();
    StrBuilder markdown(a);
    markdown.Append(TswWords(a, 400));
    markdown.Append(StrL("\n\n"));
    markdown.Append(TswJoin(a, "v%d", 400, " "));
    TswView v = TswOpen(markdown.TakeStr().s, TswContainer::Scrollable);
    TswReveal(v, "v399");
    utassert(TswScrollTopOf(v).itemIx == 1);
    // The end of the first paragraph, on the separator after it.
    int end = TswFind(v, "w399").end;
    TswRevealAt(v, Span{end, end});
    utassert(!TswPending(v));
    TswScrollTop top = TswScrollTopOf(v);
    utassert(top.itemIx == 0);
    utassert(top.offsetInItem > 1000.f);
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs an_empty_view_has_nothing_to_reveal
static void AnEmptyViewHasNothingToReveal() {
    TswView v = TswOpen("", TswContainer::Window);
    utassert(v.State()->RevealRange(Span{0, 0}, v.app, v.win).IsOk());
    utassert(!TswPending(v));
    TswClose(&v);
}

// state.rs revealing_a_visible_block_does_not_scroll
static void RevealingAVisibleBlockDoesNotScroll() {
    Arena* a = ArenaNew();
    StrBuilder markdown(a);
    markdown.Append(TswWords(a, 400));
    markdown.Append(StrL("\n\n<div>html</div>\n\n"));
    markdown.Append(TswWords(a, 400));
    TswView v = TswOpen(markdown.TakeStr().s, TswContainer::Scrollable);
    TswReveal(v, "html");
    // Still in view a little further down.
    v.State()->scrollY += 30.f;
    TestDraw(v.win);
    TswScrollTop top = TswScrollTopOf(v);
    TswReveal(v, "html");
    utassert(TswUnmoved(v, top));
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs a_line_of_an_inline_flow_counts_as_shown_once_scrolled_to
static void ALineOfAnInlineFlowCountsAsShownOnceScrolledTo() {
    // Inline code every tenth word, so the rows are taller than the body
    // line and land between pixels.
    Arena* a = ArenaNew();
    StrBuilder markdown(a);
    for (int ix = 0; ix < 400; ix++) {
        if (ix > 0) {
            markdown.AppendChar(' ');
        }
        markdown.Append(Str(ix % 10 == 0 ? fmt("`c%d`", ix) : fmt("w%d", ix)));
    }
    TswView v = TswOpen(markdown.TakeStr().s, TswContainer::Scrollable);
    TswReveal(v, "c390");
    utassert(!TswPending(v));
    utassert(TswScrollTopOf(v).offsetInItem > 1000.f);
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs a_range_starting_on_a_line_break_reveals_the_next_line
static void ARangeStartingOnALineBreakRevealsTheNextLine() {
    Arena* a = ArenaNew();
    StrBuilder code(a);
    code.Append(StrL("```\n"));
    code.Append(TswJoin(a, "line %d", 200, "\n"));
    code.Append(StrL("\n```"));
    TswView v = TswOpen(code.TakeStr().s, TswContainer::Scrollable);
    TswReveal(v, "\nline 190");
    utassert(!TswPending(v));
    utassert(TswScrollTopOf(v).offsetInItem > 1000.f);
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs a_range_starting_on_a_line_break_in_an_inline_flow_reveals_the_
// next_line
static void ARangeStartingOnALineBreakInAnInlineFlowRevealsTheNextLine() {
    // Inline code lays the paragraph out as an inline flow.
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswJoin(a, "line %d `code`", 200, "\\\n").s,
                        TswContainer::Scrollable);
    TswReveal(v, "\nline 190");
    utassert(!TswPending(v));
    utassert(TswScrollTopOf(v).offsetInItem > 1000.f);
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs a_reveal_is_dropped_when_its_text_before_it_changes
static void ARevealIsDroppedWhenItsTextBeforeItChanges() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswParagraphs(a, 200).s, TswContainer::Scrollable);
    const char* target = "first words then the target";
    StrBuilder markdown(a);
    markdown.Append(Str(target));
    markdown.Append(StrL("\n\n"));
    markdown.Append(TswParagraphs(a, 200));
    Str text = markdown.TakeStr();
    TswSetText(v, text);
    TestRunUntilParked(v.app);

    // Appending to its paragraph keeps it.
    TswRequestRange(v, TswFind(v, "target"));
    StrBuilder more(a);
    more.Append(StrL("first words then the target and more"));
    more.Append(Str(text.s + strlen(target), len(text) - (int)strlen(target)));
    v.State()->SetText(more.TakeStr(), v.app, v.win);
    utassert(TswPending(v));
    TswFlush(v);
    // An edit before it in its paragraph drops it.
    TswRequestRange(v, TswFind(v, "target"));
    StrBuilder edited(a);
    edited.Append(StrL("first WORDS then the target"));
    edited
        .Append(Str(text.s + strlen(target), len(text) - (int)strlen(target)));
    v.State()->SetText(edited.TakeStr(), v.app, v.win);
    // Rust drops it inside set_text; here the reveal is carried to the new
    // text when its parse lands, which is the render the update's flush
    // draws (port-status.md "TextView range highlights land with the
    // render").
    TswFlush(v);
    utassert(!TswPending(v));
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs a_clamped_view_does_not_reveal. Rust's view is the second row of
// an application list it would scroll; this tree's container that follows a
// fit-content view is a scroller listening to OnReveal, which is what is
// checked to stay where it is.
static void AClampedViewDoesNotRevealInAWindow() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswWords(a, 400).s, TswContainer::Clamped);
    TswReveal(v, "w390");
    utassert(!TswPending(v));
    utassert(v.Root()->divScrollY == 0.f);
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs text_outside_every_block_reveals_its_block
static void TextOutsideEveryBlockRevealsItsBlock() {
    Arena* a = ArenaNew();
    StrBuilder markdown(a);
    markdown.Append(TswParagraphs(a, 100));
    markdown.Append(StrL("\n\n<div>html text</div>"));
    TswView v = TswOpen(markdown.TakeStr().s, TswContainer::Scrollable);
    TswReveal(v, "html text");
    utassert(!TswPending(v));
    utassert(TswScrollTopOf(v).itemIx >= 90);
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs a_reveal_follows_its_text_past_an_edit_before_it
static void ARevealFollowsItsTextPastAnEditBeforeIt() {
    Arena* a = ArenaNew();
    TswView v = TswOpen(TswParagraphs(a, 200).s, TswContainer::Scrollable);
    // An inserted paragraph moves the target down one block.
    TswRequestRange(v, TswFind(v, "paragraph 150"));
    StrBuilder inserted(a);
    inserted.Append(StrL("inserted\n\n"));
    inserted.Append(TswParagraphs(a, 200));
    v.State()->SetText(inserted.TakeStr(), v.app, v.win);
    utassert(TswPending(v));
    TswFlush(v);
    TswDraws(v);
    utassert(!TswPending(v));
    TswScrollTop top = TswScrollTopOf(v);
    utassert(top.itemIx >= 141 && top.itemIx <= 151);

    // A reveal whose text changed is dropped.
    TswRequestRange(v, TswFind(v, "paragraph 150"));
    Str changed = TswParagraphs(a, 200);
    char* at = strstr((char*)changed.s, "paragraph 150");
    StrBuilder replaced(a);
    replaced.Append(Str(changed.s, (int)(at - changed.s)));
    replaced.Append(StrL("changed"));
    replaced.Append(Str(at + strlen("paragraph 150")));
    v.State()->SetText(replaced.TakeStr(), v.app, v.win);
    // Rust drops it inside set_text; here the reveal is carried to the new
    // text when its parse lands, which is the render the update's flush
    // draws (port-status.md "TextView range highlights land with the
    // render").
    TswFlush(v);
    utassert(!TswPending(v));
    TswClose(&v);
    ArenaDelete(a);
}

// state.rs malformed_ranges_and_html_views_are_rejected
static void MalformedRangesAndHtmlViewsAreRejectedInAWindow() {
    TswView v = TswOpen("first\n\nsecond", TswContainer::Window);
    utassert(v.State()->RevealRange(Span{5, 3}, v.app, v.win) ==
             RangeHighlightError::InvalidRange(0));
    utassert(v.State()->RevealRange(Span{0, 100}, v.app, v.win) ==
             RangeHighlightError::InvalidRange(0));
    utassert(v.State()->RevealRange(Span{0, 5}, v.app, v.win).IsOk());
    TswClose(&v);

    TswView html = TswOpen("<p>one</p>", TswContainer::Window, true);
    utassert(html.State()->RevealRange(Span{0, 3}, html.app, html.win) ==
             RangeHighlightError::Unsupported());
    TswClose(&html);
}

static void TestTextStateWindow() {
    PushStrFadesOnlyTheAppendedText();
    SetTextExtendingTheTextFadesLikePushStr();
    SetTextReplacingTheTextShowsItAtOnce();
    CompletedMarkupRefadesFromTheDivergence();
    WithoutStaggerAnUpdateFadesAsOneChunk();
    WordsOfOneUpdateStartOneAfterAnother();
    ANewParagraphFadesAsAWhole();
    CodeBlockTextFadesByBlock();
    TableCellsFadeByOrdinal();
    ZeroDurationRecordsNothing();
    ReducedMotionDropsTheFade();
    AFadeRepaintsOnATimerUntilNothingFadesInAWindow();
    InlineSourceRangesFollowStreamedTailReparsing();
    AsyncFullReplaceThenPushStrPreservesCompleteSource();
    HtmlPushStrKeepsEarlierBlocks();
    ElementTextOfTheSameStringIsNotComparedAgain();
    SetTextThenPushStrAppendsToReplacedContent();
    SelectAllReturnsRenderedText();
    SelectAllInSourceFormatReturnsSource();
    ParserRevisionReparsesSameNameInlineConfiguration();
    SetMarkdownExtensionsReparsesExistingText();
    AScrollableViewScrollsToAnOffscreenBlock();
    AnAppendAddingBlocksKeepsTheScrollPositionInAWindow();
    AScrollableViewScrollsToALineInsideALongParagraph();
    RevealingAVisibleLineDoesNotScroll();
    OnRevealScrollsAContainerThatIgnoresScrollRequests();
    ARevealThatCannotBeShownGivesUp();
    ARevealNotCarriedOutInTimeIsDropped();
    APositionAfterTheLastCharacterRevealsItsLine();
    TheEndOfTheTextAndItsSeparatorsRevealTheLastLine();
    TheEndOfABlockAboveRevealsItsLastLine();
    AnEmptyViewHasNothingToReveal();
    RevealingAVisibleBlockDoesNotScroll();
    ALineOfAnInlineFlowCountsAsShownOnceScrolledTo();
    ARangeStartingOnALineBreakRevealsTheNextLine();
    ARangeStartingOnALineBreakInAnInlineFlowRevealsTheNextLine();
    ARevealIsDroppedWhenItsTextBeforeItChanges();
    AClampedViewDoesNotRevealInAWindow();
    TextOutsideEveryBlockRevealsItsBlock();
    ARevealFollowsItsTextPastAnEditBeforeIt();
    MalformedRangesAndHtmlViewsAreRejectedInAWindow();
}

#endif

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
    TestListItemPrefix(a);
    TestOrderedListStarts(a);
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
    TestEqualBlockCountReplacementRemeasures();
    TestTextViewStyleIsReadableWithoutATheme();
    TestTextViewDefaultsAndOptInHighlighting();
    TestTextViewRootNamesItsForeground();
    TestMarkdownExtensionsParserConfiguration(a);
    TestMarkdownFrontmatter();
    TestMarkdownInlinePlugin();
    UnclaimedInlineMathParsesItsSpanAsProse();
    ClaimedInlineMathSurvivesProseFlattening();
    InlineHtmlFormattingTagsPairAcrossSiblings();
    HeadingRefinementChangesRenderedHeadingGeometry();
    AnUnchangedFlowIsNotLaidOutAgain();
    InlineCodeLineIsAsTallAsAPlainLine();
    TestStatelessMarkdownSettles();
    TestStreamFadeTracksRenderedAppends();
    StreamFadeUnitsAreWordsOrCjkCharacters();
    FadesLayerOverHighlightsInsideTheirRange();
    StreamedWordsFadeInOneAfterAnother();
    SetTextExtendingMarkdownAppendsAndKeepsSelection();
    SetTextStreamingMarkdownMatchesAFullParse();
    SetTextExtendingHtmlParsesItAgain();
    AFadeRepaintsOnATimerUntilNothingFades();
    TestStreamFadeStaggerStep();
    TestManagedTextViewAndParseTimePlugins(a);
    TestMarkdownTableThemeTokens();
    TestTextViewMaxLines();
#if GPUI_MARKDOWN_FULL
    TestMarkdownTaskList(a);
    TestSourceRangeStyled();
    TestSourceSegmentsCompact();
    TestSourceAlignmentCompactsLongWhitespaceRuns();
    TestSourceAlignmentPreservesMultilineCodeAndFinalNewline();
    TestSourceAlignmentKeepsSoftBreaksAndEntitiesAtomic();
    TestSourceAlignmentResumesAfterUnmappedCharacters();
    TestSourceAlignmentParsesLongTextAndCode();
    TestSourceRangeInlineCodeAndFootnote();
    TestSourceRangeCodeBlocks();
    TestSourceRangeEscapes();
    TestSourceRangeImages();
    TestSourceRangeEntities();
    TestSourceRangeSoftBreaks();
    TestSourceRangeAfterAppend();
    TestSourceRangeOverPaintedRuns();
    TestSourceRangeSelectAllAndHtml();
#endif
#if !GPUI_MARKDOWN_MINI
    RangeHighlightRequiresABackground();
    RenderedTextIsThePlainCopyText();
    AMatchAcrossMarksPaintsInItsParagraph();
    RepeatedTextMapsToTheOccurrenceAddressed();
    ARangeAcrossBlocksSkipsTheSeparator();
    TableCellsAndCodeBlocksAreLeaves();
    InvalidRangesRejectTheWholeSet();
    TextOutsideEveryBlockIsLeftUnpainted();
    ALaterHighlightPaintsOverAnEarlierOne();
    HtmlTextIsUnsupported();
    InlineObjectsAreSkipped();
    PushStrKeepsEarlierBlocksAndClipsTheChangedTail();
    PushStrKeepsABlockWhoseTextIsUnchanged();
    PushStrDropsHighlightsOfALeafThatIsGone();
    AnAppendAddingBlocksKeepsTheScrollPosition();
    ReplacingTextDropsHighlightsOnlyWhereItChanged();
    AHighlightFollowsItsBlockPastAnEarlierEdit();
    AppendingACopyOfTheLastBlockKeepsTheHighlightOnIt();
    DeletingATableRowDropsHighlightsInTheRowsItMoves();
    StreamingTableRowsKeepsTheHighlightsOfEarlierRows();
    LongAndWideTablesRemapHighlightsByWholeRows();
    AnEditInAnEarlierBlockKeepsLaterHighlights();
    StreamingThroughSetTextKeepsHighlights();
    AnAppendComparesEveryBlock();
    AFullUpdateBeforeAnAppendDropsReplacedHighlights();
    ReparsingUnchangedTextKeepsHighlights();
    HighlightsPaintAcrossInlineCodeTablesAndCodeBlocks();
    ClearRangeHighlightsRemovesThem();
    ARevealStartsOnTheLineOfItsRange();
    MalformedRangesAndHtmlViewsAreRejected();
    AScrollableViewScrollsToItsLine();
    OnRevealHearsAHiddenLine();
    ARevealGivesUp();
    ARevealFollowsItsText();
#endif
#if GPUI_MARKDOWN_FULL
    TestTextStateWindow();
#endif
    ArenaDelete(a);
}
