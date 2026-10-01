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

// RangeDecorationStyle: a geometric presentation for a range decoration.
// Rust marks it #[non_exhaustive]; Frame is the default.
enum class RangeDecorationStyle : uint8_t {
    // Fill the continuous visual range.
    Fill,
    // Draw a continuous one-pixel frame around the visual range.
    Frame,
};

// RangeDecoration: a geometric decoration over a half-open UTF-8 byte range.
// `hasColor` false is Rust's `color: None`, the editor-foreground fallback
// (12% opacity for a fill).
struct RangeDecoration {
    Selection range = {};
    RangeDecorationStyle style = RangeDecorationStyle::Frame;
    Rgba color = {};
    bool hasColor = false;

    // A frame using the editor foreground color.
    static RangeDecoration New(Selection range);
    Selection Range() const { return range; }
    RangeDecorationStyle Style() const { return style; }
    // The application-owned color override; false for the editor fallback.
    bool Color(Rgba* out) const;
    RangeDecoration WithStyle(RangeDecorationStyle value) const;
    RangeDecoration WithColor(Rgba value) const;
};

// DecorationIndex: a balanced interval index over stable insertion-order
// entries. Each midpoint stores the maximum end of its subtree, so one
// document-spanning decoration does not force a scan of every preceding
// decoration on each frame.
struct DecorationIndex {
    Vec<int> indices;
    Vec<int> maxEnds;

    void Rebuild(const RangeDecoration* decorations, int n);
    int Build(const RangeDecoration* decorations, int lo, int hi);
    // Appends the indices of decorations in [lo, hi) of the index that
    // intersect `range`, and returns the number of visited nodes, which is
    // what the complexity tests measure.
    int Query(const RangeDecoration* decorations, int lo, int hi,
              Selection range, Vec<int>* matches) const;
};

struct RangeDecorationEntries {
    uint64_t id = 0;
    Vec<RangeDecoration> decorations;
    DecorationIndex index;

    void Reindex();
};

// DecorationCollections<RangeDecoration>: the independently owned
// collections an editor's range decorations live in, in creation order (Rust
// keeps them in a BTreeMap keyed by a monotonically increasing id).
struct RangeDecorationCollections {
    uint64_t nextId = 0;
    Vec<RangeDecorationEntries*> entries;

    RangeDecorationCollections() = default;
    RangeDecorationCollections(const RangeDecorationCollections&) = delete;
    RangeDecorationCollections& operator=(const RangeDecorationCollections&) =
        delete;
    ~RangeDecorationCollections();

    // Takes decorations already normalized against the text.
    uint64_t Create(const RangeDecoration* decorations, int n);
    bool Set(uint64_t id, const RangeDecoration* decorations, int n);
    bool Append(uint64_t id, const RangeDecoration* decorations, int n);
    bool Remove(uint64_t id);
    RangeDecorationEntries* Get(uint64_t id) const;
    void AdjustForEdit(Selection editedRange, int insertedLen);
    void Clear();
    // Decorations intersecting any of the visible buffer `ranges`, owner by
    // owner and in item order within an owner, each at most once. Returns how
    // many there are; at most `cap` are written.
    int Intersecting(const Selection* ranges, int nRanges,
                     const RangeDecoration** out, int cap) const;
};

// Clips each decoration to UTF-8 boundaries of `text`, dropping reversed and
// empty ranges (normalize).
int RangeDecorationsNormalize(Str text, const RangeDecoration* in, int n,
                              Vec<RangeDecoration>* out);

struct RangeDecorationsState;

// RangeDecorationCollection: a handle for one independently owned set of
// ranges. Copies address the same collection. Dropping a handle does not clear
// it; Clear empties it and Dispose releases it for every copy. Operations on
// a disposed collection or a dropped editor are harmless no-ops. Rust's
// methods notify the editor; the view that owns the editor re-renders here.
struct RangeDecorationCollection {
    RangeDecorationsState* state = nullptr;
    uint64_t id = 0;

    RangeDecorationCollection() = default;
    RangeDecorationCollection(const RangeDecorationCollection& other);
    RangeDecorationCollection& operator=(
        const RangeDecorationCollection& other);
    ~RangeDecorationCollection();

    // Replace only this owner's decorations, clipping ranges to UTF-8
    // boundaries.
    void Set(const RangeDecoration* decorations, int n);
    // Append decorations, preserving their paint order.
    void Append(const RangeDecoration* decorations, int n);
    void Clear();
    void Dispose();
    // The tracked byte ranges in insertion order; returns how many there are.
    int GetRanges(Selection* out, int cap) const;
    bool IsValid() const;
};

// EditorState::create_range_decorations_collection: an independently owned
// collection of geometric range decorations on this editor. Ranges follow
// edits the way text decorations do: insertion at either edge does not
// expand a range, insertion inside does, replacement clips overlapping
// anchors and deletion drops empty ranges. Undo, redo and whole-document
// replacement apply the same transforms; decorations are not undo history.
// Folding changes projection, not stored ranges. Fills paint behind frames;
// within each style later collections and items paint over earlier ones.
RangeDecorationCollection InputCreateRangeDecorationsCollection(
    InputState* s, const RangeDecoration* decorations, int n);
// InputExtras::range_decorations: decorations intersecting the visible,
// non-folded buffer spans. Empty for an input that never created one.
int InputRangeDecorations(const InputState* s, const Selection* ranges,
                          int nRanges, const RangeDecoration** out, int cap);
// The editor's store, dropped with the InputState.
void InputRangeDecorationsFree(InputState* s);
void InputRangeDecorationsAdjustForEdit(InputState* s, Selection editedRange,
                                        int insertedLen);
