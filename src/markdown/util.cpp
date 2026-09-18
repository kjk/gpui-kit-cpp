/* src/util/ — the shared helpers. See util.h for the file-for-file map. */

#include "markdown/util.h"

namespace markdown {

using base::Alloc;

// ─── strings ─────────────────────────────────────────────────────────────

Str StrOwn(Arena* a, const char* s, int32_t len) {
    char* out = (char*)Alloc(a, len + 1);
    if (!out) {
        return {};
    }
    if (len > 0) {
        memcpy(out, s, (size_t)len);
    }
    out[len] = 0;
    return Str(out, len);
}

Str StrOwn(Arena* a, Str s) {
    return StrOwn(a, s.s, s.len);
}

// ─── util/char.rs ────────────────────────────────────────────────────────

int32_t Utf8Encode(char* out, uint32_t cp) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

// How many bytes the code point starting with `b` takes. A byte that starts
// nothing counts as one, which is where `from_utf8_lossy` puts U+FFFD.
static int32_t Utf8Len(uint8_t b) {
    if (b < 0x80) {
        return 1;
    }
    if ((b & 0xe0) == 0xc0) {
        return 2;
    }
    if ((b & 0xf0) == 0xe0) {
        return 3;
    }
    if ((b & 0xf8) == 0xf0) {
        return 4;
    }
    return 1;
}

// The code point at `index`, or U+FFFD when the bytes there are not one.
static int32_t Utf8Decode(Str bytes, int32_t index) {
    uint8_t b = (uint8_t)bytes.s[index];
    int32_t len = Utf8Len(b);
    if (len == 1) {
        return b < 0x80 ? (int32_t)b : 0xfffd;
    }
    if (index + len > bytes.len) {
        return 0xfffd;
    }
    uint32_t cp = (uint32_t)(b & (0xff >> (len + 1)));
    for (int32_t i = 1; i < len; i++) {
        uint8_t c = (uint8_t)bytes.s[index + i];
        if ((c & 0xc0) != 0x80) {
            return 0xfffd;
        }
        cp = (cp << 6) | (uint32_t)(c & 0x3f);
    }
    return (int32_t)cp;
}

int32_t CharAfterIndex(Str bytes, int32_t index) {
    if (index >= bytes.len) {
        return -1;
    }
    return Utf8Decode(bytes, index);
}

int32_t CharBeforeIndex(Str bytes, int32_t index) {
    if (index <= 0) {
        return -1;
    }
    // Back over the continuation bytes to where the last character starts,
    // which is what `from_utf8_lossy(&bytes[index - 4..index]).last()` finds.
    int32_t start = index - 1;
    int32_t limit = index < 4 ? 0 : index - 4;
    while (start > limit && ((uint8_t)bytes.s[start] & 0xc0) == 0x80) {
        start--;
    }
    return Utf8Decode(bytes, start);
}

// `char::is_whitespace`: the Unicode White_Space property.
static bool IsUnicodeWhitespace(uint32_t cp) {
    if (cp <= 0x20) {
        return cp == 0x20 || (cp >= 0x09 && cp <= 0x0d);
    }
    if (cp == 0x85 || cp == 0xa0 || cp == 0x1680) {
        return true;
    }
    if (cp >= 0x2000 && cp <= 0x200a) {
        return true;
    }
    return cp == 0x2028 || cp == 0x2029 || cp == 0x202f || cp == 0x205f ||
           cp == 0x3000;
}

CharKind Classify(int32_t cp) {
    if (cp < 0) {
        return CharKind::Whitespace;
    }
    if (IsUnicodeWhitespace((uint32_t)cp)) {
        return CharKind::Whitespace;
    }
    if ((cp < 0x80 && IsAsciiPunctuation((uint8_t)cp)) ||
        IsUnicodePunctuation((uint32_t)cp)) {
        return CharKind::Punctuation;
    }
    return CharKind::Other;
}

CharKind KindAfterIndex(Str bytes, int32_t index) {
    if (index == bytes.len) {
        return CharKind::Whitespace;
    }
    uint8_t byte = (uint8_t)bytes.s[index];
    if (IsAsciiWhitespace(byte)) {
        return CharKind::Whitespace;
    }
    if (IsAsciiPunctuation(byte)) {
        return CharKind::Punctuation;
    }
    if (IsAsciiAlphanumeric(byte)) {
        return CharKind::Other;
    }
    return Classify(CharAfterIndex(bytes, index));
}

// ─── util/slice.rs ───────────────────────────────────────────────────────

Position PositionFromExitEvent(const Vec<Event>& events, int32_t index) {
    Position pos;
    pos.end = events[index].point;
    Name name = events[index].name;
    int32_t i = index - 1;
    while (!(events[i].kind == Kind::Enter && events[i].name == name)) {
        i--;
    }
    pos.start = events[i].point;
    return pos;
}

Slice SliceFromPosition(Str bytes, const Position& position) {
    int32_t before = position.start.vs;
    int32_t after = position.end.vs;
    int32_t start = position.start.index;
    int32_t end = position.end.index;
    if (before > 0) {
        before = kTabSize - before;
        start += 1;
    }
    if (after > 0) {
        after -= 1;
        end += 1;
    }
    Slice slice;
    slice.bytes = Str(bytes.s + start, end - start);
    slice.before = before;
    slice.after = after;
    return slice;
}

Slice SliceFromIndices(Str bytes, int32_t start, int32_t end) {
    Slice slice;
    slice.bytes = Str(bytes.s + start, end - start);
    return slice;
}

Str SliceSerialize(Arena* a, const Slice& slice) {
    int32_t len = slice.Len();
    char* out = (char*)Alloc(a, len + 1);
    if (!out) {
        return {};
    }
    int32_t at = 0;
    for (int32_t i = 0; i < slice.before; i++) {
        out[at++] = ' ';
    }
    if (slice.bytes.len > 0) {
        memcpy(out + at, slice.bytes.s, (size_t)slice.bytes.len);
        at += slice.bytes.len;
    }
    for (int32_t i = 0; i < slice.after; i++) {
        out[at++] = ' ';
    }
    out[at] = 0;
    return Str(out, at);
}

// ─── util/edit_map.rs ────────────────────────────────────────────────────

// One of `consume`'s (at, removeAcc, addAcc) triples.
struct Jump {
    int32_t at;
    int32_t remove;
    int32_t add;
};

static void ShiftLinks(Vec<Event>& events, const Vec<Jump>& jumps) {
    int32_t jumpIndex = 0;
    int32_t index = 0;
    int32_t add = 0;
    int32_t rm = 0;
    while (index < len(events)) {
        int32_t rmCurr = rm;
        while (jumpIndex < len(jumps) && jumps[jumpIndex].at <= index) {
            add = jumps[jumpIndex].add;
            rm = jumps[jumpIndex].remove;
            jumpIndex++;
        }
        if (rm > rmCurr) {
            index += rm - rmCurr;
            continue;
        }
        if (events[index].hasLink && events[index].link.next != -1) {
            int32_t next = events[index].link.next;
            events[next].link.previous = index + add - rm;
            while (jumpIndex < len(jumps) && jumps[jumpIndex].at <= next) {
                add = jumps[jumpIndex].add;
                rm = jumps[jumpIndex].remove;
                jumpIndex++;
            }
            events[index].link.next = next + add - rm;
            index = next;
            continue;
        }
        index++;
    }
}

// The bucket `at` belongs in: the first one on its probe chain that is empty
// or already holds it. `buckets.len` is a power of two, so the mask is the
// modulo. The multiply-and-fold spreads indices that differ by 1 or 2 — which
// is most of what a resolver hands us — over the whole table instead of into
// one run. Every bit of the product has to reach the low ones: a table's map
// runs to six figures of entries, and a hash that only varies in 16 bits
// piles all of them onto one probe chain.
static int32_t BucketFor(const Vec<int32_t>& buckets,
                         const Vec<EditMap::Entry>& entries, int32_t at) {
    uint32_t h = (uint32_t)at * 2654435761u;
    h ^= h >> 15;
    int32_t mask = len(buckets) - 1;
    int32_t i = (int32_t)h & mask;
    while (buckets[i] != 0 && entries[buckets[i] - 1].at != at) {
        i = (i + 1) & mask;
    }
    return i;
}

// Grow to `wanted` buckets (a power of two) and re-place every entry.
static void RehashBuckets(EditMap& map, int32_t wanted) {
    VecReset(map.buckets);
    VecReserve(map.buckets, wanted);
    map.buckets.len = wanted;
    memset((void*)map.buckets.els, 0, (size_t)wanted * sizeof(int32_t));
    for (int32_t i = 0; i < map.map.len; i++) {
        map.buckets[BucketFor(map.buckets, map.map, map.map[i].at)] = i + 1;
    }
}

static void AddImpl(EditMap& map, int32_t at, int32_t remove, const Event* add,
                    int32_t addLen, bool before) {
    if (remove == 0 && addLen == 0) {
        return;
    }
    // Keep the load factor under 3/4, counting the entry this call may add.
    if ((len(map.map) + 1) * 4 >= len(map.buckets) * 3) {
        RehashBuckets(map, len(map.buckets) > 0 ? len(map.buckets) * 2 : 16);
    }
    int32_t bucket = BucketFor(map.buckets, map.map, at);
    if (map.buckets[bucket] != 0) {
        EditMap::Entry& e = map.map[map.buckets[bucket] - 1];
        e.remove += remove;
        if (before) {
            ArenaVec<Event> merged{};
            merged.Reserve(map.a, addLen + e.add.len);
            merged.AppendMany(map.a, add, addLen);
            for (const Event& ev : e.add) {
                merged.Append(map.a, ev);
            }
            e.add = merged;
        } else {
            e.add.AppendMany(map.a, add, addLen);
        }
        return;
    }
    EditMap::Entry e;
    e.at = at;
    e.remove = remove;
    e.add.AppendMany(map.a, add, addLen);
    VecAppend(map.map, e);
    map.buckets[bucket] = map.map.len;
}

void EditMapAdd(EditMap& map, int32_t index, int32_t remove, const Event* add,
                int32_t addLen) {
    AddImpl(map, index, remove, add, addLen, false);
}

void EditMapAddBefore(EditMap& map, int32_t index, int32_t remove,
                      const Event* add, int32_t addLen) {
    AddImpl(map, index, remove, add, addLen, true);
}

// sort_unstable_by(at), as a bottom-up merge sort. An insertion sort reads
// better and is what this was, but the map is not always short — a table adds
// an entry per cell and the resolver hands them over in an order that is
// nowhere near sorted, which made a document of tables quadratic in its cell
// count a second time over. Entries are trivially copyable, and no two share
// an `at` (`add` merges those), so this reaches the same order an unstable
// sort would.
static void SortEntries(Vec<EditMap::Entry>& entries) {
    int32_t n = len(entries);
    if (n < 2) {
        return;
    }
    Vec<EditMap::Entry> scratch;
    VecReserve(scratch, n);
    scratch.len = n;
    EditMap::Entry* src = entries.els;
    EditMap::Entry* dst = scratch.els;
    for (int32_t width = 1; width < n; width *= 2) {
        for (int32_t lo = 0; lo < n; lo += width * 2) {
            int32_t mid = lo + width < n ? lo + width : n;
            int32_t hi = lo + width * 2 < n ? lo + width * 2 : n;
            int32_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                dst[k++] = src[j].at < src[i].at ? src[j++] : src[i++];
            }
            while (i < mid) {
                dst[k++] = src[i++];
            }
            while (j < hi) {
                dst[k++] = src[j++];
            }
        }
        EditMap::Entry* t = src;
        src = dst;
        dst = t;
    }
    if (src != entries.els) {
        memcpy((void*)entries.els, (const void*)src,
               (size_t)n * sizeof(EditMap::Entry));
    }
}

