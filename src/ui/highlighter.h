#ifndef GPUI_UI_HIGHLIGHTER_H_
#define GPUI_UI_HIGHLIGHTER_H_
/* Themed highlighter façade — crates/ui/src/highlighter
   Syntax highlighting uses the simple keyword path from the showcase editor. */

#include "ui/sizing.h"
#include "ui/syntax.h"

namespace gpui {

namespace component {

struct Highlighter {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    // EditorState: the same engine again, with InputKind::Editor.
    InputState* state = nullptr;
    // The box the rows scroll inside. 0 lets the editor be as tall as its
    // content, which is what an editor inside something else that scrolls
    // wants.
    float h = 0;
    // set_editor_paddings: the room between the box's edge and the rows,
    // gutter included, inside whatever scrolls them. Zero by default; the
    // component Editor asks for the Input's.
    Edges editorPad = {};
    // theme.mono_font_size until a caller says otherwise.
    float fontSize = 0;
    // EditorState::language: what the rows are scanned as. None leaves them
    // in the editor's own colour.
    SyntaxLang lang = SyntaxLangNone;
    // create_decorations_collection: runs the caller wants painted over the
    // document, in document offsets and in order. They are laid over the
    // language's own captures.
    const TextSpan* decorations = nullptr;
    int nDecorations = 0;
    // The active-line wash and the indent guides, both off by default.
    bool activeLine = false;
    bool indentGuides = false;
    // InputState::searchable, which Rust defaults to true for the code
    // editor: ctrl-f opens a find bar over the rows, and ctrl-h opens it with
    // the replace row already out.
    bool searchable = true;
    // LayoutMode::CodeEditor { folding }: a chevron in the gutter beside every
    // line that opens a foldable block, and a click on it collapses the block.
    bool folding = false;
    const Diagnostic* diagnostics = nullptr;
    int nDiagnostics = 0;

    static Highlighter* New(Ctx* cx, InputState* state);
    static Highlighter* New(Ctx* cx, Str id, InputState* state);
    Highlighter* H(float v);
    // `.text_size(..)` on the editor: the size the rows are drawn at, and
    // what their height follows. Zero is the theme's monospace size.
    Highlighter* Font(float px);
    Highlighter* Language(Str name);
    Highlighter* Decorations(const TextSpan* runs, int n);
    Highlighter* ActiveLine(bool v = true);
    Highlighter* IndentGuides(bool v = true);
    Highlighter* Searchable(bool v);
    // EditorState::diagnostics_mut(): what a provider published over this
    // document, drawn as a wavy underline per entry in the severity's colour.
    // The array is the caller's and outlives the frame, the way the
    // decorations do.
    Highlighter* Diagnostics(const Diagnostic* items, int n);
    Highlighter* Folding(bool v = true);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_HIGHLIGHTER_H_
