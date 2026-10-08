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

struct DiffLinePosition {
    Str path = {};
    DiffSide side = DiffSide::Original;
    int line = 1;
    static DiffLinePosition New(Str path, DiffSide side, int line);
    Str Path() const { return path; }
    DiffSide Side() const { return side; }
    int Line() const { return line; }
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

// One conflict. Ranges index the modified-side lines, markers excluded.
// Ends are exclusive. A missing base has hasBase false.
struct DiffConflict {
    int currentStart = 0;
    int currentEnd = 0;
    bool hasBase = false;
    int baseStart = 0;
    int baseEnd = 0;
    int incomingStart = 0;
    int incomingEnd = 0;
    Str currentLabel = {};
    Str baseLabel = {};
    Str incomingLabel = {};
    int LinesStart() const { return currentStart; }
    int LinesEnd() const { return incomingEnd; }
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
    int originalLinesStart = 0;
    int originalLinesEnd = 0;
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
    static DiffFile* Unchanged(Str path, Str text);
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
    FocusHandle focus = {};
    bool hasSelection = false;
    DiffLineRange selected = {};
    bool hasAnchor = false;
    DiffLinePosition anchor = {};
    bool hasCursor = false;
    DiffLinePosition cursor = {};
    bool selecting = false;
    bool inlineOn = true;
    DiffInlineUnit inlineUnit = DiffInlineUnit::Word;
    int inlineMaxLineLength = 1000;
    int syntaxMaxLineLength = 1000;
    Vec<DiffEvent> emitted;
    Entity<DiffState> self = {};

    DiffState() = default;
    ~DiffState();
    DiffState(const DiffState&) = delete;
    DiffState& operator=(const DiffState&) = delete;

    static El* Render(DiffState* self, Ctx* cx);
    void Bind(App* app, Entity<DiffState> self);
    void SetFiles(DiffFile** files, int count, Ctx* cx);
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
    void ExpandUnchanged(Ctx* cx);
    void CollapseUnchanged(Ctx* cx);
    void SetFileCollapsed(Str path, bool collapsed, Ctx* cx);
    bool ConflictResolution(Str path, int ix,
                            DiffConflictResolution* out) const;
    void ResolveConflict(Str path, int ix, bool has,
                         DiffConflictResolution kind, Ctx* cx);
    // Arena string, or empty with false while a conflict is open.
    bool ResolvedText(Arena* a, Str path, Str* out) const;
    void SetSelectedLines(bool has, DiffLineRange range, Ctx* cx);
    Str SelectedText(Arena* a) const;
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
    void EnsurePresentation();
    int RowCount() const { return len(rows); }
    const DiffRow* Rows() const { return rows.els; }
    int ScrollItem() const { return scrollItem; }
};

// Byte ranges that differ, on each side. Empty when the lines share less
// than a quarter of their bytes.
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
    // Application slots. A null function leaves the default header or no
    // annotation. `user` is the closure environment for the frame.
    const DiffAnnotation* annotations = nullptr;
    int annotationCount = 0;
    El* (*annotationContent)(Ctx*, const DiffAnnotation*, void*) = nullptr;
    void* annotationUser = nullptr;
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
    static Diff* New(Ctx* cx, Entity<DiffState> state);
    Diff* LineNumber(bool v);
    Diff* SyntaxHighlight(bool v);
    Diff* HeaderVisible(bool v);
    Diff* HoverHighlight(DiffHoverHighlight v);
    Diff* HunkSeparator(DiffHunkSeparator v);
    Diff* ChangeIndicator(DiffChangeIndicator v);
    Diff* ChangeBackground(bool v);
    Diff* SoftWrap(bool v);
    Diff* Annotations(const DiffAnnotation* items, int count);
    Diff* AnnotationContent(El* (*fn)(Ctx*, const DiffAnnotation*, void*),
                            void* user);
    Diff* Header(El* (*fn)(Ctx*, const DiffFile*, void*), void* user);
    Diff* HeaderPrefix(El* (*fn)(Ctx*, const DiffFile*, void*), void* user);
    Diff* HeaderTitleSuffix(El* (*fn)(Ctx*, const DiffFile*, void*),
                            void* user);
    Diff* HeaderSuffix(El* (*fn)(Ctx*, const DiffFile*, void*), void* user);
    Diff* OnAddAnnotation(void (*fn)(Ctx*, const DiffLineRange*, void*),
                          void* user);
    Diff* OnLineClick(void (*fn)(Ctx*, const DiffLinePosition*,
                                 const ClickEvent*, void*),
                      void* user);
    Diff* OnLineHover(void (*fn)(Ctx*, const DiffLinePosition*, bool, void*),
                      void* user);
    El* IntoEl();
};

void DiffInitKeys();

// True when the paired line's text matches and only the ending differs.
bool DiffHasLineEndingChange(const DiffFile* file, DiffSide side, int ix,
                             int counterpart);

} // namespace component

template <>
struct EventEmitter<component::DiffState, component::DiffEvent> {};

} // namespace gpui
#endif // GPUI_SRC_UI_DIFF_H_
