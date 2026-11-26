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
    utassert(TokAt(rust, src, "vec") == SyntaxTok::Function);
}

static void TestRustLiteralAndCommentBoundaries() {
    SyntaxLang rust = SyntaxLangFor(StrL("rust"));
    const char* src =
        "fn get<'a>(v: &'a str) -> &'a str { return v; }\n"
        "let r#type = r##\"raw \"# quote\nstays raw\"##;\n"
        "let bytes = br#\"raw bytes\"#; let cstr = cr\"raw c\";\n"
        "let byte = b'x'; let mark = '\\n'; let wide = \"two\nlines\";\n"
        "/* outside /* inside */ still a comment */\n"
        "'outer: loop { break 'outer; }\n";
    utassert(Partitions(rust, src));
    utassert(TokAt(rust, src, "return") == SyntaxTok::Keyword);
    utassert(TokAt(rust, src, "str)") == SyntaxTok::Type);
    utassert(TokAt(rust, src, "type =") == SyntaxTok::Text);
    utassert(TokAt(rust, src, "stays raw") == SyntaxTok::String);
    utassert(TokAt(rust, src, "br#\"") == SyntaxTok::String);
    utassert(TokAt(rust, src, "cr\"") == SyntaxTok::String);
    utassert(TokAt(rust, src, "b'x'") == SyntaxTok::String);
    utassert(TokAt(rust, src, "'\\n'") == SyntaxTok::String);
    utassert(TokAt(rust, src, "lines\"") == SyntaxTok::String);
    utassert(TokAt(rust, src, "still a comment") == SyntaxTok::Comment);
    utassert(TokAt(rust, src, "loop") == SyntaxTok::Keyword);
    utassert(TokAt(rust, "Type != other", "Type") == SyntaxTok::Type);
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

// A document past the sync budget waits out a debounce before it is lexed
// on the pool. One timer keeps that time, not an animation frame requested
// every frame for the 150 ms.
struct LargeEditorView {
    InputState input;
    static El* Render(LargeEditorView* self, Ctx* cx) {
        return component::Editor::New(cx, StrL("large"), &self->input)
            ->H(200)
            ->Language(StrL("rust"))
            ->IntoEl();
    }
};

static void TheLexDebounceIsOneTimer() {
    App* app = TestAppNew();
    component::Init(app);
    Entity<LargeEditorView> view = EntityNew<LargeEditorView>(app);
    StrBuilder text;
    while (len(text) < 300 * 1024) {
        text.Append(StrL("fn main() { let x = 1; }\n"));
    }
    Window* win = TestWindowOpen(app, view, 400, 300);
    TestDraw(win);
    InputSetValue(&view.Get(app)->input, Str(text.els, len(text)));
    TestDraw(win);
    utassert(!win->animFrame);
    int timers = len(win->timers);
    utassert(timers >= 1);
    // Another frame inside the debounce arms nothing new.
    TestDraw(win);
    utassert(!win->animFrame);
    utassert(len(win->timers) == timers);
    TestAdvanceClock(app, 400);
    TestAppFree(app);
}

struct FoldLineView {
    InputState input;
    Str language;

    static El* Render(FoldLineView* self, Ctx* cx) {
        return component::Editor::New(cx, StrL("fold-line"), &self->input)
            ->Language(self->language)
            ->Folding(true)
            ->IntoEl();
    }
};

// highlighter/input_adapter.rs: statements ending with a closer, a suffix
// of code, a trailing comment, the last byte, or an ordinary line break.
static void FoldCandidatesCoverTheirLastLineWhenItCanBeHidden() {
    struct Case {
        const char* language;
        const char* code;
        FoldRange folds[6];
        int count;
    };
    const Case cases[] = {
        {"sql", "SELECT id\nFROM users\nWHERE id = 1;\n", {{0, 3}, {1, 3}}, 2},
        {"sql", "SELECT 1\nFROM t\nWHERE a = 1", {{0, 3}, {1, 3}}, 2},
        {"sql", "SELECT 1\nFROM t\nWHERE a = 1; SELECT 3\n", {{0, 2}}, 1},
        {"sql",
         "SELECT id\nFROM users\nWHERE id = 1; -- done\n",
         {{0, 3}, {1, 3}},
         2},
        {"sql", "SELECT id\nFROM users;\n", {{0, 2}}, 1},
        {"sql", "SELECT 1;\n", {}, 0},
        {"json", "{\n  \"a\": 1,\n  \"b\": 2\n}", {{0, 3}}, 1},
        {"rust", "fn main() {\n    let x = 1;\n}\n", {{0, 2}}, 1},
        {"rust",
         "fn sample<'a>(s: &'a str) {\n"
         "    let raw = r#\"{ \" }\"#;\n"
         "    /* outer /* inner */ } */\n"
         "}\n",
         {{0, 3}},
         1},
        {"python",
         "if ok:\n    a()\n    b()\nelif alternate:\n    c()\n"
         "else:\n    d()\n    e()\noutside()",
         {{0, 8}, {1, 3}, {3, 5}, {5, 8}, {6, 8}},
         5},
        {"python",
         "@deco(\n    x,\n)\n@second\nasync def f(\n    one,\n    two,\n"
         "):\n    with one:\n        return two\noutside()",
         {{0, 10}, {4, 10}, {8, 10}},
         3},
        {"python",
         "def f():\n    # leading\n    a()\n\n    # indented trailing\n"
         "# dedented trailing\n\noutside()",
         {{0, 5}, {2, 5}},
         2},
        {"python",
         "match value:\n    case 0:\n        zero()\n    case 1:\n"
         "        one()\n        extra()\n    case _:\n        more()\nnext()",
         {{0, 8}, {1, 3}, {3, 6}, {4, 6}, {6, 8}},
         5},
        {"python",
         "def f():\n    \"\"\"from top\nif example:\n    pass\n    \"\"\"\n"
         "    x = (\n        1 + 2\n    )\nnext()",
         {{0, 8}, {1, 8}, {5, 7}},
         3},
        {"python",
         "if x \\\n   and y:\n    a()\n    b()\nelse:\n    c()\n",
         {{0, 6}, {2, 4}, {4, 6}},
         3},
        {"python",
         "if flag: a()\nelse: b()\nfor x in values: print(x)\nelse:\n"
         "    print(\"end\")\n",
         {{0, 2}, {2, 5}, {3, 5}},
         3},
        {"python",
         "try:\n    first()\nexcept ValueError:\n    fail()\nelse:\n"
         "    pass\nfinally:\n    clean()\nnext()",
         {{0, 8}, {2, 4}, {4, 6}, {6, 8}},
         4},
        {"python",
         "def f():\n\tif True:\n\t\tpass\n\tprint(\"end\")\noutside()\n",
         {{0, 4}, {1, 4}},
         2},
        {"python",
         "result = (\n    a or\n    b and\n    c\n)\n",
         {{0, 4}, {1, 4}, {2, 4}},
         3},
        {"python",
         "logger(\n    \"first \"\n    f\"second {amount}\"\n    r\" third\",\n"
         "    other,\n)\n",
         {{0, 5}, {1, 4}},
         2},
        {"python",
         "raise ValueError(\n    \"invalid %s\"\n    % (value,)\n)\n",
         {{0, 3}, {1, 3}},
         2},
        {"python",
         "result = (\n    a **\n    b **\n    c\n)\n",
         {{0, 4}, {1, 4}, {2, 4}},
         3},
        {"python",
         "result = (\n    name is not\n    other and\n    name not in\n    "
         "bag\n)\n",
         {{0, 5}, {1, 5}, {3, 5}},
         3},
        {"python",
         "result = (\n    first,\n    second,\n    third,\n)\n",
         {{0, 4}},
         1},
        {"python",
         "result = first or \\\n    second and \\\n    third\n",
         {{0, 3}, {1, 3}},
         2},
        {"python",
         "result = (\n    -\n    number **\n    3\n)\n",
         {{0, 4}, {1, 4}, {2, 4}},
         3},
        {"python",
         "f(\n    \"before\" if ok\n      else \"after\",\n    \"other\",\n)\n",
         {{0, 4}, {1, 3}},
         2},
        {"python",
         "result = [\n    row\n    for row in data\n    if row is not "
         "None\n]\n",
         {{0, 4}},
         1},
        {"python", "x, y = \\\n    values\n", {{0, 2}}, 1},
        {"python", "from pkg import x, \\\n    y, \\\n    z\n", {{0, 3}}, 1},
        {"python", "x = first or \\\n    second; other()\n", {}, 0},
        {"python",
         "value.update({\n    \"a\": 1,\n    \"b\": 2,\n})\n",
         {{0, 3}},
         1},
        {"python", "(first,\n second) = values\n", {{0, 2}}, 1},
        {"python",
         "vals = (\n    a if b else\n    c if d else\n    e\n)\n",
         {{0, 4}, {1, 4}, {2, 4}},
         3},
    };
    App* app = TestAppNew();
    component::Init(app);
    auto entity = EntityNew<FoldLineView>(app);
    auto* view = entity.Get(app);
    view->input.kind = InputKind::Editor;
    Window* win = TestWindowOpen(app, entity, 420, 300);
    for (const auto& test : cases) {
        view->language = Str(test.language);
        InputSetValue(&view->input, Str(test.code));
        TestDraw(win);
        const auto& actual = view->input.folds.candidates;
        utassert(len(actual) == test.count);
        for (int i = 0; i < len(actual) && i < test.count; i++) {
            utassert(actual[i].startLine == test.folds[i].startLine &&
                     actual[i].endLine == test.folds[i].endLine);
        }
    }
    TestAppFree(app);
}

// input/input.rs: compare incremental editing with a fresh installation of
// the same document. The scanner recomputes the whole candidate list.
static void FoldCandidatesSurviveEditsBesideANode() {
    App* app = TestAppNew();
    component::Init(app);
    auto entity = EntityNew<FoldLineView>(app);
    auto* view = entity.Get(app);
    InputState* input = &view->input;
    input->kind = InputKind::Editor;
    view->language = StrL("sql");
    Window* win = TestWindowOpen(app, entity, 420, 300);
    auto checkAgainstReload = [&](int expected = -1) {
        Vec<FoldRange> edited;
        for (const auto& candidate : input->folds.candidates) {
            VecAppend(edited, candidate);
        }
        Str copy = StrDup(InputValue(input));
        InputSetValue(input, Str{});
        InputSetValue(input, copy);
        StrFree(copy);
        TestDraw(win);
        const auto& fresh = input->folds.candidates;
        utassert(len(fresh) > 0 && len(fresh) == len(edited));
        if (expected >= 0) utassert(len(fresh) == expected);
        for (int i = 0; i < len(fresh) && i < len(edited); i++) {
            utassert(fresh[i].startLine == edited[i].startLine &&
                     fresh[i].endLine == edited[i].endLine);
        }
        VecReset(edited);
    };

    Str typed =
        StrL("SELECT id, name\nFROM (\n  SELECT 1\n) t\nWHERE id > 0;\n");
    for (int i = 0; i < len(typed); i++) {
        InputInsert(input, app, win, Str(typed.s + i, 1));
        TestDraw(win);
    }
    checkAgainstReload();

    Str sql = StrL("SELECT id\nFROM t\nWHERE id = 1;\nSELECT 2;\n");
    InputSetValue(input, sql);
    TestDraw(win);
    int nextStatement = StrFind(sql, StrL("SELECT 2"));
    InputSetSelectedRange(input, app, win, nextStatement, nextStatement);
    InputInsert(input, app, win, StrL(" "));
    TestDraw(win);
    checkAgainstReload();

    view->language = StrL("rust");
    Str rust = StrL("fn a() {\n    1;\n}\n\n\nfn b() {\n    2;\n}\n");
    int fnB = StrFind(rust, StrL("fn b"));
    int afterA = StrFind(rust, StrL("}")) + 1;
    int afterB = len(rust) - 1;
    auto edit = [&](const Selection* ranges, const Str* insertions, int n) {
        InputSetValue(input, rust);
        TestDraw(win);
        utassert(
            InputReplaceTextInRanges(input, app, win, ranges, insertions, n));
        TestDraw(win);
        checkAgainstReload(2);
    };
    const Selection beforeB = {fnB, fnB};
    const Str comment = StrL("// b\n");
    edit(&beforeB, &comment, 1);
    const Selection blankLines = {afterA, fnB - 1};
    const Str deletion = {};
    edit(&blankLines, &deletion, 1);
    const Selection afterBraces[] = {{afterA, afterA}, {afterB, afterB}};
    const Str spaces[] = {StrL(" "), StrL(" ")};
    edit(afterBraces, spaces, 2);
    TestAppFree(app);
}

void TestSyntax() {
    TestSuite("Syntax");
    TestSyntaxLangFor();
    TestSyntaxCpp();
    TestSyntaxRust();
    TestRustLiteralAndCommentBoundaries();
    TestSyntaxPython();
    TestSyntaxJson();
    TestSyntaxMarkup();
    TestSyntaxMarkdown();
    TestSyntaxShellAndSql();
    TestSyntaxUnterminated();
    TestSyntaxUnknownLang();
    TestSyntaxColors();
    TheLexDebounceIsOneTimer();
    FoldCandidatesCoverTheirLastLineWhenItCanBeHidden();
    FoldCandidatesSurviveEditsBesideANode();
}
