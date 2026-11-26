#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static const char* kEditorDefault =
    "use std::collections::HashMap;\n"
    "\n"
    "#[derive(Debug, Clone)]\n"
    "struct Workspace {\n"
    "    name: String,\n"
    "    files: HashMap<String, usize>,\n"
    "}\n"
    "\n"
    "impl Workspace {\n"
    "    fn new(name: impl Into<String>) -> Self {\n"
    "        Self {\n"
    "            name: name.into(),\n"
    "            files: HashMap::new(),\n"
    "        }\n"
    "    }\n"
    "\n"
    "    fn index(&mut self, path: &str, lines: usize) {\n"
    "        // Keep the latest line count for each source file.\n"
    "        self.files.insert(path.to_owned(), lines);\n"
    "    }\n"
    "\n"
    "    fn summary(&self) -> String {\n"
    "        let total: usize = self.files.values().sum();\n"
    "        format!(\"{}: {} files, {total} lines\", self.name, "
    "self.files.len())\n"
    "    }\n"
    "}\n"
    "\n"
    "fn main() {\n"
    "    let mut workspace = Workspace::new(\"gpui-component\");\n"
    "    workspace.index(\"src/main.rs\", 128);\n"
    "    workspace.index(\"src/editor.rs\", 372);\n"
    "    println!(\"{}\", workspace.summary());\n"
    "}\n";

// The Base showcase owns the colors. Its Rust counterpart resolves these
// capture names from Syntect; use the kit's Rust scanner for the same
// highlighter seam in the C++ example.
struct ShowcaseEditorCapture {
    component::SyntaxTok token;
    Str name;
    uint32_t color;
};

static constexpr ShowcaseEditorCapture kEditorCaptures[] = {
    {component::SyntaxTok::Comment, StrL("comment"), 0x007fff},
    {component::SyntaxTok::String, StrL("string"), 0x036a07},
    {component::SyntaxTok::Number, StrL("number"), 0x0433ff},
    {component::SyntaxTok::Keyword, StrL("keyword"), 0x0433ff},
    {component::SyntaxTok::Boolean, StrL("boolean"), 0xc5060b},
    {component::SyntaxTok::Function, StrL("function"), 0x0000a2},
    {component::SyntaxTok::Type, StrL("type"), 0x6f42c1},
    {component::SyntaxTok::Property, StrL("variable"), 0x333333},
};

static bool ResolveShowcaseEditorStyle(void*, Str name, TextSpan* out) {
    for (const auto& capture : kEditorCaptures) {
        if (StrEq(capture.name, name)) {
            *out = {};
            out->color = ExampleRgb(capture.color);
            return true;
        }
    }
    return false;
}

struct ShowcaseEditorHighlighter {
    struct Capture {
        int start;
        int end;
        Str name;
    };

    InputState* editor = nullptr;
    Vec<Capture> captures;

    // syntect_highlighter.rs brace_fold_ranges. Each line starts outside a
    // quote. Escaped quotes and a trailing line comment have no braces.
    static void UpdateFolds(InputState* editor, Str text) {
        Vec<int> openings;
        Vec<FoldRange> folds;
        int line = 0;
        bool quoted = false;
        bool escaped = false;
        for (int i = 0; i < len(text); i++) {
            char ch = text.s[i];
            if (ch == '\n') {
                line++;
                quoted = escaped = false;
                continue;
            }
            if (!quoted && ch == '/' && i + 1 < len(text) &&
                text.s[i + 1] == '/') {
                while (i + 1 < len(text) && text.s[i + 1] != '\n') i++;
                continue;
            }
            if (ch == '"' && !escaped) {
                quoted = !quoted;
            } else if (!quoted && ch == '{') {
                VecAppend(openings, line);
            } else if (!quoted && ch == '}' && len(openings)) {
                int start = openings[len(openings) - 1];
                VecPop(openings);
                if (start < line) VecAppend(folds, FoldRange{start, line});
            }
            escaped = quoted && ch == '\\' && !escaped;
        }
        InputSetFoldCandidates(editor, folds.els, len(folds));
    }