void EditMapConsume(EditMap& map, Vec<Event>& events) {
    SortEntries(map.map);
    if (map.map.len == 0) {
        return;
    }

    // One jump per entry, and the entry count is already known.
    Vec<Jump> jumps;
    VecReserve(jumps, map.map.len);
    int32_t addAcc = 0;
    int32_t removeAcc = 0;
    for (int32_t index = 0; index < map.map.len; index++) {
        removeAcc += map.map[index].remove;
        addAcc += map.map[index].add.len;
        Jump j = {map.map[index].at, removeAcc, addAcc};
        VecAppend(jumps, j);
    }

    ShiftLinks(events, jumps);

    // Rust splits the list apart and puts it back together; this builds the
    // new one in order, which is the same list and one allocation.
    Vec<Event> out;
    VecReserve(out, len(events) + addAcc - removeAcc);
    int32_t index = 0;
    for (int32_t i = 0; i < map.map.len; i++) {
        const EditMap::Entry& e = map.map[i];
        for (int32_t j = index; j < e.at; j++) {
            VecAppend(out, events[j]);
        }
        for (const Event& ev : e.add) {
            VecAppend(out, ev);
        }
        index = e.at + e.remove;
    }
    for (int32_t j = index; j < len(events); j++) {
        VecAppend(out, events[j]);
    }

    VecReset(events);
    events.els = out.els;
    events.len = len(out);
    events.cap = out.cap;
    out.els = nullptr;
    out.len = 0;
    out.cap = 0;
    map.map.len = 0;
    // The sort above moved the entries the buckets point at, and the map is
    // empty now anyway.
    if (len(map.buckets) > 0) {
        memset((void*)map.buckets.els, 0,
               (size_t)len(map.buckets) * sizeof(int32_t));
    }
}

