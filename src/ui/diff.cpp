#include "base.h"
#include "ui/diff.h"

#include "base/input_keys.h"
#include "gpui/keymap.h"
#include "ui/button.h"
#include "ui/i18n.h"
#include "ui/icon.h"
#include "ui/syntax.h"
#include "ui/theme.h"
#include "ui/virtual_list.h"

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
    if (end > s.len) end = s.len;
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
    if (s.len > 0 && s.s[s.len - 1] == c) return Str(s.s, s.len - 1);
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

DiffState::~DiffState() {
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

DiffAnnotation DiffAnnotation::File(Str id, Str path) {
    DiffAnnotation a;
    a.id = id;
    a.path = path;
    return a;
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

int DiffFile::LineIndex(DiffSide side, int line) const {
    const Vec<DiffSourceLine>& lines = SideOf(this, side)->lines;
    int lo = 0;
    int hi = len(lines);
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (lines[mid].lineNumber < line)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < len(lines) && lines[lo].lineNumber == line) return lo;
    return -1;
}

int DiffFile::PatchIx(DiffSide side, int ix) const {
    return patchIxs[side == DiffSide::Original ? 0 : 1][ix];
}

Str DiffFile::TextForLines(Arena* a, DiffSide side, int start, int end) const {
    const Vec<DiffSourceLine>& lines = SideOf(this, side)->lines;
    int lo = 0;
    int hi = len(lines);
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (lines[mid].lineNumber < start)
            lo = mid + 1;
        else
            hi = mid;
    }
    int first = lo;
    lo = 0;
    hi = len(lines);
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (lines[mid].lineNumber <= end)
            lo = mid + 1;
        else
            hi = mid;
    }
    int last = lo;
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
    if (text.len <= kMax) {
        PushChunk(out, 0, text.len);
        return;
    }
    int start = 0;
    int end = 0;
    int i = 0;
    while (i < text.len) {
        int next = i + 1;
        unsigned char c = (unsigned char)text.s[i];
        if ((c & 0x80) == 0)
            next = i + 1;
        else if ((c & 0xe0) == 0xc0)
            next = i + 2;
        else if ((c & 0xf0) == 0xe0)
            next = i + 3;
        else if ((c & 0xf8) == 0xf0)
            next = i + 4;
        if (next > text.len) next = text.len;
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
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf8) == 0xf0) return 4;
    return 1;
}

