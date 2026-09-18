/* The code-block scanner — crates/ui/src/highlighter without tree-sitter.
   Rust's own tests drive a real parser; these pin what a scanner can answer:
   which language a fence names, and which token every byte belongs to. */

#include "Test.h"

using namespace gpui::component;

// The token covering the first byte of `needle`, or Text when the scan never
// reaches it.
static SyntaxTok TokAt(SyntaxLang lang, const char* src, const char* needle) {
    Str s(src);
    Str n(needle);
    int want = -1;
    for (int i = 0; i + len(n) <= len(s); i++) {
        if (StrEq(Str(s.s + i, len(n)), n)) {
            want = i;
            break;
        }
    }
    if (want < 0) {
        return SyntaxTok::Text;
    }
    SyntaxLexer lx;
    SyntaxLexStart(&lx, lang, s);
    while (SyntaxLexNext(&lx)) {
        int off = (int)(lx.text.s - s.s);
        if (want >= off && want < off + len(lx.text)) {
            return lx.tok;
        }
    }
    return SyntaxTok::Text;
}

// Every byte belongs to exactly one token, in order: what the renderer walks
// to put the block back together.
static bool Partitions(SyntaxLang lang, const char* src) {
    Str s(src);
    SyntaxLexer lx;
    SyntaxLexStart(&lx, lang, s);
    int at = 0;
    while (SyntaxLexNext(&lx)) {
        if (lx.text.s != s.s + at || len(lx.text) <= 0) {
            return false;
        }
        at += len(lx.text);
    }
    return at == len(s);
}

static bool NameIs(SyntaxLang lang, const char* want) {
    Str got = SyntaxLangName(lang);
    return base::StrEq(got, want);
}

static void TestSyntaxLangFor() {
    utassert(NameIs(SyntaxLangFor(StrL("cpp")), "cpp"));
    utassert(NameIs(SyntaxLangFor(StrL("C++")), "cpp"));
    utassert(NameIs(SyntaxLangFor(StrL("rs")), "rust"));
    utassert(NameIs(SyntaxLangFor(StrL("tsx")), "js"));
    // An HTML <code class="language-python"> names its language the long way.
    utassert(NameIs(SyntaxLangFor(StrL("language-python")), "python"));
    // A fence may carry more than the name.
    utassert(NameIs(SyntaxLangFor(StrL("go title=main.go")), "go"));
    utassert(SyntaxLangFor(StrL("")) == SyntaxLangNone);
    utassert(SyntaxLangFor(StrL("brainfuck")) == SyntaxLangNone);
}

static void TestSyntaxCpp() {
    SyntaxLang cpp = SyntaxLangFor(StrL("cpp"));
    const char* src =
        "#include \"a.h\"\n"
        "// comment\n"
        "int Add(int a) { return a + 0x2a; /* tail */ }\n";
    utassert(Partitions(cpp, src));
    utassert(TokAt(cpp, src, "#include") == SyntaxTok::Keyword);
    utassert(TokAt(cpp, src, "\"a.h\"") == SyntaxTok::String);
    utassert(TokAt(cpp, src, "// comment") == SyntaxTok::Comment);
    utassert(TokAt(cpp, src, "int Add") == SyntaxTok::Type);
    utassert(TokAt(cpp, src, "Add") == SyntaxTok::Function);
    utassert(TokAt(cpp, src, "return") == SyntaxTok::Keyword);
    utassert(TokAt(cpp, src, "0x2a") == SyntaxTok::Number);
    utassert(TokAt(cpp, src, "/* tail */") == SyntaxTok::Comment);
    // C does not capitalize its types, so a name is a name.
    utassert(TokAt(cpp, "Widget w;", "Widget") == SyntaxTok::Text);
}

static void TestSyntaxRust() {
    SyntaxLang rust = SyntaxLangFor(StrL("rust"));
    const char* src = "fn main() { let v: Vec<u8> = vec![]; true }";
    utassert(Partitions(rust, src));
    utassert(TokAt(rust, src, "fn") == SyntaxTok::Keyword);
    utassert(TokAt(rust, src, "main") == SyntaxTok::Function);
    utassert(TokAt(rust, src, "u8") == SyntaxTok::Type);
    // capsAreTypes: Rust spells its types with a capital, so Vec is one.
    utassert(TokAt(rust, src, "Vec") == SyntaxTok::Type);
    utassert(TokAt(rust, src, "true") == SyntaxTok::Boolean);
}