// reset_annotations: every collection emptied, still reusable.
void InputRangeDecorationsReset(InputState* s);

// Corners<Point<Pixels>>: one visual row's box of a projected range.
struct RangeCorners {
    Point topLeft = {};
    Point topRight = {};
    Point bottomLeft = {};
    Point bottomRight = {};
};

// element.rs frame geometry, pure so it can be tested on its own.
// frame_outline_points: one continuous outline around rows of different
// widths, closed on its first point.
int FrameOutlinePoints(const RangeCorners* corners, int n, Vec<Point>* out);
// pad_frame_corners: horizontal space between the stroke and the text.
void PadFrameCorners(RangeCorners* corners, int n, float horizontalPadding);
// snap_frame_outline: the stroke width in whole physical pixels, and the
// points moved so the stroke lands on them. Returns the stroke width.
float SnapFrameOutline(Point* points, int n, float strokeWidth,
                       float scaleFactor);
// clamp_frame_to_content_mask: keeps the vertical edges inside the mask.
void ClampFrameToContentMask(Point* points, int n, float strokeWidth,
                             Bounds contentMask);

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
    int Len() const { return len(diagnostics); }
    bool IsEmpty() const { return len(diagnostics) == 0; }
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
    int DisplayRowCount() const { return len(rows); }
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

// update_highlighter / update_highlighter_batch: hand the installed
// highlighter the edits made since it was last driven -- one through
// `update`, several as one `update_batch`, none (or more than the log keeps)
// as the whole document -- and clear them. The themed layer drives it once a
// frame, gated on docVersion, where Rust drives it from each change.
void InputDriveHighlighter(InputState* s, bool folding);
// The edits are answered some other way -- a background re-scan of the
// whole document -- so the log is dropped without driving.
void InputSkipHighlighterEdits(InputState* s);

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
    // Rust stores Arc<Regex>. The patterns are compiled by the caller there;
    // here they are either a function pointer or a regex subset matched by
    // IndentPatternMatch (`^$.*+?[]\s\S\d\w` and escapes).
    Str increasePattern = {};
    Str decreasePattern = {};
    void* data = nullptr;
    bool (*increaseIndent)(void* data, Str text) = nullptr;
    bool (*decreaseIndent)(void* data, Str text) = nullptr;

    static IndentationRules FromPatterns(Str increase, Str decrease);
};

bool IndentPatternMatch(Str pattern, Str text);

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
// EditorLanguage::context_at: the context at `offset`, asked of the provider
// this editor retains. The LanguageProvider is asked for one again only when
// it is replaced or the editor's language changes.
SyntaxContext InputSyntaxContextAt(InputState* s, App* app, Str text,
                                   int offset);
void InputSyntaxCacheFree(InputState* s);

struct FoldIconRenderer {
    void* data = nullptr;
    El* (*render)(void* data, Ctx* cx, int line, bool folded) = nullptr;

    El* Render(Ctx* cx, int line, bool folded) const;
};

// ─── gpui text_system/line_wrapper.rs and display_map/text_wrapper.rs ────
//
// What turns one logical line into the editor's visual rows. The wrapper
// sums per-character widths the way GPUI's LineWrapper does — the width a
// glyph shapes to on its own, not the run's kerned advance — so where a row
// breaks is a property of the text and the font, decided before anything is
// laid out.

// line_wrapper.rs Boundary: a row starts at `ix`, and the rows from there on
// are indented by `nextIndent` characters.
struct WrapBoundary {
    int ix = 0;
    int nextIndent = 0;

    bool operator==(const WrapBoundary& o) const {
        return ix == o.ix && nextIndent == o.nextIndent;
    }
};

// line_wrapper.rs LineFragment: text, or an element of a fixed width that
// occupies `elementLen` bytes of the line (an inline token's chip).
struct LineFragment {
    Str text = {};
    float elementWidth = 0;
    int elementLen = 0;

    static LineFragment Text(Str text) { return {text, 0, 0}; }
    static LineFragment Element(float width, int len) {
        return {Str{}, width, len};
    }
};

// LineWrapper::MAX_INDENT.
constexpr int kLineWrapperMaxIndent = 256;

// LineWrapper::width_for_char: what one character is worth, as the caller's
// font measures it.
using WrapCharWidth = float (*)(void* user, uint32_t c);

// LineWrapper::is_word_char.
bool LineWrapperIsWordChar(uint32_t c);

// LineWrapper::wrap_line: the boundaries at which `fragments` break to fit
// `wrapWidth`, appended to `out`. Empty when the line fits.
void LineWrapperWrapLine(const LineFragment* fragments, int n, float wrapWidth,
                         WrapCharWidth widthFor, void* user,
                         Vec<WrapBoundary>* out);

// text_wrapper.rs LineItem, built the way TextWrapper::_update builds one: the
// visual rows of one logical line as [rows[k], rows[k + 1]) with the last
// running to `len`, and the indent in characters the rows after the first
// carry. `wrapLine` answers the boundaries for a slice of the line starting
// at `base`; Same takes them in one pass, None wraps the first row as is and
// the rest again at the full width.
using WrapLineFn = void (*)(void* user, Str line, int base,
                            Vec<WrapBoundary>* out);
void TextWrapperWrapItem(Str line, bool wrap, WrappingIndent indent,
                         WrapLineFn wrapLine, void* user, Vec<int>* rows,
                         int* indentChars);

} // namespace gpui
#endif // GPUI_BASE_INPUT_EDITOR_H_
