#include "base.h"
#include "ui/diff.h"

#include "base/input_keys.h"
#include "base/text_boundary.h"
#include "base/text_selection.h"
#include "gpui/keymap.h"
#include "gpui/paint.h"
#include "sys/executor.h"
#include "ui/button.h"
#include "ui/diff_width_data.h"
#include "ui/i18n.h"
#include "ui/icon.h"
#include "ui/syntax.h"
#include "ui/theme.h"
#include "ui/virtual_list.h"
#include "base/scroll_bounce.h"

#include <math.h>
#include <stdlib.h>
#include <utility>

namespace gpui {
namespace component {

static int IMin(int a, int b) {
    return a < b ? a : b;
}
static int IMax(int a, int b) {
    return a > b ? a : b;
}

static Str Slice(Str s, int start, int end) {
    if (!s.s || start < 0) start = 0;
    if (end > len(s)) end = len(s);
    if (start > end) start = end;
    return Str(s.s + start, end - start);
}

static Str Own(Arena* a, Str s) {
    return StrDup(a, s);
}

static bool Starts(Str s, const char* prefix) {
    return StrStartsWith(s, prefix);
}

static Str StripSuffix(Str s, char c) {
    if (len(s) > 0 && s.s[len(s) - 1] == c) return Str(s.s, len(s) - 1);
    return s;
}

static DiffParseError Err(int line, const char* message) {
    DiffParseError e;
    e.line = line;
    e.message = Str(message);
    return e;
}

DiffFile::~DiffFile() {
    if (arena) ArenaDelete(arena);
}

static void ClearDiffCodeRuns(DiffState* state) {
    for (const DiffCodeRunGeometry& run : state->codeRuns) {
        if (run.layout) TextLayoutRelease(run.layout);
    }
    VecClear(state->codeRuns);
}

static void CancelDiffInlineJob(DiffState* state);

DiffState::~DiffState() {
    CancelDiffInlineJob(this);
    ClearDiffCodeRuns(this);
    for (int i = 0; i < len(files); i++) delete files[i];
    for (int i = 0; i < len(collapsed); i++) StrFree(collapsed[i]);
    for (int i = 0; i < len(resolutions); i++) StrFree(resolutions[i].path);
}

DiffLinePosition DiffLinePosition::New(Str path, DiffSide side, int line) {
    DiffLinePosition p;
    p.path = path;
    p.side = side;
    p.line = line < 1 ? 1 : line;
    return p;
}

DiffLineRange DiffLineRange::New(Str path, DiffSide side, int start, int end) {
    DiffLineRange r;
    r.path = path;
    r.side = side;
    r.endSide = side;
    r.start = start < 1 ? 1 : start;
    r.end = end < 1 ? 1 : end;
    return r;
}

DiffLineRange DiffLineRange::WithEndSide(DiffSide next) const {
    DiffLineRange r = *this;
    r.endSide = next;
    return r;
}

DiffAnnotation DiffAnnotation::Line(Str id, DiffLinePosition position) {
    DiffAnnotation a;
    a.id = id;
    a.path = position.path;
    a.hasPosition = true;
    a.position = position;
    return a;
}

DiffDecoration DiffDecoration::New(DiffLineRange range) {
    DiffDecoration d;
    d.range = range;
    return d;
}

DiffDecoration DiffDecoration::WithBackground(Rgba fill) const {
    DiffDecoration d = *this;
    d.hasBackground = true;
    d.background = fill;
    return d;
}

DiffSpanHighlight DiffSpanHighlight::New(DiffLinePosition position,
                                         ByteRange span) {
    DiffSpanHighlight highlight;
    highlight.position = position;
    highlight.span = span;
    return highlight;
}

DiffSpanHighlight DiffSpanHighlight::WithUnderline() const {
    DiffSpanHighlight highlight = *this;
    highlight.underlined = true;
    return highlight;
}

DiffSpanHighlight DiffSpanHighlight::WithBackground(Rgba fill) const {
    DiffSpanHighlight highlight = *this;
    highlight.hasBackground = true;
    highlight.background = fill;
    return highlight;
}

DiffSpanHighlight DiffSpanHighlight::WithForeground(Rgba color) const {
    DiffSpanHighlight highlight = *this;
    highlight.hasForeground = true;
    highlight.foreground = color;
    return highlight;
}

DiffGutterMarker DiffGutterMarker::New(Str id, DiffLinePosition position) {
    DiffGutterMarker m;
    m.id = id;
    m.position = position;
    return m;
}

DiffGutterMarker DiffGutterMarker::WithLabel(Str text) const {
    DiffGutterMarker m = *this;
    m.label = text;
    return m;
}

static bool DecorationCovers(const DiffDecoration& decoration, DiffSide side,
                             int line) {
    const DiffLineRange& range = decoration.range;
    if (range.IsSingleSide()) {
        if (side != range.side) return false;
        return line >= range.start && line <= range.end;
    }
    if (side == range.side) return line >= range.start;
    return line <= range.end;
}

static bool SamePosition(const DiffLinePosition& a, const DiffLinePosition& b) {
    return a.side == b.side && a.line == b.line && StrEq(a.path, b.path);
}

const DiffDecoration* DiffDecorationAt(const DiffDecoration* items, int count,
                                       const DiffLinePosition& position) {
    const DiffDecoration* found = nullptr;
    if (!items || count <= 0) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!StrEq(items[i].range.path, position.path)) continue;
        if (DecorationCovers(items[i], position.side, position.line))
            found = &items[i];
    }
    return found;
}

const DiffGutterMarker* DiffGutterMarkerAt(const DiffGutterMarker* items,
                                           int count,
                                           const DiffLinePosition& position) {
    if (!items || count <= 0) return nullptr;
    for (int i = 0; i < count; i++) {
        if (SamePosition(items[i].position, position)) return &items[i];
    }
    return nullptr;
}

DiffAnnotation DiffAnnotation::File(Str id, Str path) {
    DiffAnnotation a;
    a.id = id;
    a.path = path;
    return a;
}

void DiffConflict::Lines(int* start, int* end) const {
    *start = linesStart;
    *end = linesEnd;
}

void DiffConflict::CurrentLines(int* start, int* end) const {
    *start = currentLinesStart;
    *end = currentLinesEnd;
}

bool DiffConflict::BaseLines(int* start, int* end) const {
    if (!hasBaseLines) return false;
    *start = baseLinesStart;
    *end = baseLinesEnd;
    return true;
}

void DiffConflict::IncomingLines(int* start, int* end) const {
    *start = incomingLinesStart;
    *end = incomingLinesEnd;
}

bool DiffConflict::BaseLabel(Str* out) const {
    if (!hasBase) return false;
    if (out) *out = baseLabel;
    return true;
}

bool DiffConflict::Part(DiffConflictPart part, int* start, int* end) const {
    if (part == DiffConflictPart::Current) {
        *start = currentStart;
        *end = currentEnd;
        return true;
    }
    if (part == DiffConflictPart::Incoming) {
        *start = incomingStart;
        *end = incomingEnd;
        return true;
    }
    if (!hasBase) return false;
    *start = baseStart;
    *end = baseEnd;
    return true;
}

Str DiffConflict::Label(DiffConflictPart part) const {
    if (part == DiffConflictPart::Base) return baseLabel;
    if (part == DiffConflictPart::Incoming) return incomingLabel;
    return currentLabel;
}

bool DiffConflict::HasPart(int ix) const {
    int start = 0;
    int end = 0;
    DiffConflictPart parts[3] = {DiffConflictPart::Current,
                                 DiffConflictPart::Base,
                                 DiffConflictPart::Incoming};
    for (int i = 0; i < 3; i++) {
        if (!Part(parts[i], &start, &end)) continue;
        if (ix >= start && ix < end) return true;
    }
    return false;
}

DiffConflictPart DiffConflict::PartOf(int ix) const {
    int start = 0;
    int end = 0;
    if (Part(DiffConflictPart::Current, &start, &end) && ix >= start &&
        ix < end)
        return DiffConflictPart::Current;
    if (Part(DiffConflictPart::Base, &start, &end) && ix >= start && ix < end)
        return DiffConflictPart::Base;
    return DiffConflictPart::Incoming;
}

int DiffHunk::HiddenLinesBefore(const DiffHunk* previous) const {
    int after = previous ? previous->originalLinesEnd : 1;
    int hidden = originalLinesStart - after;
    return hidden > 0 ? hidden : 0;
}

static const DiffFileSide* SideOf(const DiffFile* file, DiffSide side) {
    return side == DiffSide::Original ? &file->original : &file->modified;
}

int DiffFile::LinesCount(DiffSide side) const {
    return len(SideOf(this, side)->lines);
}

Str DiffFile::Source(DiffSide side) const {
    return SideOf(this, side)->source;
}

const DiffSourceLine* DiffFile::Lines(DiffSide side) const {
    return SideOf(this, side)->lines.els;
}

int DiffFile::LineNumber(DiffSide side, int index) const {
    return SideOf(this, side)->lines[index].lineNumber;
}

