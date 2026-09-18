/* Tests for src/markdown, the C++ port of the `markdown` crate 1.0.0.

   The crate's own suite is split in two. Its `#[cfg(test)]` modules pin the
   constants, the character classifier and the option shortcuts, and those are
   ported below. Everything else it has lives in the crate's `tests/`
   directory — around 8000 CommonMark and GFM cases — which is not part of the
   published crate, the same gap taffy has
   (.claude/skills/update-port/crates.md). What stands in for them here is the
   second half of this file: an end-to-end check of each construct, reading
   the mdast the way TextView does.

   Not ported, and named here: the `Debug`/`serde` cases in `mdast.rs`,
   `unist.rs` and `configuration.rs` (neither trait is ported), and
   everything in `util/mdx.rs` and `util/location.rs` (MDX is not ported). */

// These tests reach the crate's internals (the tokenizer, CharKind, the
// event stream), which the amalgam hides from ordinary consumers.
#define GPUI_INCLUDE_PRIVATE_API 1
#include "Test.h"

using namespace markdown;

#if GPUI_MARKDOWN_MINI

static Node* MiniChild(Arena* a, Node* n, int ix) {
    return n ? NodeChild(a, n, ix) : nullptr;
}

static bool MiniTextIs(Arena* a, Node* n, const char* want) {
    if (!n) {
        return false;
    }
    Str got = NodeToString(a, n);
    return base::StrEq(got, want);
}

void TestMarkdown() {
    TestSuite("markdown mini");
    Arena* a = ArenaNew();
    ParseOptions options = ParseOptions::Gfm();

    Node* root = ToMdast(a, StrL("# Hi **bold** and _italic_\n"), options);
    Node* heading = MiniChild(a, root, 0);
    utassert(heading && heading->kind == NodeKind::Heading);
    utassert(NodePerKind(a, heading) == 1);
    utassert(MiniTextIs(a, heading, "Hi bold and italic"));
    utassert(MiniChild(a, heading, 1)->kind == NodeKind::Strong);
    utassert(MiniChild(a, heading, 3)->kind == NodeKind::Emphasis);

    root = ToMdast(a, StrL("- one\n- two\n\n3. three\n4. four\n"), options);
    Node* bullets = MiniChild(a, root, 0);
    Node* numbers = MiniChild(a, root, 1);
    utassert(bullets && bullets->kind == NodeKind::List);
    utassert(NodeChildCount(a, bullets) == 2);
    utassert(!bullets->Has(NodeOrdered));
    utassert(numbers && numbers->Has(NodeOrdered));
    utassert(NodePerKind(a, numbers) == 3);
    utassert(MiniTextIs(a, MiniChild(a, bullets, 1), "two"));

    root = ToMdast(a, StrL("- outer\n  - nested\n- next\n"), options);
    bullets = MiniChild(a, root, 0);
    Node* outer = MiniChild(a, bullets, 0);
    utassert(MiniChild(a, outer, 1)->kind == NodeKind::List);
    utassert(MiniTextIs(a, MiniChild(a, outer, 1), "nested"));

    root = ToMdast(a, StrL("Setext\n=======\n\na  \nb\n"), options);
    heading = MiniChild(a, root, 0);
    Node* paragraph = MiniChild(a, root, 1);
    utassert(heading->kind == NodeKind::Heading);
    utassert(NodePerKind(a, heading) == 1);
    utassert(MiniChild(a, paragraph, 1)->kind == NodeKind::Break);

    root = ToMdast(a,
                   StrL("> quote with `code`\n\n---\n\n"
                        "```cpp\nint x;\n```\n"),
                   options);
    utassert(MiniChild(a, root, 0)->kind == NodeKind::Blockquote);
    utassert(MiniChild(a, root, 1)->kind == NodeKind::ThematicBreak);
    Node* code = MiniChild(a, root, 2);
    utassert(code && code->kind == NodeKind::Code);
    utassert(MiniTextIs(a, code, "int x;"));
    utassert(base::StrEq(NodeGetStr(a, code, NodeStrKind::Lang), StrL("cpp")));

    root = ToMdast(a, StrL("[home](https://x.dev) ![alt](a.png) &amp; &#65;\n"),
                   options);
    paragraph = MiniChild(a, root, 0);
    utassert(MiniChild(a, paragraph, 0)->kind == NodeKind::Link);
    utassert(MiniChild(a, paragraph, 2)->kind == NodeKind::Image);
    utassert(MiniTextIs(a, paragraph, "home  & A"));

    // GFM-only syntax is intentionally readable text, not a half-supported
    // table/task/strikethrough node.
    root = ToMdast(a, StrL("~~kept~~\n\n| a | b |\n|---|---|\n"), options);
    utassert(MiniTextIs(a, MiniChild(a, root, 0), "~~kept~~"));
    utassert(MiniChild(a, root, 1)->kind == NodeKind::Paragraph);

    utassert(DecodeNamed(a, StrL("copy")).s == nullptr);
    utassert(base::StrEq(DecodeNamed(a, StrL("amp")), StrL("&")));
    utassert(base::StrEq(DecodeNumeric(a, StrL("41"), 16), StrL("A")));
    ArenaDelete(a);
}