// ─── util/skip.rs ────────────────────────────────────────────────────────

static bool NamesContain(const Name* names, int32_t namesLen, Name name) {
    for (int32_t i = 0; i < namesLen; i++) {
        if (names[i] == name) {
            return true;
        }
    }
    return false;
}

static int32_t SkipToImpl(const Vec<Event>& events, int32_t index,
                          const Name* names, int32_t namesLen, bool forward) {
    while (index < len(events)) {
        if (NamesContain(names, namesLen, events[index].name)) {
            break;
        }
        index = forward ? index + 1 : index - 1;
    }
    return index;
}

static int32_t SkipOptImpl(const Vec<Event>& events, int32_t index,
                           const Name* names, int32_t namesLen, bool forward) {
    int32_t balance = 0;
    Kind open = forward ? Kind::Enter : Kind::Exit;
    while (index < len(events)) {
        Name current = events[index].name;
        if (!NamesContain(names, namesLen, current) || events[index]
                                                               .kind != open) {
            break;
        }
        index = forward ? index + 1 : index - 1;
        balance += 1;
        for (;;) {
            balance = events[index].kind == open ? balance + 1 : balance - 1;
            int32_t next =
                forward ? index + 1 : (index > 0 ? index - 1 : index);
            if (events[index].name == current && balance == 0) {
                index = next;
                break;
            }
            index = next;
        }
    }
    return index;
}