// How many of a side's lines (sorted by number) are numbered below `line`,
// or at most `line` with `inclusive`: a lower or an upper bound.
static int LinesBefore(const Vec<DiffSourceLine>& lines, int line,
                       bool inclusive) {
    int lo = 0;
    int hi = len(lines);
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int n = lines[mid].lineNumber;
        if (n < line || (inclusive && n == line))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int DiffFile::LineIndex(DiffSide side, int line) const {
    const Vec<DiffSourceLine>& lines = SideOf(this, side)->lines;
    int lo = LinesBefore(lines, line, false);
    if (lo < len(lines) && lines[lo].lineNumber == line) return lo;
    return -1;
}

int DiffFile::PatchIx(DiffSide side, int ix) const {
    return patchIxs[side == DiffSide::Original ? 0 : 1][ix];
}

Str DiffFile::TextForLines(Arena* a, DiffSide side, int start, int end) const {
    const Vec<DiffSourceLine>& lines = SideOf(this, side)->lines;
    int first = LinesBefore(lines, start, false);
    int last = LinesBefore(lines, end, true);
    if (first >= last) return {};
    const DiffSourceLine& aLine = lines[first];
    const DiffSourceLine& bLine = lines[last - 1];
    return Own(a, Slice(Source(side), aLine.sourceStart, bLine.sourceEnd));
}

bool DiffFile::OriginalPath(Str* out) const {
    if (!original.hasPath) return false;
    *out = original.path;
    return true;
}

bool DiffFile::ModifiedPath(Str* out) const {
    if (!modified.hasPath) return false;
    *out = modified.path;
    return true;
}

bool DiffFile::HasChanges() const {
    return additions > 0 || deletions > 0 || len(extendedHeaders) > 0 ||
           binary || !original.hasPath || !modified.hasPath ||
           status == DiffFileStatus::Conflicted;
}

bool DiffFile::IsSingleColumn() const {
    return status == DiffFileStatus::Unchanged ||
           status == DiffFileStatus::Conflicted;
}

DiffFile* DiffFile::WithLanguage(Str name) {
    hasLanguage = true;
    language = Own(arena, name);
    return this;
}

static void PushChunk(Vec<DiffChunk>* out, int start, int end) {
    if (start >= end) return;
    DiffChunk c;
    c.start = start;
    c.end = end;
    VecAppend(*out, c);
}

void DiffDisplayChunks(Str text, Vec<DiffChunk>* out) {
    const int kMax = 512;
    VecClear(*out);
    if (len(text) <= kMax) {
        PushChunk(out, 0, len(text));
        return;
    }
    int start = 0;
    int end = 0;
    int i = 0;
    while (i < len(text)) {
        int next = i + Utf8LeadLen((uint8_t)text.s[i]);
        if (next > len(text)) next = len(text);
        if (next - start > kMax && start < i) {
            PushChunk(out, start, i);
            start = i;
        }
        if (next - i > kMax) {
            int at = i;
            while (at < next) {
                int step = 1;
                if (at + step - start > kMax && start < at) {
                    PushChunk(out, start, at);
                    start = at;
                }
                at += step;
            }
        }
        end = next;
        i = next;
    }
    if (start < end) PushChunk(out, start, end);
}

static int ColumnStep(unsigned char c) {
    return Utf8LeadLen(c);
}

static void FinishLine(DiffFile* file, DiffFileSide* side, Str source,
                       int number, int rangeStart, int rangeEnd) {
    Str raw = Slice(source, rangeStart, rangeEnd);
    Str content = raw;
    if (len(content) > 0 && content.s[len(content) - 1] == '\n')
        content = Str(content.s, len(content) - 1);
    if (len(content) > 0 && content.s[len(content) - 1] == '\r')
        content = Str(content.s, len(content) - 1);
    DiffSourceLine line;
    line.lineNumber = number;
    line.text = Slice(source, rangeStart, rangeStart + len(content));
    line.sourceStart = rangeStart;
    line.sourceEnd = rangeEnd;
    line.contentEnd = rangeStart + len(content);
    line.counterpart = -1;
    line.tabBegin = len(side->tabs);
    bool hasTab = false;
    for (int i = 0; i < len(content); i++) {
        if (content.s[i] == '\t') hasTab = true;
    }
    StrBuilder display;
    if (hasTab) {
        int column = 0;
        for (int i = 0; i < len(content);) {
            if (content.s[i] == '\t') {
                int spaces = 4 - column % 4;
                DiffDisplayTab tab;
                tab.sourceOffset = i;
                tab.displayOffset = len(display);
                tab.width = spaces;
                VecAppend(side->tabs, tab);
                for (int s = 0; s < spaces; s++) display.AppendChar(' ');
                column += spaces;
                i++;
            } else {
                int step = ColumnStep((unsigned char)content.s[i]);
                if (i + step > len(content)) step = 1;
                display.Append(Slice(content, i, i + step));
                column += 1;
                i += step;
            }
        }
        line.display = Own(file->arena, Str(display.els, len(display)));
    } else {
        line.display = line.text;
    }
    line.tabCount = len(side->tabs) - line.tabBegin;
    Vec<DiffChunk> chunks;
    DiffDisplayChunks(line.display, &chunks);
    line.chunkBegin = len(side->chunks);
    line.chunkCount = len(chunks);
    for (int i = 0; i < len(chunks); i++) VecAppend(side->chunks, chunks[i]);
    VecAppend(side->lines, line);
}

static void PatchOrder(DiffFile* file) {
    Vec<DiffPatchLine> lines;
    int nOrig = len(file->original.lines);
    int nMod = len(file->modified.lines);
    VecResize(file->patchIxs[0], nOrig);
    VecResize(file->patchIxs[1], nMod);
    for (int i = 0; i < nOrig; i++) file->patchIxs[0][i] = 0;
    for (int i = 0; i < nMod; i++) file->patchIxs[1][i] = 0;
    int hunks = len(file->hunks);
    int ranges = hunks == 0 ? 1 : hunks;
    for (int h = 0; h < ranges; h++) {
        int begin = hunks == 0 ? 0 : file->hunks[h].pairsStart;
        int end = hunks == 0 ? len(file->pairs) : file->hunks[h].pairsEnd;
        int ix = begin;
        while (ix < end) {
            if (!file->pairs[ix].changed) {
                DiffPatchLine line;
                line.original = file->pairs[ix].original;
                line.modified = file->pairs[ix].modified;
                VecAppend(lines, line);
                ix++;
                continue;
            }
            int start = ix;
            while (ix < end && file->pairs[ix].changed) ix++;
            for (int p = start; p < ix; p++) {
                if (file->pairs[p].original < 0) continue;
                DiffPatchLine line;
                line.original = file->pairs[p].original;
                line.modified = -1;
                VecAppend(lines, line);
            }
            for (int p = start; p < ix; p++) {
                if (file->pairs[p].modified < 0) continue;
                DiffPatchLine line;
                line.original = -1;
                line.modified = file->pairs[p].modified;
                VecAppend(lines, line);
            }
        }
    }
    for (int i = 0; i < len(lines); i++) {
        if (lines[i].original >= 0) file->patchIxs[0][lines[i].original] = i;
        if (lines[i].modified >= 0) file->patchIxs[1][lines[i].modified] = i;
    }
    file->patchLines = lines;
}

static int Digits(int n) {
    if (n < 1) n = 1;
    int d = 1;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

// The end of parsing either form of a file: the gutter's width from the last
// modified line's number, and the patch order.
static DiffFile* FinishParsedFile(DiffFile* file) {
    file->lineNumberDigits = Digits(
        len(file->modified.lines)
            ? file->modified.lines[len(file->modified.lines) - 1].lineNumber
            : 1);
    PatchOrder(file);
    return file;
}

static void Seal(DiffFile* file, DiffFileStatus status, StrBuilder* origSrc,
                 Vec<int>* origNum, Vec<int>* origStart, Vec<int>* origEnd,
                 StrBuilder* modSrc, Vec<int>* modNum, Vec<int>* modStart,
                 Vec<int>* modEnd) {
    file->status = status;
    file->original.source = Own(file->arena, Str(origSrc->els, len(origSrc)));
    file->modified.source = Own(file->arena, Str(modSrc->els, len(modSrc)));
    for (int i = 0; i < len(origNum); i++) {
        FinishLine(file, &file->original, file->original.source, (*origNum)[i],
                   (*origStart)[i], (*origEnd)[i]);
    }
    for (int i = 0; i < len(modNum); i++) {
        FinishLine(file, &file->modified, file->modified.source, (*modNum)[i],
                   (*modStart)[i], (*modEnd)[i]);
    }
    file->additions = 0;
    file->deletions = 0;
    for (int i = 0; i < len(file->pairs); i++) {
        DiffLinePair& pair = file->pairs[i];
        if (pair.changed && status != DiffFileStatus::Conflicted) {
            if (pair.modified >= 0) file->additions++;
            if (pair.original >= 0) file->deletions++;
        }
        if (pair.original >= 0 && pair.modified >= 0) {
            file->original.lines[pair.original].counterpart = pair.modified;
            file->modified.lines[pair.modified].counterpart = pair.original;
        }
    }
    int widest = 1;
    if (len(file->original.lines))
        widest =
            IMax(widest, file->original.lines[len(file->original.lines) - 1]
                             .lineNumber);
    if (len(file->modified.lines))
        widest =
            IMax(widest, file->modified.lines[len(file->modified.lines) - 1]
                             .lineNumber);
    file->lineNumberDigits = Digits(widest);
    if (file->modified.hasPath)
        file->path = file->modified.path;
    else if (file->original.hasPath)
        file->path = file->original.path;
    PatchOrder(file);
}

struct SideBuild {
    StrBuilder source;
    Vec<int> number;
    Vec<int> start;
    Vec<int> end;
    bool noNewline = false;
    int Push(int lineNumber, Str content) {
        int at = len(source);
        source.Append(content);
        source.AppendChar('\n');
        VecAppend(number, lineNumber);
        VecAppend(start, at);
        VecAppend(end, len(source));
        return len(number) - 1;
    }
    void RemoveFinalNewline() {
        if (len(end) == 0) return;
        if (len(source) > 0) source.len--;
        end[len(end) - 1]--;
        noNewline = true;
    }
};

static bool ParseU64(Str s, uint64_t* out) {
    if (len(s) <= 0) return false;
    uint64_t v = 0;
    for (int i = 0; i < len(s); i++) {
        char c = s.s[i];
        if (c < '0' || c > '9') return false;
        uint64_t d = (uint64_t)(c - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    *out = v;
    return true;
}

static int QuotedEnd(Str value) {
    if (len(value) < 2 || value.s[0] != '"') return -1;
    bool escaped = false;
    for (int i = 1; i < len(value); i++) {
        char c = value.s[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\')
            escaped = true;
        else if (c == '"')
            return i + 1;
    }
    return -1;
}

static bool Utf8Ok(const uint8_t* b, int n) {
    int i = 0;
    while (i < n) {
        uint8_t c = b[i];
        int need = Utf8LeadLen(c);
        if (need == 1 && c >= 0x80) return false;
        if (i + need > n) return false;
        for (int k = 1; k < need; k++) {
            if ((b[i + k] & 0xc0) != 0x80) return false;
        }
        i += need;
    }
    return true;
}

static bool DecodePath(Arena* a, Str value, int number, Str* out,
                       DiffParseError* error) {
    if (len(value) == 0 || value.s[0] != '"') {
        *out = Own(a, value);
        return true;
    }
    if (QuotedEnd(value) != len(value)) {
        *error = Err(number, "invalid quoted file path");
        return false;
    }
    Vec<uint8_t> bytes;
    const uint8_t* in = (const uint8_t*)value.s + 1;
    int n = len(value) - 2;
    int ix = 0;
    while (ix < n) {
        if (in[ix] != '\\') {
            VecAppend(bytes, in[ix]);
            ix++;
            continue;
        }
        ix++;
        if (ix >= n) {
            *error = Err(number, "invalid path escape");
            return false;
        }
        uint8_t escaped = in[ix];
        if (escaped >= '0' && escaped <= '7') {
            int byte = 0;
            int count = 0;
            while (count < 3 && ix < n && in[ix] >= '0' && in[ix] <= '7') {
                byte = byte * 8 + (in[ix] - '0');
                ix++;
                count++;
            }
            if (byte > 255) {
                *error = Err(number, "invalid octal path escape");
                return false;
            }
            VecAppend(bytes, (uint8_t)byte);
            continue;
        }
        uint8_t ch = 0;
        if (escaped == 'n')
            ch = '\n';
        else if (escaped == 't')
            ch = '\t';
        else if (escaped == 'r')
            ch = '\r';
        else if (escaped == 'a')
            ch = 7;
        else if (escaped == 'b')
            ch = 8;
        else if (escaped == 'f')
            ch = 12;
        else if (escaped == 'v')
            ch = 11;
        else if (escaped == '"' || escaped == '\\')
            ch = escaped;
        else {
            *error = Err(number, "unsupported path escape");
            return false;
        }
        VecAppend(bytes, ch);
        ix++;
    }
    if (!Utf8Ok(bytes.els, len(bytes))) {
        *error = Err(number, "file path is not valid UTF-8");
        return false;
    }
    *out = Own(a, Str((char*)bytes.els, len(bytes)));
    return true;
}

static bool HeaderPath(Arena* a, Str value, int number, bool* present, Str* out,
                       DiffParseError* error) {
    int tab = StrFind(value, "\t");
    if (tab >= 0) value = Str(value.s, tab);
    if (StrEq(value, "/dev/null")) {
        *present = false;
        return true;
    }
    *present = true;
    return DecodePath(a, value, number, out, error);
}

struct PrefixPair {
    const char* oldP;
    const char* newP;
};

static const PrefixPair kGitPrefixes[7] = {
    {"a/", "b/"}, {"i/", "w/"}, {"c/", "w/"}, {"c/", "i/"},
    {"o/", "w/"}, {"1/", "2/"}, {"", ""},
};

static Str StripPrefix(Arena* a, Str path, const char* prefix) {
    int n = (int)strlen(prefix);
    if (n > 0 && StrStartsWith(path, prefix))
        return Own(a, Slice(path, n, len(path)));
    return path;
}

static bool GitPaths(Arena* a, Str value, Str* oldOut, Str* newOut,
                     const char** oldP, const char** newP,
                     DiffParseError* error) {
    auto detect = [&](Str oldPath, Str newPath) {
        for (int i = 0; i < 7; i++) {
            if (Starts(oldPath, kGitPrefixes[i].oldP) &&
                Starts(newPath, kGitPrefixes[i].newP))
                return kGitPrefixes[i];
        }
        return kGitPrefixes[6];
    };
    Str oldPath;
    Str newPath;
    PrefixPair prefixes = kGitPrefixes[6];
    if (len(value) && value.s[0] == '"') {
        int end = QuotedEnd(value);
        if (end < 0) return false;
        if (!DecodePath(a, Slice(value, 0, end), 1, &oldPath, error))
            return false;
        Str rest = Slice(value, end, len(value));
        while (len(rest) && rest.s[0] == ' ') rest = Slice(rest, 1, len(rest));
        if (!DecodePath(a, rest, 1, &newPath, error)) return false;
        prefixes = detect(oldPath, newPath);
    } else if (len(value) && value.s[len(value) - 1] == '"') {
        int start = -1;
        for (int i = 0; i + 1 < len(value); i++) {
            if (value.s[i] == ' ' && value.s[i + 1] == '"') {
                int q = QuotedEnd(Slice(value, i + 1, len(value)));
                if (q == len(value) - (i + 1)) start = i + 1;
            }
        }
        if (start < 0) return false;
        oldPath = Own(a, Slice(value, 0, start - 1));
        if (!DecodePath(a, Slice(value, start, len(value)), 1, &newPath, error))
            return false;
        prefixes = detect(oldPath, newPath);
    } else {
        bool found = false;
        for (int p = 0; p < 7 && !found; p++) {
            int oldN = (int)strlen(kGitPrefixes[p].oldP);
            int newN = (int)strlen(kGitPrefixes[p].newP);
            for (int i = 0; i < len(value); i++) {
                if (value.s[i] != ' ') continue;
                Str oldS = Slice(value, 0, i);
                Str newS = Slice(value, i + 1, len(value));
                if (!Starts(oldS, kGitPrefixes[p].oldP) ||
                    !Starts(newS, kGitPrefixes[p].newP))
                    continue;
                Str path = Slice(oldS, oldN, len(oldS));
                if (len(path) == 0) continue;
                if (len(newS) == newN + len(path) &&
                    memcmp(newS.s + newN, path.s, (size_t)len(path)) == 0) {
                    oldPath = Own(a, oldS);
                    newPath = Own(a, newS);
                    prefixes = kGitPrefixes[p];
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            for (int p = 0; p < 6 && !found; p++) {
                if (kGitPrefixes[p].oldP[0] == 0) continue;
                Str mark = fmt(" %s", Str(kGitPrefixes[p].newP));
                int at = -1;
                int from = 0;
                while (from < len(value)) {
                    int hit = StrFind(Slice(value, from, len(value)), mark);
                    if (hit < 0) break;
                    at = from + hit;
                    from = at + 1;
                }
                if (at < 0) continue;
                if (!Starts(value, kGitPrefixes[p].oldP)) continue;
                oldPath = Own(a, Slice(value, 0, at));
                newPath = Own(a, Slice(value, at + 1, len(value)));
                prefixes = kGitPrefixes[p];
                found = true;
            }
        }
        if (!found) {
            int sp = StrFind(value, " ");
            if (sp < 0) return false;
            oldPath = Own(a, Slice(value, 0, sp));
            newPath = Own(a, Slice(value, sp + 1, len(value)));
            prefixes = kGitPrefixes[6];
        }
    }
    *oldOut = StripPrefix(a, oldPath, prefixes.oldP);
    *newOut = StripPrefix(a, newPath, prefixes.newP);
    *oldP = prefixes.oldP;
    *newP = prefixes.newP;
    return true;
}

struct OpenFile {
    DiffFile* file = nullptr;
    SideBuild original;
    SideBuild modified;
    bool headers = false;
    bool renamed = false;
    bool copied = false;
    // An empty truncated hunk supplies no source line, and still orders
    // the next hunk on both sides.
    bool hasLastHunk = false;
    int lastHunkOld = 0;
    int lastHunkNew = 0;
    bool hasPrefixes = false;
    const char* oldPrefix = "";
    const char* newPrefix = "";
    void Flush(Vec<int>* deleted, Vec<int>* added) {
        int n = IMax(len(deleted), len(added));
        for (int i = 0; i < n; i++) {
            DiffLinePair pair;
            pair.original = i < len(deleted) ? (*deleted)[i] : -1;
            pair.modified = i < len(added) ? (*added)[i] : -1;
            pair.changed = true;
            VecAppend(file->pairs, pair);
        }
        VecClear(*deleted);
        VecClear(*added);
    }
    void Reset() {
        file = nullptr;
        VecClear(original.source);
        VecClear(original.number);
        VecClear(original.start);
        VecClear(original.end);
        original.noNewline = false;
        VecClear(modified.source);
        VecClear(modified.number);
        VecClear(modified.start);
        VecClear(modified.end);
        modified.noNewline = false;
        headers = false;
        renamed = false;
        copied = false;
        hasLastHunk = false;
        lastHunkOld = 0;
        lastHunkNew = 0;
        hasPrefixes = false;
        oldPrefix = "";
        newPrefix = "";
    }
};

static bool HunkHeader(Str line, int number, int* oldStart, int* oldCount,
                       int* newStart, int* newCount, DiffParseError* error) {
    if (!Starts(line, "@@ ")) {
        *error = Err(number, "invalid hunk header");
        return false;
    }
    Str body = Slice(line, 3, len(line));
    int at = StrFind(body, " @@");
    if (at < 0) {
        *error = Err(number, "invalid hunk header");
        return false;
    }
    Str ranges = Slice(body, 0, at);
    Str fields[4];
    int nFields = 0;
    int i = 0;
    while (i < len(ranges) && nFields < 4) {
        while (i < len(ranges) && ranges.s[i] == ' ') i++;
        int b = i;
        while (i < len(ranges) && ranges.s[i] != ' ') i++;
        if (b < i) fields[nFields++] = Slice(ranges, b, i);
    }
    if (nFields != 2 || len(fields[0]) < 1 || fields[0].s[0] != '-' ||
        len(fields[1]) < 1 || fields[1].s[0] != '+') {
        *error = Err(number, "invalid hunk header");
        return false;
    }
    auto range = [&](Str value, int* start, int* count) -> bool {
        int comma = StrFind(value, ",");
        Str sPart = comma < 0 ? value : Slice(value, 0, comma);
        Str cPart = comma < 0 ? StrL("1") : Slice(value, comma + 1, len(value));
        uint64_t s = 0;
        uint64_t c = 0;
        if (!ParseU64(sPart, &s) || !ParseU64(cPart, &c)) return false;
        // A zero-count side still names the line before the insertion,
        // so the span is max(count, 1). A start or span that does not
        // fit in int is an invalid header. Rust keeps those as usize
        // (port-status.md).
        uint64_t span = c == 0 ? 1 : c;
        if ((c > 0 && s == 0) || s > UINT64_MAX - span) return false;
        if (s > (uint64_t)INT_MAX || c > (uint64_t)INT_MAX ||
            s + span > (uint64_t)INT_MAX)
            return false;
        *start = (int)s;
        *count = (int)c;
        return true;
    };
    if (!range(Slice(fields[0], 1, len(fields[0])), oldStart, oldCount) ||
        !range(Slice(fields[1], 1, len(fields[1])), newStart, newCount)) {
        *error = Err(number, "invalid hunk header");
        return false;
    }
    return true;
}

static int LastNumber(const SideBuild& side) {
    if (len(side.number) == 0) return -1;
    return side.number[len(side.number) - 1];
}

// A `---` / `+++` pair inside a short hunk can also be source. A
// complete reading wins, so this scan runs once and stops only when
// that pair cannot finish the declared counts.
static bool TruncatedFileHeader(Str* lines, int nLines, int start,
                                int oldRemaining, int newRemaining, int* out) {
    bool header = false;
    int headerIx = 0;
    for (int ix = start; ix < nLines; ix++) {
        Str line = lines[ix];
        if (StrEq(line, "\\ No newline at end of file")) continue;
        if (oldRemaining == 0 && newRemaining == 0) return false;
        if (!header && Starts(line, "--- ") && ix + 1 < nLines &&
            Starts(lines[ix + 1], "+++ ")) {
            header = true;
            headerIx = ix;
        }
        char tag = len(line) == 0 ? ' ' : line.s[0];
        if (tag == ' ' && oldRemaining > 0 && newRemaining > 0) {
            oldRemaining--;
            newRemaining--;
        } else if (tag == '-' && oldRemaining > 0) {
            oldRemaining--;
        } else if (tag == '+' && newRemaining > 0) {
            newRemaining--;
        } else {
            if (!header) return false;
            *out = headerIx;
            return true;
        }
    }
    if ((oldRemaining == 0 && newRemaining == 0) || !header) return false;
    *out = headerIx;
    return true;
}

static bool ParseHunk(OpenFile* open, Str* lines, int nLines, int* ix,
                      DiffParseOptions options, DiffParseError* error) {
    int oldStart = 0, oldCount = 0, newStart = 0, newCount = 0;
    if (!HunkHeader(lines[*ix], *ix + 1, &oldStart, &oldCount, &newStart,
                    &newCount, error))
        return false;
    // A zero-count side names the line before its insertion point.
    int firstOriginal = oldStart + (oldCount == 0 ? 1 : 0);
    int firstModified = newStart + (newCount == 0 ? 1 : 0);
    int lastOld = LastNumber(open->original);
    int lastNew = LastNumber(open->modified);
    // `&&` binds tighter than `||`, matching the Rust condition.
    bool backwards = options.truncatedHunks && open->hasLastHunk &&
                     (firstOriginal < open->lastHunkOld ||
                      firstModified < open->lastHunkNew);
    if (backwards || (lastOld >= 0 && lastOld >= oldStart && oldCount > 0) ||
        (lastNew >= 0 && lastNew >= newStart && newCount > 0)) {
        *error = Err(*ix + 1, "overlapping or unordered hunks");
        return false;
    }
    if ((!open->file->original.hasPath && oldCount != 0) ||
        (!open->file->modified.hasPath && newCount != 0)) {
        *error = Err(*ix + 1, "missing file side has source lines");
        return false;
    }
    int pairsStart = len(open->file->pairs);
    int originalStart = len(open->original.number);
    int modifiedStart = len(open->modified.number);
    int headerLine = *ix;
    (*ix)++;
    bool stopAtFile = false;
    int nextFile = 0;
    if (options.truncatedHunks)
        stopAtFile = TruncatedFileHeader(lines, nLines, *ix, oldCount, newCount,
                                         &nextFile);
    int oldUsed = 0;
    int newUsed = 0;
    Vec<int> deleted;
    Vec<int> added;
    int last = 0;
    while (*ix < nLines) {
        if (stopAtFile && *ix == nextFile) break;
        Str content = lines[*ix];
        if (StrEq(content, "\\ No newline at end of file")) {
            if (last == '-')
                open->original.RemoveFinalNewline();
            else if (last == '+')
                open->modified.RemoveFinalNewline();
            else if (last == ' ') {
                open->original.RemoveFinalNewline();
                open->modified.RemoveFinalNewline();
            } else {
                *error =
                    Err(*ix + 1, "newline marker has no preceding source line");
                return false;
            }
            last = 0;
            (*ix)++;
            continue;
        }
        if (oldUsed == oldCount && newUsed == newCount) break;
        // The first byte decides the tag. A truncated hunk stops before
        // a line whose first byte is not a source tag, so a multibyte
        // character is never sliced.
        char tag = len(content) == 0 ? ' ' : content.s[0];
        if (tag != ' ' && tag != '+' && tag != '-') {
            if (options.truncatedHunks) break;
            *error = Err(*ix + 1, "hunk ended before its declared line counts");
            return false;
        }
        Str text = len(content) == 0 ? Str{} : Slice(content, 1, len(content));
        if ((tag != '+' && open->original.noNewline) ||
            (tag != '-' && open->modified.noNewline)) {
            *error = Err(*ix + 1,
                         "source continues after an unterminated final line");
            return false;
        }
        if (tag == ' ') {
            open->Flush(&deleted, &added);
            if (oldUsed >= oldCount || newUsed >= newCount) {
                *error = Err(*ix + 1, "hunk exceeds declared line counts");
                return false;
            }
            int o = open->original.Push(oldStart + oldUsed, text);
            int m = open->modified.Push(newStart + newUsed, text);
            DiffLinePair pair;
            pair.original = o;
            pair.modified = m;
            pair.changed = false;
            VecAppend(open->file->pairs, pair);
            oldUsed++;
            newUsed++;
        } else if (tag == '-') {
            if (oldUsed >= oldCount) {
                *error = Err(*ix + 1, "hunk exceeds original line count");
                return false;
            }
            VecAppend(deleted, open->original.Push(oldStart + oldUsed, text));
            oldUsed++;
        } else {
            if (newUsed >= newCount) {
                *error = Err(*ix + 1, "hunk exceeds modified line count");
                return false;
            }
            VecAppend(added, open->modified.Push(newStart + newUsed, text));
            newUsed++;
        }
        last = tag;
        (*ix)++;
    }
    bool truncated = oldUsed != oldCount || newUsed != newCount;
    if (truncated && !options.truncatedHunks) {
        *error = Err(*ix + 1, "hunk ended before its declared line counts");
        return false;
    }
    open->Flush(&deleted, &added);
    DiffHunk hunk;
    hunk.pairsStart = pairsStart;
    hunk.pairsEnd = len(open->file->pairs);
    hunk.originalStart = originalStart;
    hunk.originalEnd = len(open->original.number);
    hunk.modifiedStart = modifiedStart;
    hunk.modifiedEnd = len(open->modified.number);
    hunk.label = Own(open->file->arena, lines[headerLine]);
    hunk.originalLinesStart = firstOriginal;
    hunk.originalLinesEnd = firstOriginal + oldUsed;
    hunk.truncated = truncated;
    VecAppend(open->file->hunks, hunk);
    open->hasLastHunk = true;
    open->lastHunkOld = firstOriginal;
    open->lastHunkNew = firstModified;
    return true;
}

static void FinishOpen(OpenFile* open, Vec<DiffFile*>* out) {
    DiffFileStatus status = DiffFileStatus::Modified;
    if (!open->file->original.hasPath)
        status = DiffFileStatus::Added;
    else if (!open->file->modified.hasPath)
        status = DiffFileStatus::Deleted;
    else if (open->copied)
        status = DiffFileStatus::Copied;
    else if (open->renamed)
        status = DiffFileStatus::Renamed;
    Seal(open->file, status, &open->original.source, &open->original.number,
         &open->original.start, &open->original.end, &open->modified.source,
         &open->modified.number, &open->modified.start, &open->modified.end);
    VecAppend(*out, open->file);
    open->file = nullptr;
}

static DiffFile* AllocFile() {
    DiffFile* file = new DiffFile();
    file->arena = ArenaNew();
    return file;
}

bool DiffFile::Parse(Str patch, Vec<DiffFile*>* out, DiffParseError* error) {
    return ParseWith(patch, DiffParseOptions{}, out, error);
}

bool DiffFile::IsTruncated() const {
    for (int i = 0; i < len(hunks); i++) {
        if (hunks[i].truncated) return true;
    }
    return false;
}

bool DiffFile::ParseWith(Str patch, DiffParseOptions options,
                         Vec<DiffFile*>* out, DiffParseError* error) {
    Vec<Str> raw;
    int i = 0;
    if (len(patch) == 0) {
        // An empty patch is no files. A patch of only a newline still splits.
    }
    while (i < len(patch) || (len(patch) == 0 && len(raw) == 0 && false)) {
        if (i >= len(patch)) break;
        int b = i;
        while (i < len(patch) && patch.s[i] != '\n') i++;
        int e = i;
        if (i < len(patch) && patch.s[i] == '\n') i++;
        VecAppend(raw, Slice(patch, b, e));
        if (i == b) break;
    }
    if (len(patch) == 0) {
        return true;
    }
    bool crlf = false;
    for (int k = 0; k < len(raw); k++) {
        Str line = raw[k];
        if (Starts(line, "diff --git ") || Starts(line, "--- ")) {
            crlf = len(line) > 0 && line.s[len(line) - 1] == '\r';
            break;
        }
    }
    if (crlf) {
        for (int k = 0; k < len(raw); k++) raw[k] = StripSuffix(raw[k], '\r');
    }
    OpenFile open;
    // A parse failure keeps nothing: the file being read and every file
    // already finished go.
    auto dropParsed = [&]() {
        delete open.file;
        open.file = nullptr;
        for (int f = 0; f < len(out); f++) delete (*out)[f];
        VecClear(*out);
    };
    for (int ix = 0; ix < len(raw);) {
        Str line = raw[ix];
        if (Starts(line, "diff --cc ") || Starts(line, "diff --combined ") ||
            Starts(line, "@@@")) {
            dropParsed();
            *error = Err(ix + 1, "combined diffs are not supported");
            return false;
        }
        if (Starts(line, "diff --git ")) {
            if (open.file) FinishOpen(&open, out);
            open.Reset();
            open.file = AllocFile();
            Str oldPath;
            Str newPath;
            const char* op = "";
            const char* np = "";
            DiffParseError local;
            if (!GitPaths(open.file->arena, Slice(line, 11, len(line)),
                          &oldPath, &newPath, &op, &np, &local)) {
                dropParsed();
                *error =
                    local.line ? local : Err(ix + 1, "invalid Git file header");
                if (!local.line)
                    *error = Err(ix + 1, "invalid Git file header");
                return false;
            }
            open.file->original.hasPath = true;
            open.file->original.path = oldPath;
            open.file->modified.hasPath = true;
            open.file->modified.path = newPath;
            open.hasPrefixes = true;
            open.oldPrefix = op;
            open.newPrefix = np;
            ix++;
            continue;
        }
        if (StrEq(line, "-- ") || StrEq(line, "--")) {
            if (open.file) FinishOpen(&open, out);
            open.Reset();
            ix++;
            continue;
        }
        if (Starts(line, "--- ")) {
            // A blank line ends the previous file. FinishOpen leaves the
            // side builders in place; the next file must start empty.
            if (open.file && open.headers) {
                FinishOpen(&open, out);
                open.Reset();
            }
            if (!open.file) open.file = AllocFile();
            bool oldPresent = false;
            bool newPresent = false;
            Str oldPath;
            Str newPath;
            if (!HeaderPath(open.file->arena, Slice(line, 4, len(line)), ix + 1,
                            &oldPresent, &oldPath, error)) {
                dropParsed();
                return false;
            }
            ix++;
            if (ix >= len(raw) || !Starts(raw[ix], "+++ ")) {
                dropParsed();
                *error = Err(ix + 1, "expected modified file header");
                return false;
            }
            if (!HeaderPath(open.file->arena, Slice(raw[ix], 4, len(raw[ix])),
                            ix + 1, &newPresent, &newPath, error)) {
                dropParsed();
                return false;
            }
            if (!oldPresent && !newPresent) {
                dropParsed();
                *error = Err(ix + 1, "both file sides are missing");
                return false;
            }
            const char* op = "";
            const char* np = "";
            if (open.hasPrefixes) {
                op = open.oldPrefix;
                np = open.newPrefix;
            } else {
                bool conventional = (!oldPresent || Starts(oldPath, "a/")) &&
                                    (!newPresent || Starts(newPath, "b/"));
                if (conventional) {
                    op = "a/";
                    np = "b/";
                }
            }
            open.file->original.hasPath = oldPresent;
            if (oldPresent)
                open.file->original
                    .path = StripPrefix(open.file->arena, oldPath, op);
            open.file->modified.hasPath = newPresent;
            if (newPresent)
                open.file->modified
                    .path = StripPrefix(open.file->arena, newPath, np);
            open.headers = true;
            ix++;
            continue;
        }
        if (Starts(line, "@@ ")) {
            if (!open.file) {
                dropParsed();
                *error = Err(ix + 1, "hunk has no file header");
                return false;
            }
            if (!open.headers) {
                dropParsed();
                *error = Err(
                    ix + 1, "hunk requires original and modified file headers");
                return false;
            }
            if (!ParseHunk(&open, raw.els, len(raw), &ix, options, error)) {
                dropParsed();
                return false;
            }
            continue;
        }
        if (open.file && open.file->binary) {
            ix++;
            continue;
        }
        if ((Starts(line, "+") || Starts(line, "-") || Starts(line, " ") ||
             Starts(line, "\\ No newline")) &&
            open.file && open.headers) {
            dropParsed();
            *error = Err(ix + 1, "source line outside a hunk");
            return false;
        }
        if (open.file) {
            if (Starts(line, "Binary files ") ||
                StrEq(line, "GIT binary patch"))
                open.file->binary = true;
            if (Starts(line, "rename from ")) {
                Str path;
                if (!DecodePath(open.file->arena, Slice(line, 12, len(line)),
                                ix + 1, &path, error)) {
                    dropParsed();
                    return false;
                }
                open.file->original.hasPath = true;
                open.file->original.path = path;
                open.renamed = true;
            }
            if (Starts(line, "copy from ")) {
                Str path;
                if (!DecodePath(open.file->arena, Slice(line, 10, len(line)),
                                ix + 1, &path, error)) {
                    dropParsed();
                    return false;
                }
                open.file->original.hasPath = true;
                open.file->original.path = path;
                open.copied = true;
            }
            if (Starts(line, "rename to ") || Starts(line, "copy to ")) {
                int skip = Starts(line, "rename to ") ? 10 : 8;
                Str path;
                if (!DecodePath(open.file->arena, Slice(line, skip, len(line)),
                                ix + 1, &path, error)) {
                    dropParsed();
                    return false;
                }
                open.file->modified.hasPath = true;
                open.file->modified.path = path;
            }
            if (Starts(line, "new file mode "))
                open.file->original.hasPath = false;
            if (Starts(line, "deleted file mode "))
                open.file->modified.hasPath = false;
            if (len(line) > 0)
                VecAppend(open.file->extendedHeaders,
                          Own(open.file->arena, line));
        }
        ix++;
    }
    if (open.file) FinishOpen(&open, out);
    bool blank = true;
    for (int k = 0; k < len(patch); k++) {
        char c = patch.s[k];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            blank = false;
            break;
        }
    }
    if (len(out) == 0 && !blank) {
        *error = Err(1, "expected a unified or Git diff");
        return false;
    }
    return true;
}

static void LineRanges(Str text, Vec<int>* start, Vec<int>* end) {
    if (len(text) == 0) return;
    int i = 0;
    while (i < len(text)) {
        int b = i;
        while (i < len(text) && text.s[i] != '\n') i++;
        if (i < len(text) && text.s[i] == '\n') i++;
        VecAppend(*start, b);
        VecAppend(*end, i);
        if (i == b) break;
    }
}

DiffFile* DiffFile::Unchanged(Str path, Str text) {
    DiffExcerpt whole;
    whole.first = 1;
    whole.text = text;
    return Excerpts(path, &whole, 1);
}

DiffFile* DiffFile::Excerpts(Str path, const DiffExcerpt* parts, int count) {
    DiffFile* file = AllocFile();
    file->status = DiffFileStatus::Unchanged;
    file->modified.hasPath = true;
    file->modified.path = Own(file->arena, path);
    file->original.hasPath = true;
    file->original.path = file->modified.path;
    file->path = file->modified.path;
    StrBuilder source;
    Vec<int> numbers;
    Vec<int> starts;
    Vec<int> ends;
    // Parts come in file order without overlap; Rust debug-asserts it.
    for (int p = 0; p < count; p++) {
        // A part cut off mid-line must not run into the next one.
        if (len(source) && source.els[len(source) - 1] != '\n')
            source.AppendChar('\n');
        int offset = len(source);
        int first = IMax(parts[p].first, 1);
        Vec<int> partStarts;
        Vec<int> partEnds;
        LineRanges(parts[p].text, &partStarts, &partEnds);
        for (int i = 0; i < len(partStarts); i++) {
            VecAppend(numbers, first + i);
            VecAppend(starts, offset + partStarts[i]);
            VecAppend(ends, offset + partEnds[i]);
        }
        source.Append(parts[p].text);
    }
    file->modified.source = Own(file->arena, Str(source.els, len(source)));
    for (int i = 0; i < len(starts); i++) {
        FinishLine(file, &file->modified, file->modified.source, numbers[i],
                   starts[i], ends[i]);
        DiffLinePair pair;
        pair.original = -1;
        pair.modified = i;
        pair.changed = false;
        VecAppend(file->pairs, pair);
    }
    return FinishParsedFile(file);
}

static bool MarkerLabel(Str content, const char* prefix, Str* label) {
    int n = (int)strlen(prefix);
    if (!StrStartsWith(content, prefix)) return false;
    Str rest = Slice(content, n, len(content));
    if (len(rest) && rest.s[0] != ' ') return false;
    while (len(rest) && (rest.s[0] == ' ' || rest.s[0] == '\t'))
        rest = Slice(rest, 1, len(rest));
    int end = len(rest);
    while (end > 0 && (rest.s[end - 1] == ' ' || rest.s[end - 1] == '\t'))
        end--;
    *label = Str(rest.s, end);
    return true;
}

DiffFile* DiffFile::ParseConflicts(Str path, Str text, DiffParseError* error) {
    DiffFile* file = AllocFile();
    StrBuilder source;
    Vec<int> numbers;
    Vec<int> starts;
    Vec<int> ends;
    bool open = false;
    DiffConflictPart part = DiffConflictPart::Current;
    DiffConflict conflict;
    Vec<int> lineStart;
    Vec<int> lineEnd;
    LineRanges(text, &lineStart, &lineEnd);
    int lineCount = 0;
    for (int i = 0; i < len(text); i++) {
        if (text.s[i] == '\n') lineCount++;
    }
    if (len(text) && text.s[len(text) - 1] != '\n') lineCount++;
    for (int ix = 0; ix < len(lineStart); ix++) {
        Str line = Slice(text, lineStart[ix], lineEnd[ix]);
        Str content = line;
        if (len(content) && content.s[len(content) - 1] == '\n')
            content = Str(content.s, len(content) - 1);
        if (len(content) && content.s[len(content) - 1] == '\r')
            content = Str(content.s, len(content) - 1);
        Str label;
        bool opening = MarkerLabel(content, "<<<<<<<", &label);
        Str openLabel = label;
        bool base = MarkerLabel(content, "|||||||", &label);
        Str baseLabel = label;
        bool separator = MarkerLabel(content, "=======", &label);
        bool closing = MarkerLabel(content, ">>>>>>>", &label);
        Str closeLabel = label;
        int next = len(numbers);
        if (!open) {
            if (opening) {
                open = true;
                part = DiffConflictPart::Current;
                conflict = {};
                conflict.currentStart = next;
                conflict.currentEnd = next;
                conflict.incomingStart = next;
                conflict.incomingEnd = next;
                conflict.linesStart = ix + 1;
                conflict.linesEnd = ix + 1;
                conflict.currentLinesStart = ix + 2;
                conflict.currentLinesEnd = ix + 2;
                conflict.incomingLinesStart = ix + 2;
                conflict.incomingLinesEnd = ix + 2;
                conflict.currentLabel = Own(file->arena, openLabel);
                continue;
            }
            if (base || closing) {
                delete file;
                *error = Err(ix + 1, "conflict marker outside a conflict");
                return nullptr;
            }
        } else {
            if (opening) {
                delete file;
                *error = Err(ix + 1, "nested conflict marker");
                return nullptr;
            }
            if (part == DiffConflictPart::Current && base) {
                conflict.currentEnd = next;
                conflict.currentLinesEnd = ix + 1;
                conflict.hasBase = true;
                conflict.hasBaseLines = true;
                conflict.baseStart = next;
                conflict.baseEnd = next;
                conflict.baseLinesStart = ix + 2;
                conflict.baseLinesEnd = ix + 2;
                conflict.baseLabel = Own(file->arena, baseLabel);
                part = DiffConflictPart::Base;
                continue;
            }
            if ((part == DiffConflictPart::Current ||
                 part == DiffConflictPart::Base) &&
                separator) {
                if (part == DiffConflictPart::Current) {
                    conflict.currentEnd = next;
                    conflict.currentLinesEnd = ix + 1;
                } else {
                    conflict.baseEnd = next;
                    conflict.baseLinesEnd = ix + 1;
                }
                conflict.incomingStart = next;
                conflict.incomingEnd = next;
                conflict.incomingLinesStart = ix + 2;
                conflict.incomingLinesEnd = ix + 2;
                part = DiffConflictPart::Incoming;
                continue;
            }
            if (part == DiffConflictPart::Incoming && closing) {
                conflict.incomingEnd = next;
                conflict.incomingLinesEnd = ix + 1;
                conflict.linesEnd = ix + 2;
                conflict.incomingLabel = Own(file->arena, closeLabel);
                VecAppend(file->conflicts, conflict);
                open = false;
                continue;
            }
        }
        int at = len(source);
        source.Append(line);
        VecAppend(numbers, ix + 1);
        VecAppend(starts, at);
        VecAppend(ends, len(source));
        DiffLinePair pair;
        pair.original = -1;
        pair.modified = len(numbers) - 1;
        pair.changed = open;
        VecAppend(file->pairs, pair);
    }
    if (open) {
        delete file;
        *error = Err(lineCount > 0 ? lineCount : 1, "unterminated conflict");
        return nullptr;
    }
    file->status = DiffFileStatus::Conflicted;
    file->modified.hasPath = true;
    file->modified.path = Own(file->arena, path);
    file->original.hasPath = true;
    file->original.path = file->modified.path;
    file->path = file->modified.path;
    file->modified.source = Own(file->arena, Str(source.els, len(source)));
    for (int i = 0; i < len(numbers); i++) {
        FinishLine(file, &file->modified, file->modified.source, numbers[i],
                   starts[i], ends[i]);
    }
    return FinishParsedFile(file);
}

static Str DetectedLanguage(Str path) {
    int base = 0;
    for (int i = 0; i < len(path); i++) {
        if (path.s[i] == '/' || path.s[i] == '\\') base = i + 1;
    }
    Str name = Slice(path, base, len(path));
    StrBuilder lower;
    for (int i = 0; i < len(name); i++) {
        char c = name.s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        lower.AppendChar(c);
    }
    Str n = Str(lower.els, len(lower));
    const char* language = "text";
    if (StrEq(n, "makefile") || StrEq(n, "gnumakefile"))
        language = "make";
    else if (StrEq(n, "cmakelists.txt"))
        language = "cmake";
    else {
        int dot = -1;
        for (int i = 0; i < len(n); i++)
            if (n.s[i] == '.') dot = i;
        if (dot >= 0) {
            Str ext = Slice(n, dot + 1, len(n));
            if (StrEq(ext, "jsx") || StrEq(ext, "mjs") || StrEq(ext, "cjs"))
                language = "javascript";
            else if (StrEq(ext, "mts") || StrEq(ext, "cts"))
                language = "typescript";
            else if (StrEq(ext, "cc") || StrEq(ext, "cxx") ||
                     StrEq(ext, "hpp") || StrEq(ext, "hxx"))
                language = "cpp";
            else if (StrEq(ext, "h"))
                language = "c";
            else if (StrEq(ext, "htm"))
                language = "html";
            else if (StrEq(ext, "gql"))
                language = "graphql";
            else if (StrEq(ext, "exs"))
                language = "elixir";
            else if (StrEq(ext, "yml"))
                language = "yaml";
            else {
                const char* known[] = {
                    "astro", "sh",     "bash",    "c",    "cmake", "cs",
                    "cpp",   "css",    "scss",    "diff", "ejs",   "ex",
                    "erb",   "go",     "graphql", "html", "java",  "js",
                    "json",  "jsonc",  "kt",      "kts",  "lua",   "md",
                    "mdx",   "php",    "php3",    "php4", "php5",  "phtml",
                    "proto", "py",     "pyi",     "rb",   "rs",    "scala",
                    "sql",   "svelte", "swift",   "toml", "tsx",   "ts",
                    "yaml",  "zig"};
                for (int k = 0; k < (int)(sizeof(known) / sizeof(known[0]));
                     k++) {
                    if (StrEq(ext, known[k])) {
                        language = known[k];
                        break;
                    }
                }
            }
        }
    }
    if (StrEq(Str(language), "rs")) return StrL("rust");
    return Str(language);
}

// The method was declared twice while the helper settled. Keep one.
Str DiffFile::Language() const {
    if (hasLanguage) return language;
    return DetectedLanguage(path);
}

static bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

struct Tok {
    int start = 0;
    int end = 0;
};

static bool TokSame(Str oldText, Str newText, Tok x, Tok y) {
    int n = x.end - x.start;
    if (n != y.end - y.start) return false;
    if (n == 0) return true;
    return memcmp(oldText.s + x.start, newText.s + y.start, (size_t)n) == 0;
}

static void WordTokens(Str text, Vec<Tok>* out) {
    int i = 0;
    while (i < len(text)) {
        unsigned char c = (unsigned char)text.s[i];
        int step = ColumnStep(c);
        if (i + step > len(text)) step = 1;
        bool space = step == 1 && IsSpace(text.s[i]);
        bool word = !space && (step > 1 || (c >= '0' && c <= '9') ||
                               (c >= 'A' && c <= 'Z') ||
                               (c >= 'a' && c <= 'z') || c == '_');
        int b = i;
        if (space) {
            while (i < len(text) && IsSpace(text.s[i])) i++;
        } else if (word) {
            while (i < len(text)) {
                unsigned char d = (unsigned char)text.s[i];
                int s = ColumnStep(d);
                if (i + s > len(text)) s = 1;
                bool sp = s == 1 && IsSpace(text.s[i]);
                bool w = !sp && (s > 1 || (d >= '0' && d <= '9') ||
                                 (d >= 'A' && d <= 'Z') ||
                                 (d >= 'a' && d <= 'z') || d == '_');
                if (!w) break;
                i += s;
            }
        } else {
            i += step;
        }
        Tok t;
        t.start = b;
        t.end = i;
        VecAppend(*out, t);
    }
}

static void GraphemeTokens(Str text, Vec<Tok>* out) {
    int i = 0;
    while (i < len(text)) {
        int next = TextNextGraphemeBoundary(text, i);
        VecAppend(*out, Tok{i, next});
        i = next;
    }
}

static void PushRun(Vec<DiffRun>* runs, int start, int end, Str text,
                    bool mergeSpace) {
    if (len(runs)) {
        DiffRun& last = (*runs)[len(runs) - 1];
        Str gap = Slice(text, last.end, start);
        if (len(gap) == 0 || (mergeSpace && len(StrTrim(gap)) == 0)) {
            last.end = end;
            return;
        }
    }
    DiffRun run;
    run.start = start;
    run.end = end;
    VecAppend(*runs, run);
}

void DiffChangedRuns(Str oldText, Str newText, DiffInlineUnit unit,
                     Vec<DiffRun>* oldRuns, Vec<DiffRun>* newRuns) {
    VecClear(*oldRuns);
    VecClear(*newRuns);
    if (StrEq(oldText, newText)) return;
    Vec<Tok> a;
    Vec<Tok> b;
    if (unit == DiffInlineUnit::Word) {
        WordTokens(oldText, &a);
        WordTokens(newText, &b);
    } else {
        GraphemeTokens(oldText, &a);
        GraphemeTokens(newText, &b);
    }
    int n = len(a);
    int m = len(b);
    int64_t total = (int64_t)n + m;
    // similar's Myers takes the common prefix and the common suffix before it
    // looks for a middle snake. A pure LCS backtrack can pair an earlier
    // space instead, and the whitespace merge then swallows the shared tail.
    int prefix = 0;
    while (prefix < n && prefix < m &&
           TokSame(oldText, newText, a[prefix], b[prefix]))
        prefix++;
    int suffix = 0;
    while (suffix < n - prefix && suffix < m - prefix &&
           TokSame(oldText, newText, a[n - 1 - suffix], b[m - 1 - suffix]))
        suffix++;
    int nn = n - prefix - suffix;
    int mm = m - prefix - suffix;
    // Bound the unmatched LCS matrix. Long common prefixes and suffixes
    // are cheap and do not count toward this limit.
    constexpr int kMaxCells = 401 * 401;
    if (((int64_t)nn + 1) * ((int64_t)mm + 1) > kMaxCells) return;
    for (int k = 0; k < nn; k++) a[k] = a[prefix + k];
    for (int k = 0; k < mm; k++) b[k] = b[prefix + k];
    VecResize(a, nn);
    VecResize(b, mm);
    n = nn;
    m = mm;
    Vec<int> dp;
    if (!VecResize(dp, (n + 1) * (m + 1))) return;
    for (int i = n - 1; i >= 0; i--) {
        for (int j = m - 1; j >= 0; j--) {
            bool eq = a[i].end - a[i].start == b[j].end - b[j].start &&
                      memcmp(oldText.s + a[i].start, newText.s + b[j].start,
                             (size_t)(a[i].end - a[i].start)) == 0;
            int best = dp[(i + 1) * (m + 1) + j];
            int down = dp[i * (m + 1) + (j + 1)];
            if (down > best) best = down;
            if (eq) {
                int diag = dp[(i + 1) * (m + 1) + (j + 1)] + 1;
                if (diag > best) best = diag;
            }
            dp[i * (m + 1) + j] = best;
        }
    }
    // similar's ratio counts equal tokens, regardless of their byte lengths.
    // 2 * equal / total >= 0.25, expressed without rounding.
    if (8 * ((int64_t)prefix + suffix + dp[0]) < total) return;
    int i = 0;
    int j = 0;
    struct Op {
        int kind;
        int start;
        int end;
    };
    Vec<Op> ops;
    while (i < n && j < m) {
        bool eq = a[i].end - a[i].start == b[j].end - b[j].start &&
                  memcmp(oldText.s + a[i].start, newText.s + b[j].start,
                         (size_t)(a[i].end - a[i].start)) == 0;
        if (eq && dp[i * (m + 1) + j] == dp[(i + 1) * (m + 1) + (j + 1)] + 1) {
            VecAppend(ops, Op{0, a[i].start, a[i].end});
            i++;
            j++;
        } else if (dp[i * (m + 1) + j] == dp[(i + 1) * (m + 1) + j]) {
            VecAppend(ops, Op{1, a[i].start, a[i].end});
            i++;
        } else {
            VecAppend(ops, Op{2, b[j].start, b[j].end});
            j++;
        }
    }
    while (i < n) {
        VecAppend(ops, Op{1, a[i].start, a[i].end});
        i++;
    }
    while (j < m) {
        VecAppend(ops, Op{2, b[j].start, b[j].end});
        j++;
    }
    bool merge = unit == DiffInlineUnit::Word;
    for (int k = 0; k < len(ops); k++) {
        if (ops[k].kind == 1)
            PushRun(oldRuns, ops[k].start, ops[k].end, oldText, merge);
        else if (ops[k].kind == 2)
            PushRun(newRuns, ops[k].start, ops[k].end, newText, merge);
    }
}

struct DiffInlinePair {
    int original;
    int modified;
    Str oldText;
    Str newText;
};

struct DiffInlineInput {
    int lineCounts[2] = {};
    Vec<DiffInlinePair> pairs;
};

static bool ReadDiffInlineInput(const DiffFile& file, int maxLineLength,
                                Arena* snapshot, DiffInlineInput* out) {
    out->lineCounts[0] = len(file.original.lines);
    out->lineCounts[1] = len(file.modified.lines);
    for (const DiffLinePair& pair : file.pairs) {
        if (!pair.changed || pair.original < 0 || pair.modified < 0) continue;
        Str oldText = file.original.lines[pair.original].text;
        Str newText = file.modified.lines[pair.modified].text;
        if (len(oldText) > maxLineLength || len(newText) > maxLineLength)
            continue;
        if (snapshot) {
            int oldLength = len(oldText);
            int newLength = len(newText);
            oldText = StrDup(snapshot, oldText);
            newText = StrDup(snapshot, newText);
            if (len(oldText) != oldLength || len(newText) != newLength)
                return false;
        }
        if (!VecAppend(out->pairs,
                       {pair.original, pair.modified, oldText, newText}))
            return false;
    }
    return true;
}

static void PrepareDiffInline(const DiffInlineInput& input, DiffInlineUnit unit,
                              Vec<DiffRun> (&runs)[2], Vec<int> (&starts)[2]) {
    for (int side = 0; side < 2; side++) {
        VecClear(runs[side]);
        VecClear(starts[side]);
    }
    if (!len(input.pairs)) return;
    int nOld = input.lineCounts[0];
    int nNew = input.lineCounts[1];
    // The first changed pair of each line, so runs go in line order without
    // looking through the pairs for every line.
    Vec<int> pairOfOld;
    Vec<int> pairOfNew;
    if (!VecResize(pairOfOld, nOld) || !VecResize(pairOfNew, nNew)) return;
    if (!VecResize(starts[0], nOld + 1) || !VecResize(starts[1], nNew + 1)) {
        VecClear(starts[0]);
        VecClear(starts[1]);
        return;
    }
    for (int i = 0; i < nOld; i++) pairOfOld[i] = -1;
    for (int i = 0; i < nNew; i++) pairOfNew[i] = -1;
    for (int p = 0; p < len(input.pairs); p++) {
        const DiffInlinePair& pair = input.pairs[p];
        if (pairOfOld[pair.original] < 0) pairOfOld[pair.original] = p;
        if (pairOfNew[pair.modified] < 0) pairOfNew[pair.modified] = p;
    }
    Vec<DiffRun> perOld;
    Vec<DiffRun> perNew;
    for (int which = 0; which < 2; which++) {
        const Vec<int>& pairOf = which == 0 ? pairOfOld : pairOfNew;
        for (int line = 0; line < len(pairOf); line++) {
            starts[which][line] = len(runs[which]);
            if (pairOf[line] < 0) continue;
            const DiffInlinePair& pair = input.pairs[pairOf[line]];
            DiffChangedRuns(pair.oldText, pair.newText, unit, &perOld, &perNew);
            VecAppendVec(runs[which], which == 0 ? perOld : perNew);
        }
        starts[which][len(pairOf)] = len(runs[which]);
    }
}

void DiffFile::PrepareInline(DiffInlineUnit unit, int maxLineLength) {
    DiffInlineInput input;
    if (ReadDiffInlineInput(*this, maxLineLength, nullptr, &input))
        PrepareDiffInline(input, unit, inlineRuns, inlineStarts);
}

int DiffFile::InlineCount(DiffSide side, int line) const {
    int which = side == DiffSide::Original ? 0 : 1;
    if (line < 0 || line + 1 >= len(inlineStarts[which])) return 0;
    return inlineStarts[which][line + 1] - inlineStarts[which][line];
}

const DiffRun* DiffFile::InlineAt(DiffSide side, int line, int* count) const {
    int which = side == DiffSide::Original ? 0 : 1;
    *count = InlineCount(side, line);
    if (*count <= 0) return nullptr;
    return &inlineRuns[which][inlineStarts[which][line]];
}

bool DiffHasLineEndingChange(const DiffFile* file, DiffSide side, int ix,
                             int counterpart) {
    if (!file || counterpart < 0) return false;
    DiffSide other =
        side == DiffSide::Original ? DiffSide::Modified : DiffSide::Original;
    const DiffSourceLine& line = SideOf(file, side)->lines[ix];
    const DiffSourceLine& peer = SideOf(file, other)->lines[counterpart];
    if (len(line.text) != len(peer.text)) return false;
    if (len(line.text) &&
        memcmp(line.text.s, peer.text.s, (size_t)len(line.text)) != 0)
        return false;
    Str a = Slice(SideOf(file, side)->source, line.contentEnd, line.sourceEnd);
    Str b = Slice(SideOf(file, other)->source, peer.contentEnd, peer.sourceEnd);
    if (len(a) != len(b)) return true;
    return len(a) && memcmp(a.s, b.s, (size_t)len(a)) != 0;
}

// ─── Rows ──────────────────────────────────────────────────────────────────

// Which pairs a file shows: each run of changed pairs with `contextLines`
// around it (every pair without context), the ranges the user expanded, and
// a hidden run shorter than `minCollapsedLines` shown after all. With
// `byHunk` a run and its context stay inside the hunk that holds them, and a
// hidden run does not cross a hunk's end; `hunkEnd[i]` is where pair i's
// hunk ends (the file's end when not by hunk).
static void VisiblePairs(const DiffState* state, int fileIx, bool byHunk,
                         Vec<char>* visible, Vec<int>* hunkEnd) {
    const DiffFile* file = state->files[fileIx];
    int n = len(file->pairs);
    VecClear(*visible);
    VecResize(*visible, n);
    Vec<int> hunkStart;
    VecClear(*hunkEnd);
    VecResize(*hunkEnd, n);
    VecResize(hunkStart, n);
    for (int i = 0; i < n; i++) {
        (*hunkEnd)[i] = n;
        hunkStart[i] = 0;
    }
    for (int h = 0; byHunk && h < len(file->hunks); h++) {
        for (int i = file->hunks[h].pairsStart;
             i < file->hunks[h].pairsEnd && i < n; i++) {
            hunkStart[i] = file->hunks[h].pairsStart;
            (*hunkEnd)[i] = file->hunks[h].pairsEnd;
        }
    }
    Vec<int> boundaries;
    VecResize(boundaries, n + 1);
    for (int i = 0; i <= n; i++) boundaries[i] = 0;
    if (!state->hasContext) {
        boundaries[0] += 1;
        boundaries[n] -= 1;
    } else {
        int ix = 0;
        while (ix < n) {
            if (!file->pairs[ix].changed) {
                ix++;
                continue;
            }
            int start = ix;
            while (ix < n && ix < (*hunkEnd)[start] && file->pairs[ix].changed)
                ix++;
            int from = start - state->contextLines;
            if (from < hunkStart[start]) from = hunkStart[start];
            if (from < 0) from = 0;
            int to = ix + state->contextLines;
            if (to > (*hunkEnd)[start]) to = (*hunkEnd)[start];
            boundaries[from] += 1;
            boundaries[to] -= 1;
        }
    }
    for (int i = 0; i < len(state->expanded); i++) {
        if (state->expanded[i].file != fileIx) continue;
        int start = IMin(IMax(state->expanded[i].start, 0), n);
        int end = IMin(IMax(state->expanded[i].end, 0), n);
        if (start < end) {
            boundaries[start] += 1;
            boundaries[end] -= 1;
        }
    }
    int active = 0;
    for (int i = 0; i < n; i++) {
        active += boundaries[i];
        (*visible)[i] = active > 0 ? 1 : 0;
    }
    int ix = 0;
    while (ix < n) {
        if ((*visible)[ix]) {
            ix++;
            continue;
        }
        int start = ix;
        while (ix < n && ix < (*hunkEnd)[start] && !(*visible)[ix]) ix++;
        if (ix - start < state->minCollapsedLines) {
            for (int k = start; k < ix; k++) (*visible)[k] = 1;
        }
    }
}

static void ProjectFile(DiffState* state, int fileIx) {
    const DiffFile* file = state->files[fileIx];
    int n = len(file->pairs);
    Vec<char> visible;
    Vec<int> hunkEnd;
    VisiblePairs(state, fileIx, true, &visible, &hunkEnd);
    int hunkIx = 0;
    int ix2 = 0;
    while (ix2 < n) {
        while (hunkIx < len(file->hunks) && file->hunks[hunkIx]
                                                    .pairsStart == ix2) {
            DiffRow row;
            row.kind = DiffRowKind::Hunk;
            row.file = fileIx;
            row.hunk = hunkIx;
            VecAppend(state->rows, row);
            hunkIx++;
        }
        if (!visible[ix2]) {
            int start = ix2;
            while (ix2 < n && ix2 < hunkEnd[start] && !visible[ix2]) ix2++;
            DiffRow row;
            row.kind = DiffRowKind::Fold;
            row.file = fileIx;
            row.pairsStart = start;
            row.pairsEnd = ix2;
            VecAppend(state->rows, row);
        } else if (state->mode == DiffMode::Unified && file->pairs[ix2]
                                                           .changed) {
            int start = ix2;
            while (ix2 < n && ix2 < hunkEnd[start] && file->pairs[ix2].changed)
                ix2++;
            for (int p = start; p < ix2; p++) {
                if (file->pairs[p].original < 0) continue;
                DiffRow row;
                row.kind = DiffRowKind::Code;
                row.file = fileIx;
                row.original = file->pairs[p].original;
                row.modified = -1;
                row.changed = true;
                VecAppend(state->rows, row);
            }
            for (int p = start; p < ix2; p++) {
                if (file->pairs[p].modified < 0) continue;
                DiffRow row;
                row.kind = DiffRowKind::Code;
                row.file = fileIx;
                row.original = -1;
                row.modified = file->pairs[p].modified;
                row.changed = true;
                VecAppend(state->rows, row);
            }
        } else {
            DiffRow row;
            row.kind = DiffRowKind::Code;
            row.file = fileIx;
            row.original = file->pairs[ix2].original;
            row.modified = file->pairs[ix2].modified;
            row.changed = file->pairs[ix2].changed;
            VecAppend(state->rows, row);
            ix2++;
        }
    }
}

static bool ResolutionOf(const DiffState* state, Str path, int ix,
                         DiffConflictResolution* out) {
    for (int i = 0; i < len(state->resolutions); i++) {
        if (state->resolutions[i].ix == ix &&
            StrEq(state->resolutions[i].path, path)) {
            *out = state->resolutions[i].kind;
            return true;
        }
    }
    return false;
}

static void ProjectConflicts(DiffState* state, int fileIx) {
    const DiffFile* file = state->files[fileIx];
    int n = len(file->pairs);
    Vec<char> visible;
    Vec<int> hunkEnd;
    VisiblePairs(state, fileIx, false, &visible, &hunkEnd);
    auto code = [&](int line, bool changed) {
        DiffRow row;
        row.kind = DiffRowKind::Code;
        row.file = fileIx;
        row.modified = line;
        row.original = -1;
        row.changed = changed;
        VecAppend(state->rows, row);
    };
    int next = 0;
    int ix = 0;
    while (ix < n) {
        if (next < len(file->conflicts) && file->conflicts[next]
                                                   .SourceLinesStart() == ix) {
            const DiffConflict& conflict = file->conflicts[next];
            DiffConflictResolution kind;
            bool resolved = ResolutionOf(state, file->path, next, &kind);
            if (!resolved) {
                DiffConflictPart parts[3] = {DiffConflictPart::Current,
                                             DiffConflictPart::Base,
                                             DiffConflictPart::Incoming};
                for (int p = 0; p < 3; p++) {
                    int start = 0;
                    int end = 0;
                    if (!conflict.Part(parts[p], &start, &end)) continue;
                    DiffRow row;
                    row.kind = DiffRowKind::Conflict;
                    row.file = fileIx;
                    row.conflict = next;
                    row.part = parts[p];
                    VecAppend(state->rows, row);
                    for (int line = start; line < end; line++) code(line, true);
                }
            } else {
                DiffRow row;
                row.kind = DiffRowKind::Conflict;
                row.file = fileIx;
                row.conflict = next;
                row.part = DiffConflictPart::Current;
                VecAppend(state->rows, row);
                if (kind == DiffConflictResolution::Current ||
                    kind == DiffConflictResolution::Both) {
                    for (int line = conflict.currentStart;
                         line < conflict.currentEnd; line++)
                        code(line, false);
                }
                if (kind == DiffConflictResolution::Incoming ||
                    kind == DiffConflictResolution::Both) {
                    for (int line = conflict.incomingStart;
                         line < conflict.incomingEnd; line++)
                        code(line, false);
                }
            }
            ix = conflict.SourceLinesEnd();
            next++;
            continue;
        }
        if (visible[ix]) {
            code(ix, false);
            ix++;
            continue;
        }
        int start = ix;
        while (ix < n && !visible[ix]) ix++;
        DiffRow row;
        row.kind = DiffRowKind::Fold;
        row.file = fileIx;
        row.pairsStart = start;
        row.pairsEnd = ix;
        VecAppend(state->rows, row);
    }
}

static bool Collapsed(const DiffState* state, Str path) {
    for (int i = 0; i < len(state->collapsed); i++) {
        if (StrEq(state->collapsed[i], path)) return true;
    }
    return false;
}

static int SourceRow(const DiffState* state, DiffLinePosition position);

static bool DiffScrollSource(const DiffState* state, int top,
                             DiffLinePosition* out) {
    if (top < 0 || top >= len(state->rows)) return false;
    int fileIx = state->rows[top].file;
    if (fileIx < 0 || fileIx >= len(state->files)) return false;
    const DiffFile* file = state->files[fileIx];
    for (int i = top; i < len(state->rows); i++) {
        const DiffRow& row = state->rows[i];
        if (row.file != fileIx) break;
        int original = row.original;
        int modified = row.modified;
        if (row.kind == DiffRowKind::Fold &&
            row.pairsStart < file->PairCount()) {
            const DiffLinePair& pair = file->Pairs()[row.pairsStart];
            original = pair.original;
            modified = pair.modified;
        }
        DiffSide side = modified >= 0 ? DiffSide::Modified : DiffSide::Original;
        int line = modified >= 0 ? modified : original;
        if (line >= 0) {
            *out = DiffLinePosition::New(file->Path(), side,
                                         file->LineNumber(side, line));
            return true;
        }
    }
    return false;
}

static void Rebuild(DiffState* state, bool reset) {
    VirtualListScrollHandle& scroll = state->listScroll;
    DiffLinePosition kept = {};
    int anchorFile = -1;
    bool hasKept = false;
    int before = len(state->rows);
    if (!reset && before) {
        int top = 0;
        if (scroll.pending) {
            top = scroll.pendingIx;
        } else {
            const float* sizes = len(state->rowHeights) == before
                                     ? state->rowHeights.els
                                     : nullptr;
            float offsetInRow = 0.f;
            VirtualListLogicalFromPixel(sizes, before, 24.f, scroll.offset,
                                        &top, &offsetInRow);
        }
        if (top > 0 && top < before) {
            anchorFile = state->rows[top].file;
            hasKept = DiffScrollSource(state, top, &kept);
        }
    }
    VecClear(state->rows);
    VecClear(state->rowHeights);
    VecClear(state->measuredAnnotations);
    VecClear(state->headerRows);
    VecClear(state->changeRows);
    for (int ix = 0; ix < len(state->files); ix++) {
        DiffRow header;
        header.kind = DiffRowKind::File;
        header.file = ix;
        VecAppend(state->rows, header);
        VecAppend(state->headerRows, len(state->rows) - 1);
        DiffFile* file = state->files[ix];
        if (Collapsed(state, file->path)) continue;
        if (len(file->pairs) == 0) {
            DiffRow row;
            row.kind = DiffRowKind::Notice;
            row.file = ix;
            VecAppend(state->rows, row);
        } else if (file->status == DiffFileStatus::Unchanged) {
            for (int line = 0; line < len(file->pairs); line++) {
                DiffRow row;
                row.kind = DiffRowKind::Code;
                row.file = ix;
                row.modified = line;
                row.original = -1;
                row.changed = false;
                VecAppend(state->rows, row);
            }
        } else if (file->status == DiffFileStatus::Conflicted) {
            ProjectConflicts(state, ix);
        } else {
            ProjectFile(state, ix);
        }
    }
    int origTotal = 0;
    int modTotal = 0;
    for (int i = 0; i < len(state->files); i++) {
        origTotal += state->files[i]->LinesCount(DiffSide::Original);
        modTotal += state->files[i]->LinesCount(DiffSide::Modified);
    }
    VecClear(state->rowOfOriginal);
    VecClear(state->rowOfModified);
    VecClear(state->rowFileBase);
    VecResize(state->rowOfOriginal, origTotal);
    VecResize(state->rowOfModified, modTotal);
    for (int i = 0; i < origTotal; i++) state->rowOfOriginal[i] = -1;
    for (int i = 0; i < modTotal; i++) state->rowOfModified[i] = -1;
    VecResize(state->rowFileBase, len(state->files) * 2);
    int oAt = 0;
    int mAt = 0;
    for (int i = 0; i < len(state->files); i++) {
        state->rowFileBase[i * 2] = oAt;
        state->rowFileBase[i * 2 + 1] = mAt;
        oAt += state->files[i]->LinesCount(DiffSide::Original);
        mAt += state->files[i]->LinesCount(DiffSide::Modified);
    }
    for (int r = 0; r < len(state->rows); r++) {
        const DiffRow& row = state->rows[r];
        auto map = [&](int file, int original, int modified) {
            if (original >= 0)
                state->rowOfOriginal[state->rowFileBase[file * 2] + original] =
                    r;
            if (modified >= 0)
                state->rowOfModified[state->rowFileBase[file * 2 + 1] +
                                     modified] = r;
        };
        if (row.kind == DiffRowKind::Code)
            map(row.file, row.original, row.modified);
        if (row.kind == DiffRowKind::Fold) {
            const DiffFile* file = state->files[row.file];
            for (int p = row.pairsStart;
                 p < row.pairsEnd && p < len(file->pairs); p++)
                map(row.file, file->pairs[p].original, file->pairs[p].modified);
        }
    }
    for (int r = 0; r < len(state->rows); r++) {
        if (state->rows[r].kind == DiffRowKind::Code &&
            state->rows[r].changed &&
            (r == 0 || !state->rows[r - 1].changed ||
             state->rows[r - 1].file != state->rows[r].file))
            VecAppend(state->changeRows, r);
    }
    int next = hasKept ? SourceRow(state, kept) : -1;
    if (next < 0 && anchorFile >= 0 && anchorFile < len(state->headerRows)) {
        next = state->headerRows[anchorFile];
    }
    scroll.itemsCount = len(state->rows);
    state->scrollItem = next >= 0 ? next : 0;
    if (!reset && before && scroll.itemsCount) {
        VirtualListScrollToItemDeferred(&scroll, state->scrollItem,
                                        ScrollStrategy::Start);
    } else {
        scroll.offset = 0.f;
        scroll.pending = false;
    }
}

static int DisplayOff(const DiffFileSide* side, const DiffSourceLine& line,
                      int sourceOff) {
    if (sourceOff < 0) sourceOff = 0;
    if (sourceOff > len(line.text)) sourceOff = len(line.text);
    int lo = 0, hi = line.tabCount;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (side->tabs[line.tabBegin + mid].sourceOffset < sourceOff)
            lo = mid + 1;
        else
            hi = mid;
    }
    int expansion = 0;
    if (lo > 0) {
        const DiffDisplayTab& tab = side->tabs[line.tabBegin + lo - 1];
        expansion = tab.displayOffset + tab.width - tab.sourceOffset - 1;
    }
    return sourceOff + expansion;
}

static Str SyntaxLanguage(const DiffFile* file, const DiffFileSide* side) {
    if (file->hasLanguage) return file->language;
    if (side->hasPath) return DetectedLanguage(side->path);
    return file->Language();
}

static void CollectSyntaxSide(const DiffFile* file, int fileIx, DiffSide side,
                              int maxLen, DiffSyntaxHighlighter fn, void* data,
                              Vec<DiffSyntaxSpan>* out) {
    const DiffFileSide* src =
        side == DiffSide::Original ? &file->original : &file->modified;
    int nLines = len(src->lines);
    if (nLines == 0 || !fn) return;
    Str language = SyntaxLanguage(file, src);
    if (!language.s || len(language) == 0 || StrEq(language, "text")) return;
    int nHunks = len(file->hunks);
    int ranges = nHunks == 0 ? 1 : nHunks;
    for (int h = 0; h < ranges; h++) {
        int begin = 0;
        int end = nLines;
        if (nHunks > 0) {
            begin = side == DiffSide::Original ? file->hunks[h].originalStart
                                               : file->hunks[h].modifiedStart;
            end = side == DiffSide::Original ? file->hunks[h].originalEnd
                                             : file->hunks[h].modifiedEnd;
        }
        if (begin < 0) begin = 0;
        if (end > nLines) end = nLines;
        if (begin >= end) continue;
        int base = src->lines[begin].sourceStart;
        int last = src->lines[end - 1].sourceEnd;
        Str text = Slice(src->source, base, last);
        Vec<DiffSyntaxStyle> hits;
        fn(data, language, text, &hits);
        for (int k = 0; k < len(hits); k++) {
            int hs = hits[k].start;
            int he = hits[k].end;
            if (hs < 0 || he < hs || he > len(text)) continue;
            if (!Utf8IsCharBoundary(text, hs) || !Utf8IsCharBoundary(text, he))
                continue;
            int absStart = hs + base;
            int absEnd = he + base;
            int count = end - begin;
            int lo = 0;
            int hi = count;
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (src->lines[begin + mid].sourceEnd <= absStart)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            for (int offset = lo; begin + offset < end; offset++) {
                const DiffSourceLine& line = src->lines[begin + offset];
                if (line.sourceStart >= absEnd) break;
                if (line.contentEnd - line.sourceStart > maxLen) continue;
                int start =
                    absStart > line.sourceStart ? absStart : line.sourceStart;
                int stop = absEnd < line.contentEnd ? absEnd : line.contentEnd;
                if (start >= stop) continue;
                DiffSyntaxSpan span;
                static_cast<DiffSyntaxStyle&>(span) = hits[k];
                span.file = fileIx;
                span.side = (uint8_t)side;
                span.line = begin + offset;
                span.start = DisplayOff(src, line, start - line.sourceStart);
                span.end = DisplayOff(src, line, stop - line.sourceStart);
                VecAppend(*out, span);
            }
        }
    }
}

void DiffState::EnsureSyntax() {
    if (syntaxPrepared) return;
    VecClear(syntaxSpans);
    syntaxPrepared = true;
    if (!syntaxFn) return;
    // Rust prepares this on the background executor. The inline runs above
    // are already computed on this thread, and so is the highlighter.
    for (int i = 0; i < len(files); i++) {
        CollectSyntaxSide(files[i], i, DiffSide::Original, syntaxMaxLineLength,
                          syntaxFn, syntaxData, &syntaxSpans);
        CollectSyntaxSide(files[i], i, DiffSide::Modified, syntaxMaxLineLength,
                          syntaxFn, syntaxData, &syntaxSpans);
    }
}

int DiffState::SyntaxStyleCount(int file, DiffSide side, int line) const {
    int n = 0;
    for (int i = 0; i < len(syntaxSpans); i++) {
        const DiffSyntaxSpan& span = syntaxSpans[i];
        if (span.file == file && span.side == (uint8_t)side &&
            span.line == line)
            n++;
    }
    return n;
}

DiffSyntaxStyle DiffState::SyntaxStyleAt(int file, DiffSide side, int line,
                                         int ix) const {
    int n = 0;
    for (int i = 0; i < len(syntaxSpans); i++) {
        const DiffSyntaxSpan& span = syntaxSpans[i];
        if (span.file != file || span.side != (uint8_t)side ||
            span.line != line)
            continue;
        if (n == ix) return span;
        n++;
    }
    return {};
}

struct DiffInlineJob {
    DiffState* owner = nullptr; // Main thread only.
    TaskId task = 0;
    DiffInlineUnit unit = DiffInlineUnit::Word;
    Arena* snapshot = ArenaNew();
    DiffInlineInput input;
    Vec<DiffRun> runs[2];
    Vec<int> starts[2];
    Mutex lifetime;
    // One reference for the owner and one for the executor.
    int refs = 2;
    bool discarded = false;

    ~DiffInlineJob() { ArenaDelete(snapshot); }
};

static void ReleaseDiffInlineJob(DiffInlineJob* job) {
    job->lifetime.Lock();
    bool last = --job->refs == 0;
    job->lifetime.Unlock();
    if (last) delete job;
}

static void CancelDiffInlineJob(DiffState* state) {
    auto* job = state->inlineJob;
    if (!job) return;
    state->inlineJob = nullptr;
    job->owner = nullptr;
    ExecCancel(job->task);
    ReleaseDiffInlineJob(job);
}

static void InvalidateDiffInline(DiffState* state) {
    CancelDiffInlineJob(state);
    state->inlinePreparedFiles = 0;
    for (auto* file : state->files) {
        for (int side = 0; side < 2; side++) {
            VecClear(file->inlineRuns[side]);
            VecClear(file->inlineStarts[side]);
        }
    }
}

static void DiffInlineWork(DiffInlineJob* job) {
    PrepareDiffInline(job->input, job->unit, job->runs, job->starts);
}

static void DiffInlineDiscard(DiffInlineJob* job) {
    job->lifetime.Lock();
    job->discarded = true;
    job->lifetime.Unlock();
    ReleaseDiffInlineJob(job);
}

static void DiffInlineLanded(DiffInlineJob* job) {
    if (auto* state = job->owner) {
        auto* file = state->files[state->inlinePreparedFiles++];
        for (int side = 0; side < 2; side++) {
            std::swap(*VecNT(file->inlineRuns[side]), *VecNT(job->runs[side]));
            std::swap(*VecNT(file->inlineStarts[side]),
                      *VecNT(job->starts[side]));
        }
        state->inlineJob = nullptr;
        ReleaseDiffInlineJob(job);
        NotifyEntity(state->app, state->self.id, nullptr);
    }
    ReleaseDiffInlineJob(job);
}

void DiffState::EnsureInlinePresentation(bool background) {
    if (!inlineOn) return;
    if (inlineJob && background) {
        inlineJob->lifetime.Lock();
        bool discarded = inlineJob->discarded;
        inlineJob->lifetime.Unlock();
        if (!discarded) return;
    }
    CancelDiffInlineJob(this);
    // Start one file at a time on render. Each completion repaints the
    // landed file and lets that frame start the next one.
    while (inlinePreparedFiles < len(files)) {
        auto* file = files[inlinePreparedFiles];
        if (background && self.IsValid() && ExecOnMainThread()) {
            auto* job = new DiffInlineJob;
            job->owner = this;
            job->unit = inlineUnit;
            if (ReadDiffInlineInput(*file, inlineMaxLineLength, job->snapshot,
                                    &job->input)) {
                if (!len(job->input.pairs)) {
                    delete job;
                    inlinePreparedFiles++;
                    continue;
                }
                job->task = ExecSpawn(MkFunc0(&DiffInlineWork, job),
                                      MkFunc0(&DiffInlineLanded, job),
                                      MkFunc0(&DiffInlineDiscard, job));
                if (job->task) {
                    inlineJob = job;
                    return;
                }
            }
            delete job;
        }
        file->PrepareInline(inlineUnit, inlineMaxLineLength);
        inlinePreparedFiles++;
    }
}

void DiffState::EnsurePresentation() {
    EnsureSyntax();
    EnsureInlinePresentation(false);
}

void DiffState::Bind(App* appIn, Entity<DiffState> handle) {
    app = appIn;
    self = handle;
    focus = FocusHandleNew(appIn);
}

static void Emit(DiffState* state, Ctx* cx, DiffEvent ev) {
    VecAppend(state->emitted, ev);
    if (cx && state->self.IsValid())
        EntityEmit(cx->app, cx->win, state->self, &ev);
    if (cx) Notify(cx);
}

static int FileIx(const DiffState* state, Str path) {
    for (int i = 0; i < len(state->files); i++) {
        if (StrEq(state->files[i]->path, path)) return i;
    }
    return -1;
}

void DiffState::SetFiles(DiffFile** next, int count, Ctx* cx) {
    CancelDiffInlineJob(this);
    ClearDiffCodeRuns(this);
    for (int i = 0; i < len(files); i++) delete files[i];
    VecClear(files);
    for (int i = 0; i < count; i++) VecAppend(files, next[i]);
    horizontalCodeFont = 0;
    // The last hover borrowed its path and text from the files just freed.
    // Rust compares an owned copy; here the next report is heard afresh.
    if (textHoverHasHit) textHoverHeard = false;
    textHoverHasHit = false;
    textHover = {};
    for (int i = len(collapsed) - 1; i >= 0; i--) {
        if (FileIx(this, collapsed[i]) < 0) {
            StrFree(collapsed[i]);
            VecRemoveAt(collapsed, i);
        }
    }
    for (int i = 0; i < len(resolutions); i++) StrFree(resolutions[i].path);
    VecClear(resolutions);
    VecClear(expanded);
    hasSelection = false;
    hasAnchor = false;
    hasCursor = false;
    selecting = false;
    textSelection = {};
    syntaxPrepared = false;
    InvalidateDiffInline(this);
    Rebuild(this, true);
    if (cx && !cx->win) EnsurePresentation();
    if (cx) Notify(cx);
}

Vec<DiffFile*> DiffState::TakeFiles() {
    Vec<DiffFile*> prior;
    if (!VecAppendVec(prior, files)) return prior;
    VecClear(files);
    SetFiles(nullptr, 0, nullptr);
    return prior;
}

DiffState* DiffState::WithMode(DiffMode next) {
    mode = next;
    Rebuild(this, true);
    return this;
}

DiffState* DiffState::WithContextLines(bool has, int lines) {
    hasContext = has;
    contextLines = lines;
    Rebuild(this, true);
    return this;
}

DiffState* DiffState::WithExpansionLines(int lines) {
    expansionLines = lines < 1 ? 1 : lines;
    return this;
}

DiffState* DiffState::WithMinCollapsedLines(int lines) {
    minCollapsedLines = lines < 1 ? 1 : lines;
    Rebuild(this, true);
    return this;
}

DiffState* DiffState::WithInlineMaxLineLength(int length) {
    SetInlineMaxLineLength(length, nullptr);
    return this;
}

DiffState* DiffState::WithSyntaxMaxLineLength(int length) {
    syntaxMaxLineLength = length < 0 ? 0 : length;
    syntaxPrepared = false;
    return this;
}

DiffState* DiffState::WithSyntaxHighlighter(DiffSyntaxHighlighter fn,
                                            void* data) {
    syntaxFn = fn;
    syntaxData = data;
    syntaxPrepared = false;
    return this;
}

void DiffState::SetSyntaxHighlighter(DiffSyntaxHighlighter fn, void* data,
                                     Ctx* cx) {
    syntaxFn = fn;
    syntaxData = data;
    syntaxPrepared = false;
    EnsureSyntax();
    if (cx) Notify(cx);
}

void DiffState::ClearSyntaxHighlighter(Ctx* cx) {
    syntaxFn = nullptr;
    syntaxData = nullptr;
    syntaxPrepared = false;
    EnsureSyntax();
    if (cx) Notify(cx);
}

DiffFile** DiffState::Files(int* count) const {
    if (count) *count = len(files);
    return files.els;
}

DiffState* DiffState::WithInlineUnit(bool on, DiffInlineUnit unit) {
    if (!on) inlineUnit = unit;
    SetInlineUnit(on, unit, nullptr);
    return this;
}

bool DiffState::IsFileCollapsed(Str path) const {
    return Collapsed(this, path);
}

void DiffState::SetMode(DiffMode next, Ctx* cx) {
    if (mode == next) return;
    mode = next;
    Rebuild(this, false);
    horizontalScrollX = 0.f;
    if (cx) Notify(cx);
}

void DiffState::SetContextLines(bool has, int lines, Ctx* cx) {
    if (hasContext == has && contextLines == lines) return;
    hasContext = has;
    contextLines = lines;
    VecClear(expanded);
    Rebuild(this, false);
    if (cx) Notify(cx);
}

void DiffState::SetExpansionLines(int lines, Ctx* cx) {
    lines = lines < 1 ? 1 : lines;
    if (expansionLines == lines) return;
    expansionLines = lines;
    if (cx) Notify(cx);
}

void DiffState::SetMinCollapsedLines(int lines, Ctx* cx) {
    lines = lines < 1 ? 1 : lines;
    if (minCollapsedLines == lines) return;
    minCollapsedLines = lines;
    Rebuild(this, false);
    if (cx) Notify(cx);
}

void DiffState::SetInlineUnit(bool on, DiffInlineUnit unit, Ctx* cx) {
    if (inlineOn == on && (!on || inlineUnit == unit)) return;
    inlineOn = on;
    if (on) inlineUnit = unit;
    InvalidateDiffInline(this);
    if (cx && !cx->win) EnsureInlinePresentation(false);
    if (cx) Notify(cx);
}

void DiffState::SetInlineMaxLineLength(int length, Ctx* cx) {
    if (length < 0) length = 0;
    if (inlineMaxLineLength == length) return;
    inlineMaxLineLength = length;
    InvalidateDiffInline(this);
    if (cx && !cx->win) EnsureInlinePresentation(false);
    if (cx) Notify(cx);
}

void DiffState::SetSyntaxMaxLineLength(int length, Ctx* cx) {
    if (length < 0) length = 0;
    if (syntaxMaxLineLength == length) return;
    syntaxMaxLineLength = length;
    // The scanner reads this limit while painting. An application
    // highlighter has a prepared cache, so that one is dropped.
    syntaxPrepared = false;
    if (cx) Notify(cx);
}

void DiffState::ExpandUnchanged(Ctx* cx) {
    VecClear(expanded);
    for (int i = 0; i < len(files); i++) {
        DiffFoldRange range;
        range.file = i;
        range.start = 0;
        range.end = len(files[i]->pairs);
        VecAppend(expanded, range);
    }
    Rebuild(this, false);
    if (cx) Notify(cx);
}

void DiffState::CollapseUnchanged(Ctx* cx) {
    VecClear(expanded);
    Rebuild(this, false);
    if (cx) Notify(cx);
}

static bool UpdateFileCollapsed(DiffState* state, Str path, bool collapse) {
    auto& collapsed = state->collapsed;
    for (int i = 0; i < len(collapsed); i++) {
        if (!StrEq(collapsed[i], path)) continue;
        if (collapse) return false;
        StrFree(collapsed[i]);
        VecRemoveAt(collapsed, i);
        return true;
    }
    if (!collapse) return false;
    VecAppend(collapsed, StrDup(path));
    return true;
}

void DiffState::SetFilesCollapsed(const Str* paths, int count, bool collapse,
                                  Ctx* cx) {
    bool changed = false;
    for (int i = 0; i < count; i++) {
        int file = FileIx(this, paths[i]);
        if (file >= 0) {
            changed |= UpdateFileCollapsed(this, files[file]->path, collapse);
        }
    }
    if (changed) {
        Rebuild(this, false);
        if (cx) Notify(cx);
    }
}

void DiffState::SetAllFilesCollapsed(bool collapse, Ctx* cx) {
    bool changed = false;
    if (collapse) {
        for (int i = 0; i < len(files); i++) {
            changed |= UpdateFileCollapsed(this, files[i]->path, true);
        }
    } else if (len(collapsed)) {
        for (int i = 0; i < len(collapsed); i++) StrFree(collapsed[i]);
        VecClear(collapsed);
        changed = true;
    }
    if (changed) {
        Rebuild(this, false);
        if (cx) Notify(cx);
    }
}

void DiffState::SetFileCollapsed(Str path, bool collapse, Ctx* cx) {
    SetFilesCollapsed(&path, 1, collapse, cx);
}

bool DiffState::ConflictResolution(Str path, int ix,
                                   DiffConflictResolution* out) const {
    int file = FileIx(this, path);
    if (file < 0) return false;
    return ResolutionOf(this, files[file]->path, ix, out);
}

void DiffState::ResolveConflict(Str path, int ix, bool has,
                                DiffConflictResolution kind, Ctx* cx) {
    int file = FileIx(this, path);
    if (file < 0 || ix < 0 || ix >= len(files[file]->conflicts)) return;
    DiffConflictResolution prev;
    bool had = ResolutionOf(this, files[file]->path, ix, &prev);
    if (had == has && (!has || prev == kind)) return;
    for (int i = 0; i < len(resolutions); i++) {
        if (resolutions[i].ix == ix &&
            StrEq(resolutions[i].path, files[file]->path)) {
            StrFree(resolutions[i].path);
            VecRemoveAt(resolutions, i);
            break;
        }
    }
    if (has) {
        DiffResolution res;
        res.path = StrDup(files[file]->path);
        res.ix = ix;
        res.kind = kind;
        VecAppend(resolutions, res);
    }
    Rebuild(this, false);
    if (cx) Notify(cx);
}

bool DiffState::ResolvedText(Arena* a, Str path, Str* out) const {
    int fileIx = FileIx(this, path);
    if (fileIx < 0) return false;
    const DiffFile* file = files[fileIx];
    if (file->status != DiffFileStatus::Conflicted) return false;
    StrBuilder text;
    int ix = 0;
    const Vec<DiffSourceLine>& lines = file->modified.lines;
    for (int c = 0; c < len(file->conflicts); c++) {
        DiffConflictResolution kind;
        if (!ResolutionOf(this, file->path, c, &kind)) return false;
        const DiffConflict& conflict = file->conflicts[c];
        for (int line = ix;
             line < conflict.SourceLinesStart() && line < len(lines); line++) {
            text.Append(Slice(file->modified.source, lines[line].sourceStart,
                              lines[line].sourceEnd));
        }
        if (kind == DiffConflictResolution::Current ||
            kind == DiffConflictResolution::Both) {
            for (int line = conflict.currentStart; line < conflict.currentEnd;
                 line++)
                text.Append(Slice(file->modified.source,
                                  lines[line].sourceStart,
                                  lines[line].sourceEnd));
        }
        if (kind == DiffConflictResolution::Incoming ||
            kind == DiffConflictResolution::Both) {
            for (int line = conflict.incomingStart; line < conflict.incomingEnd;
                 line++)
                text.Append(Slice(file->modified.source,
                                  lines[line].sourceStart,
                                  lines[line].sourceEnd));
        }
        ix = conflict.SourceLinesEnd();
    }
    for (int line = ix; line < len(lines); line++)
        text.Append(Slice(file->modified.source, lines[line].sourceStart,
                          lines[line].sourceEnd));
    *out = Own(a, Str(text.els, text.len));
    return true;
}

static bool ClipRange(const DiffState* state, DiffLineRange in,
                      DiffLineRange* out) {
    int fileIx = FileIx(state, in.path);
    if (fileIx < 0) return false;
    const DiffFile* file = state->files[fileIx];
    auto firstAt = [&](DiffSide side, int line) -> int {
        const Vec<DiffSourceLine>& lines = SideOf(file, side)->lines;
        int lo = LinesBefore(lines, line, false);
        if (lo >= len(lines)) return -1;
        return lines[lo].lineNumber;
    };
    auto lastAt = [&](DiffSide side, int line) -> int {
        const Vec<DiffSourceLine>& lines = SideOf(file, side)->lines;
        int lo = LinesBefore(lines, line, true);
        if (lo == 0) return -1;
        return lines[lo - 1].lineNumber;
    };
    if (in.IsSingleSide()) {
        int start = IMin(in.start, in.end);
        int end = IMax(in.start, in.end);
        start = firstAt(in.side, start);
        end = lastAt(in.side, end);
        if (start < 0 || end < 0 || start > end) return false;
        *out = DiffLineRange::New(file->path, in.side, start, end);
        return true;
    }
    int start = firstAt(in.side, in.start);
    if (start < 0) start = lastAt(in.side, in.start);
    int end = lastAt(in.endSide, in.end);
    if (end < 0) end = firstAt(in.endSide, in.end);
    if (start < 0 || end < 0) return false;
    *out = DiffLineRange::New(file->path, in.side, start, end)
               .WithEndSide(in.endSide);
    return true;
}

void DiffState::SetSelectedLines(bool has, DiffLineRange range, Ctx* cx) {
    if (textSelection.file >= 0) {
        textSelection = {};
        if (cx) Notify(cx);
    }
    DiffLineRange clipped;
    bool ok = has && ClipRange(this, range, &clipped);
    if (!ok) {
        if (!hasSelection) return;
        hasSelection = false;
        hasAnchor = false;
        hasCursor = false;
        if (cx) Notify(cx);
        return;
    }
    if (hasSelection && selected.start == clipped.start &&
        selected.end == clipped.end && selected.side == clipped.side &&
        selected.endSide == clipped.endSide &&
        StrEq(selected.path, clipped.path))
        return;
    hasSelection = true;
    selected = clipped;
    anchor = DiffLinePosition::New(clipped.path, clipped.side, clipped.start);
    cursor = DiffLinePosition::New(clipped.path, clipped.endSide, clipped.end);
    hasAnchor = true;
    hasCursor = true;
    if (cx) Notify(cx);
}

Str DiffState::SelectedText(Arena* a) const {
    if (textSelection.hitText && textSelection.file >= 0 &&
        textSelection.file < len(files) &&
        textSelection.anchor != textSelection.cursor) {
        Str source = files[textSelection.file]->Source(textSelection.side);
        return Own(
            a, Slice(source, IMin(textSelection.anchor, textSelection.cursor),
                     IMax(textSelection.anchor, textSelection.cursor)));
    }
    if (!hasSelection) return {};
    int fileIx = FileIx(this, selected.path);
    if (fileIx < 0) return {};
    const DiffFile* file = files[fileIx];
    if (selected.IsSingleSide()) {
        int start = IMin(selected.start, selected.end);
        int end = IMax(selected.start, selected.end);
        return file->TextForLines(a, selected.side, start, end);
    }
    int startIx = file->LineIndex(selected.side, selected.start);
    int endIx = file->LineIndex(selected.endSide, selected.end);
    if (startIx < 0 || endIx < 0) return {};
    int start = file->PatchIx(selected.side, startIx);
    int end = file->PatchIx(selected.endSide, endIx);
    int lo = IMin(start, end);
    int hi = IMax(start, end);
    StrBuilder text;
    for (int i = lo; i <= hi && i < len(file->patchLines); i++) {
        int sideIx = file->patchLines[i].Line(DiffSide::Modified);
        DiffSide side = DiffSide::Modified;
        if (sideIx < 0) {
            side = DiffSide::Original;
            sideIx = file->patchLines[i].Line(DiffSide::Original);
        }
        if (sideIx < 0) continue;
        if (len(text) && text.els[len(text) - 1] != '\n') text.AppendChar('\n');
        const DiffSourceLine& line = SideOf(file, side)->lines[sideIx];
        text.Append(Slice(SideOf(file, side)->source, line.sourceStart,
                          line.sourceEnd));
    }
    return Own(a, Str(text.els, len(text)));
}

static int SourceRow(const DiffState* state, DiffLinePosition position) {
    int file = FileIx(state, position.path);
    if (file < 0) return -1;
    int ix = state->files[file]->LineIndex(position.side, position.line);
    if (ix < 0) return -1;
    if (position.side == DiffSide::Original)
        return state->rowOfOriginal[state->rowFileBase[file * 2] + ix];
    return state->rowOfModified[state->rowFileBase[file * 2 + 1] + ix];
}

bool DiffState::HasLine(DiffLinePosition position) const {
    int file = FileIx(this, position.path);
    return file >= 0 && files[file]
                                ->LineIndex(position.side, position.line) >= 0;
}

void DiffState::ScrollToLine(DiffLinePosition position, Ctx* cx) {
    if (FileIx(this, position.path) < 0) return;
    if (files[FileIx(this, position.path)]
            ->LineIndex(position.side, position.line) < 0)
        return;
    if (UpdateFileCollapsed(this, position.path, false)) {
        Rebuild(this, false);
    }
    int row = SourceRow(this, position);
    if (row >= 0 && rows[row].kind == DiffRowKind::Fold) {
        DiffFoldRange range;
        range.file = rows[row].file;
        range.start = rows[row].pairsStart;
        range.end = rows[row].pairsEnd;
        VecAppend(expanded, range);
        Rebuild(this, false);
        row = SourceRow(this, position);
    }
    if (row >= 0) {
        scrollItem = row;
        VirtualListScrollToItemDeferred(&listScroll, row, ScrollStrategy::Top);
        if (cx) Notify(cx);
    }
}

void DiffState::ScrollToFile(Str path, Ctx* cx) {
    int file = FileIx(this, path);
    if (file < 0 || file >= len(headerRows)) return;
    scrollItem = headerRows[file];
    VirtualListScrollToItemDeferred(&listScroll, scrollItem,
                                    ScrollStrategy::Top);
    if (cx) Notify(cx);
}

static int ScrollTop(const DiffState* self) {
    if (len(self->rowHeights) == self->listScroll.itemsCount &&
        self->listScroll.itemsCount > 0) {
        int top = 0;
        float into = 0.f;
        VirtualListLogicalFromPixel(self->rowHeights.els, len(self->rowHeights),
                                    24.f, self->listScroll.offset, &top, &into);
        return top;
    }
    if (self->listScroll.itemsCount > 0 && self->listScroll.contentSize > 0) {
        float row = self->listScroll.contentSize / (float)self->listScroll
                                                       .itemsCount;
        if (row > 0) {
            int top = (int)(self->listScroll.offset / row);
            if (top < 0) top = 0;
            if (top >= self->listScroll.itemsCount)
                top = self->listScroll.itemsCount - 1;
            return top;
        }
    }
    return self->scrollItem;
}

// The last row's bottom is inside the viewport. bounds_for_item is none
// until that row has been laid out; content size and the offset answer
// the same question once a layout has run, including after a jump to the
// end that never measured the rows above.
static bool ScrolledToEnd(const DiffState* self) {
    const VirtualListScrollHandle& h = self->listScroll;
    if (h.itemsCount <= 0 || h.viewport <= 0 || h.contentSize <= 0) {
        return false;
    }
    return h.offset + h.viewport >= h.contentSize - 0.5f;
}

static void MoveChange(DiffState* self, bool forward, Ctx* cx) {
    int top = ScrollTop(self);
    int n = len(self->changeRows);
    if (n == 0) return;
    int target = -1;
    if (forward) {
        // At the end the list clamps, so a later change below the top never
        // becomes the top and the key would repeat it. Wrap to the first.
        int i = 0;
        while (i < n && self->changeRows[i] <= top) i++;
        bool atEnd = ScrolledToEnd(self);
        target = i < n && !atEnd ? self->changeRows[i] : self->changeRows[0];
    } else {
        int i = 0;
        while (i < n && self->changeRows[i] < top) i++;
        target = i > 0 ? self->changeRows[i - 1] : self->changeRows[n - 1];
    }
    self->scrollItem = target;
    // Top wouldn't move a row already on screen, and the search above starts
    // from the top row, so the key would stay on the same change.
    VirtualListScrollToItemDeferred(&self->listScroll, target,
                                    ScrollStrategy::Start);
    if (cx) Notify(cx);
}

void DiffState::NextChange(Ctx* cx) {
    MoveChange(this, true, cx);
}

void DiffState::PreviousChange(Ctx* cx) {
    MoveChange(this, false, cx);
}

void DiffState::ExpandFold(int file, int start, int end, DiffFoldExpansion how,
                           Ctx* cx) {
    if (file < 0 || file >= len(files)) return;
    int lines = expansionLines;
    int rStart = start;
    int rEnd = end;
    if (how == DiffFoldExpansion::Up) rStart = IMax(end - lines, start);
    if (how == DiffFoldExpansion::Down) rEnd = IMin(start + lines, end);
    DiffFoldRange range;
    range.file = file;
    range.start = rStart;
    range.end = rEnd;
    VecAppend(expanded, range);
    Rebuild(this, false);
    if (cx) Notify(cx);
}

void DiffState::ChooseConflict(int file, int ix, bool has,
                               DiffConflictResolution kind, Ctx* cx) {
    if (file < 0 || file >= len(files)) return;
    ResolveConflict(files[file]->path, ix, has, kind, cx);
    DiffEvent ev;
    ev.kind = DiffEventKind::ConflictResolved;
    ev.path = files[file]->path;
    ev.conflict = ix;
    Emit(this, cx, ev);
}

void DiffState::ToggleFileCollapsed(int file, Ctx* cx) {
    if (file < 0 || file >= len(files)) return;
    bool collapse = !Collapsed(this, files[file]->path);
    SetFileCollapsed(files[file]->path, collapse, cx);
    DiffEvent ev;
    ev.kind =
        collapse ? DiffEventKind::FileCollapsed : DiffEventKind::FileExpanded;
    ev.path = files[file]->path;
    Emit(this, cx, ev);
}

static bool RangeEq(const DiffLineRange& a, const DiffLineRange& b) {
    return a.start == b.start && a.end == b.end && a.side == b.side &&
           a.endSide == b.endSide && StrEq(a.path, b.path);
}

static DiffLineRange Between(DiffLinePosition anchorPos,
                             DiffLinePosition cursorPos) {
    if (anchorPos.side == cursorPos.side) {
        return DiffLineRange::New(cursorPos.path, cursorPos.side,
                                  IMin(anchorPos.line, cursorPos.line),
                                  IMax(anchorPos.line, cursorPos.line));
    }
    return DiffLineRange::New(cursorPos.path, anchorPos.side, anchorPos.line,
                              cursorPos.line)
        .WithEndSide(cursorPos.side);
}

static bool CanExtend(const DiffState* state, DiffLinePosition anchorPos,
                      DiffLinePosition pos) {
    return StrEq(anchorPos.path, pos.path) &&
           (anchorPos.side == pos.side || state->mode == DiffMode::Unified);
}

void DiffState::BeginLineSelection(DiffLinePosition position, bool extend,
                                   Ctx* cx) {
    int file = FileIx(this, position.path);
    if (file < 0 || files[file]->LineIndex(position.side, position.line) < 0)
        return;
    textSelection = {};
    DiffLinePosition use = position;
    if (extend && hasAnchor && CanExtend(this, anchor, position)) use = anchor;
    selecting = true;
    DiffLineRange started = Between(use, position);
    DiffEvent ev;
    ev.kind = DiffEventKind::SelectionStarted;
    ev.hasRange = true;
    ev.path = started.path;
    ev.side = started.side;
    ev.endSide = started.endSide;
    ev.start = started.start;
    ev.end = started.end;
    Emit(this, cx, ev);
    bool changed = !hasSelection || !RangeEq(selected, started);
    hasSelection = true;
    selected = started;
    anchor = use;
    cursor = position;
    hasAnchor = true;
    hasCursor = true;
    if (changed) {
        DiffEvent changedEv = ev;
        changedEv.kind = DiffEventKind::SelectionChanged;
        Emit(this, cx, changedEv);
    } else if (cx) {
        Notify(cx);
    }
}

void DiffState::DragLineSelection(DiffLinePosition position, Ctx* cx) {
    if (!selecting || !hasAnchor) return;
    if (!CanExtend(this, anchor, position)) return;
    if (hasCursor && cursor.line == position.line &&
        cursor.side == position.side && StrEq(cursor.path, position.path))
        return;
    int file = FileIx(this, position.path);
    if (file < 0 || files[file]->LineIndex(position.side, position.line) < 0)
        return;
    DiffLineRange range = Between(anchor, position);
    bool changed = !hasSelection || !RangeEq(selected, range);
    hasSelection = true;
    selected = range;
    cursor = position;
    hasCursor = true;
    if (changed) {
        DiffEvent ev;
        ev.kind = DiffEventKind::SelectionChanged;
        ev.hasRange = true;
        ev.path = range.path;
        ev.side = range.side;
        ev.endSide = range.endSide;
        ev.start = range.start;
        ev.end = range.end;
        Emit(this, cx, ev);
    }
}

void DiffState::EndLineSelection(Ctx* cx) {
    if (!selecting) return;
    selecting = false;
    if (!hasSelection) return;
    DiffEvent ev;
    ev.kind = DiffEventKind::SelectionEnded;
    ev.hasRange = true;
    ev.path = selected.path;
    ev.side = selected.side;
    ev.endSide = selected.endSide;
    ev.start = selected.start;
    ev.end = selected.end;
    Emit(this, cx, ev);
}

El* DiffState::Render(DiffState*, Ctx* cx) {
    return Div(cx->a);
}

// ─── Paint ─────────────────────────────────────────────────────────────────

static Rgba Tint(Rgba c, uint8_t a) {
    c.a = a;
    return c;
}

static TempStr TrCountTemp(const char* key, int count) {
    TrArg args[] = {{"count", fmt("%d", count)}};
    return TrFmtTemp(key, args, 1);
}

static void OnToggleFile(DiffState* self, Ctx* cx, const ClickEvent*,
                         int64_t file) {
    self->ToggleFileCollapsed((int)file, cx);
}

static void OnExpand(DiffState* self, Ctx* cx, const ClickEvent*,
                     int64_t packed) {
    int file = (int)(packed >> 32);
    int which = (int)((packed >> 24) & 0xff);
    int start = (int)(packed & 0xffffff);
    // The end is recovered from the row. A packed end does not fit; the
    // handler finds the fold that starts here.
    int end = start;
    for (int i = 0; i < len(self->rows); i++) {
        if (self->rows[i].kind == DiffRowKind::Fold &&
            self->rows[i].file == file && self->rows[i].pairsStart == start) {
            end = self->rows[i].pairsEnd;
            break;
        }
    }
    DiffFoldExpansion how = DiffFoldExpansion::All;
    if (which == 1) how = DiffFoldExpansion::Down;
    if (which == 2) how = DiffFoldExpansion::Up;
    self->ExpandFold(file, start, end, how, cx);
}

static void OnChoose(DiffState* self, Ctx* cx, const ClickEvent*,
                     int64_t packed) {
    int file = (int)(packed >> 16);
    int ix = (int)((packed >> 8) & 0xff);
    int which = (int)(packed & 0xff);
    if (which == 0)
        self->ChooseConflict(file, ix, false, DiffConflictResolution::Current,
                             cx);
    else if (which == 1)
        self->ChooseConflict(file, ix, true, DiffConflictResolution::Current,
                             cx);
    else if (which == 2)
        self->ChooseConflict(file, ix, true, DiffConflictResolution::Incoming,
                             cx);
    else
        self->ChooseConflict(file, ix, true, DiffConflictResolution::Both, cx);
}

static int CompareDiffPositions(const DiffLinePosition& a,
                                const DiffLinePosition& b) {
    int path = StrCmp(a.path, b.path);
    if (path != 0) return path;
    if (a.side != b.side) return (int)a.side - (int)b.side;
    return (a.line > b.line) - (a.line < b.line);
}

struct DiffHighlightRuns {
    TextSpan* washes = nullptr;
    TextSpan* colors = nullptr;
    // UnderlineStyle runs in supply order. Alpha 0 is the text's own colour,
    // Rust's `color: None`, resolved per text run.
    TextSpan* underlines = nullptr;
    int washCount = 0;
    int colorCount = 0;
    int underlineCount = 0;
};

static int InsertDiffHighlight(TextSpan* runs, int count, const TextSpan& span,
                               int cap, TextSpan* scratch) {
    // Search matches usually arrive in display order without overlapping.
    if (count == 0 || runs[count - 1].hi <= span.lo) {
        runs[count++] = span;
        return count;
    }
    return InputComposeSpans(runs, count, &span, 1, cap, scratch);
}

static DiffHighlightRuns BuildDiffHighlightRuns(Ctx* cx, const Diff* diff,
                                                const DiffFile* file,
                                                DiffSide side, int lineIx,
                                                bool showInline) {
    DiffHighlightRuns runs;
    int inlineCount = 0;
    const DiffRun* changes =
        showInline ? file->InlineAt(side, lineIx, &inlineCount) : nullptr;
    if (!diff->spanHighlightCount && !inlineCount) return runs;
    const DiffFileSide* source = SideOf(file, side);
    const DiffSourceLine& line = source->lines[lineIx];
    DiffLinePosition position =
        DiffLinePosition::New(file->path, side, line.lineNumber);
    int lo = 0, hi = diff->spanHighlightCount;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (CompareDiffPositions(diff->spanHighlights[mid]->position,
                                 position) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    int end = lo;
    while (end < diff->spanHighlightCount &&
           SamePosition(diff->spanHighlights[end]->position, position))
        end++;
    if (end == lo && !inlineCount) return runs;

    // Later fills and foregrounds override earlier values independently.
    // A new range can introduce at most two boundaries in either run list.
    int cap = inlineCount + 2 * (end - lo);
    runs.washes = (TextSpan*)Alloc(cx->a, cap * (int)sizeof(TextSpan));
    const Theme& theme = ActiveTheme(cx->app);
    Rgba status = side == DiffSide::Original ? theme.danger : theme.success;
    for (int i = 0; i < inlineCount; i++) {
        TextSpan wash;
        wash.lo = DisplayOff(source, line, changes[i].start);
        wash.hi = DisplayOff(source, line, changes[i].end);
        wash.bg = RgbaOpacity(status, 0.3f);
        runs.washes[runs.washCount++] = wash;
    }
    if (end == lo) return runs;
    runs.colors = (TextSpan*)Alloc(cx->a, cap * (int)sizeof(TextSpan));
    auto* scratch = (TextSpan*)Alloc(cx->a, cap * (int)sizeof(TextSpan));
    runs.underlines =
        (TextSpan*)Alloc(cx->a, (end - lo) * (int)sizeof(TextSpan));
    Rgba fill = RgbaOpacity(theme.warning, 0.5f);
    for (int i = lo; i < end; i++) {
        const DiffSpanHighlight& item = *diff->spanHighlights[i];
        int start = IMax(0, item.span.start);
        int stop = IMin(len(line.text), item.span.end);
        if (start >= stop) continue;
        while (!Utf8IsCharBoundary(line.text, start)) start--;
        while (!Utf8IsCharBoundary(line.text, stop)) stop++;
        TextSpan span;
        span.lo = DisplayOff(source, line, start);
        span.hi = DisplayOff(source, line, stop);
        span.bg = item.hasBackground ? item.background : fill;
        runs.washCount = InsertDiffHighlight(runs.washes, runs.washCount, span,
                                             cap, scratch);
        if (item.underlined) {
            // thickness px(1.), color span.foreground(), not wavy.
            TextSpan rule;
            rule.lo = span.lo;
            rule.hi = span.hi;
            rule.underline = true;
            rule.color =
                item.hasForeground ? item.foreground : Rgba{0, 0, 0, 0};
            runs.underlines[runs.underlineCount++] = rule;
        }
        if (item.hasForeground) {
            span.bg = {0, 0, 0, 0};
            span.color = item.foreground;
            runs.colorCount = InsertDiffHighlight(runs.colors, runs.colorCount,
                                                  span, cap, scratch);
        }
    }
    return runs;
}

static TextSpan* DiffHighlightSlice(Ctx* cx, const TextSpan* spans, int count,
                                    int lo, int hi, int* outCount) {
    int start = 0, end = count;
    while (start < end) {
        int mid = start + (end - start) / 2;
        if (spans[mid].hi <= lo)
            start = mid + 1;
        else
            end = mid;
    }
    end = start;
    while (end < count && spans[end].lo < hi) end++;
    *outCount = end - start;
    if (start == end) return nullptr;
    auto* slice = (TextSpan*)Alloc(cx->a, *outCount * (int)sizeof(TextSpan));
    for (int i = start; i < end; i++) {
        TextSpan span = spans[i];
        span.lo = IMax(lo, span.lo) - lo;
        span.hi = IMin(hi, span.hi) - lo;
        slice[i - start] = span;
    }
    return slice;
}

static void StyleDiffRun(El* bit, const DiffSyntaxSpan* span) {
    if (span) {
        if (span->weight) bit->Weight((FontWeight)span->weight);
        if (span->italic) bit->Italic();
        if (span->hasColor) bit->Fg(span->color);
        if (span->hasBackground) bit->Bg(span->background);
        if (span->underline) bit->Underline();
    }
}

static void StyleWrappedDiffLine(Ctx* cx, El* text, const DiffState* state,
                                 int file, DiffSide side, int line, bool syntax,
                                 const DiffHighlightRuns& runs) {
    ArenaVec<TextHighlight> styled;
    if (syntax && state->syntaxFn) {
        for (const DiffSyntaxSpan& span : state->syntaxSpans) {
            if (span.file != file || span.side != (uint8_t)side ||
                span.line != line)
                continue;
            HighlightStyle style;
            style.weight = (FontWeight)span.weight;
            if (span.italic) style.fontStyle = HighlightFontStyle::Italic;
            style.hasColor = span.hasColor;
            style.color = span.color;
            if (span.hasBackground) style.background = span.background;
            if (span.underline) style.underline.thickness = 1;
            styled.Append(cx->a, {span.start, span.end, style});
        }
    } else if (syntax) {
        SyntaxLexer lexer;
        SyntaxLexStart(&lexer, SyntaxLangFor(state->files[file]->Language()),
                       text->text);
        const Theme& theme = ActiveTheme(cx->app);
        while (SyntaxLexNext(&lexer)) {
            if (lexer.tok == SyntaxTok::Text) continue;
            HighlightStyle style;
            style.hasColor = true;
            style.color =
                SyntaxTokColor(lexer.tok, theme.mode, theme.foreground);
            int at = (int)(lexer.text.s - text->text.s);
            styled.Append(cx->a, {at, at + len(lexer.text), style});
        }
    }
    if (!len(styled)) return;
    // Explicit diff span colours take precedence over the syntax colour. Cut
    // both lists at each boundary to keep the shaped font ranges disjoint.
    ArenaVec<TextHighlight> merged;
    int i = 0, c = 0;
    for (int at = 0; at < len(text->text);) {
        while (i < len(styled) && styled[i].hi <= at) ++i;
        while (c < runs.colorCount && runs.colors[c].hi <= at) ++c;
        int end = len(text->text);
        HighlightStyle style;
        bool use = false;
        if (i < len(styled)) {
            const TextHighlight& span = styled[i];
            if (span.lo > at) {
                end = IMin(end, span.lo);
            } else {
                end = IMin(end, span.hi);
                style = span.style;
                use = true;
            }
        }
        if (c < runs.colorCount) {
            const TextSpan& span = runs.colors[c];
            if (span.lo > at) {
                end = IMin(end, span.lo);
            } else {
                end = IMin(end, span.hi);
                style.color = span.color;
                style.hasColor = use = true;
            }
        }
        if (use) merged.Append(cx->a, {at, end, style});
        at = end;
    }
    text->Highlights(merged.Flatten(cx->a), len(merged));
}

// The span highlights' underlines over each text run of `row`, every run a
// slice of `display`. A rule with no colour of its own takes the run's.
static void UnderlineDiffRuns(Ctx* cx, El* row, Str display,
                              const DiffHighlightRuns& runs) {
    if (!runs.underlineCount) return;
    Rgba text = ActiveTheme(cx->app).foreground;
    for (El* bit = row->first; bit; bit = bit->next) {
        if (!bit->text.s || !len(bit->text)) continue;
        int lo = (int)(bit->text.s - display.s);
        int hi = lo + len(bit->text);
        auto* rules = (TextSpan*)Alloc(
            cx->a, runs.underlineCount * (int)sizeof(TextSpan));
        int n = 0;
        for (int i = 0; i < runs.underlineCount; i++) {
            TextSpan rule = runs.underlines[i];
            if (rule.hi <= lo || rule.lo >= hi) continue;
            rule.lo = IMax(lo, rule.lo) - lo;
            rule.hi = IMin(hi, rule.hi) - lo;
            if (rule.color.a == 0)
                rule.color = bit->style.hasColor ? bit->style.color : text;
            rules[n++] = rule;
        }
        if (n) bit->Underlines(rules, n);
    }
}

static void RecordDiffCodeRun(PaintCtx* paint, El* el, void* data) {
    auto* frame = static_cast<DiffCodeRunGeometry*>(data);
    DiffState* state =
        static_cast<DiffState*>(EntityGet(paint->app, el->selectionOwner));
    if (!state) return;
    Bounds b = el->Bounds();
    Bounds visible = b;
    if (paint->hasHitMask) {
        Bounds mask = paint->hitMask;
        float x = std::max(b.x, mask.x);
        float y = std::max(b.y, mask.y);
        visible = {x, y, std::min(b.Right(), mask.Right()) - x,
                   std::min(b.Bottom(), mask.Bottom()) - y};
    }
    if (visible.w <= 0.f || visible.h <= 0.f) return;
    DiffCodeRunGeometry run = *frame;
    run.bounds = b;
    run.visible = visible;
    run.text = el->text;
    run.layout = el->laidLayout;
    if (run.layout) TextLayoutAddRef(run.layout);
    VecAppend(state->codeRuns, run);
}

static El* CodeTextRuns(Ctx* cx, const DiffState* state, int fileIx,
                        const DiffFile* file, DiffSide side, int ix,
                        bool syntax, bool wrap, const DiffHighlightRuns& runs);

// The initial estimate uses only a few candidates. Once a line is painted,
// grow the scroll range to its actual width, including fallback glyphs and
// differences introduced by the syntax runs. The clip only limits paint.
static void RecordDiffCodeWidth(PaintCtx* paint, El* row, void*) {
    DiffState* state =
        static_cast<DiffState*>(EntityGet(paint->app, row->selectionOwner));
    if (!state || !row->first || !row->last) return;
    float width = row->last->Bounds().Right() - row->first->x;
    if (width > state->horizontalCodeWidth + 0.01f) {
        state->horizontalCodeWidth = width;
        AppInvalidate(paint->window);
    }
}

static El* CodeText(Ctx* cx, const Diff* diff, const DiffState* state,
                    int fileIx, const DiffFile* file, DiffSide side, int ix,
                    bool syntax, bool showInline) {
    const DiffFileSide* source = SideOf(file, side);
    const DiffSourceLine& line = source->lines[ix];
    const auto runs =
        BuildDiffHighlightRuns(cx, diff, file, side, ix, showInline);
    El* row = CodeTextRuns(cx, state, fileIx, file, side, ix, syntax,
                           diff->softWrap, runs);
    UnderlineDiffRuns(cx, row, line.display, runs);
    if (!diff->softWrap && len(line.display) > 0) {
        row->SelectionOwner(state->self.id);
        row->lifecycle = ArenaNew<ElLifecycle>(cx->a);
        row->lifecycle->afterPaint = &RecordDiffCodeWidth;
    }
    return row;
}

static El* CodeTextRuns(Ctx* cx, const DiffState* state, int fileIx,
                        const DiffFile* file, DiffSide side, int ix,
                        bool syntax, bool wrap, const DiffHighlightRuns& runs) {
    const DiffSourceLine& line = SideOf(file, side)->lines[ix];
    const DiffFileSide* source = SideOf(file, side);
    const DiffTextSelection& selection = state->textSelection;
    int selectionLo = 0;
    int selectionHi = 0;
    if (selection.hitText && selection.anchor != selection.cursor &&
        selection.file == fileIx && selection.side == side) {
        int start =
            IMax(line.sourceStart, IMin(selection.anchor, selection.cursor));
        int end =
            IMin(line.contentEnd, IMax(selection.anchor, selection.cursor));
        if (start < end) {
            selectionLo = DisplayOff(source, line, start - line.sourceStart);
            selectionHi = DisplayOff(source, line, end - line.sourceStart);
        }
    }
    El* row = Div(cx->a)->FlexRow();
    auto textRun = [&](Str text, int lo) {
        El* bit = TextEl(cx->a, text)->Mono();
        int hi = lo + len(text);
        auto* geometry = ArenaNew<DiffCodeRunGeometry>(cx->a);
        geometry->file = fileIx;
        geometry->side = side;
        geometry->line = ix;
        geometry->displayStart = lo;
        bit->SelectionOwner(state->self.id);
        bit->lifecycle = ArenaNew<ElLifecycle>(cx->a);
        bit->lifecycle->afterPaint = &RecordDiffCodeRun;
        bit->lifecycle->user = geometry;
        int start = IMax(lo, selectionLo);
        int end = IMin(hi, selectionHi);
        if (start < end) {
            bit->GlyphSelection()->SelRange(start - lo, end - lo,
                                            ActiveTheme(cx->app).selection);
        }
        int count;
        if (runs.washCount) {
            const TextSpan* spans = DiffHighlightSlice(
                cx, runs.washes, runs.washCount, lo, hi, &count);
            if (count) bit->Washes(spans, count);
        }
        if (runs.colorCount) {
            const TextSpan* spans = DiffHighlightSlice(
                cx, runs.colors, runs.colorCount, lo, hi, &count);
            if (count) bit->Spans(spans, count);
        }
        row->Child(bit);
        return bit;
    };
    if (!len(line.display)) {
        textRun(StrL(""), 0)->W(1)->H(ThemeFontSize(cx->app));
        return row;
    }
    if (wrap) {
        float font = ThemeFontSize(cx->app);
        El* text = textRun(line.display, 0)
                       ->Font(font)
                       ->LineHeight(24.f / font)
                       ->Wrap()
                       ->Flex1()
                       ->MinW(0);
        StyleWrappedDiffLine(cx, text, state, fileIx, side, ix, syntax, runs);
        return row->Flex1()->MinW(0);
    }
    if (syntax && state && state->syntaxFn) {
        int at = 0;
        for (int i = 0; i < len(state->syntaxSpans); i++) {
            const DiffSyntaxSpan& span = state->syntaxSpans[i];
            if (span.file != fileIx || span.side != (uint8_t)side ||
                span.line != ix)
                continue;
            int lo = span.start;
            int hi = span.end;
            if (lo < at) lo = at;
            if (hi > len(line.display)) hi = len(line.display);
            if (lo > at) textRun(Slice(line.display, at, lo), at);
            if (lo < hi)
                StyleDiffRun(textRun(Slice(line.display, lo, hi), lo), &span);
            if (hi > at) at = hi;
        }
        if (at < len(line.display))
            textRun(Slice(line.display, at, len(line.display)), at);
        return row;
    }
    if (!syntax) {
        textRun(line.display, 0);
        return row;
    }
    SyntaxLang lang = SyntaxLangFor(file->Language());
    if (lang == SyntaxLangNone) {
        textRun(line.display, 0);
        return row;
    }
    SyntaxLexer lx;
    SyntaxLexStart(&lx, lang, line.display);
    Rgba fg = ActiveTheme(cx->app).foreground;
    while (SyntaxLexNext(&lx)) {
        El* bit = textRun(lx.text, (int)(lx.text.s - line.display.s));
        if (lx.tok != SyntaxTok::Text)
            bit->Fg(SyntaxTokColor(lx.tok, ActiveTheme(cx->app).mode, fg));
    }
    return row;
}

// DiffLine::source_offset: the source byte a display offset falls on. An
// offset inside a tab's expansion is the tab.
static int SourceOff(const DiffFileSide* side, const DiffSourceLine& line,
                     int displayOff) {
    displayOff = IMin(IMax(displayOff, 0), len(line.display));
    int lo = 0, hi = line.tabCount;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (side->tabs[line.tabBegin + mid].displayOffset <= displayOff)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0) return displayOff;
    const DiffDisplayTab& tab = side->tabs[line.tabBegin + lo - 1];
    if (displayOff < tab.displayOffset + tab.width) return tab.sourceOffset;
    return displayOff - (tab.displayOffset + tab.width - tab.sourceOffset - 1);
}

// One painted source line, as the pointer listeners of the frame that
// built it see it: Rust's SelectionGeometry CodeRun. Frame-arena, carried
// as the listeners' argument, the way input.cpp's TokenHover is. `diff`
// is that frame's Diff, for its handlers.
struct DiffTextRun {
    const Diff* diff = nullptr;
    int file = 0;
    DiffSide side = DiffSide::Original;
    int ix = 0;
    // The paint offset the source was drawn at, and its font size.
    float scrollX = 0;
    float font = 0;
};

// Find the nearest visible source run on the anchor side. Compare row
// distance first so a long or heavily indented line cannot steal the hit
// from a shorter line placed directly under the pointer.
static const DiffCodeRunGeometry* DiffSelectionRun(const DiffState* self,
                                                   Point at, int file,
                                                   DiffSide side) {
    const DiffCodeRunGeometry* best = nullptr;
    float bestY = 0;
    float bestX = 0;
    bool bestRow = false;
    for (const DiffCodeRunGeometry& run : self->codeRuns) {
        if (run.file != file || run.side != side) continue;
        Bounds b = run.visible;
        bool inRow = at.y >= b.y && at.y < b.Bottom();
        float y = std::max(0.f, std::max(b.y - at.y, at.y - b.Bottom()));
        float x = std::max(0.f, std::max(b.x - at.x, at.x - b.Right()));
        if (!best || (inRow && !bestRow) ||
            (inRow == bestRow && (y < bestY || (y == bestY && x < bestX)))) {
            best = &run;
            bestY = y;
            bestX = x;
            bestRow = inRow;
        }
    }
    return best;
}

static Selection DiffSelectionAt(const DiffState* self, PaintCtx* paint,
                                 const DiffCodeRunGeometry& run, Point at,
                                 int clicks) {
    const DiffFileSide* source = SideOf(self->files[run.file], run.side);
    const DiffSourceLine& line = source->lines[run.line];
    if (clicks >= 3) return {line.sourceStart, line.contentEnd};
    TextLayout* layout = run.layout;
    if (!layout && len(run.text)) {
        layout = TextLayoutNew(paint, run.text, ThemeFontSize(paint->app), 0,
                               false, kFontMono, 0, nullptr);
    }
    float x = std::max(run.visible.x, std::min(at.x, run.visible.Right()));
    int start = 0;
    int end = 0;
    if (layout) {
        float relX = x - run.bounds.x;
        start = end =
            TextLayoutHitPoint(layout, run.text, relX, at.y - run.bounds.y);
        if (clicks == 2) {
            Vec<TextGlyphBox> glyphs;
            TextGlyphBoxes(layout, run.text, &glyphs);
            for (const TextGlyphBox& glyph : glyphs) {
                if (relX >= glyph.left && relX < glyph.right) {
                    start = end = glyph.lo;
                    break;
                }
            }
        }
        if (layout != run.layout) TextLayoutRelease(layout);
    }
    start += run.displayStart;
    end += run.displayStart;
    if (clicks == 2) {
        TextWordRangeAt(line.display, start, &start, &end);
    }
    return {line.sourceStart + SourceOff(source, line, start),
            line.sourceStart + SourceOff(source, line, end)};
}

static void ClearDiffReviewSelection(DiffState* self, Ctx* cx) {
    bool changed = self->hasSelection;
    self->hasSelection = false;
    self->selecting = false;
    if (changed) {
        DiffEvent event;
        event.kind = DiffEventKind::SelectionChanged;
        Emit(self, cx, event);
    }
}

static bool DiffTouchCaret(const DiffState* state, int sourceOffset,
                           SelectionEdge edge, Bounds* out) {
    const DiffTextSelection& selection = state->textSelection;
    const auto* source = SideOf(state->files[selection.file], selection.side);
    for (const DiffCodeRunGeometry& run : state->codeRuns) {
        if (run.file != selection.file || run.side != selection.side ||
            !run.layout || !len(run.text))
            continue;
        const DiffSourceLine& line = source->lines[run.line];
        if (sourceOffset < line.sourceStart || sourceOffset > line.contentEnd) {
            continue;
        }
        int offset = DisplayOff(source, line, sourceOffset - line.sourceStart) -
                     run.displayStart;
        if (offset < 0 || offset > len(run.text)) continue;
        bool trailing = edge == SelectionEdge::End || offset == len(run.text);
        if (offset == 0) trailing = false;
        if (edge == SelectionEdge::Start && offset == len(run.text) &&
            run.displayStart + offset < len(line.display))
            continue;
        if (edge == SelectionEdge::End && offset == 0 && run.displayStart > 0) {
            continue;
        }
        int start = trailing ? Utf8Prev(run.text, offset) : offset;
        uint32_t cp = 0;
        int end = trailing ? offset : offset + Utf8At(run.text, offset, &cp);
        Bounds glyph;
        if (!TextLayoutRangeRects(run.layout, run.text, start, end, &glyph,
                                  1)) {
            continue;
        }
        float x = run.bounds.x + (trailing ? glyph.Right() : glyph.x);
        float y = run.bounds.y + glyph.y;
        if (x < run.visible.x || x > run.visible.Right() ||
            y >= run.visible.Bottom() || y + glyph.h <= run.visible.y) {
            continue;
        }
        *out = {x, y, 1.f, glyph.h};
        return true;
    }
    return false;
}

static bool DiffTouchSnapshot(void* user, const Window*,
                              TouchSelectionSnapshot* out) {
    auto* state = static_cast<DiffState*>(user);
    const DiffTextSelection& sel = state->textSelection;
    if (!sel.hitText || sel.anchor == sel.cursor || sel.file < 0 ||
        sel.file >= len(state->files))
        return false;
    if (out) {
        Bounds start;
        Bounds end;
        bool startVisible = DiffTouchCaret(state, IMin(sel.anchor, sel.cursor),
                                           SelectionEdge::Start, &start);
        bool endVisible = DiffTouchCaret(state, IMax(sel.anchor, sel.cursor),
                                         SelectionEdge::End, &end);
        *out = TouchSelectionSnapshot::New(start, end)
                   .WithEdgeVisible(SelectionEdge::Start, startVisible)
                   .WithEdgeVisible(SelectionEdge::End, endVisible);
    }
    return true;
}

static SelectionEdge ExtendDiffTouch(void* user, Window* win,
                                     SelectionEdge edge, Point point) {
    auto* state = static_cast<DiffState*>(user);
    DiffTextSelection& sel = state->textSelection;
    const auto* run = DiffSelectionRun(state, point, sel.file, sel.side);
    if (!run) return edge;
    int fixed = edge == SelectionEdge::Start ? IMax(sel.anchor, sel.cursor)
                                             : IMin(sel.anchor, sel.cursor);
    int moving = DiffSelectionAt(state, &win->paint, *run, point, 1).start;
    if (moving == fixed) return edge;
    sel.anchor = IMin(fixed, moving);
    sel.cursor = IMax(fixed, moving);
    Ctx cx = {win->app, win, win->frameArena, state->self.id};
    Notify(&cx);
    return moving < fixed ? SelectionEdge::Start : SelectionEdge::End;
}

static int CopyDiffTouch(void* user, Window* win, char* out, int capacity) {
    auto* state = static_cast<DiffState*>(user);
    Str selected = DiffTouchSnapshot(state, win, nullptr)
                       ? state->SelectedText(GetTempArena())
                       : Str{};
    return StrCopyCapped(out, capacity, selected);
}

static void ClearDiffTouch(void* user, Window* win) {
    auto* state = static_cast<DiffState*>(user);
    state->textSelection = {};
    AppInvalidate(win);
}

static void OnDiffCodeLongPress(DiffState* state, Ctx* cx,
                                const LongPressEvent* event, int64_t arg) {
    const auto* cell = reinterpret_cast<const DiffTextRun*>((intptr_t)arg);
    Point at = event->startPosition;
    const auto* run = DiffSelectionRun(state, at, cell->file, cell->side);
    if (!run || !run->visible.Contains(at)) return;
    Selection word = DiffSelectionAt(state, &cx->win->paint, *run, at, 2);
    if (word.start == word.end) return;
    TextSelection::Clear(cx->win, cx->app);
    FocusHandleFocus(cx->win, state->focus);
    ClearDiffReviewSelection(state, cx);
    state->hasAnchor = state->hasCursor = false;
    DiffTextSelection& selection = state->textSelection;
    selection = {};
    selection.file = run->file;
    selection.side = run->side;
    selection.anchor = word.start;
    selection.cursor = word.end;
    selection.hitText = true;
    WindowSelectionAttachTouchSource(
        cx->win, {state->self.id, &DiffTouchSnapshot, &ExtendDiffTouch,
                  &ClearDiffTouch, &CopyDiffTouch});
    WindowPreventDefault(cx);
    Notify(cx);
}

static void OnDiffCodeDown(DiffState* self, Ctx* cx, const MouseDownEvent* ev,
                           int64_t arg) {
    if (!ev->IsFocusing()) return;
    const auto* cell = reinterpret_cast<const DiffTextRun*>((intptr_t)arg);
    Point at = {ev->x, ev->y};
    const auto* run = DiffSelectionRun(self, at, cell->file, cell->side);
    if (!run) return;
    TextSelection::Clear(cx->win, cx->app);
    FocusHandleFocus(cx->win, self->focus);
    ClearDiffReviewSelection(self, cx);
    self->hasAnchor = self->hasCursor = false;
    DiffTextSelection& sel = self->textSelection;
    bool extend = ev->modifiers.shift && sel.file == run->file &&
                  sel.side == run->side && sel.hitText;
    if (!extend) {
        sel = {};
        sel.file = run->file;
        sel.side = run->side;
        sel.clicks = ev->clickCount;
        sel.hitText = len(run->text) > 0 && run->visible.Contains(at);
        int clicks = sel.hitText ? sel.clicks : 1;
        sel.unit = DiffSelectionAt(self, &cx->win->paint, *run, at, clicks);
        sel.anchor = sel.unit.start;
        sel.cursor = sel.unit.end;
    } else {
        sel.clicks = 1;
        sel.cursor = DiffSelectionAt(self, &cx->win->paint, *run, at, 1).end;
    }
    sel.dragging = true;
    Notify(cx);
}

static void ExtendDiffTextSelection(DiffState* self, Ctx* cx, Point at) {
    DiffTextSelection& sel = self->textSelection;
    if (!sel.dragging) return;
    const auto* run = DiffSelectionRun(self, at, sel.file, sel.side);
    if (!run) return;
    bool hit = sel.hitText || (len(run->text) > 0 && run->visible.Contains(at));
    Selection unit =
        DiffSelectionAt(self, &cx->win->paint, *run, at, sel.clicks);
    int anchor = sel.anchor;
    int cursor = unit.end;
    if (sel.clicks > 1) {
        bool backwards = unit.start < sel.unit.start;
        anchor = backwards ? sel.unit.end : sel.unit.start;
        cursor = backwards ? unit.start : unit.end;
    }
    if (hit != sel.hitText || anchor != sel.anchor || cursor != sel.cursor) {
        sel.hitText = hit;
        sel.anchor = anchor;
        sel.cursor = cursor;
        Notify(cx);
    }
}

static void StopDiffSelectionScroll(DiffState* self, Window* win) {
    WindowCancelTimer(win, self->selectionTimer);
    self->selectionTimer = 0;
}

static void DiffSelectionScrollTick(DiffState* self, Ctx* cx, const void*) {
    float delta = 0;
    if (!cx->win->mouseDown || !self->textSelection.dragging ||
        !AutoScrollComputeDelta(self->selectionPointer.y,
                                self->selectionViewport, &delta)) {
        StopDiffSelectionScroll(self, cx->win);
        return;
    }
    ExtendDiffTextSelection(self, cx, self->selectionPointer);
    VirtualListScrollHandle& scroll = self->listScroll;
    float limit = std::max(0.f, scroll.contentSize - scroll.viewport);
    float next = std::max(0.f, std::min(scroll.offset + delta, limit));
    if (next != scroll.offset) {
        scroll.offset = next;
        scroll.pending = false;
        Notify(cx);
    } else {
        StopDiffSelectionScroll(self, cx->win);
    }
}

static void OnDiffSelectionMove(DiffState* self, Ctx* cx,
                                const MouseMoveEvent* ev) {
    if (!ev->Dragging() || !self->textSelection.dragging) return;
    self->selectionPointer = {ev->x, ev->y};
    ExtendDiffTextSelection(self, cx, self->selectionPointer);
    float delta = 0;
    if (self->selectionViewport.h > 0 &&
        AutoScrollComputeDelta(ev->y, self->selectionViewport, &delta)) {
        if (!self->selectionTimer) {
            self->selectionTimer = WindowSetInterval(
                cx->win, 16, ListenTo(self->self, &DiffSelectionScrollTick));
        }
    } else {
        StopDiffSelectionScroll(self, cx->win);
    }
}

static void OnDiffSelectionDrag(DiffState* self, Ctx* cx,
                                const DragMoveEvent* ev) {
    OnDiffSelectionMove(self, cx, &ev->event);
}

static void OnDiffSelectionUp(DiffState* self, Ctx* cx,
                              const MouseUpEvent* ev) {
    if (ev->button != MouseButton::Left) return;
    self->textSelection.dragging = false;
    StopDiffSelectionScroll(self, cx->win);
    self->EndLineSelection(cx);
}

static void OnDiffSelectionOutside(DiffState* self, Ctx* cx,
                                   const MouseDownEvent* ev) {
    if (!ev->IsFocusing()) return;
    ClearDiffReviewSelection(self, cx);
    if (self->textSelection.file >= 0) {
        self->textSelection = {};
        Notify(cx);
    }
}

static void OnDiffGutterDown(DiffState* self, Ctx* cx, const MouseDownEvent* ev,
                             int64_t arg) {
    if (!ev->IsFocusing()) return;
    TextSelection::Clear(cx->win, cx->app);
    FocusHandleFocus(cx->win, self->focus);
    auto* position = reinterpret_cast<const DiffLinePosition*>((intptr_t)arg);
    self->BeginLineSelection(*position, ev->modifiers.shift, cx);
}

static void OnDiffGutterMove(DiffState* self, Ctx* cx, const MouseMoveEvent* ev,
                             int64_t arg) {
    if (!ev->Dragging() || !self->selecting) return;
    auto* position = reinterpret_cast<const DiffLinePosition*>((intptr_t)arg);
    self->DragLineSelection(*position, cx);
}

// SelectionGeometry::text_at: the character painted at window x, given
// the box the event landed in. That box is the source or the clip around
// it, masked to what is visible, so its left edge less the horizontal
// scroll is where the text starts. Unlike a caret hit it never snaps to
// the nearest text: past a line's end is over no character.
static bool DiffTextAt(const DiffState* state, PaintCtx* paint,
                       const DiffTextRun* run, Point point, Bounds box,
                       DiffTextPosition* out) {
    if (!run || run->file < 0 || run->file >= len(state->files)) return false;
    const DiffFile* file = state->files[run->file];
    const DiffFileSide* source = SideOf(file, run->side);
    if (run->ix < 0 || run->ix >= len(source->lines)) return false;
    const DiffSourceLine& line = source->lines[run->ix];
    if (run->diff->softWrap) {
        const auto* shaped =
            DiffSelectionRun(state, point, run->file, run->side);
        if (!shaped || shaped->line != run->ix || !shaped->layout ||
            !shaped->visible.Contains(point))
            return false;
        Vec<TextGlyphBox> glyphs;
        TextGlyphBoxes(shaped->layout, shaped->text, &glyphs);
        Point local = {point.x - shaped->bounds.x, point.y - shaped->bounds.y};
        for (const TextGlyphBox& glyph : glyphs) {
            if (local.x >= glyph.left && local.x < glyph.right &&
                local.y >= glyph.top && local.y < glyph.top + glyph.h) {
                out->position = DiffLinePosition::New(file->path, run->side,
                                                      line.lineNumber);
                out->offset = SourceOff(source, line, glyph.lo);
                out->text = line.text;
                return true;
            }
        }
        return false;
    }
    float relX = point.x - (box.x - run->scrollX);
    if (relX < 0 || !len(line.display)) return false;
    Size extent;
    TextLayout* layout = TextLayoutNew(paint, line.display, run->font, 0, false,
                                       kFontMono, 0, &extent);
    if (!layout) return false;
    Vec<TextGlyphBox> glyphs;
    int n = TextGlyphBoxes(layout, line.display, &glyphs);
    TextLayoutRelease(layout);
    int at = -1;
    for (int i = 0; i < n; i++) {
        if (relX >= glyphs[i].left && relX < glyphs[i].right) {
            at = glyphs[i].lo;
            break;
        }
    }
    if (at < 0 || at >= len(line.display)) return false;
    out->position =
        DiffLinePosition::New(file->path, run->side, line.lineNumber);
    out->offset = SourceOff(source, line, at);
    out->text = line.text;
    return true;
}

static bool SameTextHover(const DiffState* state, const DiffTextPosition* hit,
                          Modifiers modifiers) {
    if (!state->textHoverHeard) return false;
    const Modifiers& was = state->textHoverModifiers;
    if (was.control != modifiers.control || was.alt != modifiers.alt ||
        was.shift != modifiers.shift || was.platform != modifiers.platform ||
        was.function != modifiers.function)
        return false;
    if (state->textHoverHasHit != (hit != nullptr)) return false;
    if (!hit) return true;
    const DiffTextPosition& seen = state->textHover;
    return SamePosition(seen.position, hit->position) &&
           seen.offset == hit->offset && StrEq(seen.text, hit->text);
}

static void HearTextHover(DiffState* state, Ctx* cx, const Diff* diff,
                          const DiffTextPosition* hit, Modifiers modifiers) {
    state->textHoverHeard = true;
    state->textHoverHasHit = hit != nullptr;
    state->textHover = hit ? *hit : DiffTextPosition{};
    state->textHoverModifiers = modifiers;
    if (diff->onTextHover)
        diff->onTextHover(cx, hit, modifiers, diff->textHoverUser);
}

static void OnDiffTextMove(DiffState* self, Ctx* cx, const MouseMoveEvent* ev,
                           int64_t arg) {
    const auto* run = (const DiffTextRun*)(intptr_t)arg;
    if (!cx->win || !run) return;
    OnDiffSelectionMove(self, cx, ev);
    if (!run->diff->onTextHover) return;
    DiffTextPosition hit;
    bool has =
        DiffTextAt(self, &cx->win->paint, run, {ev->x, ev->y}, ev->el, &hit);
    const DiffTextPosition* now = has ? &hit : nullptr;
    if (SameTextHover(self, now, ev->modifiers)) return;
    HearTextHover(self, cx, run->diff, now, ev->modifiers);
}

static Listener DiffTextMoveListener(Entity<DiffState> state,
                                     const DiffTextRun* run) {
    return ListenTo(state, &OnDiffTextMove, (int64_t)(intptr_t)run);
}

// The text run under the window point in the last frame, with the box its
// move listener was registered on.
static const DiffTextRun* DiffTextRunAt(Window* win, Entity<DiffState> state,
                                        float x, float y, Bounds* box) {
    const HitRect* hit = HitTestRect(&win->paint, x, y);
    if (!hit || hit->onMouseMove.Fn() != DiffTextMoveListener(state, nullptr)
                                             .Fn())
        return nullptr;
    *box = hit->bounds;
    return (const DiffTextRun*)(intptr_t)hit->onMouseMove.arg;
}

// The pointer left the text. Rust's layer hears every move and reports
// None when the point is over no character; here a move onto another line's
// text reports that line instead, so the leave says nothing.
static void OnDiffTextLeave(DiffState* self, Ctx* cx, const HoverEvent* ev,
                            int64_t arg) {
    const auto* run = (const DiffTextRun*)(intptr_t)arg;
    if (!cx->win || !run || (ev && ev->hovered)) return;
    Bounds box;
    if (cx->win->pointerInWindow &&
        DiffTextRunAt(cx->win, run->diff->state, cx->win->mouseX,
                      cx->win->mouseY, &box))
        return;
    Modifiers modifiers = cx->win->modifiers;
    if (SameTextHover(self, nullptr, modifiers)) return;
    HearTextHover(self, cx, run->diff, nullptr, modifiers);
}

// SelectionGeometry::text_hover_after: a modifier went down or up with the
// pointer at rest. Only a pointer last heard over text hears it, at the
// character there now, since the rows may have scrolled under it.
static void OnDiffModifiersChanged(DiffState* self, Ctx* cx,
                                   const ModifiersChangedEvent* ev,
                                   int64_t arg) {
    const auto* diff = (const Diff*)(intptr_t)arg;
    if (!cx->win || !diff || !self->textHoverHeard || !self->textHoverHasHit)
        return;
    const Modifiers& was = self->textHoverModifiers;
    const Modifiers& now = ev->modifiers;
    if (was.control == now.control && was.alt == now.alt &&
        was.shift == now.shift && was.platform == now.platform &&
        was.function == now.function)
        return;
    Bounds box;
    float x = cx->win->mouseX;
    float y = cx->win->mouseY;
    const DiffTextRun* run = DiffTextRunAt(cx->win, diff->state, x, y, &box);
    DiffTextPosition hit;
    bool has = run && DiffTextAt(self, &cx->win->paint, run, {x, y}, box, &hit);
    HearTextHover(self, cx, diff, has ? &hit : nullptr, now);
}

static void OnDiffTextClick(DiffState* self, Ctx* cx, const ClickEvent* ev,
                            int64_t arg) {
    const DiffTextSelection& selection = self->textSelection;
    if (ev->clickCount == 1 && selection.hitText &&
        selection.anchor != selection.cursor)
        return;
    const auto* run = (const DiffTextRun*)(intptr_t)arg;
    if (!run || run->file < 0 || run->file >= len(self->files)) return;
    const DiffFile* file = self->files[run->file];
    const DiffFileSide* source = SideOf(file, run->side);
    if (run->ix < 0 || run->ix >= len(source->lines)) return;
    const Diff* diff = run->diff;
    if (!ev->keyboard) {
        // A drag selects text; only a stationary press is a click.
        if (cx->win && (fabsf(ev->x - cx->win->pressedX) > 3.f ||
                        fabsf(ev->y - cx->win->pressedY) > 3.f))
            return;
        DiffTextPosition hit;
        if (diff->onTextClick && cx->win &&
            DiffTextAt(self, &cx->win->paint, run, {ev->x, ev->y}, ev->el,
                       &hit) &&
            diff->onTextClick(cx, &hit, ev, diff->textClickUser))
            return;
    }
    if (diff->onLineClick) {
        DiffLinePosition position = DiffLinePosition::New(
            file->path, run->side, source->lines[run->ix].lineNumber);
        diff->onLineClick(cx, &position, ev, diff->clickUser);
    }
}

bool DiffRunBounds(Window* win, DiffSide side, int lineIx, Bounds* out) {
    if (!win || !out) return false;
    uintptr_t fn = DiffTextMoveListener(Entity<DiffState>{}, nullptr).Fn();
    // The source and the clip around it both listen; the source is the
    // narrower of the two.
    int64_t found = 0;
    for (int i = 0; i < len(win->paint.hits); i++) {
        const HitRect& hit = win->paint.hits[i];
        if (hit.onMouseMove.Fn() != fn || !hit.onMouseMove.arg) continue;
        const auto* run = (const DiffTextRun*)(intptr_t)hit.onMouseMove.arg;
        if (run->side != side || run->ix != lineIx) continue;
        if (found && found != hit.onMouseMove.arg) continue;
        if (!found || hit.bounds.w < out->w) *out = hit.bounds;
        found = hit.onMouseMove.arg;
    }
    return found != 0;
}

struct DiffAnnotationPress {
    const Diff* view = nullptr;
    DiffLineRange range = {};
};

static void AddDiffAnnotation(DiffState*, Ctx* cx, const ClickEvent*,
                              int64_t arg) {
    const auto* press =
        reinterpret_cast<const DiffAnnotationPress*>((intptr_t)arg);
    press->view->onAddAnnotation(cx, &press->range, press->view->addUser);
}

static El* DiffAnnotationButton(Ctx* cx, const Diff* diff, const DiffFile* file,
                                DiffSide side, int line, float left,
                                const DiffLineRange* selected) {
    if (!diff->onAddAnnotation) return nullptr;
    int number = file->LineNumber(side, line);
    auto* press = ArenaNew<DiffAnnotationPress>(cx->a);
    press->view = diff;
    press->range = selected
                       ? *selected
                       : DiffLineRange::New(file->Path(), side, number, number);
    bool selectionEnd = false;
    if (selected) {
        if (selected->IsSingleSide()) {
            selectionEnd = number == IMax(selected->start, selected->end);
        } else {
            int first =
                file->PatchIx(selected->side,
                              file->LineIndex(selected->side, selected->start));
            int last = file->PatchIx(
                selected->endSide,
                file->LineIndex(selected->endSide, selected->end));
            selectionEnd = file->PatchIx(side, line) == IMax(first, last);
        }
    }
    const Theme& theme = ActiveTheme(cx->app);
    const TrArg args[] = {{"line", fmt("%d", number)}};
    El* button =
        Div(cx->a)
            ->PathClick(side == DiffSide::Original
                            ? StrL("add-original-annotation")
                            : StrL("add-modified-annotation"))
            ->Absolute()
            ->Top(0)
            ->Left(left)
            ->W(24)
            ->H(24)
            ->FlexRow()
            ->ItemsCenter()
            ->JustifyCenter()
            ->Bg(theme.tokens.primary)
            ->Radius(theme.radius)
            ->Role(AccessibilityRole::Button)
            ->AriaLabel(StrDup(cx->a, TrFmtTemp("Diff.AddAnnotation", args, 1)))
            ->StopMouseDown()
            ->SuppressTextSelection()
            ->OnClick(ListenTo(diff->state, &AddDiffAnnotation,
                               (int64_t)(intptr_t)press))
            ->Child(IconEl(cx->a, IconName::Plus, 12)->Fg(theme.primaryFg));
    if (!selectionEnd) button->GroupHoverVisible();
    return button;
}

static El* RenderRow(void* user, Ctx* cx, int ix) {
    Diff* diff = (Diff*)user;
    DiffState* state = diff->state.Get(cx);
    if (!state || ix < 0 || ix >= len(state->rows)) return Div(cx->a);
    const DiffRow& row = state->rows[ix];
    DiffFile* file = state->files[row.file];
    const Theme& theme = ActiveTheme(cx->app);
    if (!diff->headerVisible && row.kind == DiffRowKind::File)
        return Div(cx->a)->H(0);
    if (row.kind == DiffRowKind::File) {
        El* header =
            Div(cx->a)->FlexRow()->H(28)->W(kFill)->ItemsCenter()->Gap(8)->PadX(
                8);
        header->Bg(Tint(theme.muted, 40));
        header->Child(
            Button::New(cx, fmt("diff-file-%d", row.file))
                ->Ghost()
                ->WithSize(UiSize::XSmall)
                ->Icon(state->IsFileCollapsed(file->path)
                           ? IconName::ChevronRight
                           : IconName::ChevronDown)
                ->OnClick(ListenTo(diff->state, &OnToggleFile, row.file))
                ->IntoEl());
        if (diff->header) {
            El* custom = diff->header(cx, file, diff->headerUser);
            if (custom) header->Child(custom);
        } else {
            if (diff->headerPrefix) {
                El* prefix = diff->headerPrefix(cx, file, diff->headerUser);
                if (prefix) header->Child(prefix);
            }
            header->Child(TextEl(cx->a, file->path));
            if (diff->headerTitleSuffix) {
                El* suffix =
                    diff->headerTitleSuffix(cx, file, diff->headerUser);
                if (suffix) header->Child(suffix);
            }
            TempStr stats = fmt("+%d -%d", file->additions, file->deletions);
            header->Child(TextEl(cx->a, stats)->Fg(theme.mutedFg));
            if (diff->headerSuffix) {
                El* suffix = diff->headerSuffix(cx, file, diff->headerUser);
                if (suffix) header->Child(suffix);
            }
        }
        if (diff->annotationContent && diff->annotations) {
            for (int i = 0; i < diff->annotationCount; i++) {
                const DiffAnnotation& ann = diff->annotations[i];
                if (ann.hasPosition || !StrEq(ann.path, file->path)) continue;
                El* note =
                    diff->annotationContent(cx, &ann, diff->annotationUser);
                if (note) header->Child(note);
            }
        }
        return header;
    }
    if (row.kind == DiffRowKind::Notice) {
        const char* key = "Diff.NoTextChanges";
        if (file->binary)
            key = "Diff.BinaryChanges";
        else if (file->status == DiffFileStatus::Added)
            key = "Diff.AddedFile";
        else if (file->status == DiffFileStatus::Deleted)
            key = "Diff.DeletedFile";
        else if (file->status == DiffFileStatus::Renamed)
            key = "Diff.RenamedFile";
        else if (file->status == DiffFileStatus::Copied)
            key = "Diff.CopiedFile";
        else if (!file->HasChanges())
            key = "Diff.NoChanges";
        return Div(cx->a)->H(24)->PadX(12)->Child(TextEl(cx->a, Tr(key)));
    }
    if (row.kind == DiffRowKind::Hunk) {
        if (diff->hunkSeparator == DiffHunkSeparator::Simple)
            return Div(cx->a)->H(4)->W(kFill)->Bg(Tint(theme.muted, 80));
        if (diff->hunkSeparator == DiffHunkSeparator::LineInfo) {
            const DiffHunk* prev =
                row.hunk > 0 ? &file->hunks[row.hunk - 1] : nullptr;
            int hidden = file->hunks[row.hunk].HiddenLinesBefore(prev);
            if (hidden == 0) return Div(cx->a);
            return Div(cx->a)
                ->H(24)
                ->PadX(12)
                ->Bg(Tint(theme.muted, 60))
                ->Child(TextEl(cx->a, TrCountTemp("Diff.HiddenLines", hidden))
                            ->Fg(theme.mutedFg));
        }
        return Div(cx->a)
            ->H(24)
            ->PadX(12)
            ->Bg(Tint(theme.muted, 60))
            ->Child(TextEl(cx->a, file->hunks[row.hunk].label)
                        ->Fg(theme.mutedFg));
    }
    if (row.kind == DiffRowKind::Fold) {
        int count = row.pairsEnd - row.pairsStart;
        El* fold =
            Div(cx->a)->FlexRow()->H(24)->W(kFill)->ItemsCenter()->Gap(4)->PadX(
                8);
        fold->Bg(Tint(theme.muted, 60));
        int64_t base =
            ((int64_t)row.file << 32) | (int64_t)(row.pairsStart & 0xffffff);
        fold->Child(
            Button::New(cx, fmt("diff-fold-%d-%d", row.file, row.pairsStart))
                ->Ghost()
                ->WithSize(UiSize::XSmall)
                ->Icon(IconName::ChevronsUpDown)
                ->Label(TrCountTemp("Diff.UnchangedLines", count))
                ->OnClick(ListenTo(diff->state, &OnExpand, base))
                ->IntoEl());
        return fold;
    }
    if (row.kind == DiffRowKind::Conflict) {
        const DiffConflict& conflict = file->conflicts[row.conflict];
        const char* key = "Diff.CurrentChange";
        if (row.part == DiffConflictPart::Incoming) key = "Diff.IncomingChange";
        if (row.part == DiffConflictPart::Base) key = "Diff.BaseChange";
        Rgba tint = row.part == DiffConflictPart::Incoming ? theme.danger
                                                           : theme.success;
        if (row.part == DiffConflictPart::Base) tint = theme.mutedFg;
        El* head =
            Div(cx->a)->FlexRow()->H(28)->W(kFill)->ItemsCenter()->Gap(8)->PadX(
                8);
        head->Bg(Tint(tint, 40));
        Str label = conflict.Label(row.part);
        head->Child(TextEl(cx->a, len(label) ? label : Tr(key)));
        if (row.part == DiffConflictPart::Current) {
            int64_t pack =
                ((int64_t)row.file << 16) | ((int64_t)row.conflict << 8);
            DiffConflictResolution cur;
            bool resolved =
                state->ConflictResolution(file->path, row.conflict, &cur);
            if (resolved) {
                head->Child(
                    Button::New(cx, fmt("diff-undo-%d", row.conflict))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Label(Tr("Diff.Undo"))
                        ->OnClick(ListenTo(diff->state, &OnChoose, pack))
                        ->IntoEl());
            } else {
                head->Child(
                    Button::New(cx, fmt("diff-cur-%d", row.conflict))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Label(Tr("Diff.AcceptCurrent"))
                        ->OnClick(ListenTo(diff->state, &OnChoose, pack | 1))
                        ->IntoEl());
                head->Child(
                    Button::New(cx, fmt("diff-in-%d", row.conflict))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Label(Tr("Diff.AcceptIncoming"))
                        ->OnClick(ListenTo(diff->state, &OnChoose, pack | 2))
                        ->IntoEl());
                head->Child(
                    Button::New(cx, fmt("diff-both-%d", row.conflict))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Label(Tr("Diff.AcceptBoth"))
                        ->OnClick(ListenTo(diff->state, &OnChoose, pack | 3))
                        ->IntoEl());
            }
        }
        return head;
    }
    bool split = state->mode == DiffMode::Split && !file->IsSingleColumn();
    // The lane is one row square, between the line numbers and the code,
    // on every code line once markers and a renderer are both set.
    // Rows are fill-width, so the lane takes space from the row. Rust
    // adds the same width to the measured content width; see port-status.md.
    bool markerLane = diff->gutterMarkerContent && diff->gutterMarkerCount > 0;
    auto lineSelected = [&](DiffSide side, int line) {
        if (line < 0 || !state->hasSelection) return false;
        int number = SideOf(file, side)->lines[line].lineNumber;
        const DiffLineRange& range = state->selected;
        if (!StrEq(range.path, file->path)) return false;
        if (range.IsSingleSide()) {
            if (side != range.side) return false;
            int lo = range.start < range.end ? range.start : range.end;
            int hi = range.start > range.end ? range.start : range.end;
            return number >= lo && number <= hi;
        }
        if (side == range.side) return number >= range.start;
        if (side == range.endSide) return number <= range.end;
        return false;
    };
    auto decorationAt = [&](DiffSide side, int line) -> const DiffDecoration* {
        if (line < 0 || !diff->decorations) return nullptr;
        int number = SideOf(file, side)->lines[line].lineNumber;
        DiffLinePosition pos = DiffLinePosition::New(file->path, side, number);
        return DiffDecorationAt(diff->decorations, diff->decorationCount, pos);
    };
    auto markerAt = [&](DiffSide side, int line) -> const DiffGutterMarker* {
        if (line < 0 || !diff->gutterMarkers) return nullptr;
        int number = SideOf(file, side)->lines[line].lineNumber;
        DiffLinePosition pos = DiffLinePosition::New(file->path, side, number);
        return DiffGutterMarkerAt(diff->gutterMarkers, diff->gutterMarkerCount,
                                  pos);
    };
    auto cell = [&](DiffSide side, int line, bool changed, bool present,
                    const DiffDecoration* rowDeco,
                    const DiffGutterMarker* rowMarker, bool useRowMarks) {
        El* block = nullptr;
        El* box = Div(cx->a)->FlexRow()->MinH(24)->Grow();
        if (diff->softWrap)
            box->ItemsStart();
        else
            box->H(24)->ItemsCenter();
        if (!present) {
            // An empty split cell is one source row, the same height as
            // the line beside it. It has no marker lane.
            box->Bg(Tint(theme.muted, 30));
            return box;
        }
        // px_2. The row's horizontal inset matches the width budget.
        box->PadX(8);
        float annotationLeft = 8.f;
        if (diff->onAddAnnotation) box->Group();
        int number = SideOf(file, side)->lines[line].lineNumber;
        bool selected = lineSelected(side, line);
        Rgba bg = changed ? (side == DiffSide::Original ? theme.danger
                                                        : theme.success)
                          : theme.background;
        if (selected) {
            box->Bg(theme.selection);
        } else if (changed && diff->changeBackground) {
            box->Bg(Tint(bg, 30));
        }
        // Above the change fill and below the text. A selected line shows
        // the selection instead. The last supplied decoration wins inside
        // DiffDecorationAt; a unified row tries the original side first.
        const DiffDecoration* deco = nullptr;
        if (!selected) {
            deco = useRowMarks ? rowDeco : decorationAt(side, line);
        }
        if (deco) {
            Rgba fill = deco->hasBackground ? deco->background
                                            : RgbaOpacity(theme.warning, 0.2f);
            box->Child(Div(cx->a)->Absolute()->Inset0()->Bg(fill));
        }
        if (diff->lineNumber) {
            int width = file->lineNumberDigits * 8 + 12;
            annotationLeft += (float)width;
            auto* pos = ArenaNew<DiffLinePosition>(cx->a);
            *pos = DiffLinePosition::New(file->path, side, number);
            auto arg = (int64_t)(intptr_t)pos;
            box->Child(
                Div(cx->a)
                    ->PathClick(side == DiffSide::Original
                                    ? StrL("original-line-number")
                                    : StrL("modified-line-number"))
                    ->W((float)width)
                    ->H(24)
                    ->Shrink(0)
                    ->FlexCol()
                    ->JustifyCenter()
                    ->SuppressTextSelection()
                    ->OnMouseDown(ListenTo(diff->state, &OnDiffGutterDown, arg))
                    ->OnMouseMove(ListenTo(diff->state, &OnDiffGutterMove, arg))
                    ->Child(TextEl(cx->a, fmt("%d", number))
                                ->Fg(theme.mutedFg)));
        }
        if (markerLane) {
            annotationLeft += 24.f;
            El* lane = Div(cx->a)
                           ->Id(StrL("marker-lane"))
                           ->W(24)
                           ->H(24)
                           ->Shrink(0)
                           ->FlexRow()
                           ->ItemsCenter()
                           ->JustifyCenter()
                           ->StopMouseDown();
            const DiffGutterMarker* marker =
                useRowMarks ? rowMarker : markerAt(side, line);
            if (marker) {
                El* child = diff->gutterMarkerContent(cx, marker,
                                                      diff->gutterMarkerUser);
                if (child) lane->Child(child);
            }
            box->Child(lane);
        }
        if (diff->changeIndicator == DiffChangeIndicator::Signs && changed) {
            box->Child(TextEl(cx->a, side == DiffSide::Original ? StrL("-")
                                                                : StrL("+"))
                           ->Fg(side == DiffSide::Original ? theme.danger
                                                           : theme.success)
                           ->Shrink(0));
        }
        if (diff->changeIndicator == DiffChangeIndicator::Bars && changed) {
            box->Child(Div(cx->a)->W(3)->H(24)->Shrink(0)->Bg(
                side == DiffSide::Original ? theme.danger : theme.success));
        }
        El* source = CodeText(cx, diff, state, row.file, file, side, line,
                              diff->syntaxHighlight &&
                                  len(SideOf(file, side)->lines[line].text) <=
                                      state->syntaxMaxLineLength,
                              changed && !selected);
        const auto& sourceLine = SideOf(file, side)->lines[line];
        if (WindowAccessibilityActive(cx->win)) {
            const char* description = !changed ? "Diff.SourceLine"
                                      : side == DiffSide::Original
                                          ? "Diff.RemovedLine"
                                          : "Diff.AddedLine";
            const TrArg args[] = {
                {"side", Tr(side == DiffSide::Original ? "Diff.Original"
                                                       : "Diff.Modified")},
                {"line", fmt("%d", number)},
            };
            source->Role(AccessibilityRole::Label)
                ->AriaValue(sourceLine.text)
                ->AriaDescription(
                    StrDup(cx->a, TrFmtTemp(description, args, 2)));
        }
        bool hover = diff->onTextHover != nullptr;
        bool click = diff->onLineClick || diff->onTextClick;
        DiffTextRun* run = ArenaNew<DiffTextRun>(cx->a);
        run->diff = diff;
        run->file = row.file;
        run->side = side;
        run->ix = line;
        run->scrollX = diff->softWrap ? 0 : state->horizontalScrollX;
        run->font = ThemeFontSize(cx->app);
        if (diff->hasTextCursor) source->Cursor(diff->textCursor);
        if (click) {
            source->OnClick(ListenTo(diff->state, &OnDiffTextClick,
                                     (int64_t)(intptr_t)run));
        }
        source->OnMouseMove(DiffTextMoveListener(diff->state, run));
        if (!diff->hasTextCursor) source->Cursor(CursorKind::IBeam);
        El* hoverBox = source;
        if (!diff->softWrap) {
            // The gutters stay inside their columns as the code moves.
            source->PathClick(StrL("text"))
                ->Shrink(0)
                ->PaintOffset(-state->horizontalScrollX, 0);
            El* clip = Div(cx->a)
                           ->FlexRow()
                           ->Flex1()
                           ->MinW(0)
                           ->H(kFill)
                           ->ItemsCenter()
                           ->ClipX()
                           ->Child(source);
            // A move past the line's end stays inside the code column.
            clip->OnMouseMove(DiffTextMoveListener(diff->state, run));
            hoverBox = clip;
            box->Child(clip);
        } else {
            box->Child(source);
        }
        hoverBox
            ->PathClick(side == DiffSide::Original ? StrL("original-code")
                                                   : StrL("modified-code"))
            ->SuppressTextSelection()
            ->OnMouseDown(
                ListenTo(diff->state, &OnDiffCodeDown, (int64_t)(intptr_t)run))
            ->OnLongPress(ListenTo(diff->state, &OnDiffCodeLongPress,
                                   (int64_t)(intptr_t)run));
        if (hover) {
            hoverBox->OnHover(ListenTo(diff->state, &OnDiffTextLeave,
                                       (int64_t)(intptr_t)run));
        }
        box->Child(DiffAnnotationButton(cx, diff, file, side, line,
                                        annotationLeft,
                                        selected ? &state->selected : nullptr));
        if (present && diff->annotationContent && diff->annotations) {
            for (int i = 0; i < diff->annotationCount; i++) {
                const DiffAnnotation& ann = diff->annotations[i];
                if (!ann.hasPosition || !StrEq(ann.path, file->path)) continue;
                if (split ? ann.position.side != side || ann.position
                                                                 .line != number
                          : SourceRow(state, ann.position) != ix)
                    continue;
                El* note =
                    diff->annotationContent(cx, &ann, diff->annotationUser);
                if (note) {
                    if (!block) {
                        block = Div(cx->a)->FlexCol()->Grow()->MinW(0)->Child(
                            box->FlexNone());
                    }
                    block->Child(note);
                }
            }
        }
        return block ? block : box;
    };
    if (split) {
        El* both = Div(cx->a)->FlexRow()->W(kFill);
        both->Child(cell(DiffSide::Original, row.original, row.changed,
                         row.original >= 0, nullptr, nullptr, false)
                        ->WFrac(diff->splitRatio)
                        ->FlexNone()
                        ->MinW(0)
                        ->ClipX());
        both->Child(cell(DiffSide::Modified, row.modified, row.changed,
                         row.modified >= 0, nullptr, nullptr, false)
                        ->WFrac(1.f - diff->splitRatio)
                        ->FlexNone()
                        ->MinW(0)
                        ->ClipX());
        return both;
    }
    DiffSide side = row.modified >= 0 ? DiffSide::Modified : DiffSide::Original;
    int line = row.modified >= 0 ? row.modified : row.original;
    // Unified rows of an unchanged line carry both sides. The original
    // side is asked first, matching the gutter order.
    const DiffDecoration* deco = nullptr;
    if (!lineSelected(side, line)) {
        if (row.original >= 0)
            deco = decorationAt(DiffSide::Original, row.original);
        if (!deco && row.modified >= 0)
            deco = decorationAt(DiffSide::Modified, row.modified);
    }
    const DiffGutterMarker* marker = nullptr;
    if (row.original >= 0) marker = markerAt(DiffSide::Original, row.original);
    if (!marker && row.modified >= 0)
        marker = markerAt(DiffSide::Modified, row.modified);
    return cell(side, line, row.changed, true, deco, marker, true)->W(kFill);
}

static uint32_t Act(const char* name) {
    return ActionOf(Str(name));
}

static int ActiveDiffSide(const DiffState* self, DiffSide* side) {
    if (self->hasSelection && self->hasCursor) {
        *side = self->cursor.side;
        return FileIx(self, self->cursor.path);
    }
    const DiffTextSelection& text = self->textSelection;
    if (text.hitText && text.anchor != text.cursor) {
        *side = text.side;
        return text.file;
    }
    if (!len(self->rows)) return -1;
    int top = IMin(IMax(ScrollTop(self), 0), len(self->rows) - 1);
    int file = self->rows[top].file;
    *side = self->files[file]->LinesCount(DiffSide::Modified)
                ? DiffSide::Modified
                : DiffSide::Original;
    return file;
}

static void OnDiffAction(DiffState* self, Ctx* cx, const ActionEvent* ev) {
    bool all = ev->action == input::SelectAll();
    bool extend = ev->action == Act("diff::ExtendUp") ||
                  ev->action == Act("diff::ExtendDown");
    bool up = ev->action == Act("diff::ScrollUp") ||
              ev->action == Act("diff::ExtendUp");
    bool down = ev->action == Act("diff::ScrollDown") ||
                ev->action == Act("diff::ExtendDown");
    if (all || up || down) {
        DiffSide side;
        int file = ActiveDiffSide(self, &side);
        if (file < 0 || file >= len(self->files)) return;
        const DiffFile* doc = self->files[file];
        int count = doc->LinesCount(side);
        if (!count) return;
        int index = self->hasCursor && self->cursor.side == side &&
                            StrEq(self->cursor.path, doc->Path())
                        ? doc->LineIndex(side, self->cursor.line)
                        : -1;
        index = all         ? count - 1
                : index < 0 ? 0
                            : IMin(IMax(index + (up ? -1 : 1), 0), count - 1);
        DiffLinePosition cursor = DiffLinePosition::New(
            doc->Path(), side, doc->LineNumber(side, index));
        DiffLinePosition anchor = cursor;
        if (all) {
            anchor.line = doc->LineNumber(side, 0);
        } else if (extend && self->hasAnchor &&
                   CanExtend(self, self->anchor, cursor)) {
            anchor = self->anchor;
        }
        DiffLineRange range = Between(anchor, cursor);
        bool changed = !self->hasSelection || !RangeEq(self->selected, range);
        TextSelection::Clear(cx->win, cx->app);
        self->SetSelectedLines(true, range, cx);
        self->anchor = anchor;
        self->cursor = cursor;
        DiffEvent event;
        event.hasRange = true;
        event.path = range.path;
        event.side = range.side;
        event.endSide = range.endSide;
        event.start = range.start;
        event.end = range.end;
        if (changed) {
            event.kind = DiffEventKind::SelectionChanged;
            Emit(self, cx, event);
        }
        event.kind = DiffEventKind::SelectionEnded;
        Emit(self, cx, event);
        if (!all) self->ScrollToLine(cursor, cx);
        return;
    }
    if (ev->action == input::Copy()) {
        Str text = self->SelectedText(cx->a);
        if (cx->win && len(text)) {
            ClipboardSetText(cx->win, text);
            return;
        }
    }
    const_cast<ActionEvent*>(ev)->propagate = true;
}

void DiffInitKeys() {
    static uint32_t generation = 0;
    if (generation == KeymapGeneration()) return;
    generation = KeymapGeneration();
    KeyBinding bindings[] = {
        {"up", Act("diff::ScrollUp"), "Diff"},
        {"down", Act("diff::ScrollDown"), "Diff"},
        {"shift-up", Act("diff::ExtendUp"), "Diff"},
        {"shift-down", Act("diff::ExtendDown"), "Diff"},
        {"secondary-c", input::Copy(), "Diff"},
        {"secondary-a", input::SelectAll(), "Diff"},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

Diff* Diff::New(Ctx* cx, Entity<DiffState> state) {
    Diff* d = ArenaNew<Diff>(cx->a);
    d->a = cx->a;
    d->cx = cx;
    d->state = state;
    return d;
}

Diff* Diff::LineNumber(bool v) {
    lineNumber = v;
    return this;
}
Diff* Diff::SyntaxHighlight(bool v) {
    syntaxHighlight = v;
    return this;
}
Diff* Diff::HeaderVisible(bool v) {
    headerVisible = v;
    return this;
}
Diff* Diff::HoverHighlight(DiffHoverHighlight v) {
    hoverHighlight = v;
    return this;
}
Diff* Diff::HunkSeparator(DiffHunkSeparator v) {
    hunkSeparator = v;
    return this;
}
Diff* Diff::ChangeIndicator(DiffChangeIndicator v) {
    changeIndicator = v;
    return this;
}
Diff* Diff::ChangeBackground(bool v) {
    changeBackground = v;
    return this;
}
Diff* Diff::SoftWrap(bool v) {
    softWrap = v;
    return this;
}
Diff* Diff::SplitRatio(float ratio) {
    splitRatio = isfinite(ratio) ? std::min(std::max(ratio, 0.1f), 0.9f) : 0.5f;
    return this;
}
Diff* Diff::Annotations(const DiffAnnotation* items, int count) {
    annotations = items;
    annotationCount = count;
    return this;
}
Diff* Diff::Decorations(const DiffDecoration* items, int count) {
    decorations = items;
    decorationCount = count;
    return this;
}
Diff* Diff::SpanHighlights(const DiffSpanHighlight* items, int count) {
    spanHighlightCount = items ? IMax(0, count) : 0;
    spanHighlights = nullptr;
    if (!spanHighlightCount) return this;
    spanHighlights = (const DiffSpanHighlight**)Alloc(
        a, count * (int)sizeof(*spanHighlights));
    for (int i = 0; i < count; i++) spanHighlights[i] = &items[i];
    qsort(spanHighlights, count, sizeof(*spanHighlights),
          [](const void* lhs, const void* rhs) {
              const auto* first = *(const DiffSpanHighlight* const*)lhs;
              const auto* second = *(const DiffSpanHighlight* const*)rhs;
              int position =
                  CompareDiffPositions(first->position, second->position);
              if (position != 0) return position;
              return (first > second) - (first < second);
          });
    return this;
}

Diff* Diff::GutterMarkers(const DiffGutterMarker* items, int count) {
    gutterMarkers = items;
    gutterMarkerCount = count;
    return this;
}
Diff* Diff::RenderGutterMarker(El* (*fn)(Ctx*, const DiffGutterMarker*, void*),
                               void* user) {
    gutterMarkerContent = fn;
    gutterMarkerUser = user;
    return this;
}
Diff* Diff::RenderAnnotation(El* (*fn)(Ctx*, const DiffAnnotation*, void*),
                             void* user) {
    annotationContent = fn;
    annotationUser = user;
    return this;
}
Diff* Diff::RenderHeader(El* (*fn)(Ctx*, const DiffFile*, void*), void* user) {
    header = fn;
    headerUser = user;
    return this;
}
Diff* Diff::RenderHeaderPrefix(El* (*fn)(Ctx*, const DiffFile*, void*),
                               void* user) {
    headerPrefix = fn;
    headerUser = user;
    return this;
}
Diff* Diff::RenderHeaderTitleSuffix(El* (*fn)(Ctx*, const DiffFile*, void*),
                                    void* user) {
    headerTitleSuffix = fn;
    headerUser = user;
    return this;
}
Diff* Diff::RenderHeaderSuffix(El* (*fn)(Ctx*, const DiffFile*, void*),
                               void* user) {
    headerSuffix = fn;
    headerUser = user;
    return this;
}
Diff* Diff::AnnotationContent(El* (*fn)(Ctx*, const DiffAnnotation*, void*),
                              void* user) {
    return RenderAnnotation(fn, user);
}
Diff* Diff::Header(El* (*fn)(Ctx*, const DiffFile*, void*), void* user) {
    return RenderHeader(fn, user);
}
Diff* Diff::HeaderPrefix(El* (*fn)(Ctx*, const DiffFile*, void*), void* user) {
    return RenderHeaderPrefix(fn, user);
}
Diff* Diff::HeaderTitleSuffix(El* (*fn)(Ctx*, const DiffFile*, void*),
                              void* user) {
    return RenderHeaderTitleSuffix(fn, user);
}
Diff* Diff::HeaderSuffix(El* (*fn)(Ctx*, const DiffFile*, void*), void* user) {
    return RenderHeaderSuffix(fn, user);
}
Diff* Diff::OnAddAnnotation(void (*fn)(Ctx*, const DiffLineRange*, void*),
                            void* user) {
    onAddAnnotation = fn;
    addUser = user;
    return this;
}
Diff* Diff::OnLineClick(void (*fn)(Ctx*, const DiffLinePosition*,
                                   const ClickEvent*, void*),
                        void* user) {
    onLineClick = fn;
    clickUser = user;
    return this;
}
Diff* Diff::OnLineHover(void (*fn)(Ctx*, const DiffLinePosition*, bool, void*),
                        void* user) {
    onLineHover = fn;
    hoverUser = user;
    return this;
}

Diff* Diff::OnTextHover(void (*fn)(Ctx*, const DiffTextPosition*, Modifiers,
                                   void*),
                        void* user) {
    onTextHover = fn;
    textHoverUser = user;
    return this;
}

Diff* Diff::OnTextClick(bool (*fn)(Ctx*, const DiffTextPosition*,
                                   const ClickEvent*, void*),
                        void* user) {
    onTextClick = fn;
    textClickUser = user;
    return this;
}

Diff* Diff::TextCursor(CursorKind cursor) {
    hasTextCursor = true;
    textCursor = cursor;
    return this;
}

int DiffDisplayCells(Str text) {
    int cells = 0;
    uint8_t state = 0;
    for (int end = len(text); end > 0;) {
        int start = Utf8Prev(text, end);
        uint32_t scalar = 0;
        if (Utf8At(text, start, &scalar) != end - start) scalar = 0xFFFD;
        end = start;
        // Ordinary ASCII is common in the diff. A pending state can still
        // affect an ASCII character, for example an emoji keycap digit.
        if (state == 0 && scalar >= 0x20 && scalar <= 0x7E) {
            cells++;
            continue;
        }
        int lo = 0;
        int hi = dimofi(kDiffWidthClassRanges);
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (kDiffWidthClassRanges[mid].hi < scalar)
                lo = mid + 1;
            else
                hi = mid;
        }
        uint8_t id = lo < dimofi(kDiffWidthClassRanges) &&
                             kDiffWidthClassRanges[lo].lo <= scalar
                         ? kDiffWidthClassRanges[lo].id
                         : 0;
        uint16_t transition = kDiffWidthTransitions[id][state];
        cells += (int8_t)(transition >> 8);
        state = (uint8_t)transition;
    }
    return cells;
}

static void MeasureDiffCode(Ctx* cx, DiffState* state, float font) {
    // Shape the widest display-cell candidates once per file set and font.
    struct Candidate {
        int cells;
        Str text;
    };
    Candidate candidates[8] = {};
    state->horizontalNumberDigits = 0;
    for (const DiffFile* file : state->files) {
        state->horizontalNumberDigits =
            IMax(state->horizontalNumberDigits, file->lineNumberDigits);
        for (const DiffFileSide* side : {&file->original, &file->modified}) {
            for (const DiffSourceLine& line : side->lines) {
                int count = DiffDisplayCells(line.display);
                for (int i = 0; i < 8; i++) {
                    if (candidates[i].text.s && count <= candidates[i].cells)
                        continue;
                    for (int j = 7; j > i; j--)
                        candidates[j] = candidates[j - 1];
                    candidates[i] = {count, line.display};
                    break;
                }
            }
        }
    }
    state->horizontalCodeWidth = 0;
    for (const auto& candidate : candidates) {
        if (!candidate.text.s) break;
        float width = MeasureText(&cx->win->paint, candidate.text, font, 0,
                                  false, kFontMono)
                          .w;
        state
            ->horizontalCodeWidth = std::max(state->horizontalCodeWidth, width);
    }
    state->horizontalCodeFont = font;
}

static void RecordDiffScrollRange(PaintCtx* paint, El* box, void* data) {
    auto* state = (DiffState*)data;
    state->selectionViewport = box->Bounds();
    WindowSelectionTouchSourcePainted(paint->window, state->self.id);
    if (len(state->rowHeights) && state->rowMeasureWidth != box->w) {
        AppInvalidate(paint->window);
    }
    float max = std::max(0.f, box->contentW - box->w);
    if (state->horizontalScrollX > max) {
        state->horizontalScrollX = max;
        AppInvalidate(paint->window);
    }
}

static bool PrepareDiffRowMeasurements(Ctx* cx, const Diff* diff,
                                       DiffState* state) {
    if (!diff->softWrap && diff->headerVisible &&
        (!diff->annotationContent || !diff->annotationCount)) {
        VecClear(state->rowHeights);
        VecClear(state->measuredAnnotations);
        return false;
    }
    int count = len(state->rows);
    float width = state->selectionViewport.w;
    float font = ThemeFontSize(cx->app);
    uint64_t style =
        (diff->softWrap ? 1 : 0) | (diff->headerVisible ? 2 : 0) |
        (diff->lineNumber ? 4 : 0) | (diff->syntaxHighlight ? 8 : 0) |
        (diff->gutterMarkerContent && diff->gutterMarkerCount ? 16 : 0) |
        ((uint64_t)diff->hunkSeparator << 5) |
        ((uint64_t)diff->changeIndicator << 8);
    if (count != len(state->rowHeights) || width != state->rowMeasureWidth ||
        font != state->rowMeasureFont || style != state->rowMeasureStyle ||
        diff->splitRatio != state->rowMeasureSplit) {
        if (!VecResize(state->rowHeights, count) ||
            !VecResize(state->rowNeedsMeasure, count))
            return false;
        for (int i = 0; i < count; i++) {
            bool hidden = !diff->headerVisible &&
                          state->rows[i].kind == DiffRowKind::File;
            state->rowHeights[i] = hidden ? 0.f : 24.f;
            state->rowNeedsMeasure[i] = hidden ? 0 : 1;
        }
        state->rowMeasureWidth = width;
        state->rowMeasureFont = font;
        state->rowMeasureStyle = style;
        state->rowMeasureSplit = diff->splitRatio;
    }
    // Re-evaluate annotations because their body may change height even
    // when the annotation and its line position remain the same.
    for (int row : state->measuredAnnotations) {
        if (row < count &&
            (diff->headerVisible || state->rows[row].kind != DiffRowKind::File))
            state->rowNeedsMeasure[row] = 1;
    }
    VecClear(state->measuredAnnotations);
    if (diff->annotationContent && diff->annotations) {
        for (int i = 0; i < diff->annotationCount; i++) {
            const DiffAnnotation& ann = diff->annotations[i];
            int file = FileIx(state, ann.path);
            if (file < 0) continue;
            int row = ann.hasPosition       ? SourceRow(state, ann.position)
                      : diff->headerVisible ? state->headerRows[file]
                                            : -1;
            if (row >= 0 && row < count) {
                state->rowNeedsMeasure[row] = 1;
                VecAppend(state->measuredAnnotations, row);
            }
        }
    }
    return true;
}

static void OnDiffHorizontalScroll(DiffState* state, Ctx* cx,
                                   const ScrollEvent* event) {
    if (state->horizontalScrollX == event->offsetX) return;
    state->horizontalScrollX = event->offsetX;
    Notify(cx);
}

El* Diff::IntoEl() {
    DiffInitKeys();
    DiffState* st = state.Get(cx);
    if (!st) return Div(a);
    ClearDiffCodeRuns(st);
    st->EnsureSyntax();
    st->EnsureInlinePresentation(true);
    st->listScroll.itemsCount = len(st->rows);
    // A column so the caller's height (the story's 32rem) reaches the list.
    // The list is height 100%, which collapses when its parent is only as
    // tall as that percentage.
    El* root = Div(a)
                   ->FlexCol()
                   ->SizeFull()
                   ->KeyContext(StrL("Diff"))
                   ->TrackFocus(st->focus)
                   ->OnMouseMove(ListenTo(state, &OnDiffSelectionMove))
                   ->OnDragMove(ListenTo(state, &OnDiffSelectionDrag))
                   ->OnMouseUp(ListenTo(state, &OnDiffSelectionUp))
                   ->OnMouseUpOut(ListenTo(state, &OnDiffSelectionUp))
                   ->OnMouseDownOut(ListenTo(state, &OnDiffSelectionOutside));
    root->prePaint = &RecordDiffScrollRange;
    root->customUser = st;
    root->OnAction(Act("diff::ScrollUp"), ListenTo(state, &OnDiffAction));
    root->OnAction(Act("diff::ScrollDown"), ListenTo(state, &OnDiffAction));
    root->OnAction(Act("diff::ExtendUp"), ListenTo(state, &OnDiffAction));
    root->OnAction(Act("diff::ExtendDown"), ListenTo(state, &OnDiffAction));
    root->OnAction(input::Copy(), ListenTo(state, &OnDiffAction));
    root->OnAction(input::SelectAll(), ListenTo(state, &OnDiffAction));
    // A key alone changes what the pointer is over: with it held, the word
    // beneath may be a link. Heard only while the diff has focus.
    if (onTextHover) {
        root->OnModifiersChanged(
            ListenTo(state, &OnDiffModifiersChanged, (int64_t)(intptr_t)this));
    }
    VirtualList* list = VirtualList::New(cx, len(st->rows))
                            ->RowH(24)
                            ->Handle(&st->listScroll)
                            ->Row(&RenderRow, this);
    if (PrepareDiffRowMeasurements(cx, this, st)) {
        list->Sizes(st->rowHeights.els)->MeasureRows(st->rowNeedsMeasure.els);
    }
    El* rows = list->IntoEl();
    El* body = ScrollBounce::New(cx, StrL("diff-bounce"),
                                 VirtualListScrollbar(&st->listScroll), rows)
                   ->IntoEl()
                   ->Flex1()
                   ->MinH(0)
                   ->H(kFill);
    if (!softWrap) {
        float font = ThemeFontSize(cx->app);
        if (cx->win && st->horizontalCodeFont != font)
            MeasureDiffCode(cx, st, font);
        float columnContent = st->horizontalCodeWidth + 16;
        if (lineNumber)
            columnContent += (float)(st->horizontalNumberDigits * 8 + 12);
        if (gutterMarkerContent && gutterMarkerCount > 0) columnContent += 24;
        if (changeIndicator == DiffChangeIndicator::Signs)
            columnContent += font;
        if (changeIndicator == DiffChangeIndicator::Bars) columnContent += 3;

        Scrollbar::Apply(
            cx, root, StrL("diff-horizontal"), 0, st->horizontalScrollX,
            ListenTo(state, &OnDiffHorizontalScroll), ScrollAxis::Horizontal);
        // The widest code line overflows the narrower column in split
        // mode and the full viewport in unified mode.
        float base = st->mode == DiffMode::Split
                         ? std::max(splitRatio, 1.f - splitRatio)
                         : 0.f;
        root->Child(Div(a)
                        ->Absolute()
                        ->Top(0)
                        ->LeftRel(base)
                        ->W(st->horizontalNumberDigits ? columnContent : 0)
                        ->H(1));
        // Pin the file headers and gutters to the viewport. Only each
        // source's paint offset follows horizontalScrollX.
        body->Absolute()->Inset0()->SizeFull();
    } else {
        st->horizontalScrollX = 0;
    }
    root->Child(body);
    return root;
}

} // namespace component
} // namespace gpui