#else

// The tree of a source, in an arena the caller owns.
// A node's strings are ArenaStr — an offset into the arena the tree was
// parsed into — so reading one back needs that arena. Every check here runs
// against the tree the line above it parsed, so this is where Parse leaves
// it rather than two hundred call sites threading it through.
static Arena* gParsedInto = nullptr;

static Node* Parse(Arena* a, const char* source) {
    gParsedInto = a;
    return ToMdast(a, Str(source), ParseOptions::Gfm());
}

// The n-th child, or null.
static Node* Child(Node* n, int32_t ix) {
    if (!n) {
        return nullptr;
    }
    return NodeChild(gParsedInto, n, (int)ix);
}

// One of a node's strings, out of the arena the tree was parsed into.
static Str S(const Node* n, NodeStrKind k) {
    return NodeGetStr(gParsedInto, n, k);
}

static bool Is(const Node* n, NodeStrKind k, const char* want) {
    return base::StrEq(S(n, k), want);
}

// Whether the node carries this string at all, which is what a null `s` used
// to mean.
static bool IsUnset(const Node* n, NodeStrKind k) {
    return !NodeHasStr(gParsedInto, n, k);
}

// The text of a node and everything under it.
static bool TextIs(Arena* a, Node* n, const char* want) {
    return n && base::StrEq(NodeToString(a, n), want);
}

// ─── src/util/constant.rs: `constants` ────────────────────────────────────

static void TestMarkdownConstants() {
    TestSuite("markdown constants");
    // The digits of the largest code point, in each base: "1114111" and
    // "10ffff".
    utassert(kCharacterReferenceDecimalSizeMax == len(fmt("%d", 0x10ffff)));
    utassert(kCharacterReferenceHexadecimalSizeMax == len(fmt("%x", 0x10ffff)));

    // The two runs and the table are three parallel things now, so the walk
    // has to agree with the offsets the table holds: an entry pointing a byte
    // into the middle of a name would still read as a string, just the wrong
    // one, and nothing else would notice.
    utassert(base::SeqStrCount(kCharacterReferenceNames) == 2125);
    utassert(base::SeqStrCount(kCharacterReferenceValues) == 2125);
    Str name = base::SeqStrFirst(kCharacterReferenceNames);
    Str value = base::SeqStrFirst(kCharacterReferenceValues);
    int32_t longestName = 0;
    for (int32_t i = 0; i < 2125; i++) {
        utassert(kCharacterReferences[i]
                     .nameOff == name.s - kCharacterReferenceNames);
        utassert(kCharacterReferences[i]
                     .valueOff == value.s - kCharacterReferenceValues);
        utassert(len(name) > 0);
        if (len(name) > longestName) {
            longestName = len(name);
        }
        name = base::SeqStrNext(name);
        value = base::SeqStrNext(value);
    }
    utassert(kCharacterReferenceNamedSizeMax == longestName);

    int32_t longestRaw = 0;
    int rawCount = 0;
    for (Str raw = base::SeqStrFirst(kHtmlRawNames); len(raw) > 0;
         raw = base::SeqStrNext(raw)) {
        if (len(raw) > longestRaw) {
            longestRaw = len(raw);
        }
        rawCount++;
    }
    utassert(rawCount == 4);
    utassert(kHtmlRawSizeMax == longestRaw);
    utassert(base::SeqStrCount(kHtmlBlockNames) == 62);

    // The names are sorted, which is what `DecodeNamed` binary searches.
    for (int32_t i = 1; i < 2125; i++) {
        Str prev =
            Str(kCharacterReferenceNames + kCharacterReferences[i - 1].nameOff);
        Str cur =
            Str(kCharacterReferenceNames + kCharacterReferences[i].nameOff);
        utassert(base::StrCmp(prev, cur) < 0);
    }
}

