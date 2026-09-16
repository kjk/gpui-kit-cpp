#ifndef GPUI_BASE_INPUT_EDITOR_H_
#define GPUI_BASE_INPUT_EDITOR_H_
/* Editor-side value and projection structure from
   crates/base/src/input/editor/{decorations,diagnostics,display_map,
   highlighting,indent}.rs.

   The live editor remains InputState. These types preserve the independent
   data seams Rust builds around it without importing ropey, sum_tree,
   tree-sitter, or an LSP crate. All offsets and columns are UTF-8 bytes, the
   convention used by the rest of this runtime. */

#include "base/input_rope.h"

namespace gpui {

struct TabSize {
    int tabSize = 2;
    bool hardTabs = false;

    Str ToString(Arena* a) const;
    int IndentCount(Str line) const;
};

struct TextDecoration {
    Selection range = {};
    TextSpan style = {};

    static TextDecoration New(Selection range, const TextSpan& style);
};

struct DecorationCollectionsState;

// A copyable handle, like Rust's weak-entity collection handle. The backing
// store is reference-counted explicitly and a handle becomes a harmless
// no-op after its owning DecorationCollections is dropped.
struct TextDecorationCollection {
    DecorationCollectionsState* state = nullptr;
    uint64_t id = 0;

    TextDecorationCollection() = default;
    TextDecorationCollection(const TextDecorationCollection& other);
    TextDecorationCollection& operator=(const TextDecorationCollection& other);
    ~TextDecorationCollection();

    bool Set(const TextDecoration* decorations, int n);
    bool Append(const TextDecoration* decorations, int n);
    void Clear();
    int GetRanges(Selection* out, int cap) const;
    bool IsValid() const;
};

// InputBaseState<EditorMode>::extras.decorations. This owner may be kept next
// to an InputState; collection handles can be passed independently.
struct DecorationCollections {
    DecorationCollectionsState* state = nullptr;

    explicit DecorationCollections(InputState* input = nullptr);
    DecorationCollections(const DecorationCollections&) = delete;
    DecorationCollections& operator=(const DecorationCollections&) = delete;
    ~DecorationCollections();

    TextDecorationCollection Create(const TextDecoration* decorations = nullptr,
                                    int n = 0);
    void AdjustForEdit(Selection editedRange, int insertedLen);
    void Clear();
    // Ordered, non-overlapping runtime spans. Earlier collections win where
    // this renderer's non-optional TextSpan properties cannot be merged.
    int BuildSpans(TextSpan* out, int cap) const;
};

struct DiagnosticEntry {
    Selection range = {};
    Diagnostic diagnostic = {};
};

struct DiagnosticSummary {
    int count = 0;
    int start = 0;
    int end = 0;
};

// The source uses a SumTree. Diagnostics are normally counted in tens here,
// so a sorted flat Vec gives the same range and point queries without adding
// another general-purpose tree to Base.
struct DiagnosticSet {
    Arena* arena = nullptr;
    Str text = {};
    Vec<DiagnosticEntry> diagnostics;

    explicit DiagnosticSet(Str text = {});
    DiagnosticSet(const DiagnosticSet&) = delete;
    DiagnosticSet& operator=(const DiagnosticSet&) = delete;
    ~DiagnosticSet();

    void Reset(Str value);
    void Push(const Diagnostic& diagnostic);
    void Extend(const Diagnostic* values, int n);
    int Len() const { return diagnostics.len; }
    bool IsEmpty() const { return diagnostics.len == 0; }
    void Clear();
    DiagnosticSummary Summary() const;
    int Range(Selection range, const DiagnosticEntry** out, int cap) const;
    const DiagnosticEntry* ForOffset(int offset) const;
    const DiagnosticEntry* At(int index) const;
};

struct BufferPoint {
    int line = 0;
    int col = 0;

    static BufferPoint New(int line, int col) { return {line, col}; }
};

struct DisplayPoint {
    int row = 0;
    int col = 0;

    static DisplayPoint New(int row, int col) { return {row, col}; }
};

enum class WrappingIndent : uint8_t {
    None,
    Same
};

struct DisplayMapRow {
    int bufferLine = 0;
    int startCol = 0;
    int endCol = 0;
};

// Public buffer -> wrap -> fold facade. The runtime's painter performs
// shaped pixel wrapping; this dependency-free projection accepts the byte
// capacity measured by that layout (`SetWrapColumns`) and owns the same
// coordinate/fold state as Rust's DisplayMap.
struct DisplayMap {
    Str text = {}; // owned
    int wrapColumns = 0;
    WrappingIndent wrappingIndent = WrappingIndent::Same;
    TabSize tab = {};
    FoldMap foldMap;
    Vec<DisplayMapRow> rows;