static void FinishLine(DiffFile* file, DiffFileSide* side, Str source,
                       int number, int rangeStart, int rangeEnd) {
    Str raw = Slice(source, rangeStart, rangeEnd);
    Str content = raw;
    if (content.len > 0 && content.s[content.len - 1] == '\n')
        content = Str(content.s, content.len - 1);
    if (content.len > 0 && content.s[content.len - 1] == '\r')
        content = Str(content.s, content.len - 1);
    DiffSourceLine line;
    line.lineNumber = number;
    line.text = Slice(source, rangeStart, rangeStart + content.len);
    line.sourceStart = rangeStart;
    line.sourceEnd = rangeEnd;
    line.contentEnd = rangeStart + content.len;
    line.counterpart = -1;
    line.tabBegin = len(side->tabs);
    bool hasTab = false;
    for (int i = 0; i < content.len; i++) {
        if (content.s[i] == '\t') hasTab = true;
    }
    StrBuilder display;
    if (hasTab) {
        int column = 0;
        for (int i = 0; i < content.len;) {
            if (content.s[i] == '\t') {
                int spaces = 4 - column % 4;
                DiffDisplayTab tab;
                tab.sourceOffset = i;
                tab.displayOffset = display.len;
                tab.width = spaces;
                VecAppend(side->tabs, tab);
                for (int s = 0; s < spaces; s++) display.AppendChar(' ');
                column += spaces;
                i++;
            } else {
                int step = ColumnStep((unsigned char)content.s[i]);
                if (i + step > content.len) step = 1;
                display.Append(Slice(content, i, i + step));
                column += 1;
                i += step;
            }
        }
        line.display = Own(file->arena, Str(display.els, display.len));
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

static void Seal(DiffFile* file, DiffFileStatus status, StrBuilder* origSrc,
                 Vec<int>* origNum, Vec<int>* origStart, Vec<int>* origEnd,
                 StrBuilder* modSrc, Vec<int>* modNum, Vec<int>* modStart,
                 Vec<int>* modEnd) {
    file->status = status;
    file->original.source = Own(file->arena, Str(origSrc->els, origSrc->len));
    file->modified.source = Own(file->arena, Str(modSrc->els, modSrc->len));
    for (int i = 0; i < len(*origNum); i++) {
        FinishLine(file, &file->original, file->original.source, (*origNum)[i],
                   (*origStart)[i], (*origEnd)[i]);
    }
    for (int i = 0; i < len(*modNum); i++) {
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
        int at = source.len;
        source.Append(content);
        source.AppendChar('\n');
        VecAppend(number, lineNumber);
        VecAppend(start, at);
        VecAppend(end, source.len);
        return len(number) - 1;
    }
    void RemoveFinalNewline() {
        if (len(end) == 0) return;
        if (source.len > 0) source.len--;
        end[len(end) - 1]--;
        noNewline = true;
    }
};

static bool ParseU64(Str s, uint64_t* out) {
    if (s.len <= 0) return false;
    uint64_t v = 0;
    for (int i = 0; i < s.len; i++) {
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
    if (value.len < 2 || value.s[0] != '"') return -1;
    bool escaped = false;
    for (int i = 1; i < value.len; i++) {
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
        int need = 1;
        if ((c & 0x80) == 0)
            need = 1;
        else if ((c & 0xe0) == 0xc0)
            need = 2;
        else if ((c & 0xf0) == 0xe0)
            need = 3;
        else if ((c & 0xf8) == 0xf0)
            need = 4;
        else
            return false;
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
    if (value.len == 0 || value.s[0] != '"') {
        *out = Own(a, value);
        return true;
    }
    if (QuotedEnd(value) != value.len) {
        *error = Err(number, "invalid quoted file path");
        return false;
    }
    Vec<uint8_t> bytes;
    const uint8_t* in = (const uint8_t*)value.s + 1;
    int n = value.len - 2;
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
        return Own(a, Slice(path, n, path.len));
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
    if (value.len && value.s[0] == '"') {
        int end = QuotedEnd(value);
        if (end < 0) return false;
        if (!DecodePath(a, Slice(value, 0, end), 1, &oldPath, error))
            return false;
        Str rest = Slice(value, end, value.len);
        while (rest.len && rest.s[0] == ' ') rest = Slice(rest, 1, rest.len);
        if (!DecodePath(a, rest, 1, &newPath, error)) return false;
        prefixes = detect(oldPath, newPath);
    } else if (value.len && value.s[value.len - 1] == '"') {
        int start = -1;
        for (int i = 0; i + 1 < value.len; i++) {
            if (value.s[i] == ' ' && value.s[i + 1] == '"') {
                int q = QuotedEnd(Slice(value, i + 1, value.len));
                if (q == value.len - (i + 1)) start = i + 1;
            }
        }
        if (start < 0) return false;
        oldPath = Own(a, Slice(value, 0, start - 1));
        if (!DecodePath(a, Slice(value, start, value.len), 1, &newPath, error))
            return false;
        prefixes = detect(oldPath, newPath);
    } else {
        bool found = false;
        for (int p = 0; p < 7 && !found; p++) {
            int oldN = (int)strlen(kGitPrefixes[p].oldP);
            int newN = (int)strlen(kGitPrefixes[p].newP);
            for (int i = 0; i < value.len; i++) {
                if (value.s[i] != ' ') continue;
                Str oldS = Slice(value, 0, i);
                Str newS = Slice(value, i + 1, value.len);
                if (!Starts(oldS, kGitPrefixes[p].oldP) ||
                    !Starts(newS, kGitPrefixes[p].newP))
                    continue;
                Str path = Slice(oldS, oldN, oldS.len);
                if (path.len == 0) continue;
                if (newS.len == newN + path.len &&
                    memcmp(newS.s + newN, path.s, (size_t)path.len) == 0) {
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
                while (from < value.len) {
                    int hit = StrFind(Slice(value, from, value.len), mark);
                    if (hit < 0) break;
                    at = from + hit;
                    from = at + 1;
                }
                if (at < 0) continue;
                if (!Starts(value, kGitPrefixes[p].oldP)) continue;
                oldPath = Own(a, Slice(value, 0, at));
                newPath = Own(a, Slice(value, at + 1, value.len));
                prefixes = kGitPrefixes[p];
                found = true;
            }
        }
        if (!found) {
            int sp = StrFind(value, " ");
            if (sp < 0) return false;
            oldPath = Own(a, Slice(value, 0, sp));
            newPath = Own(a, Slice(value, sp + 1, value.len));
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
    bool hasPrefixes = false;
    const char* oldPrefix = "";
    const char* newPrefix = "";
    void Flush(Vec<int>* deleted, Vec<int>* added) {
        int n = IMax(len(*deleted), len(*added));
        for (int i = 0; i < n; i++) {
            DiffLinePair pair;
            pair.original = i < len(*deleted) ? (*deleted)[i] : -1;
            pair.modified = i < len(*added) ? (*added)[i] : -1;
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
    Str body = Slice(line, 3, line.len);
    int at = StrFind(body, " @@");
    if (at < 0) {
        *error = Err(number, "invalid hunk header");
        return false;
    }
    Str ranges = Slice(body, 0, at);
    Str fields[4];
    int nFields = 0;
    int i = 0;
    while (i < ranges.len && nFields < 4) {
        while (i < ranges.len && ranges.s[i] == ' ') i++;
        int b = i;
        while (i < ranges.len && ranges.s[i] != ' ') i++;
        if (b < i) fields[nFields++] = Slice(ranges, b, i);
    }
    if (nFields != 2 || fields[0].len < 1 || fields[0].s[0] != '-' ||
        fields[1].len < 1 || fields[1].s[0] != '+') {
        *error = Err(number, "invalid hunk header");
        return false;
    }
    auto range = [&](Str value, int* start, int* count) -> bool {
        int comma = StrFind(value, ",");
        Str sPart = comma < 0 ? value : Slice(value, 0, comma);
        Str cPart = comma < 0 ? StrL("1") : Slice(value, comma + 1, value.len);
        uint64_t s = 0;
        uint64_t c = 0;
        if (!ParseU64(sPart, &s) || !ParseU64(cPart, &c)) return false;
        if ((c > 0 && s == 0) || s > UINT64_MAX - c) return false;
        if (s > (uint64_t)INT_MAX || c > (uint64_t)INT_MAX ||
            s + c > (uint64_t)INT_MAX)
            return false;
        *start = (int)s;
        *count = (int)c;
        return true;
    };
    if (!range(Slice(fields[0], 1, fields[0].len), oldStart, oldCount) ||
        !range(Slice(fields[1], 1, fields[1].len), newStart, newCount)) {
        *error = Err(number, "invalid hunk header");
        return false;
    }
    return true;
}

static int LastNumber(const SideBuild& side) {
    if (len(side.number) == 0) return -1;
    return side.number[len(side.number) - 1];
}

static bool ParseHunk(OpenFile* open, Str* lines, int nLines, int* ix,
                      DiffParseError* error) {
    int oldStart = 0, oldCount = 0, newStart = 0, newCount = 0;
    if (!HunkHeader(lines[*ix], *ix + 1, &oldStart, &oldCount, &newStart,
                    &newCount, error))
        return false;
    int lastOld = LastNumber(open->original);
    int lastNew = LastNumber(open->modified);
    if ((lastOld >= 0 && lastOld >= oldStart && oldCount > 0) ||
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
    int oldUsed = 0;
    int newUsed = 0;
    Vec<int> deleted;
    Vec<int> added;
    int last = 0;
    while (*ix < nLines) {
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
        char tag = ' ';
        Str text = {};
        if (content.len == 0) {
            tag = ' ';
            text = {};
        } else {
            tag = content.s[0];
            text = Slice(content, 1, content.len);
        }
        if (tag != ' ' && tag != '+' && tag != '-') {
            *error = Err(*ix + 1, "hunk ended before its declared line counts");
            return false;
        }
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
    if (oldUsed != oldCount || newUsed != newCount) {
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
    int first = oldStart + (oldCount == 0 ? 1 : 0);
    hunk.originalLinesStart = first;
    hunk.originalLinesEnd = first + oldCount;
    VecAppend(open->file->hunks, hunk);
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
    Vec<Str> raw;
    int i = 0;
    if (patch.len == 0) {
        // An empty patch is no files. A patch of only a newline still splits.
    }
    while (i < patch.len || (patch.len == 0 && len(raw) == 0 && false)) {
        if (i >= patch.len) break;
        int b = i;
        while (i < patch.len && patch.s[i] != '\n') i++;
        int e = i;
        if (i < patch.len && patch.s[i] == '\n') i++;
        VecAppend(raw, Slice(patch, b, e));
        if (i == b) break;
    }
    if (patch.len == 0) {
        return true;
    }
    bool crlf = false;
    for (int k = 0; k < len(raw); k++) {
        Str line = raw[k];
        if (Starts(line, "diff --git ") || Starts(line, "--- ")) {
            crlf = line.len > 0 && line.s[line.len - 1] == '\r';
            break;
        }
    }
    if (crlf) {
        for (int k = 0; k < len(raw); k++) raw[k] = StripSuffix(raw[k], '\r');
    }
    OpenFile open;
    for (int ix = 0; ix < len(raw);) {
        Str line = raw[ix];
        if (Starts(line, "diff --cc ") || Starts(line, "diff --combined ") ||
            Starts(line, "@@@")) {
            if (open.file) delete open.file;
            for (int f = 0; f < len(*out); f++) delete (*out)[f];
            VecClear(*out);
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
            if (!GitPaths(open.file->arena, Slice(line, 11, line.len), &oldPath,
                          &newPath, &op, &np, &local)) {
                delete open.file;
                for (int f = 0; f < len(*out); f++) delete (*out)[f];
                VecClear(*out);
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
            if (!HeaderPath(open.file->arena, Slice(line, 4, line.len), ix + 1,
                            &oldPresent, &oldPath, error)) {
                delete open.file;
                for (int f = 0; f < len(*out); f++) delete (*out)[f];
                VecClear(*out);
                return false;
            }
            ix++;
            if (ix >= len(raw) || !Starts(raw[ix], "+++ ")) {
                delete open.file;
                for (int f = 0; f < len(*out); f++) delete (*out)[f];
                VecClear(*out);
                *error = Err(ix + 1, "expected modified file header");
                return false;
            }
            if (!HeaderPath(open.file->arena, Slice(raw[ix], 4, raw[ix].len),
                            ix + 1, &newPresent, &newPath, error)) {
                delete open.file;
                for (int f = 0; f < len(*out); f++) delete (*out)[f];
                VecClear(*out);
                return false;
            }
            if (!oldPresent && !newPresent) {
                delete open.file;
                for (int f = 0; f < len(*out); f++) delete (*out)[f];
                VecClear(*out);
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
                for (int f = 0; f < len(*out); f++) delete (*out)[f];
                VecClear(*out);
                *error = Err(ix + 1, "hunk has no file header");
                return false;
            }
            if (!open.headers) {
                delete open.file;
                for (int f = 0; f < len(*out); f++) delete (*out)[f];
                VecClear(*out);
                *error = Err(
                    ix + 1, "hunk requires original and modified file headers");
                return false;
            }
            if (!ParseHunk(&open, raw.els, len(raw), &ix, error)) {
                delete open.file;
                for (int f = 0; f < len(*out); f++) delete (*out)[f];
                VecClear(*out);
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
            delete open.file;
            for (int f = 0; f < len(*out); f++) delete (*out)[f];
            VecClear(*out);
            *error = Err(ix + 1, "source line outside a hunk");
            return false;
        }
        if (open.file) {
            if (Starts(line, "Binary files ") ||
                StrEq(line, "GIT binary patch"))
                open.file->binary = true;
            if (Starts(line, "rename from ")) {
                Str path;
                if (!DecodePath(open.file->arena, Slice(line, 12, line.len),
                                ix + 1, &path, error)) {
                    delete open.file;
                    for (int f = 0; f < len(*out); f++) delete (*out)[f];
                    VecClear(*out);
                    return false;
                }
                open.file->original.hasPath = true;
                open.file->original.path = path;
                open.renamed = true;
            }
            if (Starts(line, "copy from ")) {
                Str path;
                if (!DecodePath(open.file->arena, Slice(line, 10, line.len),
                                ix + 1, &path, error)) {
                    delete open.file;
                    for (int f = 0; f < len(*out); f++) delete (*out)[f];
                    VecClear(*out);
                    return false;
                }
                open.file->original.hasPath = true;
                open.file->original.path = path;
                open.copied = true;
            }
            if (Starts(line, "rename to ") || Starts(line, "copy to ")) {
                int skip = Starts(line, "rename to ") ? 10 : 8;
                Str path;
                if (!DecodePath(open.file->arena, Slice(line, skip, line.len),
                                ix + 1, &path, error)) {
                    delete open.file;
                    for (int f = 0; f < len(*out); f++) delete (*out)[f];
                    VecClear(*out);
                    return false;
                }
                open.file->modified.hasPath = true;
                open.file->modified.path = path;
            }
            if (Starts(line, "new file mode "))
                open.file->original.hasPath = false;
            if (Starts(line, "deleted file mode "))
                open.file->modified.hasPath = false;
            if (line.len > 0)
                VecAppend(open.file->extendedHeaders,
                          Own(open.file->arena, line));
        }
        ix++;
    }
    if (open.file) FinishOpen(&open, out);
    bool blank = true;
    for (int k = 0; k < patch.len; k++) {
        char c = patch.s[k];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            blank = false;
            break;
        }
    }
    if (len(*out) == 0 && !blank) {
        *error = Err(1, "expected a unified or Git diff");
        return false;
    }
    return true;
}

static void LineRanges(Str text, Vec<int>* start, Vec<int>* end) {
    if (text.len == 0) return;
    int i = 0;
    while (i < text.len) {
        int b = i;
        while (i < text.len && text.s[i] != '\n') i++;
        if (i < text.len && text.s[i] == '\n') i++;
        VecAppend(*start, b);
        VecAppend(*end, i);
        if (i == b) break;
    }
}

DiffFile* DiffFile::Unchanged(Str path, Str text) {
    DiffFile* file = AllocFile();
    file->status = DiffFileStatus::Unchanged;
    file->modified.hasPath = true;
    file->modified.path = Own(file->arena, path);
    file->original.hasPath = true;
    file->original.path = file->modified.path;
    file->path = file->modified.path;
    file->modified.source = Own(file->arena, text);
    Vec<int> starts;
    Vec<int> ends;
    LineRanges(text, &starts, &ends);
    for (int i = 0; i < len(starts); i++) {
        FinishLine(file, &file->modified, file->modified.source, i + 1,
                   starts[i], ends[i]);
        DiffLinePair pair;
        pair.original = -1;
        pair.modified = i;
        pair.changed = false;
        VecAppend(file->pairs, pair);
    }
    file->lineNumberDigits = Digits(
        len(file->modified.lines)
            ? file->modified.lines[len(file->modified.lines) - 1].lineNumber
            : 1);
    PatchOrder(file);
    return file;
}

static bool MarkerLabel(Str content, const char* prefix, Str* label) {
    int n = (int)strlen(prefix);
    if (!StrStartsWith(content, prefix)) return false;
    Str rest = Slice(content, n, content.len);
    if (rest.len && rest.s[0] != ' ') return false;
    while (rest.len && (rest.s[0] == ' ' || rest.s[0] == '\t'))
        rest = Slice(rest, 1, rest.len);
    int end = rest.len;
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
    for (int i = 0; i < text.len; i++) {
        if (text.s[i] == '\n') lineCount++;
    }
    if (text.len && text.s[text.len - 1] != '\n') lineCount++;
    for (int ix = 0; ix < len(lineStart); ix++) {
        Str line = Slice(text, lineStart[ix], lineEnd[ix]);
        Str content = line;
        if (content.len && content.s[content.len - 1] == '\n')
            content = Str(content.s, content.len - 1);
        if (content.len && content.s[content.len - 1] == '\r')
            content = Str(content.s, content.len - 1);
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
                conflict.hasBase = true;
                conflict.baseStart = next;
                conflict.baseEnd = next;
                conflict.baseLabel = Own(file->arena, baseLabel);
                part = DiffConflictPart::Base;
                continue;
            }
            if ((part == DiffConflictPart::Current ||
                 part == DiffConflictPart::Base) &&
                separator) {
                if (part == DiffConflictPart::Current)
                    conflict.currentEnd = next;
                else
                    conflict.baseEnd = next;
                conflict.incomingStart = next;
                conflict.incomingEnd = next;
                part = DiffConflictPart::Incoming;
                continue;
            }
            if (part == DiffConflictPart::Incoming && closing) {
                conflict.incomingEnd = next;
                conflict.incomingLabel = Own(file->arena, closeLabel);
                VecAppend(file->conflicts, conflict);
                open = false;
                continue;
            }
        }
        int at = source.len;
        source.Append(line);
        VecAppend(numbers, ix + 1);
        VecAppend(starts, at);
        VecAppend(ends, source.len);
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
    file->modified.source = Own(file->arena, Str(source.els, source.len));
    for (int i = 0; i < len(numbers); i++) {
        FinishLine(file, &file->modified, file->modified.source, numbers[i],
                   starts[i], ends[i]);
    }
    file->lineNumberDigits = Digits(
        len(file->modified.lines)
            ? file->modified.lines[len(file->modified.lines) - 1].lineNumber
            : 1);
    PatchOrder(file);
    return file;
}

static Str DetectedLanguage(Str path) {
    int base = 0;
    for (int i = 0; i < path.len; i++) {
        if (path.s[i] == '/' || path.s[i] == '\\') base = i + 1;
    }
    Str name = Slice(path, base, path.len);
    StrBuilder lower;
    for (int i = 0; i < name.len; i++) {
        char c = name.s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        lower.AppendChar(c);
    }
    Str n = Str(lower.els, lower.len);
    const char* language = "text";
    if (StrEq(n, "makefile") || StrEq(n, "gnumakefile"))
        language = "make";
    else if (StrEq(n, "cmakelists.txt"))
        language = "cmake";
    else {
        int dot = -1;
        for (int i = 0; i < n.len; i++)
            if (n.s[i] == '.') dot = i;
        if (dot >= 0) {
            Str ext = Slice(n, dot + 1, n.len);
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
    while (i < text.len) {
        unsigned char c = (unsigned char)text.s[i];
        int step = ColumnStep(c);
        if (i + step > text.len) step = 1;
        bool space = step == 1 && IsSpace(text.s[i]);
        bool word = !space && (step > 1 || (c >= '0' && c <= '9') ||
                               (c >= 'A' && c <= 'Z') ||
                               (c >= 'a' && c <= 'z') || c == '_');
        int b = i;
        if (space) {
            while (i < text.len && IsSpace(text.s[i])) i++;
        } else if (word) {
            while (i < text.len) {
                unsigned char d = (unsigned char)text.s[i];
                int s = ColumnStep(d);
                if (i + s > text.len) s = 1;
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

static void CharTokens(Str text, Vec<Tok>* out) {
    int i = 0;
    while (i < text.len) {
        int step = ColumnStep((unsigned char)text.s[i]);
        if (i + step > text.len) step = 1;
        Tok t;
        t.start = i;
        t.end = i + step;
        VecAppend(*out, t);
        i += step;
    }
}

static void PushRun(Vec<DiffRun>* runs, int start, int end, Str text,
                    bool mergeSpace) {
    if (len(*runs)) {
        DiffRun& last = (*runs)[len(*runs) - 1];
        Str gap = Slice(text, last.end, start);
        bool blank = gap.len == 0;
        if (!blank && mergeSpace) {
            blank = true;
            for (int i = 0; i < gap.len; i++) {
                if (!IsSpace(gap.s[i])) blank = false;
            }
        }
        if (blank) {
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
    Vec<Tok> a;
    Vec<Tok> b;
    if (unit == DiffInlineUnit::Word) {
        WordTokens(oldText, &a);
        WordTokens(newText, &b);
    } else {
        CharTokens(oldText, &a);
        CharTokens(newText, &b);
    }
    int n = len(a);
    int m = len(b);
    // Short lines only. A longer line is left unemphasized, the same as the
    // length cap one step above this.
    if (n > 400 || m > 400) return;
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
    int equalBytes = 0;
    for (int k = 0; k < prefix; k++) equalBytes += a[k].end - a[k].start;
    for (int k = 0; k < suffix; k++)
        equalBytes += a[n - suffix + k].end - a[n - suffix + k].start;
    int nn = n - prefix - suffix;
    int mm = m - prefix - suffix;
    for (int k = 0; k < nn; k++) a[k] = a[prefix + k];
    for (int k = 0; k < mm; k++) b[k] = b[prefix + k];
    VecResize(a, nn);
    VecResize(b, mm);
    n = nn;
    m = mm;
    Vec<int> dp;
    VecResize(dp, (n + 1) * (m + 1));
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
            Op op;
            op.kind = 0;
            op.start = a[i].start;
            op.end = a[i].end;
            VecAppend(ops, op);
            equalBytes += a[i].end - a[i].start;
            i++;
            j++;
        } else if (dp[i * (m + 1) + j] == dp[(i + 1) * (m + 1) + j]) {
            Op op;
            op.kind = 1;
            op.start = a[i].start;
            op.end = a[i].end;
            VecAppend(ops, op);
            i++;
        } else {
            Op op;
            op.kind = 2;
            op.start = b[j].start;
            op.end = b[j].end;
            VecAppend(ops, op);
            j++;
        }
    }
    while (i < n) {
        Op op;
        op.kind = 1;
        op.start = a[i].start;
        op.end = a[i].end;
        VecAppend(ops, op);
        i++;
    }
    while (j < m) {
        Op op;
        op.kind = 2;
        op.start = b[j].start;
        op.end = b[j].end;
        VecAppend(ops, op);
        j++;
    }
    int total = oldText.len + newText.len;
    float ratio = total == 0 ? 1.f : (2.f * (float)equalBytes) / (float)total;
    if (ratio < 0.25f) return;
    bool merge = unit == DiffInlineUnit::Word;
    for (int k = 0; k < len(ops); k++) {
        if (ops[k].kind == 1)
            PushRun(oldRuns, ops[k].start, ops[k].end, oldText, merge);
        else if (ops[k].kind == 2)
            PushRun(newRuns, ops[k].start, ops[k].end, newText, merge);
    }
}

void DiffFile::PrepareInline(DiffInlineUnit unit, int maxLineLength) {
    VecReset(inlineRuns[0]);
    VecReset(inlineRuns[1]);
    VecReset(inlineStarts[0]);
    VecReset(inlineStarts[1]);
    VecResize(inlineStarts[0], len(original.lines) + 1);
    VecResize(inlineStarts[1], len(modified.lines) + 1);
    inlineStarts[0][0] = 0;
    inlineStarts[1][0] = 0;
    Vec<Vec<DiffRun>*> scratch;
    // Per-line runs collected then flattened. A side Vec of runs is built
    // one line at a time so nothing nests a Vec inside a Vec element.
    Vec<DiffRun> oldLine;
    Vec<DiffRun> newLine;
    Vec<int> oldCount;
    Vec<int> newCount;
    VecResize(oldCount, len(original.lines));
    VecResize(newCount, len(modified.lines));
    Vec<DiffRun> oldAll;
    Vec<DiffRun> newAll;
    for (int p = 0; p < len(pairs); p++) {
        if (!pairs[p].changed || pairs[p].original < 0 || pairs[p].modified < 0)
            continue;
        Str oldText = original.lines[pairs[p].original].text;
        Str newText = modified.lines[pairs[p].modified].text;
        if (oldText.len > maxLineLength || newText.len > maxLineLength)
            continue;
        if (oldText.len == newText.len &&
            memcmp(oldText.s ? oldText.s : "", newText.s ? newText.s : "",
                   (size_t)oldText.len) == 0)
            continue;
        VecClear(oldLine);
        VecClear(newLine);
        DiffChangedRuns(oldText, newText, unit, &oldLine, &newLine);
        for (int i = 0; i < len(oldLine); i++) VecAppend(oldAll, oldLine[i]);
        for (int i = 0; i < len(newLine); i++) VecAppend(newAll, newLine[i]);
        oldCount[pairs[p].original] = len(oldLine);
        newCount[pairs[p].modified] = len(newLine);
    }
    int at = 0;
    for (int i = 0; i < len(original.lines); i++) {
        inlineStarts[0][i] = at;
        at += oldCount[i];
    }
    inlineStarts[0][len(original.lines)] = at;
    at = 0;
    for (int i = 0; i < len(modified.lines); i++) {
        inlineStarts[1][i] = at;
        at += newCount[i];
    }
    inlineStarts[1][len(modified.lines)] = at;
    // The counts were recorded in pair order, not line order, so rebuild
    // from a second pass that appends in line order.
    VecClear(oldAll);
    VecClear(newAll);
    Vec<DiffRun>* bags[2] = {&oldAll, &newAll};
    (void)bags;
    Vec<DiffRun> perOld;
    Vec<DiffRun> perNew;
    // Store runs in line order.
    for (int line = 0; line < len(original.lines); line++) {
        inlineStarts[0][line] = len(inlineRuns[0]);
        for (int p = 0; p < len(pairs); p++) {
            if (pairs[p].original != line || pairs[p].modified < 0 ||
                !pairs[p].changed)
                continue;
            Str oldText = original.lines[line].text;
            Str newText = modified.lines[pairs[p].modified].text;
            if (oldText.len > maxLineLength || newText.len > maxLineLength)
                break;
            VecClear(perOld);
            VecClear(perNew);
            DiffChangedRuns(oldText, newText, unit, &perOld, &perNew);
            for (int i = 0; i < len(perOld); i++)
                VecAppend(inlineRuns[0], perOld[i]);
            break;
        }
    }
    inlineStarts[0][len(original.lines)] = len(inlineRuns[0]);
    for (int line = 0; line < len(modified.lines); line++) {
        inlineStarts[1][line] = len(inlineRuns[1]);
        for (int p = 0; p < len(pairs); p++) {
            if (pairs[p].modified != line || pairs[p].original < 0 ||
                !pairs[p].changed)
                continue;
            Str oldText = original.lines[pairs[p].original].text;
            Str newText = modified.lines[line].text;
            if (oldText.len > maxLineLength || newText.len > maxLineLength)
                break;
            VecClear(perOld);
            VecClear(perNew);
            DiffChangedRuns(oldText, newText, unit, &perOld, &perNew);
            for (int i = 0; i < len(perNew); i++)
                VecAppend(inlineRuns[1], perNew[i]);
            break;
        }
    }
    inlineStarts[1][len(modified.lines)] = len(inlineRuns[1]);
    (void)oldCount;
    (void)newCount;
    (void)oldAll;
    (void)newAll;
    (void)scratch;
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
    if (line.text.len != peer.text.len) return false;
    if (line.text.len &&
        memcmp(line.text.s, peer.text.s, (size_t)line.text.len) != 0)
        return false;
    Str a = Slice(SideOf(file, side)->source, line.contentEnd, line.sourceEnd);
    Str b = Slice(SideOf(file, other)->source, peer.contentEnd, peer.sourceEnd);
    if (a.len != b.len) return true;
    return a.len && memcmp(a.s, b.s, (size_t)a.len) != 0;
}

// ─── Rows ──────────────────────────────────────────────────────────────────

static void VisiblePairs(const DiffFile* file, bool hasContext, int context,
                         int minCollapsed, const DiffState* state,
                         Vec<char>* visible) {
    int n = len(file->pairs);
    VecClear(*visible);
    VecResize(*visible, n);
    Vec<int> hunkEnd;
    Vec<int> hunkStart;
    VecResize(hunkEnd, n);
    VecResize(hunkStart, n);
    for (int i = 0; i < n; i++) {
        hunkEnd[i] = n;
        hunkStart[i] = 0;
    }
    for (int h = 0; h < len(file->hunks); h++) {
        for (int i = file->hunks[h].pairsStart;
             i < file->hunks[h].pairsEnd && i < n; i++) {
            hunkStart[i] = file->hunks[h].pairsStart;
            hunkEnd[i] = file->hunks[h].pairsEnd;
        }
    }
    Vec<int> boundaries;
    VecResize(boundaries, n + 1);
    for (int i = 0; i <= n; i++) boundaries[i] = 0;
    if (!hasContext) {
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
            while (ix < n && ix < hunkEnd[start] && file->pairs[ix].changed)
                ix++;
            int from = start - context;
            if (from < hunkStart[start]) from = hunkStart[start];
            if (from < 0) from = 0;
            int to = ix + context;
            if (to > hunkEnd[start]) to = hunkEnd[start];
            boundaries[from] += 1;
            boundaries[to] -= 1;
        }
    }
    for (int i = 0; i < len(state->expanded); i++) {
        if (state->expanded[i].file != -1) {
            // Filled by the caller with file already filtered: file field
            // is the pair range's owner, matched below.
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
        while (ix < n && ix < hunkEnd[start] && !(*visible)[ix]) ix++;
        if (ix - start < minCollapsed) {
            for (int k = start; k < ix; k++) (*visible)[k] = 1;
        }
    }
}

static void ProjectFile(DiffState* state, int fileIx) {
    const DiffFile* file = state->files[fileIx];
    int n = len(file->pairs);
    Vec<char> visible;
    VisiblePairs(file, state->hasContext, state->contextLines,
                 state->minCollapsedLines, state, &visible);
    // Recompute with expansions folded into the same boundary pass.
    {
        Vec<int> hunkEnd;
        Vec<int> hunkStart;
        VecResize(hunkEnd, n);
        VecResize(hunkStart, n);
        for (int i = 0; i < n; i++) {
            hunkEnd[i] = n;
            hunkStart[i] = 0;
        }
        for (int h = 0; h < len(file->hunks); h++) {
            for (int i = file->hunks[h].pairsStart;
                 i < file->hunks[h].pairsEnd && i < n; i++) {
                hunkStart[i] = file->hunks[h].pairsStart;
                hunkEnd[i] = file->hunks[h].pairsEnd;
            }
        }
        Vec<int> boundaries;
        VecResize(boundaries, n + 1);
        for (int i = 0; i <= n; i++) boundaries[i] = 0;
        if (!state->hasContext) {
            if (n >= 0) {
                boundaries[0] += 1;
                boundaries[n] -= 1;
            }
        } else {
            int ix = 0;
            while (ix < n) {
                if (!file->pairs[ix].changed) {
                    ix++;
                    continue;
                }
                int start = ix;
                while (ix < n && ix < hunkEnd[start] && file->pairs[ix].changed)
                    ix++;
                int from = start - state->contextLines;
                if (from < hunkStart[start]) from = hunkStart[start];
                if (from < 0) from = 0;
                int to = ix + state->contextLines;
                if (to > hunkEnd[start]) to = hunkEnd[start];
                boundaries[from] += 1;
                boundaries[to] -= 1;
            }
        }
        for (int i = 0; i < len(state->expanded); i++) {
            if (state->expanded[i].file != fileIx) continue;
            int start = IMin(state->expanded[i].start, n);
            int end = IMin(state->expanded[i].end, n);
            if (start < 0) start = 0;
            if (start < end) {
                boundaries[start] += 1;
                boundaries[end] -= 1;
            }
        }
        int active = 0;
        for (int i = 0; i < n; i++) {
            active += boundaries[i];
            visible[i] = active > 0 ? 1 : 0;
        }
        int ix = 0;
        while (ix < n) {
            if (visible[ix]) {
                ix++;
                continue;
            }
            int start = ix;
            while (ix < n && ix < hunkEnd[start] && !visible[ix]) ix++;
            if (ix - start < state->minCollapsedLines) {
                for (int k = start; k < ix; k++) visible[k] = 1;
            }
        }
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
                while (ix2 < n && ix2 < hunkEnd[start] &&
                       file->pairs[ix2].changed)
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
    VecResize(visible, n);
    for (int i = 0; i < n; i++) visible[i] = 0;
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
            while (ix < n && file->pairs[ix].changed) ix++;
            int from = start - state->contextLines;
            if (from < 0) from = 0;
            int to = ix + state->contextLines;
            if (to > n) to = n;
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
        visible[i] = active > 0 ? 1 : 0;
    }
    int ix = 0;
    while (ix < n) {
        if (visible[ix]) {
            ix++;
            continue;
        }
        int start = ix;
        while (ix < n && !visible[ix]) ix++;
        if (ix - start < state->minCollapsedLines) {
            for (int k = start; k < ix; k++) visible[k] = 1;
        }
    }
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
    ix = 0;
    while (ix < n) {
        if (next < len(file->conflicts) && file->conflicts[next]
                                                   .LinesStart() == ix) {
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
            ix = conflict.LinesEnd();
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

static void Rebuild(DiffState* state, bool reset) {
    int anchor = state->scrollItem;
    DiffLinePosition kept;
    bool hasKept = false;
    if (!reset && anchor >= 0 && anchor < len(state->rows)) {
        // Best-effort: keep the scroll index. Rust restores the source line.
    }
    VecClear(state->rows);
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
    if (reset) state->scrollItem = 0;
    state->listScroll.itemsCount = len(state->rows);
    (void)hasKept;
    (void)kept;
    (void)anchor;
}

void DiffState::EnsurePresentation() {
    if (!inlineOn) {
        for (int i = 0; i < len(files); i++) {
            VecClear(files[i]->inlineRuns[0]);
            VecClear(files[i]->inlineRuns[1]);
            VecClear(files[i]->inlineStarts[0]);
            VecClear(files[i]->inlineStarts[1]);
        }
        return;
    }
    for (int i = 0; i < len(files); i++)
        files[i]->PrepareInline(inlineUnit, inlineMaxLineLength);
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
    for (int i = 0; i < len(files); i++) delete files[i];
    VecClear(files);
    for (int i = 0; i < count; i++) VecAppend(files, next[i]);
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
    Rebuild(this, true);
    EnsurePresentation();
    if (cx) Notify(cx);
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
    inlineMaxLineLength = length < 0 ? 0 : length;
    return this;
}

DiffState* DiffState::WithSyntaxMaxLineLength(int length) {
    syntaxMaxLineLength = length < 0 ? 0 : length;
    return this;
}

DiffFile** DiffState::Files(int* count) const {
    if (count) *count = len(files);
    return files.els;
}

DiffState* DiffState::WithInlineUnit(bool on, DiffInlineUnit unit) {
    inlineOn = on;
    inlineUnit = unit;
    EnsurePresentation();
    return this;
}

bool DiffState::IsFileCollapsed(Str path) const {
    return Collapsed(this, path);
}

void DiffState::SetMode(DiffMode next, Ctx* cx) {
    if (mode == next) return;
    mode = next;
    Rebuild(this, false);
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

void DiffState::SetFileCollapsed(Str path, bool collapse, Ctx* cx) {
    int file = FileIx(this, path);
    if (file < 0) return;
    bool was = Collapsed(this, files[file]->path);
    if (collapse == was) return;
    if (collapse)
        VecAppend(collapsed, StrDup(files[file]->path));
    else {
        for (int i = 0; i < len(collapsed); i++) {
            if (StrEq(collapsed[i], files[file]->path)) {
                StrFree(collapsed[i]);
                VecRemoveAt(collapsed, i);
                break;
            }
        }
    }
    Rebuild(this, false);
    if (cx) Notify(cx);
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
        for (int line = ix; line < conflict.LinesStart() && line < len(lines);
             line++) {
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
        ix = conflict.LinesEnd();
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
        int lo = 0;
        int hi = len(lines);
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (lines[mid].lineNumber < line)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo >= len(lines)) return -1;
        return lines[lo].lineNumber;
    };
    auto lastAt = [&](DiffSide side, int line) -> int {
        const Vec<DiffSourceLine>& lines = SideOf(file, side)->lines;
        int lo = 0;
        int hi = len(lines);
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (lines[mid].lineNumber <= line)
                lo = mid + 1;
            else
                hi = mid;
        }
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
        if (text.len && text.els[text.len - 1] != '\n') text.AppendChar('\n');
        const DiffSourceLine& line = SideOf(file, side)->lines[sideIx];
        text.Append(Slice(SideOf(file, side)->source, line.sourceStart,
                          line.sourceEnd));
    }
    return Own(a, Str(text.els, text.len));
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

void DiffState::ScrollToLine(DiffLinePosition position, Ctx* cx) {
    if (FileIx(this, position.path) < 0) return;
    if (files[FileIx(this, position.path)]
            ->LineIndex(position.side, position.line) < 0)
        return;
    if (Collapsed(this, position.path)) {
        for (int i = 0; i < len(collapsed); i++) {
            if (StrEq(collapsed[i], position.path)) {
                StrFree(collapsed[i]);
                VecRemoveAt(collapsed, i);
                break;
            }
        }
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

static void MoveChange(DiffState* self, bool forward, Ctx* cx) {
    int top = ScrollTop(self);
    int n = len(self->changeRows);
    if (n == 0) return;
    int target = -1;
    if (forward) {
        int i = 0;
        while (i < n && self->changeRows[i] <= top) i++;
        target = i < n ? self->changeRows[i] : self->changeRows[0];
    } else {
        int i = 0;
        while (i < n && self->changeRows[i] < top) i++;
        target = i > 0 ? self->changeRows[i - 1] : self->changeRows[n - 1];
    }
    self->scrollItem = target;
    VirtualListScrollToItemDeferred(&self->listScroll, target,
                                    ScrollStrategy::Top);
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

static TempStr TrNum(const char* key, int count) {
    Str t = Tr(key);
    int at = StrFind(t, "%{count}");
    if (at < 0) return fmt("%s", t);
    return fmt("%s%d%s", Str(t.s, at), count,
               Str(t.s + at + 8, t.len - at - 8));
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

static El* CodeText(Ctx* cx, const DiffFile* file, DiffSide side, int ix,
                    bool syntax) {
    const DiffSourceLine& line = SideOf(file, side)->lines[ix];
    El* row = Div(cx->a)->FlexRow();
    if (!syntax) {
        row->Child(TextEl(cx->a, line.display)->Mono());
        return row;
    }
    SyntaxLang lang = SyntaxLangFor(file->Language());
    if (lang == SyntaxLangNone) {
        row->Child(TextEl(cx->a, line.display)->Mono());
        return row;
    }
    SyntaxLexer lx;
    SyntaxLexStart(&lx, lang, line.display);
    Rgba fg = ActiveTheme(cx->app).foreground;
    while (SyntaxLexNext(&lx)) {
        El* bit = TextEl(cx->a, lx.text)->Mono();
        if (lx.tok != SyntaxTok::Text)
            bit->Fg(SyntaxTokColor(lx.tok, ActiveTheme(cx->app).mode, fg));
        row->Child(bit);
    }
    return row;
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
                ->Child(TextEl(cx->a, TrNum("Diff.HiddenLines", hidden))
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
                ->Label(TrNum("Diff.UnchangedLines", count))
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
        head->Child(TextEl(cx->a, label.len ? label : Tr(key)));
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
    auto cell = [&](DiffSide side, int line, bool changed, bool present) {
        El* box = Div(cx->a)->FlexRow()->H(24)->Grow()->ItemsCenter();
        if (!present) {
            box->Bg(Tint(theme.muted, 30));
            return box;
        }
        Rgba bg = changed ? (side == DiffSide::Original ? theme.danger
                                                        : theme.success)
                          : theme.background;
        if (changed && diff->changeBackground) box->Bg(Tint(bg, 30));
        if (diff->lineNumber) {
            int number = SideOf(file, side)->lines[line].lineNumber;
            int width = file->lineNumberDigits * 8 + 12;
            box->Child(Div(cx->a)
                           ->W((float)width)
                           ->Child(TextEl(cx->a, fmt("%d", number))
                                       ->Fg(theme.mutedFg)));
        }
        if (diff->changeIndicator == DiffChangeIndicator::Signs && changed) {
            box->Child(TextEl(cx->a, side == DiffSide::Original ? StrL("-")
                                                                : StrL("+"))
                           ->Fg(side == DiffSide::Original ? theme.danger
                                                           : theme.success));
        }
        if (diff->changeIndicator == DiffChangeIndicator::Bars && changed) {
            box->Child(Div(cx->a)->W(3)->H(24)->Bg(
                side == DiffSide::Original ? theme.danger : theme.success));
        }
        box->Child(CodeText(
            cx, file, side, line,
            diff->syntaxHighlight && SideOf(file, side)->lines[line].text.len <=
                                         state->syntaxMaxLineLength));
        // Annotation content shares the uniform row. Rust remeasures the row
        // when the comment's height changes; see port-status.md.
        if (present && diff->annotationContent && diff->annotations) {
            int number = SideOf(file, side)->lines[line].lineNumber;
            for (int i = 0; i < diff->annotationCount; i++) {
                const DiffAnnotation& ann = diff->annotations[i];
                if (!ann.hasPosition || !StrEq(ann.path, file->path)) continue;
                if (ann.position.side != side || ann.position.line != number)
                    continue;
                El* note =
                    diff->annotationContent(cx, &ann, diff->annotationUser);
                if (note) box->Child(note);
            }
        }
        return box;
    };
    if (split) {
        El* both = Div(cx->a)->FlexRow()->W(kFill);
        both->Child(cell(DiffSide::Original, row.original, row.changed,
                         row.original >= 0));
        both->Child(cell(DiffSide::Modified, row.modified, row.changed,
                         row.modified >= 0));
        return both;
    }
    DiffSide side = row.modified >= 0 ? DiffSide::Modified : DiffSide::Original;
    int line = row.modified >= 0 ? row.modified : row.original;
    return cell(side, line, row.changed, true)->W(kFill);
}

static uint32_t Act(const char* name) {
    return ActionOf(Str(name));
}

static void OnDiffAction(DiffState* self, Ctx* cx, const ActionEvent* ev) {
    int dir = 0;
    if (ev->action == Act("diff::ScrollDown") ||
        ev->action == Act("diff::ExtendDown"))
        dir = 1;
    if (ev->action == Act("diff::ScrollUp") ||
        ev->action == Act("diff::ExtendUp"))
        dir = -1;
    if (dir != 0) {
        int next = self->scrollItem + dir;
        if (next < 0) next = 0;
        if (next >= len(self->rows)) next = len(self->rows) - 1;
        self->scrollItem = next;
        VirtualListScrollToItemDeferred(&self->listScroll, next,
                                        ScrollStrategy::Top);
        Notify(cx);
        return;
    }
    if (ev->action == input::Copy()) {
        Str text = self->SelectedText(cx->a);
        if (cx->win && text.len) ClipboardSetText(cx->win, text);
        return;
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
Diff* Diff::Annotations(const DiffAnnotation* items, int count) {
    annotations = items;
    annotationCount = count;
    return this;
}
Diff* Diff::AnnotationContent(El* (*fn)(Ctx*, const DiffAnnotation*, void*),
                              void* user) {
    annotationContent = fn;
    annotationUser = user;
    return this;
}
Diff* Diff::Header(El* (*fn)(Ctx*, const DiffFile*, void*), void* user) {
    header = fn;
    headerUser = user;
    return this;
}
Diff* Diff::HeaderPrefix(El* (*fn)(Ctx*, const DiffFile*, void*), void* user) {
    headerPrefix = fn;
    headerUser = user;
    return this;
}
Diff* Diff::HeaderTitleSuffix(El* (*fn)(Ctx*, const DiffFile*, void*),
                              void* user) {
    headerTitleSuffix = fn;
    headerUser = user;
    return this;
}
Diff* Diff::HeaderSuffix(El* (*fn)(Ctx*, const DiffFile*, void*), void* user) {
    headerSuffix = fn;
    headerUser = user;
    return this;
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

El* Diff::IntoEl() {
    DiffInitKeys();
    DiffState* st = state.Get(cx);
    if (!st) return Div(a);
    st->listScroll.itemsCount = len(st->rows);
    El* root =
        Div(a)->SizeFull()->KeyContext(StrL("Diff"))->TrackFocus(st->focus);
    root->OnAction(Act("diff::ScrollUp"), ListenTo(state, &OnDiffAction));
    root->OnAction(Act("diff::ScrollDown"), ListenTo(state, &OnDiffAction));
    root->OnAction(Act("diff::ExtendUp"), ListenTo(state, &OnDiffAction));
    root->OnAction(Act("diff::ExtendDown"), ListenTo(state, &OnDiffAction));
    root->OnAction(input::Copy(), ListenTo(state, &OnDiffAction));
    root->Child(VirtualList::New(cx, len(st->rows))
                    ->RowH(24)
                    ->Handle(&st->listScroll)
                    ->Row(&RenderRow, this)
                    ->IntoEl());
    return root;
}

} // namespace component
} // namespace gpui