// ─── src/util/char.rs: `test_classify` ────────────────────────────────────

// `gpui::CharKind` is a different enum, so this one says which it means.
static void TestMarkdownClassify() {
    TestSuite("markdown classify");
    utassert(Classify(' ') == markdown::CharKind::Whitespace);
    utassert(Classify('.') == markdown::CharKind::Punctuation);
    utassert(Classify('a') == markdown::CharKind::Other);
    // Beyond ASCII, which is what util/unicode.rs is for.
    utassert(Classify(0x00a0) ==
             markdown::CharKind::Whitespace); // no-break space
    utassert(Classify(0x2014) == markdown::CharKind::Punctuation); // em dash
    utassert(Classify(0x00e9) == markdown::CharKind::Other);       // é
    // End of file counts as whitespace.
    utassert(Classify(-1) == markdown::CharKind::Whitespace);
}

// ─── src/configuration.rs: `test_constructs` / `test_parse_options` ───────

static void TestMarkdownOptions() {
    TestSuite("markdown options");
    Constructs constructs;
    utassert(constructs.attention);
    utassert(!constructs.gfmAutolinkLiteral);
    utassert(!constructs.frontmatter);

    constructs = Constructs::Gfm();
    utassert(constructs.attention);
    utassert(constructs.gfmAutolinkLiteral);
    utassert(!constructs.frontmatter);

    ParseOptions options;
    utassert(options.constructs.attention);
    utassert(!options.constructs.gfmAutolinkLiteral);
    utassert(options.gfmStrikethroughSingleTilde);

    options = ParseOptions::Gfm();
    utassert(options.constructs.gfmAutolinkLiteral);
}

// ─── src/util/character_reference.rs ──────────────────────────────────────

static void TestMarkdownCharacterReference(Arena* a) {
    TestSuite("markdown character reference");
    utassert(base::StrEq(DecodeNamed(a, StrL("amp")), StrL("&")));
    utassert(base::StrEq(DecodeNamed(a, StrL("AMP")), StrL("&")));
    utassert(base::StrEq(DecodeNamed(a, StrL("copy")), StrL("\xc2\xa9")));
    utassert(
        base::StrEq(DecodeNamed(a, StrL("CounterClockwiseContourIntegral")),
                    StrL("\xe2\x88\xb3")));
    // The two values `constant.cpp` writes as `\u` escapes rather than as
    // themselves, because gcc rejects a bidi character in a literal.
    utassert(base::StrEq(DecodeNamed(a, StrL("lrm")), StrL("\xe2\x80\x8e")));
    utassert(base::StrEq(DecodeNamed(a, StrL("rlm")), StrL("\xe2\x80\x8f")));
    // Not a name: `None`.
    utassert(DecodeNamed(a, StrL("nope")).s == nullptr);
    utassert(DecodeNamed(a, StrL("")).s == nullptr);

    utassert(base::StrEq(DecodeNumeric(a, StrL("65"), 10), StrL("A")));
    utassert(base::StrEq(DecodeNumeric(a, StrL("41"), 16), StrL("A")));
    // Out of range, a surrogate, and a forbidden control: U+FFFD.
    utassert(base::StrEq(DecodeNumeric(a, StrL("1114112"), 10),
                         StrL("\xef\xbf\xbd")));
    utassert(
        base::StrEq(DecodeNumeric(a, StrL("d800"), 16), StrL("\xef\xbf\xbd")));
    utassert(
        base::StrEq(DecodeNumeric(a, StrL("0"), 10), StrL("\xef\xbf\xbd")));
}

// ─── src/util/normalize_identifier.rs ─────────────────────────────────────