int32_t SkipOpt(const Vec<Event>& events, int32_t index, const Name* names,
                int32_t namesLen) {
    return SkipOptImpl(events, index, names, namesLen, true);
}

int32_t SkipOptBack(const Vec<Event>& events, int32_t index, const Name* names,
                    int32_t namesLen) {
    return SkipOptImpl(events, index, names, namesLen, false);
}

int32_t SkipTo(const Vec<Event>& events, int32_t index, const Name* names,
               int32_t namesLen) {
    return SkipToImpl(events, index, names, namesLen, true);
}

int32_t SkipToBack(const Vec<Event>& events, int32_t index, const Name* names,
                   int32_t namesLen) {
    return SkipToImpl(events, index, names, namesLen, false);
}

// ─── util/normalize_identifier.rs ────────────────────────────────────────

Str NormalizeIdentifier(Arena* a, Str value) {
    char* out = (char*)Alloc(a, value.len + 1);
    if (!out) {
        return {};
    }
    int32_t at = 0;
    bool inWhitespace = true;
    int32_t index = 0;
    int32_t start = 0;
    while (index < value.len) {
        char c = value.s[index];
        if (c == '\t' || c == '\n' || c == '\r' || c == ' ') {
            if (!inWhitespace) {
                memcpy(out + at, value.s + start, (size_t)(index - start));
                at += index - start;
                inWhitespace = true;
            }
        } else if (inWhitespace) {
            if (start != 0) {
                out[at++] = ' ';
            }
            start = index;
            inWhitespace = false;
        }
        index++;
    }
    if (!inWhitespace) {
        memcpy(out + at, value.s + start, (size_t)(value.len - start));
        at += value.len - start;
    }
    // `to_lowercase().to_uppercase()`, ASCII only. See util.h.
    for (int32_t i = 0; i < at; i++) {
        if (out[i] >= 'a' && out[i] <= 'z') {
            out[i] = (char)(out[i] - 32);
        }
    }
    out[at] = 0;
    return Str(out, at);
}

