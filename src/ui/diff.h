#ifndef GPUI_SRC_UI_DIFF_H_
#define GPUI_SRC_UI_DIFF_H_
/* Readonly patch viewer — crates/component/src/diff.
   One DiffFile per change, one DiffState for every file, Diff paints them. */

#include "base/virtual_list.h"
#include "ui/sizing.h"

namespace gpui {
namespace component {

enum class DiffSide : uint8_t {
    Original,
    Modified
};
enum class DiffMode : uint8_t {
    Split,
    Unified
};
enum class DiffFileStatus : uint8_t {
    Added,
    Deleted,
    Modified,
    Renamed,
    Copied,
    Unchanged,
    Conflicted,
};
enum class DiffInlineUnit : uint8_t {
    Word,
    Character
};
enum class DiffHoverHighlight : uint8_t {
    None,
    Line,
    LineNumber,
    Both
};
enum class DiffHunkSeparator : uint8_t {
    Metadata,
    LineInfo,
    Simple
};
enum class DiffChangeIndicator : uint8_t {
    Signs,
    Bars,
    None
};
enum class DiffConflictResolution : uint8_t {
    Current,
    Incoming,
    Both
};
enum class DiffConflictPart : uint8_t {
    Current,
    Base,
    Incoming
};
enum class DiffFoldExpansion : uint8_t {
    Up,
    Down,
    All
};

enum class DiffEventKind : uint8_t {
    SelectionStarted,
    SelectionChanged,
    SelectionEnded,
    FileExpanded,
    FileCollapsed,
    ConflictResolved,
};

struct DiffEvent {
    DiffEventKind kind = DiffEventKind::SelectionChanged;
    // SelectionChanged with hasRange false is SelectionChanged(None).
    bool hasRange = false;
    Str path = {};
    DiffSide side = DiffSide::Original;
    DiffSide endSide = DiffSide::Original;
    int start = 1;
    int end = 1;
    int conflict = 0;
};

struct DiffParseError {
    int line = 0;
    Str message = {};
    int Line() const { return line; }
    Str Message() const { return message; }
};

// Relaxations for DiffFile::ParseWith. Every one is off, which parses
// exactly as DiffFile::Parse does.
struct DiffParseOptions {
    bool truncatedHunks = false;
    static DiffParseOptions New() { return {}; }
    DiffParseOptions TruncatedHunks(bool on) const {
        DiffParseOptions next = *this;
        next.truncatedHunks = on;
        return next;
    }
    bool HasTruncatedHunks() const { return truncatedHunks; }
};

struct DiffLinePosition {
    Str path = {};
    DiffSide side = DiffSide::Original;
    int line = 1;
    static DiffLinePosition New(Str path, DiffSide side, int line);
    Str Path() const { return path; }
    DiffSide Side() const { return side; }
    int Line() const { return line; }
};

// One character of a source line, as the pointer finds it.
struct DiffTextPosition {
    DiffLinePosition position = {};
    int offset = 0;
    Str text = {};
    // The line the character is on.
    const DiffLinePosition& Position() const { return position; }
    // The character's byte offset in the line's content, before tab
    // expansion: the unit DiffSpanHighlight spans use.
    int Offset() const { return offset; }
    // The line's content, so the handler can read the word around the
    // offset without a copy of the patch.
    Str Text() const { return text; }
};

struct DiffLineRange {
    Str path = {};
    DiffSide side = DiffSide::Original;
    int start = 1;
    DiffSide endSide = DiffSide::Original;
    int end = 1;
    static DiffLineRange New(Str path, DiffSide side, int start, int end);
    DiffLineRange WithEndSide(DiffSide side) const;
    Str Path() const { return path; }
    DiffSide Side() const { return side; }
    int Start() const { return start; }
    DiffSide EndSide() const { return endSide; }
    int End() const { return end; }
    bool IsSingleSide() const { return side == endSide; }
};

struct DiffAnnotation {
    Str id = {};
    Str path = {};
    bool hasPosition = false;
    DiffLinePosition position = {};
    static DiffAnnotation Line(Str id, DiffLinePosition position);
    static DiffAnnotation File(Str id, Str path);
    Str Id() const { return id; }
    Str Path() const { return path; }
    const DiffLinePosition* Position() const {
        return hasPosition ? &position : nullptr;
    }
};

// A background on the lines of a range. It does not change the selection,
// copying or layout. No background means the translucent warning fill.
struct DiffDecoration {
    DiffLineRange range = {};
    bool hasBackground = false;
    Rgba background = {};
    static DiffDecoration New(DiffLineRange range);
    DiffDecoration WithBackground(Rgba fill) const;
    const DiffLineRange& Range() const { return range; }
    const Rgba* Background() const {
        return hasBackground ? &background : nullptr;
    }
};

// Highlights a half-open byte range in the source line, before tab
// expansion. The default background is the theme warning color at 50%. A
// fully transparent fill leaves whatever the line shows beneath the span:
// for emphasis carried by the text itself, such as a link.
struct DiffSpanHighlight {
    DiffLinePosition position = {};
    ByteRange span = {};
    bool hasBackground = false;
    bool hasForeground = false;
    bool underlined = false;
    Rgba background = {};
    Rgba foreground = {};
    static DiffSpanHighlight New(DiffLinePosition position, ByteRange span);
    // Underlines the span in its text color, the way a link reads.
    DiffSpanHighlight WithUnderline() const;
    DiffSpanHighlight WithBackground(Rgba fill) const;
    DiffSpanHighlight WithForeground(Rgba color) const;
    bool IsUnderlined() const { return underlined; }
    const DiffLinePosition& Position() const { return position; }
    ByteRange Span() const { return span; }
    const Rgba* Background() const {
        return hasBackground ? &background : nullptr;
    }
    const Rgba* Foreground() const {
        return hasForeground ? &foreground : nullptr;
    }
};

// One marker in the lane between the line numbers and the code. The
// application renders it. The lane keeps the first marker supplied for a
// line.
struct DiffGutterMarker {
    Str id = {};
    DiffLinePosition position = {};
    Str label = {};
    static DiffGutterMarker New(Str id, DiffLinePosition position);
    DiffGutterMarker WithLabel(Str text) const;
    Str Id() const { return id; }
    const DiffLinePosition& Position() const { return position; }
    Str Label() const { return label; }
};

// The last supplied decoration that covers `position`, or null.
const DiffDecoration* DiffDecorationAt(const DiffDecoration* items, int count,
                                       const DiffLinePosition& position);
// The first supplied marker on `position`, or null.
const DiffGutterMarker* DiffGutterMarkerAt(const DiffGutterMarker* items,
                                           int count,
                                           const DiffLinePosition& position);

// One conflict. `currentStart`..`incomingEnd` index the modified-side
// lines with markers excluded (Rust `source_lines`). The one-based
// ranges are working-file lines: `lines` includes markers, and a part
// may be empty. Ends are exclusive. A missing base has hasBase false.
struct DiffConflict {
    int currentStart = 0;
    int currentEnd = 0;
    bool hasBase = false;
    int baseStart = 0;
    int baseEnd = 0;
    int incomingStart = 0;
    int incomingEnd = 0;
    int linesStart = 1;
    int linesEnd = 1;
    int currentLinesStart = 1;
    int currentLinesEnd = 1;
    bool hasBaseLines = false;
    int baseLinesStart = 1;
    int baseLinesEnd = 1;
    int incomingLinesStart = 1;
    int incomingLinesEnd = 1;
    Str currentLabel = {};
    Str baseLabel = {};
    Str incomingLabel = {};
    int SourceLinesStart() const { return currentStart; }
    int SourceLinesEnd() const { return incomingEnd; }
    void Lines(int* start, int* end) const;
    void CurrentLines(int* start, int* end) const;
    bool BaseLines(int* start, int* end) const;
    void IncomingLines(int* start, int* end) const;
    Str CurrentLabel() const { return currentLabel; }
    // False for a two-way conflict. A present blank label is empty.
    bool BaseLabel(Str* out) const;
    Str IncomingLabel() const { return incomingLabel; }
    bool Part(DiffConflictPart part, int* start, int* end) const;
    Str Label(DiffConflictPart part) const;
    DiffConflictPart PartOf(int ix) const;
    bool HasPart(int ix) const;
};

struct DiffSourceLine {
    int lineNumber = 0;
    Str text = {};
    Str display = {};
    int sourceStart = 0;
    int sourceEnd = 0;
    int contentEnd = 0;
    int counterpart = -1;
    int tabBegin = 0;
    int tabCount = 0;
    int chunkBegin = 0;
    int chunkCount = 0;
};

struct DiffDisplayTab {
    int sourceOffset = 0;
    int displayOffset = 0;
    int width = 0;
};

struct DiffChunk {
    int start = 0;
    int end = 0;
};

struct DiffLinePair {
    int original = -1;
    int modified = -1;
    bool changed = false;
    int Original() const { return original; }
    int Modified() const { return modified; }
    bool IsChanged() const { return changed; }
};

struct DiffHunk {
    int pairsStart = 0;
    int pairsEnd = 0;
    int originalStart = 0;
    int originalEnd = 0;
    int modifiedStart = 0;
    int modifiedEnd = 0;
    Str label = {};
    // One-based original lines this hunk supplies. A truncated hunk
    // covers the lines present, not the count written in `label`.
    int originalLinesStart = 0;
    int originalLinesEnd = 0;
    bool truncated = false;
    int HiddenLinesBefore(const DiffHunk* previous) const;
    Str Label() const { return label; }
};

struct DiffPatchLine {
    int original = -1;
    int modified = -1;
    int Line(DiffSide side) const {
        return side == DiffSide::Original ? original : modified;
    }
};

struct DiffFileSide {
    bool hasPath = false;
    Str path = {};
    Str source = {};
    Vec<DiffSourceLine> lines;
    Vec<DiffDisplayTab> tabs;
    Vec<DiffChunk> chunks;
};

struct DiffRun {
    int start = 0;
    int end = 0;
};

// One part for DiffFile::Excerpts: Rust's `(first line, text)` tuple. The
// text's first line is file line `first`.
struct DiffExcerpt {
    int first = 1;
    Str text = {};
};

// Immutable parsed file. Strings live in `arena`. Not stored in a Vec:
// the state holds DiffFile*.
struct DiffFile {
    Arena* arena = nullptr;
    DiffFileStatus status = DiffFileStatus::Modified;
    Str path = {};
    bool hasLanguage = false;
    Str language = {};
    DiffFileSide original;
    DiffFileSide modified;
    Vec<DiffLinePair> pairs;
    Vec<DiffHunk> hunks;
    Vec<Str> extendedHeaders;
    bool binary = false;
    int additions = 0;
    int deletions = 0;
    int lineNumberDigits = 1;
    Vec<DiffPatchLine> patchLines;
    Vec<int> patchIxs[2];
    Vec<DiffConflict> conflicts;
    Vec<DiffRun> inlineRuns[2];
    Vec<int> inlineStarts[2];