static void TestMarkdownNormalizeIdentifier(Arena* a) {
    TestSuite("markdown normalize identifier");
    utassert(base::StrEq(NormalizeIdentifier(a, StrL(" a ")), StrL("A")));
    utassert(base::StrEq(NormalizeIdentifier(a, StrL(" a\n b")), StrL("A B")));
    utassert(base::StrEq(NormalizeIdentifier(a, StrL("")), StrL("")));
    // The crate's own quirk, kept: whitespace after a word that starts at
    // offset 0 collapses to nothing rather than to a space, because `start`
    // is what it tests for "have we written anything yet". Its doc comment
    // says `a\t\r\nb` normalizes to `a b`; markdown-rs 1.0.0 answers `ab`,
    // and so does this, so a reference here matches the definitions it does.
    utassert(base::StrEq(NormalizeIdentifier(a, StrL("a\n b")), StrL("AB")));
    utassert(base::StrEq(NormalizeIdentifier(a, StrL("Foo\t\tBar")),
                         StrL("FOOBAR")));
}

static void TestMarkdownPositions() {
    TestSuite("markdown positions");
    // A node keeps offsets; the lines and the columns are counted out of the
    // source, by the rules the tokenizer counted them by on the way past.
    Str md = StrL("ab\ncd\n\nef");
    UnistPosition p = GetUnistPosition(md, 3, 8);
    utassert(p.start.line == 2 && p.start.column == 1 && p.start.offset == 3);
    utassert(p.end.line == 4 && p.end.column == 2 && p.end.offset == 8);

    // A CR that an LF follows is not a character of its own, so the line
    // ending counts once however it is written.
    utassert(GetUnistPosition(StrL("a\r\nb"), 3, 3).start.line == 2);
    utassert(GetUnistPosition(StrL("a\r\nb"), 3, 3).start.column == 1);
    // A lone CR is a line ending all the same.
    utassert(GetUnistPosition(StrL("a\rb"), 2, 2).start.line == 2);

    // A tab runs to the next stop four columns apart: from column 1 to
    // column 5, and a tab already on a stop is a full four wide.
    utassert(GetUnistPosition(StrL("\tx"), 1, 1).start.column == 5);
    utassert(GetUnistPosition(StrL("ab\tx"), 3, 3).start.column == 5);
    utassert(GetUnistPosition(StrL("abcd\tx"), 5, 5).start.column == 9);

    // A multi-byte character is as many columns as it is bytes, which is
    // what the tokenizer does and not what a reader would say.
    utassert(GetUnistPosition(StrL("\xc3\xa9x"), 2, 2).start.column == 3);

    // The whole of the source, and an offset past its end that clamps.
    UnistPosition all = GetUnistPosition(md, 0, (uint32_t)len(md));
    utassert(all.start.offset == 0 && all.end.offset == len(md));
    utassert(GetUnistPosition(md, 0, 999).end.offset == len(md));

    // The offset a heading starts at, counted the way the tokenizer would
    // have: no node keeps one any more, so this is the source and a number,
    // which is all the function ever wanted.
    utassert(GetUnistPosition(StrL("one\n\n# two\n"), 5, 10).start.line == 3);
    utassert(GetUnistPosition(StrL("one\n\n# two\n"), 5, 10).end.column == 6);
}

// ─── the tree ─────────────────────────────────────────────────────────────

static void TestMarkdownFlow(Arena* a) {
    TestSuite("markdown flow");
    Node* root = Parse(a, "# Hi *Earth*!\n");
    utassert(root->kind == NodeKind::Root);
    utassert(NodeChildCount(gParsedInto, root) == 1);
    Node* heading = Child(root, 0);
    utassert(heading->kind == NodeKind::Heading);
    utassert(NodePerKind(gParsedInto, heading) == 1);
    utassert(TextIs(a, heading, "Hi Earth!"));
    utassert(Child(heading, 1)->kind == NodeKind::Emphasis);

    root = Parse(a, "Setext\n===\n");
    utassert(Child(root, 0)->kind == NodeKind::Heading);
    utassert(NodePerKind(gParsedInto, Child(root, 0)) == 1);

    root = Parse(a, "a\n\n> b\n>\n> c\n");
    utassert(NodeChildCount(gParsedInto, root) == 2);
    utassert(Child(root, 0)->kind == NodeKind::Paragraph);
    utassert(Child(root, 1)->kind == NodeKind::Blockquote);
    utassert(NodeChildCount(gParsedInto, Child(root, 1)) == 2);

    root = Parse(a, "***\n");
    utassert(Child(root, 0)->kind == NodeKind::ThematicBreak);

    root = Parse(a, "```rust meta\nlet a = 1;\n```\n");
    Node* code = Child(root, 0);
    utassert(code->kind == NodeKind::Code);
    utassert(Is(code, NodeStrKind::Lang, "rust"));
    utassert(Is(code, NodeStrKind::Meta, "meta"));
    utassert(Is(code, NodeStrKind::Value, "let a = 1;"));

    root = Parse(a, "    indented\n");
    utassert(Child(root, 0)->kind == NodeKind::Code);
    utassert(Is(Child(root, 0), NodeStrKind::Value, "indented"));
    utassert(IsUnset(Child(root, 0), NodeStrKind::Lang));
}