    explicit DisplayMap(int wrapColumns = 0);
    DisplayMap(const DisplayMap&) = delete;
    DisplayMap& operator=(const DisplayMap&) = delete;
    ~DisplayMap();

    void SetText(Str value);
    void OnTextChanged(Str value);
    void SetWrapColumns(int columns);
    void SetWrappingIndent(WrappingIndent indent);
    void SetTabSize(TabSize value);
    BufferPoint ClipBufferPoint(BufferPoint point) const;
    DisplayPoint BufferPosToDisplayPos(BufferPoint point) const;
    BufferPoint DisplayPosToBufferPos(DisplayPoint point) const;
    int DisplayRowCount() const { return rows.len; }
    int WrapRowCount() const;
    int BufferLineCount() const;
    int DisplayRowToBufferLine(int row) const;
    Selection BufferLineToDisplayRowRange(int line) const;
    bool IsBufferLineHidden(int line) const;
    int BufferLineToDisplayRow(int line) const;
    void SetFoldCandidates(const FoldRange* ranges, int n);
    void SetFolded(int startLine, bool folded);
    void ToggleFold(int startLine);
    bool IsFoldedAt(int startLine) const;
    bool IsFoldCandidate(int startLine) const;
    void ClearFolds();
    void AdjustFoldsForEdit(Str oldText, Selection editedRange, Str inserted);

  private:
    void Rebuild();
};

// HighlightStyleResolver and InputHighlighter moved to gpui/gpui.h when
// InputState grew the installed instance; this header keeps the pieces only
// the themed layer reaches for.

struct InputHighlighterFactory {
    void* data = nullptr;
    bool (*create)(void* data, Str language, InputHighlighter* out) = nullptr;

    bool Create(Str language, InputHighlighter* out) const;
};

// input/editor/highlighting.rs and language_config.rs. Base's parser seam is
// function-pointer based; a provider may answer Code everywhere and install
// no parser, which is the dependency-free default.
enum class SyntaxContext : uint8_t {
    Code,
    String,
    Comment,
};

struct SyntaxContextProvider {
    void* data = nullptr;
    SyntaxContext (*contextAt)(void* data, Str text, int offset) = nullptr;

    SyntaxContext ContextAt(Str text, int offset) const {
        return contextAt ? contextAt(data, text, offset) : SyntaxContext::Code;
    }
};

struct BracketPair {
    Str open = {};
    Str close = {};

    static BracketPair New(Str open, Str close) { return {open, close}; }
};

struct AutoClosingPair {
    Str open = {};
    Str close = {};
    const SyntaxContext* notIn = nullptr;
    int nNotIn = 0;

    static AutoClosingPair New(Str open, Str close) { return {open, close}; }
};

struct IndentationRules {
    void* data = nullptr;
    bool (*increaseIndent)(void* data, Str text) = nullptr;
    bool (*decreaseIndent)(void* data, Str text) = nullptr;
};

struct LanguageConfig {
    const BracketPair* brackets = nullptr;
    int nBrackets = 0;
    const AutoClosingPair* autoClosingPairs = nullptr;
    int nAutoClosingPairs = 0;
    // False means fall back to brackets; true with a zero count disables
    // automatic closing explicitly.
    bool hasAutoClosingPairs = false;
    Str autoCloseBefore = {};
    IndentationRules indentation = {};
    bool hasIndentationRules = false;

    static LanguageConfig Default();
};

struct LanguageProvider {
    void* data = nullptr;
    Str (*languageName)(void* data, Arena* a, Str name) = nullptr;
    bool (*config)(void* data, Str canonicalName,
                   LanguageConfig* out) = nullptr;
    bool (*syntaxContextProvider)(void* data, Str canonicalName,
                                  SyntaxContextProvider* out) = nullptr;
};

void InputSetLanguageProvider(App* app, const LanguageProvider& provider);
void InputSetLanguageConfig(App* app, Str language,
                            const LanguageConfig& config);
LanguageConfig InputLanguageConfig(App* app, Str language);
SyntaxContextProvider InputSyntaxContextProvider(App* app, Str language);

struct FoldIconRenderer {
    void* data = nullptr;
    El* (*render)(void* data, Ctx* cx, int line, bool folded) = nullptr;

    El* Render(Ctx* cx, int line, bool folded) const;
};

} // namespace gpui
#endif // GPUI_BASE_INPUT_EDITOR_H_
