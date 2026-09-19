/* End-to-end tests for the public surface shared by src/html5ever and
   src/html5ever-mini. The full-only cases pin the HTML5 tree-construction
   rules which are deliberately outside the mini contract. */

#define GPUI_INCLUDE_PRIVATE_API 1
#include "Test.h"

using namespace html5ever;

static Node* ElementChild(Arena* a, Node* parent, Str name,
                          int occurrence = 0) {
    for (Node* node = NodeFirst(a, parent); node; node = NodeNext(a, node)) {
        if (node->kind == NodeKind::Element &&
            base::StrEqI(NodeName(a, node), name)) {
            if (occurrence-- == 0) return node;
        }
    }
    return nullptr;
}

static void TestSharedSurface(Arena* a) {
    Node* doc =
        ParseFragment(a, StrL("<p id='a&amp;b'>one<br>two &lt; three</p>"));
    Node* p = ElementChild(a, doc, StrL("p"));
    utassert(p != nullptr);
    utassert(base::StrEq(AttrValue(a, p, StrL("id")), StrL("a&b")));
    Node* first = NodeFirst(a, p);
    utassert(first && base::StrEq(NodeData(a, first), StrL("one")));
    utassert(ElementChild(a, p, StrL("br")) != nullptr);

    Str serialized = Serialize(a, doc);
    utassert(base::StrContains(serialized, StrL("<p id=\"a&amp;b\">")));
    utassert(base::StrContains(serialized, StrL("two &lt; three")));
}

#if GPUI_HTML5EVER_FULL

static Node* DocumentBody(Arena* a, Node* doc) {
    Node* html = ElementChild(a, doc, StrL("html"));
    return ElementChild(a, html, StrL("body"));
}

static void TestNamedCharacterReferences(Arena* a) {
    Node* doc = ParseFragment(a, StrL("<p>&eacute;&nbsp;&frac12;</p>"));
    Node* p = ElementChild(a, doc, StrL("p"));
    utassert(p != nullptr);
    Node* text = NodeFirst(a, p);
    utassert(text && text->kind == NodeKind::Text);
    Str data = NodeData(a, text);
    utassert(base::StrContains(data, StrL("é")));
    utassert(base::StrContains(data, StrL("½")));
}

static void TestDocumentAndImpliedEnds(Arena* a) {
    Node* doc = ParseDocument(a, StrL("<!doctype html><p>one<p>two"));
    utassert(NodeFirst(a, doc) && NodeFirst(a, doc)->kind == NodeKind::Doctype);
    Node* body = DocumentBody(a, doc);
    utassert(body != nullptr);
    Node* first = ElementChild(a, body, StrL("p"));
    Node* second = ElementChild(a, body, StrL("p"), 1);
    utassert(first && second);
    utassert(NodeFirst(a, first) &&
             base::StrEq(NodeData(a, NodeFirst(a, first)), StrL("one")));
    utassert(NodeFirst(a, second) &&
             base::StrEq(NodeData(a, NodeFirst(a, second)), StrL("two")));
}

static void TestTableModes(Arena* a) {
    Node* doc =
        ParseDocument(a, StrL("<table>before<tr><td>x<td>y</table>after"));
    Node* body = DocumentBody(a, doc);
    utassert(body && NodeFirst(a, body) &&
             NodeFirst(a, body)->kind == NodeKind::Text);
    utassert(base::StrEq(NodeData(a, NodeFirst(a, body)), StrL("before")));
    Node* table = ElementChild(a, body, StrL("table"));
    Node* tbody = ElementChild(a, table, StrL("tbody"));
    utassert(tbody && tbody->implicit);
    Node* row = ElementChild(a, tbody, StrL("tr"));
    utassert(row != nullptr);
    utassert(ElementChild(a, row, StrL("td")) != nullptr);
}

static void TestForeignContent(Arena* a) {
    Node* doc =
        ParseDocument(a, StrL("<svg><g></g></svg><math><mi>x</mi></math>"));
    Node* body = DocumentBody(a, doc);
    Node* svg = ElementChild(a, body, StrL("svg"));
    Node* math = ElementChild(a, body, StrL("math"));
    utassert(svg && svg->ns == Namespace::Svg);
    utassert(ElementChild(a, svg, StrL("g"))->ns == Namespace::Svg);
    utassert(math && math->ns == Namespace::MathMl);
}

#endif

static void TestIncrementalAndScriptPause(Arena* a) {
    Parser* parser = ParserNew(a);
    ParserProcess(parser, StrL("<p>hel"));
    utassert(!ParserIsPaused(parser));
    ParserProcess(parser, StrL("lo</p>"));
    Node* doc = ParserFinish(parser);
    utassert(doc != nullptr);
    Str serialized = Serialize(a, doc);
    utassert(base::StrContains(serialized, StrL("hello")));

    Parser* split = ParserNew(a);
    ParserProcess(split, StrL("<p"));
    ParserProcess(split, StrL(">x</p>"));
    Node* splitDoc = ParserFinish(split);
    utassert(splitDoc != nullptr);
    utassert(base::StrContains(Serialize(a, splitDoc), StrL("x")));

    ParseOptions opt;
    opt.scriptingEnabled = true;
    Parser* scripts = ParserNew(a, opt);
    ParserProcess(scripts, StrL("<script>x=1</script><p>after</p>"));
    utassert(ParserIsPaused(scripts));
    ParserResumeAfterCurrentScript(scripts);
    Node* withScript = ParserFinish(scripts);
    utassert(withScript != nullptr);
    utassert(base::StrContains(Serialize(a, withScript), StrL("after")));

    ParseOptions off;
    off.scriptingEnabled = false;
    Parser* noPause = ParserNew(a, off);
    ParserProcess(noPause, StrL("<script>x=1</script><p>after</p>"));
    utassert(!ParserIsPaused(noPause));
    utassert(ParserFinish(noPause) != nullptr);
}

void TestHtml5ever() {
    TestSuite("html5ever");
    Arena* a = ArenaNew();
    TestSharedSurface(a);
    TestIncrementalAndScriptPause(a);
#if GPUI_HTML5EVER_FULL
    TestNamedCharacterReferences(a);
    TestDocumentAndImpliedEnds(a);
    TestTableModes(a);
    TestForeignContent(a);
#endif
    ArenaDelete(a);
}