static void TestMarkdownLists(Arena* a) {
    TestSuite("markdown lists");
    Node* root = Parse(a, "- one\n- two\n");
    Node* list = Child(root, 0);
    utassert(list->kind == NodeKind::List);
    utassert(!list->Has(NodeOrdered));
    utassert(!list->Has(NodeSpread));
    utassert(NodeChildCount(gParsedInto, list) == 2);
    utassert(TextIs(a, Child(list, 1), "two"));

    root = Parse(a, "3. three\n4. four\n");
    list = Child(root, 0);
    utassert(list->Has(NodeOrdered));
    utassert(list->Has(NodeHasStart));
    utassert(NodePerKind(gParsedInto, list) == 3);

    // A blank line between items makes the list loose.
    root = Parse(a, "- one\n\n- two\n");
    utassert(Child(root, 0)->Has(NodeSpread));

    // GFM task lists.
    root = Parse(a, "- [x] done\n- [ ] todo\n");
    list = Child(root, 0);
    utassert(Child(list, 0)->Has(NodeHasChecked));
    utassert(Child(list, 0)->Has(NodeChecked));
    utassert(Child(list, 1)->Has(NodeHasChecked));
    utassert(!Child(list, 1)->Has(NodeChecked));
    // The checkbox is not part of the text.
    utassert(TextIs(a, Child(list, 0), "done"));
}

static void TestMarkdownText(Arena* a) {
    TestSuite("markdown text");
    Node* root = Parse(a, "a **b** _c_ `d` ~~e~~\n");
    Node* p = Child(root, 0);
    utassert(p->kind == NodeKind::Paragraph);
    utassert(Child(p, 1)->kind == NodeKind::Strong);
    utassert(Child(p, 3)->kind == NodeKind::Emphasis);
    utassert(Child(p, 5)->kind == NodeKind::InlineCode);
    utassert(Is(Child(p, 5), NodeStrKind::Value, "d"));
    utassert(Child(p, 7)->kind == NodeKind::Delete);

    // A character reference decodes; an escape keeps the character.
    root = Parse(a, "&amp; &#65; \\*not em\\*\n");
    utassert(TextIs(a, Child(root, 0), "& A *not em*"));

    // Two trailing spaces are a hard break.
    root = Parse(a, "a  \nb\n");
    p = Child(root, 0);
    utassert(Child(p, 1)->kind == NodeKind::Break);

    // A backslash at the end of a line is one too.
    root = Parse(a, "a\\\nb\n");
    utassert(Child(Child(root, 0), 1)->kind == NodeKind::Break);
}

