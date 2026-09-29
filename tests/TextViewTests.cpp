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
    utassert(StrEq(state->streamRenderedText, StrL("hello")));
    utassert(state->streamFadeFrom < 0);

    state->PushStr(StrL(" world"), &app, win);
    El* faded = TextView::New(&cx, entity)->IntoEl();
    utassert(state->streamFadeFrom == 5);
    utassert(faded && faded->first && faded->first->style.opacity < 1.f);

    state->SetText(StrL("replacement"), &app, win);
    TextView::New(&cx, entity)->IntoEl();
    utassert(state->streamFadeFrom < 0);

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

// stream_fade.rs: stagger_holds_while_the_update_lights_up_within_one_fade
// and an_update_too_large_to_light_up_in_one_fade_fades_as_one_chunk.
static void TestStreamFadeStaggerStep() {
    TextViewMotion motion =
        TextViewMotion{}.WithStreamFade(600.f).WithStreamFadeStagger(100.f);
    utassert(motion.StaggerStepMs(1) == 0.f);
    utassert(motion.StaggerStepMs(3) == 100.f);
    // The 7th word starts at 600 ms, exactly one fade in -- still the stagger
    // as asked.
    utassert(motion.StaggerStepMs(7) == 100.f);
    // An 8th word would start past the fade: that is a sweep, not typing.
    utassert(motion.StaggerStepMs(8) == 0.f);
    utassert(motion.StaggerStepMs(200) == 0.f);
    // Without a stagger nothing changes -- every update was already one chunk.
    TextViewMotion plain = TextViewMotion{}.WithStreamFade(600.f);
    utassert(plain.StaggerStepMs(200) == 0.f);
}

// ─── selected_source_range ────────────────────────────────────────────────
//
// Ports of format/markdown.rs's selected_source_range_* and source_segments_*
// tests and state.rs's selected_source_range_* ones. Rust sets a rendered
// selection on the paragraph's inline state; here MdSelectedSourceRange
// takes the same selection of the node's rendered text. The two MDX cases
// (selected_source_range_maps_mdx_*) are not ported: MDX is not
// (src/markdown/readme.md).

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
    TestMarkdownExtensionsParserConfiguration(a);
    TestMarkdownFrontmatter();
    TestMarkdownInlinePlugin();
    UnclaimedInlineMathParsesItsSpanAsProse();
    ClaimedInlineMathSurvivesProseFlattening();
    InlineHtmlFormattingTagsPairAcrossSiblings();
    HeadingRefinementChangesRenderedHeadingGeometry();
    AnUnchangedFlowIsNotLaidOutAgain();
    TestStatelessMarkdownSettles();
    TestStreamFadeTracksRenderedAppends();
    TestStreamFadeStaggerStep();
    TestManagedTextViewAndParseTimePlugins(a);
    TestMarkdownTableThemeTokens();
    TestTextViewMaxLines();
#if GPUI_MARKDOWN_FULL
    TestMarkdownTaskList(a);
    TestSourceRangeStyled();
    TestSourceSegmentsCompact();
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
    ArenaDelete(a);
}