    static void Update(void* data, const InputEdit*, Str text, bool folding) {
        auto* self = static_cast<ShowcaseEditorHighlighter*>(data);
        VecClear(self->captures);
        component::SyntaxLexer lexer;
        SyntaxLexStart(&lexer, component::SyntaxLangFor(StrL("rust")), text);
        while (SyntaxLexNext(&lexer)) {
            for (const auto& capture : kEditorCaptures) {
                if (capture.token != lexer.tok) continue;
                int start = (int)(lexer.text.s - text.s);
                VecAppend(
                    self->captures,
                    Capture{start, start + len(lexer.text), capture.name});
                break;
            }
        }
        if (folding) UpdateFolds(self->editor, text);
    }

    static void UpdateBatch(void* data, const InputEditWithText* edits, int n,
                            bool folding) {
        if (n) Update(data, nullptr, edits[n - 1].text, folding);
    }

    static int Styles(void* data, Selection range,
                      const HighlightStyleResolver* resolver, Arena* arena,
                      TextSpan** out) {
        auto* self = static_cast<ShowcaseEditorHighlighter*>(data);
        int lo = 0;
        int hi = len(self->captures);
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (self->captures[mid].end <= range.start)
                lo = mid + 1;
            else
                hi = mid;
        }
        ArenaVec<TextSpan> spans;
        for (int i = lo; i < len(self->captures); i++) {
            const auto& capture = self->captures[i];
            if (capture.start >= range.end) break;
            TextSpan span;
            if (resolver && resolver->Style(capture.name, &span)) {
                span.lo = std::max(capture.start, range.start);
                span.hi = std::min(capture.end, range.end);
                if (span.lo < span.hi) spans.Append(arena, span);
            }
        }
        *out = spans.Flatten(arena);
        return len(spans);
    }
};

static void OnEditor(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->editorOn = true;
    app->textareaOn = false;
    InputFocus(&app->editor, cx);
    Notify(cx);
}

El* ShowcaseEditor(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    if (!app->editorInited) {
        app->editor.kind = InputKind::Editor;
        app->editor.mode.kind = LayoutModeKind::CodeEditor;
        app->editor.mode.folding = true;
        // mod.rs: `.show_whitespaces(true)`.
        app->editor.showWhitespaces = true;
        auto* highlighter = new ShowcaseEditorHighlighter();
        highlighter->editor = &app->editor;
        app->editor.highlighter.data = highlighter;
        app->editor.highlighter.language = [](void*) { return StrL("rust"); };
        app->editor.highlighter.update = &ShowcaseEditorHighlighter::Update;
        app->editor.highlighter
            .updateBatch = &ShowcaseEditorHighlighter::UpdateBatch;
        app->editor.highlighter.styles = &ShowcaseEditorHighlighter::Styles;
        app->editor.highlighter.drop = [](void* data) {
            delete static_cast<ShowcaseEditorHighlighter*>(data);
        };
        InputSetValue(&app->editor, Str(kEditorDefault));
        InputMoveTo(&app->editor, cx, 0);
        app->editorInited = true;
    }
    InputEditorStyle style = ShowcaseEditorStyle();
    style.highlightStyles.style = &ResolveShowcaseEditorStyle;
    return Div(a)
        ->FlexCol()
        ->W(320)
        ->Gap(4)
        ->ItemsStart()
        ->Child(Div(a)->H(16)->ItemsCenter()->Child(
            TextEl(a, StrL("Rust Editor"))->Font(12)->Fg(ExampleRgb(0x171717))))
        ->Child(InputBase::New(cx, StrL("example-editor"), true)
                    ->OnClick(Listen(cx, &OnEditor))
                    ->W(320)
                    ->H(128)
                    ->PadX(8)
                    ->PadY(8)
                    ->ClipY()
                    ->FlexCol()
                    ->FocusId(0)
                    ->Border(1, app->editorOn ? ExampleRgb(0x171717)
                                              : ExampleRgb(0xd4d4d4))
                    ->Child(Editor::New(cx, &app->editor, style)));
}

SHOWCASE_PAGE(CompEditor, ShowcaseEditor);