static void TestMarkdownLinks(Arena* a) {
    TestSuite("markdown links");
    Node* root = Parse(a, "[text](/url \"title\")\n");
    Node* link = Child(Child(root, 0), 0);
    utassert(link->kind == NodeKind::Link);
    utassert(Is(link, NodeStrKind::Url, "/url"));
    utassert(Is(link, NodeStrKind::Title, "title"));
    utassert(TextIs(a, link, "text"));

    root = Parse(a, "![alt](/img.png)\n");
    Node* image = Child(Child(root, 0), 0);
    utassert(image->kind == NodeKind::Image);
    utassert(Is(image, NodeStrKind::Url, "/img.png"));
    utassert(Is(image, NodeStrKind::Alt, "alt"));

    // A definition and the three kinds of reference to it.
    root = Parse(a, "[Foo]: /f\n\n[Foo]\n\n[bar][Foo]\n\n[Foo][]\n");
    Node* definition = Child(root, 0);
    utassert(definition->kind == NodeKind::Definition);
    utassert(Is(definition, NodeStrKind::Identifier, "foo"));
    utassert(Is(definition, NodeStrKind::Url, "/f"));
    Node* shortcut = Child(Child(root, 1), 0);
    utassert(shortcut->kind == NodeKind::LinkReference);
    utassert(NodeRefKind(shortcut) == ReferenceKind::Shortcut);
    utassert(Is(shortcut, NodeStrKind::Identifier, "foo"));
    utassert(NodeRefKind(Child(Child(root, 2), 0)) == ReferenceKind::Full);
    utassert(NodeRefKind(Child(Child(root, 3), 0)) == ReferenceKind::Collapsed);

    // An undefined reference is not a link at all.
    root = Parse(a, "[nope]\n");
    utassert(Child(Child(root, 0), 0)->kind == NodeKind::Text);

    // Autolinks, and the GFM literal kind.
    root = Parse(a, "<https://x.com/> and www.y.com and a@b.com\n");
    Node* p = Child(root, 0);
    utassert(Child(p, 0)->kind == NodeKind::Link);
    utassert(Is(Child(p, 0), NodeStrKind::Url, "https://x.com/"));
    utassert(Is(Child(p, 2), NodeStrKind::Url, "http://www.y.com"));
    utassert(Is(Child(p, 4), NodeStrKind::Url, "mailto:a@b.com"));
}

static void TestMarkdownTable(Arena* a) {
    TestSuite("markdown table");
    Node* root = Parse(a,
                       "| a | b | c | d |\n"
                       "| - |:- |:-:| -:|\n"
                       "| 1 | 2 | 3 | 4 |\n");
    Node* table = Child(root, 0);
    utassert(table->kind == NodeKind::Table);
    Arena* into = gParsedInto;
    utassert(ArenaAlignCount(into, NodePerKind(into, table)) == 4);
    utassert(ArenaAlignAt(into, NodePerKind(into, table), 0) ==
             AlignKind::None);
    utassert(ArenaAlignAt(into, NodePerKind(into, table), 1) ==
             AlignKind::Left);
    utassert(ArenaAlignAt(into, NodePerKind(into, table), 2) ==
             AlignKind::Center);
    utassert(ArenaAlignAt(into, NodePerKind(into, table), 3) ==
             AlignKind::Right);
    // Past the end, and a table with no alignments at all.
    utassert(ArenaAlignAt(into, NodePerKind(into, table), 4) ==
             AlignKind::None);
    utassert(ArenaAlignCount(into, kArenaAlignNone) == 0);
    utassert(NodeChildCount(gParsedInto, table) == 2);
    Node* head = Child(table, 0);
    utassert(head->kind == NodeKind::TableRow);
    utassert(NodeChildCount(gParsedInto, head) == 4);
    utassert(Child(head, 0)->kind == NodeKind::TableCell);
    utassert(TextIs(a, Child(head, 2), "c"));
    utassert(TextIs(a, Child(Child(table, 1), 3), "4"));

    // The column count is varint-encoded, so 128 columns is where it stops
    // fitting in one byte. A table that wide is absurd and costs two.
    const int32_t wide = 130;
    TempStr src = AllocStrTemp(2 * (4 * 130 + 2));
    int32_t at = 0;
    for (int32_t row = 0; row < 2; row++) {
        for (int32_t i = 0; i < wide; i++) {
            // The four alignments cycling, so the packing is written and
            // read back at every offset within a byte.
            const char* cell = row == 0 ? "| h " : "| - ";
            if (row == 1 && i % 4 == 1) {
                cell = "|:- ";
            } else if (row == 1 && i % 4 == 2) {
                cell = "|:-:";
            } else if (row == 1 && i % 4 == 3) {
                cell = "| -:";
            }
            memcpy(src.s + at, cell, 4);
            at += 4;
        }
        src.s[at++] = '|';
        src.s[at++] = '\n';
    }
    src.s[at] = 0;
    src.len = at;
    Node* wideRoot = Parse(a, src.s);
    Node* wideTable = Child(wideRoot, 0);
    utassert(wideTable->kind == NodeKind::Table);
    utassert(ArenaAlignCount(gParsedInto,
                             NodePerKind(gParsedInto, wideTable)) == wide);
    for (int32_t i = 0; i < wide; i++) {
        AlignKind want = AlignKind::None;
        if (i % 4 == 1) {
            want = AlignKind::Left;
        } else if (i % 4 == 2) {
            want = AlignKind::Center;
        } else if (i % 4 == 3) {
            want = AlignKind::Right;
        }
        utassert(ArenaAlignAt(gParsedInto, NodePerKind(gParsedInto, wideTable),
                              i) == want);
    }
}