    DiffFile() = default;
    ~DiffFile();
    DiffFile(const DiffFile&) = delete;
    DiffFile& operator=(const DiffFile&) = delete;

    static bool Parse(Str patch, Vec<DiffFile*>* out, DiffParseError* error);
    static bool ParseWith(Str patch, DiffParseOptions options,
                          Vec<DiffFile*>* out, DiffParseError* error);
    bool IsTruncated() const;
    // A file shown in full, for reference, without changes. It appears as a
    // single column in every display mode.
    static DiffFile* Unchanged(Str path, Str text);
    // Parts of a file shown for reference, without changes, each keeping
    // its own line numbers, in file order without overlap. A first line
    // below 1 is clamped to 1. Like Unchanged it is a single column in
    // every display mode; the lines between the parts are simply not there.
    static DiffFile* Excerpts(Str path, const DiffExcerpt* parts, int count);
    static DiffFile* ParseConflicts(Str path, Str text, DiffParseError* error);
    DiffFile* WithLanguage(Str language);
    Str Language() const;
    DiffFileStatus Status() const { return status; }
    Str Path() const { return path; }
    bool OriginalPath(Str* out) const;
    bool ModifiedPath(Str* out) const;
    int ExtendedHeaderCount() const { return len(extendedHeaders); }
    Str ExtendedHeader(int ix) const { return extendedHeaders[ix]; }
    bool IsBinary() const { return binary; }
    int Additions() const { return additions; }
    int Deletions() const { return deletions; }
    bool HasChanges() const;
    bool IsSingleColumn() const;
    int ConflictCount() const { return len(conflicts); }
    const DiffConflict* Conflicts() const { return conflicts.els; }
    int LinesCount(DiffSide side) const;
    Str Source(DiffSide side) const;
    const DiffSourceLine* Lines(DiffSide side) const;
    // Inclusive one-based line numbers. Unavailable context is omitted.
    Str TextForLines(Arena* a, DiffSide side, int start, int end) const;
    int LineNumber(DiffSide side, int index) const;
    int LineIndex(DiffSide side, int line) const;
    const DiffLinePair* Pairs() const { return pairs.els; }
    int PairCount() const { return len(pairs); }
    const DiffHunk* Hunks() const { return hunks.els; }
    int HunkCount() const { return len(hunks); }
    int LineNumberDigits() const { return lineNumberDigits; }
    int PatchCount() const { return len(patchLines); }
    const DiffPatchLine* PatchLines() const { return patchLines.els; }
    int PatchIx(DiffSide side, int ix) const;
    void PrepareInline(DiffInlineUnit unit, int maxLineLength);
    int InlineCount(DiffSide side, int line) const;
    const DiffRun* InlineAt(DiffSide side, int line, int* count) const;
};

enum class DiffRowKind : uint8_t {
    File,
    Notice,
    Hunk,
    Fold,
    Conflict,
    Code,
};

struct DiffRow {
    DiffRowKind kind = DiffRowKind::File;
    int file = 0;
    int hunk = 0;
    int conflict = 0;
    DiffConflictPart part = DiffConflictPart::Current;
    int pairsStart = 0;
    int pairsEnd = 0;
    int original = -1;
    int modified = -1;
    bool changed = false;
    int File() const { return file; }
};

struct DiffFoldRange {
    int file = 0;
    int start = 0;
    int end = 0;
};

struct DiffResolution {
    Str path = {};
    int ix = 0;
    DiffConflictResolution kind = DiffConflictResolution::Current;
};

// One style an application highlighter returns for a byte range of one
// hunk side. `weight` 0 leaves the face alone. `hasColor` false leaves
// the colour alone. Ranges are UTF-8 offsets into that hunk's text.
struct DiffSyntaxStyle {
    int start = 0;
    int end = 0;
    uint16_t weight = 0;
    bool italic = false;
    bool hasColor = false;
    Rgba color = {};
    bool hasBackground = false;
    Rgba background = {};
    bool underline = false;
};

// Called once per hunk side with the file's language and that side's
// text. Appends styles to `out`. A range that splits a character is ignored.
using DiffSyntaxHighlighter = void (*)(void* data, Str language, Str text,
                                       Vec<DiffSyntaxStyle>* out);

// One prepared style, already mapped onto a line's display text: `start`
// and `end` are offsets into that line's display text.
struct DiffSyntaxSpan : DiffSyntaxStyle {
    int file = 0;
    uint8_t side = 0;
    int line = 0;
};

struct DiffCodeRunGeometry {
    int file = 0;
    DiffSide side = DiffSide::Original;
    int line = 0;
    int displayStart = 0;
    Str text = {};
    Bounds bounds = {};
    Bounds visible = {};
    TextLayout* layout = nullptr;
};

// Endpoints are source bytes, so the selection survives virtual row reuse,
// horizontal scrolling, syntax boundaries, and display-only tab expansion.
struct DiffTextSelection {
    int file = -1;
    DiffSide side = DiffSide::Original;
    int anchor = 0;
    int cursor = 0;
    Selection unit = {};
    int clicks = 1;
    bool dragging = false;
    bool hitText = false;
};

struct DiffInlineJob;

struct DiffState {
    App* app = nullptr;
    Vec<DiffFile*> files;
    DiffMode mode = DiffMode::Unified;
    bool hasContext = true;
    int contextLines = 3;
    int expansionLines = 20;
    int minCollapsedLines = 2;
    Vec<Str> collapsed;
    Vec<DiffResolution> resolutions;
    Vec<DiffFoldRange> expanded;
    Vec<DiffRow> rows;
    Vec<int> headerRows;
    Vec<int> changeRows;
    Vec<int> rowOfOriginal;
    Vec<int> rowOfModified;
    Vec<int> rowFileBase;
    int scrollItem = 0;
    VirtualListScrollHandle listScroll = {};
    Vec<float> rowHeights;
    Vec<uint8_t> rowNeedsMeasure;
    Vec<int> measuredAnnotations;
    float rowMeasureWidth = -1.f;
    float rowMeasureFont = 0.f;
    float rowMeasureSplit = 0.f;
    uint64_t rowMeasureStyle = 0;
    float horizontalScrollX = 0;
    float horizontalCodeWidth = 0;
    float horizontalCodeFont = 0;
    int horizontalNumberDigits = 0;
    FocusHandle focus = {};
    bool hasSelection = false;
    DiffLineRange selected = {};
    bool hasAnchor = false;
    DiffLinePosition anchor = {};
    bool hasCursor = false;
    DiffLinePosition cursor = {};
    bool selecting = false;
    DiffTextSelection textSelection = {};
    Vec<DiffCodeRunGeometry> codeRuns;
    Bounds selectionViewport = {};
    Point selectionPointer = {};
    int selectionTimer = 0;
    bool inlineOn = true;
    DiffInlineUnit inlineUnit = DiffInlineUnit::Word;
    int inlineMaxLineLength = 1000;
    int inlinePreparedFiles = 0;
    DiffInlineJob* inlineJob = nullptr;
    int syntaxMaxLineLength = 1000;
    Vec<DiffEvent> emitted;
    Entity<DiffState> self = {};
    DiffSyntaxHighlighter syntaxFn = nullptr;
    void* syntaxData = nullptr;
    bool syntaxPrepared = false;
    Vec<DiffSyntaxSpan> syntaxSpans;
    // What the text hover handler heard last, so it only hears changes:
    // the character (none when !textHoverHasHit) and the modifiers.
    bool textHoverHeard = false;
    bool textHoverHasHit = false;
    DiffTextPosition textHover = {};
    Modifiers textHoverModifiers = {};

