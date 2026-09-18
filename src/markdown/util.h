/* src/util/ — the helpers the constructs, the resolvers and to_mdast share.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md).

   | Rust                          | here                        |
   | ----------------------------- | --------------------------- |
   | `util/char.rs`                | CharKind, Classify, …       |
   | `util/slice.rs`               | Position, Slice             |
   | `util/edit_map.rs`            | EditMap                     |
   | `util/skip.rs`                | SkipOpt, SkipTo, …          |
   | `util/normalize_identifier.rs`| NormalizeIdentifier         |
   | `util/infer.rs`               | ListLoose, GfmTableAlign, … |
   | `util/character_reference.rs` | DecodeNamed, DecodeNumeric  |
   | `util/unicode.rs`             | IsUnicodePunctuation        | */

#ifndef GPUI_MARKDOWN_UTIL_H_
#define GPUI_MARKDOWN_UTIL_H_

#include "markdown/constant.h"
#include "markdown/event.h"
#include "markdown/mdast.h"

namespace markdown {

using base::Arena;
using base::ArenaVec;

// ─── strings ─────────────────────────────────────────────────────────────

// A copy of `s` in `a`. Unlike StrDup this never returns a null pointer, so
// an empty string still reads as Rust's `Some("")` rather than `None`.
Str StrOwn(Arena* a, Str s);
Str StrOwn(Arena* a, const char* s, int32_t len);

// ─── util/char.rs ────────────────────────────────────────────────────────

enum class CharKind : uint8_t {
    Whitespace,
    Punctuation,
    Other,
};

// util/unicode.rs. Defined in unicode.cpp, which is generated.
bool IsUnicodePunctuation(uint32_t cp);

// The code point ending at / starting at `index`, or -1 at either edge —
// Rust's `Option<char>` from `before_index` / `after_index`.
int32_t CharBeforeIndex(Str bytes, int32_t index);
int32_t CharAfterIndex(Str bytes, int32_t index);

// `classify_opt`: -1 (end of file) is whitespace.
CharKind Classify(int32_t cp);
CharKind KindAfterIndex(Str bytes, int32_t index);

inline bool IsAsciiWhitespace(uint8_t b) {
    return b == ' ' || b == '\t' || b == '\n' || b == '\r' || b == 0x0c;
}
inline bool IsAsciiPunctuation(uint8_t b) {
    return (b >= '!' && b <= '/') || (b >= ':' && b <= '@') ||
           (b >= '[' && b <= '`') || (b >= '{' && b <= '~');
}
inline bool IsAsciiDigit(uint8_t b) {
    return b >= '0' && b <= '9';
}
inline bool IsAsciiHexDigit(uint8_t b) {
    return IsAsciiDigit(b) || (b >= 'a' && b <= 'f') || (b >= 'A' && b <= 'F');
}
inline bool IsAsciiAlpha(uint8_t b) {
    return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z');
}
inline bool IsAsciiAlphanumeric(uint8_t b) {
    return IsAsciiAlpha(b) || IsAsciiDigit(b);
}
inline bool IsAsciiControl(uint8_t b) {
    return b < 0x20 || b == 0x7f;
}

// One UTF-8 code point into `out`. Returns how many bytes it took.
int32_t Utf8Encode(char* out, uint32_t cp);

// ─── util/slice.rs ───────────────────────────────────────────────────────

// The span of an event pair.
struct Position {
    Point start = {};
    Point end = {};
};

// `Position::from_exit_event`: walks back to the matching enter.
Position PositionFromExitEvent(const Vec<Event>& events, int32_t index);

// A stretch of the source, plus the spaces a half-consumed tab stands in for
// at either end.
struct Slice {
    Str bytes = {};
    int32_t before = 0;
    int32_t after = 0;