static void TestMarkdownHtmlAndFootnotes(Arena* a) {
    TestSuite("markdown html");
    Node* root = Parse(a, "<div>\n  <b>x</b>\n</div>\n");
    utassert(Child(root, 0)->kind == NodeKind::Html);
    utassert(
        Is(Child(root, 0), NodeStrKind::Value, "<div>\n  <b>x</b>\n</div>"));

    root = Parse(a, "a <b>c</b> d\n");
    Node* p = Child(root, 0);
    utassert(Child(p, 1)->kind == NodeKind::Html);
    utassert(Is(Child(p, 1), NodeStrKind::Value, "<b>"));

    TestSuite("markdown footnotes");
    root = Parse(a, "Call[^1].\n\n[^1]: The note.\n");
    Node* call = Child(Child(root, 0), 1);
    utassert(call->kind == NodeKind::FootnoteReference);
    utassert(Is(call, NodeStrKind::Identifier, "1"));
    Node* definition = Child(root, 1);
    utassert(definition->kind == NodeKind::FootnoteDefinition);
    utassert(Is(definition, NodeStrKind::Identifier, "1"));
    utassert(TextIs(a, definition, "The note."));
}

// A document with one of everything, which is what the story's README looks
// like: it is here to catch a crash rather than to pin a shape.
static void TestMarkdownDocument(Arena* a) {
    TestSuite("markdown document");
    Node* root = Parse(a,
                       "# Title\n"
                       "\n"
                       "Text with **bold**, a [link](/l) and `code`.\n"
                       "\n"
                       "> A quote\n"
                       "> over two lines.\n"
                       "\n"
                       "1. first\n"
                       "2. second\n"
                       "   - nested\n"
                       "\n"
                       "| a | b |\n"
                       "| - | - |\n"
                       "| 1 | 2 |\n"
                       "\n"
                       "```c\n"
                       "int main(void) { return 0; }\n"
                       "```\n"
                       "\n"
                       "---\n"
                       "\n"
                       "![img](/i.png)\n");
    utassert(NodeChildCount(gParsedInto, root) == 8);
    utassert(Child(root, 0)->kind == NodeKind::Heading);
    utassert(Child(root, 1)->kind == NodeKind::Paragraph);
    utassert(Child(root, 2)->kind == NodeKind::Blockquote);
    utassert(Child(root, 3)->kind == NodeKind::List);
    utassert(Child(root, 4)->kind == NodeKind::Table);
    utassert(Child(root, 5)->kind == NodeKind::Code);
    utassert(Child(root, 6)->kind == NodeKind::ThematicBreak);
    utassert(Child(root, 7)->kind == NodeKind::Paragraph);

    // The empty document is a root with nothing in it.
    root = ToMdast(a, StrL(""), ParseOptions::Gfm());
    utassert(root->kind == NodeKind::Root);
    utassert(NodeChildCount(gParsedInto, root) == 0);
}

void TestMarkdown() {
    Arena* a = ArenaNew();
    TestMarkdownConstants();
    TestMarkdownClassify();
    TestMarkdownOptions();
    TestMarkdownCharacterReference(a);
    TestMarkdownNormalizeIdentifier(a);
    TestMarkdownFlow(a);
    TestMarkdownLists(a);
    TestMarkdownText(a);
    TestMarkdownLinks(a);
    TestMarkdownTable(a);
    TestMarkdownHtmlAndFootnotes(a);
    TestMarkdownDocument(a);
    TestMarkdownPositions();
    ArenaDelete(a);
}

#endif // GPUI_MARKDOWN_MINI