static void TestSyntaxPython() {
    SyntaxLang py = SyntaxLangFor(StrL("py"));
    const char* src =
        "def f(x):  # note\n"
        "    \"\"\"doc\n"
        "    string\"\"\"\n"
        "    return None\n";
    utassert(Partitions(py, src));
    utassert(TokAt(py, src, "def") == SyntaxTok::Keyword);
    utassert(TokAt(py, src, "f(") == SyntaxTok::Function);
    utassert(TokAt(py, src, "# note") == SyntaxTok::Comment);
    // A triple-quoted string spans lines; a scanner that stopped at the
    // first line would paint the rest as code.
    utassert(TokAt(py, src, "doc") == SyntaxTok::String);
    utassert(TokAt(py, src, "string") == SyntaxTok::String);
    utassert(TokAt(py, src, "None") == SyntaxTok::Boolean);
}

static void TestSyntaxJson() {
    SyntaxLang json = SyntaxLangFor(StrL("json"));
    const char* src = "{\"name\": \"gpui\", \"n\": 3, \"ok\": true}";
    utassert(Partitions(json, src));
    // The key is a property, the value beside it a string.
    utassert(TokAt(json, src, "\"name\"") == SyntaxTok::Property);
    utassert(TokAt(json, src, "\"gpui\"") == SyntaxTok::String);
    utassert(TokAt(json, src, "3") == SyntaxTok::Number);
    utassert(TokAt(json, src, "true") == SyntaxTok::Boolean);
}

static void TestSyntaxMarkup() {
    SyntaxLang html = SyntaxLangFor(StrL("html"));
    const char* src = "<!-- hi --><a href=\"/x\">text</a>";
    utassert(Partitions(html, src));
    utassert(TokAt(html, src, "<!-- hi -->") == SyntaxTok::Comment);
    utassert(TokAt(html, src, "a href") == SyntaxTok::Tag);
    utassert(TokAt(html, src, "href") == SyntaxTok::Attribute);
    utassert(TokAt(html, src, "\"/x\"") == SyntaxTok::String);
    utassert(TokAt(html, src, "text") == SyntaxTok::Text);
    // The name after the closing `</` is the tag again, not an attribute.
    utassert(TokAt(html, src, "a>") == SyntaxTok::Tag);
}

static void TestSyntaxShellAndSql() {
    SyntaxLang sh = SyntaxLangFor(StrL("bash"));
    const char* src = "for f in *.c; do echo 'a $b'; done # done\n";
    utassert(Partitions(sh, src));
    utassert(TokAt(sh, src, "for") == SyntaxTok::Keyword);
    // A shell's single quotes take no escapes and no expansion.
    utassert(TokAt(sh, src, "'a $b'") == SyntaxTok::String);
    utassert(TokAt(sh, src, "# done") == SyntaxTok::Comment);

    // SQL is written in either case and means the same thing.
    SyntaxLang sql = SyntaxLangFor(StrL("sql"));
    utassert(TokAt(sql, "SELECT a FROM t -- all", "SELECT") ==
             SyntaxTok::Keyword);
    utassert(TokAt(sql, "select a from t", "select") == SyntaxTok::Keyword);
    utassert(TokAt(sql, "SELECT a FROM t -- all", "-- all") ==
             SyntaxTok::Comment);
}

// An unterminated string or comment must not swallow the rest of the block.
static void TestSyntaxUnterminated() {
    SyntaxLang cpp = SyntaxLangFor(StrL("cpp"));
    const char* src = "char* s = \"oops;\nint n = 1;\n";
    utassert(Partitions(cpp, src));
    utassert(TokAt(cpp, src, "int n") == SyntaxTok::Type);
    utassert(Partitions(cpp, "/* never closed\nint n;\n"));
}

// A language the table does not know hands the whole block back as one run,
// which is what the renderer paints unhighlighted.
static void TestSyntaxUnknownLang() {
    SyntaxLexer lx;
    Str src = StrL("anything at all");
    SyntaxLexStart(&lx, SyntaxLangNone, src);
    utassert(SyntaxLexNext(&lx));
    utassert(lx.tok == SyntaxTok::Text);
    utassert(len(lx.text) == len(src));
    utassert(!SyntaxLexNext(&lx));
}