    int32_t Len() const { return len(bytes) + before + after; }
};

Slice SliceFromPosition(Str bytes, const Position& position);
Slice SliceFromIndices(Str bytes, int32_t start, int32_t end);
// The slice with its virtual spaces spelled out. Allocated from `a`.
Str SliceSerialize(Arena* a, const Slice& slice);

// ─── util/edit_map.rs ────────────────────────────────────────────────────

// Changes to a list of events, collected and applied in one pass so the
// indices a resolver works with stay meaningful while it works.
struct EditMap {
    struct Entry {
        int32_t at = 0;
        int32_t remove = 0;
        ArenaVec<Event> add{};
    };

    // The added events live here rather than in a Vec of Vecs: `Vec<T>` is
    // memcpy-only, and an ArenaVec is POD.
    Arena* a = nullptr;
    Vec<Entry> map;

    // Rust's `add` walks the entries it already holds looking for one at the
    // same index. That is linear per call and a table adds one entry per
    // cell, which makes a document of tables quadratic in its cell count.
    // This is that walk, replaced by an open-addressed table from `at` to
    // `map` index + 1 (0 is an empty bucket). `buckets.len` is a power of two
    // and always > `map.len`. It changes nothing about the entries or the
    // events they produce; it only finds an existing one faster.
    Vec<int32_t> buckets;
};

void EditMapAdd(EditMap& map, int32_t index, int32_t remove, const Event* add,
                int32_t addLen);
void EditMapAddBefore(EditMap& map, int32_t index, int32_t remove,
                      const Event* add, int32_t addLen);
inline bool EditMapEmpty(const EditMap& map) {
    return map.map.len == 0;
}
void EditMapConsume(EditMap& map, Vec<Event>& events);

// ─── util/skip.rs ────────────────────────────────────────────────────────

// The names each of these takes are a small set, passed as a pointer and a
// count rather than Rust's slice literal.
int32_t SkipOpt(const Vec<Event>& events, int32_t index, const Name* names,
                int32_t namesLen);
int32_t SkipOptBack(const Vec<Event>& events, int32_t index, const Name* names,
                    int32_t namesLen);
int32_t SkipTo(const Vec<Event>& events, int32_t index, const Name* names,
               int32_t namesLen);
int32_t SkipToBack(const Vec<Event>& events, int32_t index, const Name* names,
                   int32_t namesLen);

// ─── util/normalize_identifier.rs ────────────────────────────────────────

// Collapses whitespace and case-folds, so `[A  B]` and `[a b]` are the same
// definition.
//
// Rust folds with `to_lowercase().to_uppercase()`, which is all of Unicode.
// This folds ASCII only: a label whose letters are not ASCII matches when its
// bytes match, and not across a case difference. See the readme.
Str NormalizeIdentifier(Arena* a, Str value);

// ─── util/infer.rs ───────────────────────────────────────────────────────

bool ListLoose(const Vec<Event>& events, int32_t index, bool includeItems);
bool ListItemLoose(const Vec<Event>& events, int32_t index);
// The delimiter row read twice: once to count the columns, once to fill in
// the one allocation that holds them. Nothing in between allocates, so the
// second walk is over the same events with the answers already known.
ArenaAlign GfmTableAlign(const Vec<Event>& events, int32_t index, Arena* a);

// ─── util/character_reference.rs ─────────────────────────────────────────

// The marker is `&` (named), `#` (decimal) or `x` (hexadecimal).
int32_t CharacterReferenceValueMax(uint8_t marker);
bool CharacterReferenceValueTest(uint8_t marker, uint8_t byte);
// Null when a named reference is not in the table.
Str CharacterReferenceDecode(Arena* a, Str value, uint8_t marker);

// The same, without an arena. Neither half of it needs one: a named
// reference's text is a slice of the static table, and a numeric one is at
// most the four bytes `buf` holds. The arena version copies that into the
// arena, which is waste for a caller that is only going to copy it somewhere
// else — and worse than waste in `to_mdast`, where an allocation between a
// node's value and the text being appended to it is what stops the value
// growing in place.
//
// The answer points either into the static table or into `buf`, so it lives
// as long as the caller's own frame.
base::TempStr CharacterReferenceDecodeTemp(Str value, uint8_t marker);

} // namespace markdown

#endif // GPUI_MARKDOWN_UTIL_H_