// ─── util/infer.rs ───────────────────────────────────────────────────────

bool ListLoose(const Vec<Event>& events, int32_t index, bool includeItems) {
    int32_t balance = 0;
    Name name = events[index].name;
    while (index < len(events)) {
        const Event& event = events[index];
        if (event.kind == Kind::Enter) {
            balance += 1;
            if (includeItems && balance == 2 && event.name == Name::ListItem &&
                ListItemLoose(events, index)) {
                return true;
            }
        } else {
            balance -= 1;
            if (balance == 1 && event.name == Name::BlankLineEnding) {
                bool atEmptyListItem = false;
                bool atEmptyBlockQuote = false;
                int32_t before = index - 2;
                if (events[before].name == Name::ListItem) {
                    before -= 1;
                    if (events[before].name == Name::SpaceOrTab) {
                        before -= 2;
                    }
                    if (events[before].name == Name::BlockQuote &&
                        events[before - 1].name == Name::BlockQuotePrefix) {
                        atEmptyBlockQuote = true;
                    } else if (events[before].name == Name::ListItemPrefix) {
                        atEmptyListItem = true;
                    }
                }
                if (!atEmptyListItem && !atEmptyBlockQuote) {
                    return true;
                }
            }
            if (balance == 0 && event.name == name) {
                break;
            }
        }
        index++;
    }
    return false;
}

bool ListItemLoose(const Vec<Event>& events, int32_t index) {
    int32_t balance = 0;
    while (index < len(events)) {
        const Event& event = events[index];
        if (event.kind == Kind::Enter) {
            balance += 1;
        } else {
            balance -= 1;
            if (balance == 1 && event.name == Name::BlankLineEnding) {
                bool atPrefix = false;
                int32_t before = index - 2;
                if (events[before].name == Name::SpaceOrTab) {
                    before -= 2;
                }
                if (events[before].name == Name::ListItemPrefix) {
                    atPrefix = true;
                }
                if (!atPrefix) {
                    return true;
                }
            }
            if (balance == 0 && event.name == Name::ListItem) {
                break;
            }
        }
        index++;
    }
    return false;
}

// One walk of the delimiter row, writing when there is somewhere to write
// and counting either way. `out` is none on the counting pass.
static int32_t ScanTableAlign(const Vec<Event>& events, int32_t index, Arena* a,
                              ArenaAlign out) {
    bool inDelimiterRow = false;
    int32_t count = 0;
    while (index < len(events)) {
        const Event& event = events[index];
        if (inDelimiterRow) {
            if (event.kind == Kind::Enter) {
                if (event.name == Name::GfmTableDelimiterCellValue) {
                    // A marker before the cell's text is a colon on the left.
                    AlignKind kind =
                        events[index + 1].name == Name::GfmTableDelimiterMarker
                            ? AlignKind::Left
                            : AlignKind::None;
                    if (out != kArenaAlignNone) {
                        ArenaAlignSet(a, out, count, kind);
                    }
                    count++;
                }
            } else if (event.name == Name::GfmTableDelimiterCellValue) {
                // And one after it is a colon on the right, which either
                // centres what the left colon started or stands alone.
                if (count > 0 &&
                    events[index - 1].name == Name::GfmTableDelimiterMarker) {
                    if (out != kArenaAlignNone) {
                        AlignKind was = ArenaAlignAt(a, out, count - 1);
                        ArenaAlignSet(a, out, count - 1,
                                      was == AlignKind::Left
                                          ? AlignKind::Center
                                          : AlignKind::Right);
                    }
                }
            } else if (event.name == Name::GfmTableDelimiterRow) {
                break;
            }
        } else if (event.kind == Kind::Enter &&
                   event.name == Name::GfmTableDelimiterRow) {
            inDelimiterRow = true;
        }
        index++;
    }
    return count;
}