// Markdown: the line shapes and the marks inside a line. Upstream reads a
// tree-sitter grammar with an injection per fence; what a scanner can answer
// is which of these a byte belongs to.
static void TestSyntaxMarkdown() {
    SyntaxLang md = SyntaxLangFor(StrL("markdown"));
    utassert(md != SyntaxLangNone);
    utassert(md == SyntaxLangFor(StrL("md")));

    const char* src =
        "# Heading **one**\n"
        "\n"
        "Prose with `a span` and [a link](https://example.com/x) in it.\n"
        "\n"
        "- a list item\n"
        "2. an ordered one\n"
        "> a quote\n"
        "\n"
        "An <img src=\"x.png\" /> tag.\n"
        "\n"
        "```rust\n"
        "let x = [1];\n"
        "```\n"
        "\n"
        "After the fence.\n";

    utassert(TokAt(md, src, "# Heading") == SyntaxTok::Title);
    // The emphasis inside a heading is the heading's own colour: the whole
    // line is one token.
    utassert(TokAt(md, src, "**one**") == SyntaxTok::Title);
    utassert(TokAt(md, src, "Prose with") == SyntaxTok::Text);
    utassert(TokAt(md, src, "`a span`") == SyntaxTok::Literal);
    utassert(TokAt(md, src, "[a link]") == SyntaxTok::Function);
    utassert(TokAt(md, src, "(https://example.com/x)") == SyntaxTok::Comment);
    utassert(TokAt(md, src, "in it.") == SyntaxTok::Text);
    utassert(TokAt(md, src, "- a list") == SyntaxTok::Keyword);
    utassert(TokAt(md, src, "a list item") == SyntaxTok::Text);
    utassert(TokAt(md, src, "2. an ordered") == SyntaxTok::Keyword);
    utassert(TokAt(md, src, "> a quote") == SyntaxTok::Comment);
    utassert(TokAt(md, src, "<img src=") == SyntaxTok::Tag);
    utassert(TokAt(md, src, "tag.") == SyntaxTok::Text);
    // The fence is `@text.literal` on the block, including the delimiters
    // and the verbatim body — `[1]` included, not scanned as rust.
    utassert(TokAt(md, src, "```rust") == SyntaxTok::Literal);
    utassert(TokAt(md, src, "let x = [1];") == SyntaxTok::Literal);
    utassert(TokAt(md, src, "After the fence.") == SyntaxTok::Text);
    utassert(Partitions(md, src));

    // A `(` that follows no link text is prose, and an unclosed `[` is too.
    const char* plain = "Not a link (just parens) and [an unclosed one.\n";
    utassert(TokAt(md, plain, "(just parens)") == SyntaxTok::Text);
    utassert(TokAt(md, plain, "[an unclosed") == SyntaxTok::Text);
    utassert(Partitions(md, plain));
}

static void TestSyntaxColors() {
    Rgba fg = Rgb(1, 2, 3);
    // theme/default-theme.json: keyword is #0433ff light, #c28b12 dark.
    Rgba light = SyntaxTokColor(SyntaxTok::Keyword, ThemeMode::Light, fg);
    utassert(light.r == 0x04 && light.g == 0x33 && light.b == 0xff);
    Rgba dark = SyntaxTokColor(SyntaxTok::Keyword, ThemeMode::Dark, fg);
    utassert(dark.r == 0xc2 && dark.g == 0x8b && dark.b == 0x12);
    // Text has no entry: the block's own foreground stands.
    Rgba text = SyntaxTokColor(SyntaxTok::Text, ThemeMode::Dark, fg);
    utassert(text.r == 1 && text.g == 2 && text.b == 3);
}

void TestSyntax() {
    TestSuite("Syntax");
    TestSyntaxLangFor();
    TestSyntaxCpp();
    TestSyntaxRust();
    TestSyntaxPython();
    TestSyntaxJson();
    TestSyntaxMarkup();
    TestSyntaxMarkdown();
    TestSyntaxShellAndSql();
    TestSyntaxUnterminated();
    TestSyntaxUnknownLang();
    TestSyntaxColors();
}