    DiffState() = default;
    ~DiffState();
    DiffState(const DiffState&) = delete;
    DiffState& operator=(const DiffState&) = delete;

    static El* Render(DiffState* self, Ctx* cx);
    void Bind(App* app, Entity<DiffState> self);
    void SetFiles(DiffFile** files, int count, Ctx* cx);
    // Clear the view and transfer its file ownership to the caller.
    Vec<DiffFile*> TakeFiles();
    DiffState* WithMode(DiffMode mode);
    DiffState* WithContextLines(bool has, int lines);
    DiffState* WithExpansionLines(int lines);
    DiffState* WithMinCollapsedLines(int lines);
    DiffState* WithInlineUnit(bool on, DiffInlineUnit unit);
    DiffState* WithInlineMaxLineLength(int length);
    DiffState* WithSyntaxMaxLineLength(int length);
    DiffFile** Files(int* count) const;
    int FileCount() const { return len(files); }
    DiffFile* FileAt(int ix) const { return files[ix]; }
    DiffMode Mode() const { return mode; }
    bool HasContextLines() const { return hasContext; }
    int ContextLines() const { return contextLines; }
    int ExpansionLines() const { return expansionLines; }
    int MinCollapsedLines() const { return minCollapsedLines; }
    bool HasInlineUnit() const { return inlineOn; }
    DiffInlineUnit InlineUnit() const { return inlineUnit; }
    int InlineMaxLineLength() const { return inlineMaxLineLength; }
    int SyntaxMaxLineLength() const { return syntaxMaxLineLength; }
    bool HasSelectedLines() const { return hasSelection; }
    DiffLineRange SelectedLines() const { return selected; }
    bool IsFileCollapsed(Str path) const;
    void SetMode(DiffMode mode, Ctx* cx);
    void SetContextLines(bool has, int lines, Ctx* cx);
    void SetExpansionLines(int lines, Ctx* cx);
    void SetMinCollapsedLines(int lines, Ctx* cx);
    void SetInlineUnit(bool on, DiffInlineUnit unit, Ctx* cx);
    void SetInlineMaxLineLength(int length, Ctx* cx);
    void SetSyntaxMaxLineLength(int length, Ctx* cx);
    // HighlightStyle over one byte range of a hunk side. `weight` 0 and
    // `hasColor` false leave those fields unset. A range that splits a
    // UTF-8 sequence is dropped.
    DiffState* WithSyntaxHighlighter(DiffSyntaxHighlighter fn, void* data);
    void SetSyntaxHighlighter(DiffSyntaxHighlighter fn, void* data, Ctx* cx);
    void ClearSyntaxHighlighter(Ctx* cx);
    int SyntaxStyleCount(int file, DiffSide side, int line) const;
    DiffSyntaxStyle SyntaxStyleAt(int file, DiffSide side, int line,
                                  int ix) const;
    void ExpandUnchanged(Ctx* cx);
    void CollapseUnchanged(Ctx* cx);
    // Update the requested paths in one rebuild without emitting a user event.
    void SetFilesCollapsed(const Str* paths, int count, bool collapsed,
                           Ctx* cx);
    void SetAllFilesCollapsed(bool collapsed, Ctx* cx);
    void SetFileCollapsed(Str path, bool collapsed, Ctx* cx);
    bool ConflictResolution(Str path, int ix,
                            DiffConflictResolution* out) const;
    void ResolveConflict(Str path, int ix, bool has,
                         DiffConflictResolution kind, Ctx* cx);
    // Arena string, or empty with false while a conflict is open.
    bool ResolvedText(Arena* a, Str path, Str* out) const;
    void SetSelectedLines(bool has, DiffLineRange range, Ctx* cx);
    Str SelectedText(Arena* a) const;
    // Whether the patch carries the source line at `position`, shown or
    // folded. A line beyond the context the patch was made with is not
    // there to scroll to.
    bool HasLine(DiffLinePosition position) const;
    void ScrollToLine(DiffLinePosition position, Ctx* cx);
    void ScrollToFile(Str path, Ctx* cx);
    void NextChange(Ctx* cx);
    void PreviousChange(Ctx* cx);
    void ExpandFold(int file, int start, int end, DiffFoldExpansion how,
                    Ctx* cx);
    void ChooseConflict(int file, int ix, bool has, DiffConflictResolution kind,
                        Ctx* cx);
    void ToggleFileCollapsed(int file, Ctx* cx);
    void BeginLineSelection(DiffLinePosition position, bool extend, Ctx* cx);
    void DragLineSelection(DiffLinePosition position, Ctx* cx);
    void EndLineSelection(Ctx* cx);
    // Synchronously prepare the files for callers that inspect spans without
    // rendering a view. Normal rendering prepares inline runs asynchronously.
    void EnsurePresentation();
    void EnsureSyntax();
    void EnsureInlinePresentation(bool background);
    int RowCount() const { return len(rows); }
    const DiffRow* Rows() const { return rows.els; }
    int ScrollItem() const { return scrollItem; }
};

// Byte ranges that differ, on each side. Empty when the lines share less
// than a quarter of their comparison tokens.
void DiffChangedRuns(Str oldText, Str newText, DiffInlineUnit unit,
                     Vec<DiffRun>* oldRuns, Vec<DiffRun>* newRuns);

// Display chunks of at most 512 bytes, split on code points.
void DiffDisplayChunks(Str text, Vec<DiffChunk>* out);

struct Diff {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Entity<DiffState> state = {};
    bool lineNumber = true;
    bool syntaxHighlight = true;
    bool headerVisible = true;
    DiffHoverHighlight hoverHighlight = DiffHoverHighlight::None;
    DiffHunkSeparator hunkSeparator = DiffHunkSeparator::Metadata;
    DiffChangeIndicator changeIndicator = DiffChangeIndicator::Signs;
    bool changeBackground = true;
    bool softWrap = false;
    float splitRatio = 0.5f;
    // Application slots. A null function leaves the default header or no
    // annotation. `user` is the closure environment for the frame.
    const DiffAnnotation* annotations = nullptr;
    int annotationCount = 0;
    El* (*annotationContent)(Ctx*, const DiffAnnotation*, void*) = nullptr;
    void* annotationUser = nullptr;
    // Supplied every render, the same way annotations are. The lane is
    // shown only when there is at least one marker and a renderer.
    const DiffDecoration* decorations = nullptr;
    int decorationCount = 0;
    const DiffSpanHighlight** spanHighlights = nullptr;
    int spanHighlightCount = 0;
    const DiffGutterMarker* gutterMarkers = nullptr;
    int gutterMarkerCount = 0;
    El* (*gutterMarkerContent)(Ctx*, const DiffGutterMarker*, void*) = nullptr;
    void* gutterMarkerUser = nullptr;
    El* (*header)(Ctx*, const DiffFile*, void*) = nullptr;
    El* (*headerPrefix)(Ctx*, const DiffFile*, void*) = nullptr;
    El* (*headerTitleSuffix)(Ctx*, const DiffFile*, void*) = nullptr;
    El* (*headerSuffix)(Ctx*, const DiffFile*, void*) = nullptr;
    void* headerUser = nullptr;
    void (*onAddAnnotation)(Ctx*, const DiffLineRange*, void*) = nullptr;
    void* addUser = nullptr;
    void (*onLineClick)(Ctx*, const DiffLinePosition*, const ClickEvent*,
                        void*) = nullptr;
    void* clickUser = nullptr;
    void (*onLineHover)(Ctx*, const DiffLinePosition*, bool, void*) = nullptr;
    void* hoverUser = nullptr;
    void (*onTextHover)(Ctx*, const DiffTextPosition*, Modifiers,
                        void*) = nullptr;
    void* textHoverUser = nullptr;
    bool (*onTextClick)(Ctx*, const DiffTextPosition*, const ClickEvent*,
                        void*) = nullptr;
    void* textClickUser = nullptr;
    bool hasTextCursor = false;
    CursorKind textCursor = CursorKind::Arrow;
    static Diff* New(Ctx* cx, Entity<DiffState> state);
    Diff* LineNumber(bool v);
    Diff* SyntaxHighlight(bool v);
    Diff* HeaderVisible(bool v);
    Diff* HoverHighlight(DiffHoverHighlight v);
    Diff* HunkSeparator(DiffHunkSeparator v);
    Diff* ChangeIndicator(DiffChangeIndicator v);
    Diff* ChangeBackground(bool v);
    Diff* SoftWrap(bool v);
    // Share of the viewport used by the original column in split mode.
    Diff* SplitRatio(float ratio);
    Diff* Annotations(const DiffAnnotation* items, int count);
    Diff* RenderAnnotation(El* (*fn)(Ctx*, const DiffAnnotation*, void*),
                           void* user);
    Diff* Decorations(const DiffDecoration* items, int count);
    Diff* SpanHighlights(const DiffSpanHighlight* items, int count);
    Diff* GutterMarkers(const DiffGutterMarker* items, int count);
    Diff* RenderGutterMarker(El* (*fn)(Ctx*, const DiffGutterMarker*, void*),
                             void* user);
    Diff* RenderHeader(El* (*fn)(Ctx*, const DiffFile*, void*), void* user);
    Diff* RenderHeaderPrefix(El* (*fn)(Ctx*, const DiffFile*, void*),
                             void* user);
    Diff* RenderHeaderTitleSuffix(El* (*fn)(Ctx*, const DiffFile*, void*),
                                  void* user);
    Diff* RenderHeaderSuffix(El* (*fn)(Ctx*, const DiffFile*, void*),
                             void* user);
    // Compatibility aliases for the render_* names.
    Diff* AnnotationContent(El* (*fn)(Ctx*, const DiffAnnotation*, void*),
                            void* user);
    Diff* Header(El* (*fn)(Ctx*, const DiffFile*, void*), void* user);
    Diff* HeaderPrefix(El* (*fn)(Ctx*, const DiffFile*, void*), void* user);
    Diff* HeaderTitleSuffix(El* (*fn)(Ctx*, const DiffFile*, void*),
                            void* user);
    Diff* HeaderSuffix(El* (*fn)(Ctx*, const DiffFile*, void*), void* user);
    Diff* OnAddAnnotation(void (*fn)(Ctx*, const DiffLineRange*, void*),
                          void* user);
    // Handles a click on a line's code, outside a text-selection drag. A
    // click that OnTextClick claims does not reach this handler.
    Diff* OnLineClick(void (*fn)(Ctx*, const DiffLinePosition*,
                                 const ClickEvent*, void*),
                      void* user);
    // Reports the character under the pointer and the held modifiers, or
    // null when the pointer is over no source text. Fires as the pointer
    // moves and, while the diff has focus, when a modifier goes down or up
    // with the pointer at rest - so a link can light up on the key alone.
    Diff* OnTextHover(void (*fn)(Ctx*, const DiffTextPosition*, Modifiers,
                                 void*),
                      void* user);
    // Handles a click on a character of source text, outside a
    // text-selection drag. The text handler runs first and its return value
    // decides who wins: true claims the click and OnLineClick is not called
    // for it; false lets the click fall through to the line handler. A
    // click that lands on no character goes to the line handler directly.
    Diff* OnTextClick(bool (*fn)(Ctx*, const DiffTextPosition*,
                                 const ClickEvent*, void*),
                      void* user);
    // The pointer shown over source text, such as a pointing hand while the
    // hovered span is a link.
    Diff* TextCursor(CursorKind cursor);
    Diff* OnLineHover(void (*fn)(Ctx*, const DiffLinePosition*, bool, void*),
                      void* user);
    El* IntoEl();
};

void DiffInitKeys();

// Where `side`'s line `lineIx` (an index into DiffFile::Lines) of the first
// file showing it was painted in `win`'s last frame, clipped to what is
// visible: SelectionGeometry::run_bounds, which Rust keeps for tests.
// False when no such line is on screen.
bool DiffRunBounds(Window* win, DiffSide side, int lineIx, Bounds* out);
// unicode-width's non-CJK width, used to rank initial scroll candidates.
int DiffDisplayCells(Str text);

// True when the paired line's text matches and only the ending differs.
bool DiffHasLineEndingChange(const DiffFile* file, DiffSide side, int ix,
                             int counterpart);

} // namespace component

template <>
struct EventEmitter<component::DiffState, component::DiffEvent> {};

} // namespace gpui
#endif // GPUI_SRC_UI_DIFF_H_