ArenaAlign GfmTableAlign(const Vec<Event>& events, int32_t index, Arena* a) {
    int32_t count = ScanTableAlign(events, index, a, kArenaAlignNone);
    if (count <= 0) {
        return kArenaAlignNone;
    }
    ArenaAlign out = ArenaAlignNew(a, count);
    ScanTableAlign(events, index, a, out);
    return out;
}

// ─── util/character_reference.rs ─────────────────────────────────────────

int32_t CharacterReferenceValueMax(uint8_t marker) {
    if (marker == '&') {
        return kCharacterReferenceNamedSizeMax;
    }
    if (marker == 'x') {
        return kCharacterReferenceHexadecimalSizeMax;
    }
    return kCharacterReferenceDecimalSizeMax;
}

bool CharacterReferenceValueTest(uint8_t marker, uint8_t byte) {
    if (marker == '&') {
        return IsAsciiAlphanumeric(byte);
    }
    if (marker == 'x') {
        return IsAsciiHexDigit(byte);
    }
    return IsAsciiDigit(byte);
}

// The table entry for a name, or null. The value is a NUL-terminated run in
// `kCharacterReferenceValues`, which outlives every caller — so a caller that
// wants it in an arena copies it and one that does not need not.
static const char* NamedValue(Str name) {
    // The table is sorted by name, so this is a binary search where Rust
    // walks the 2125 entries with `find`.
    int32_t lo = 0;
    int32_t hi = 2125 - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        const char* candidate =
            kCharacterReferenceNames + kCharacterReferences[mid].nameOff;
        int cmp = StrCmp(Str(candidate), name);
        if (cmp < 0) {
            lo = mid + 1;
        } else if (cmp > 0) {
            hi = mid - 1;
        } else {
            return kCharacterReferenceValues + kCharacterReferences[mid]
                                                   .valueOff;
        }
    }
    return nullptr;
}

Str DecodeNamed(Arena* a, Str name) {
    const char* value = NamedValue(name);
    return value ? StrOwn(a, value, (int32_t)strlen(value)) : Str{};
}

// The codepoint a numeric reference names, already through the crate's own
// rules about what is not one.
static uint32_t DecodeNumericCp(Str value, int radix) {
    uint32_t cp = 0;
    bool overflow = false;
    for (int32_t i = 0; i < value.len; i++) {
        uint8_t c = (uint8_t)value.s[i];
        uint32_t digit;
        if (c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (uint32_t)(c - 'a' + 10);
        } else {
            digit = (uint32_t)(c - 'A' + 10);
        }
        if (cp > 0x10ffff) {
            overflow = true;
            break;
        }
        cp = cp * (uint32_t)radix + digit;
    }
    // `char::from_u32` rejects the surrogates and anything past U+10FFFF;
    // the control characters are the crate's own list.
    bool bad = overflow || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
               cp <= 0x08 || cp == 0x0b || (cp >= 0x0e && cp <= 0x1f) ||
               (cp >= 0x7f && cp <= 0x9f);
    return bad ? 0xfffd : cp;
}

Str DecodeNumeric(Arena* a, Str value, int radix) {
    char* out = (char*)a->Push(4, 1, false);
    int32_t n = Utf8Encode(out, DecodeNumericCp(value, radix));
    return Str(out, n);
}

// The two decoders without the copy: `DecodeNamed` finds a NUL-terminated
// run in the static table, and `DecodeNumeric` encodes one codepoint.
base::TempStr CharacterReferenceDecodeTemp(Str value, uint8_t marker) {
    if (marker == '#' || marker == 'x') {
        base::TempStr out = base::AllocStrTemp(4);
        uint32_t cp = DecodeNumericCp(value, marker == '#' ? 10 : 16);
        out.len = Utf8Encode(out.s, cp);
        return out;
    }
    const char* found = NamedValue(value);
    return found ? Str((char*)found, (int32_t)strlen(found)) : Str{};
}

Str CharacterReferenceDecode(Arena* a, Str value, uint8_t marker) {
    if (marker == '#') {
        return DecodeNumeric(a, value, 10);
    }
    if (marker == 'x') {
        return DecodeNumeric(a, value, 16);
    }
    return DecodeNamed(a, value);
}

} // namespace markdown
