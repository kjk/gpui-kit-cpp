/* Not a port — `ArenaStr` is this tree's, not markdown-rs's.

   Four bytes: where the string starts in the arena's position space. The
   length lives with the characters, varint-encoded ahead of them, so a
   string under 128 long carries it in one byte. A quarter of what a `Str`
   costs in the holder, which is what an mdast Node holding eight of them is
   for.

   `ArenaPtr<T>` is the same trade for a pointer: four bytes of offset into
   the same position space. */

#include "Test.h"

static void WhatGoesInComesOut() {
    Arena* a = ArenaNew();
    ArenaStr s = ArenaStrDup(a, StrL("hello"));
    utassert(ArenaStrIsSet(s));
    utassert(ArenaStrLen(a, s) == 5);
    utassert(base::StrEq(ArenaStrGet(a, s), StrL("hello")));

    // NUL-terminated the way StrDup's is, so a caller that needs a C string
    // has one without copying again.
    Str got = ArenaStrGet(a, s);
    utassert(got.s[len(got)] == 0);

    // Several, none of them disturbing the others.
    ArenaStr one = ArenaStrDup(a, StrL("one"));
    ArenaStr two = ArenaStrDup(a, StrL("two"));
    ArenaStr three = ArenaStrDup(a, StrL(""));
    utassert(base::StrEq(ArenaStrGet(a, one), StrL("one")));
    utassert(base::StrEq(ArenaStrGet(a, two), StrL("two")));
    // Empty is nothing stored at all, which is what a Str with a null s means.
    utassert(!ArenaStrIsSet(three));
    utassert(ArenaStrGet(a, three).s == nullptr);
    utassert(!ArenaStrIsSet(ArenaStrDup(a, Str{})));
    ArenaDelete(a);
}

// The offset is into the whole chain's position space, not into one block, so
// a string keeps its identity after the arena has grown onto another block.
static void ItSurvivesTheArenaChainingOn() {
    Arena* a = ArenaNew();
    ArenaStr first = ArenaStrDup(a, StrL("first"));
    uint64_t before = ArenaUsed(a);

    // Past the default reserve, which forces a new block.
    for (int i = 0; i < 4096; i++) {
        Alloc(a, 4096);
    }
    utassert(ArenaUsed(a) > before);

    ArenaStr last = ArenaStrDup(a, StrL("last"));
    // The early one is still readable, and so is the late one — which is the
    // whole point of the position space over a per-block offset.
    utassert(base::StrEq(ArenaStrGet(a, first), StrL("first")));
    utassert(base::StrEq(ArenaStrGet(a, last), StrL("last")));
    ArenaDelete(a);
}

// A slice of something the arena already holds does not need copying again.
// The length is one byte ahead of the characters for anything short, so a
// string costs one byte of arena more than its characters and terminator —
// and four bytes less everywhere it is held, which is the trade.
static void TheLengthRidesAlongInOneByte() {
    Arena* a = ArenaNew();
    uint64_t before = ArenaUsed(a);
    ArenaStr s = ArenaStrDup(a, StrL("hello"));
    utassert(ArenaUsed(a) == before + 1 + 5 + 1);
    utassert(ArenaStrLen(a, s) == 5);

    // 127 is the last length that fits in one byte, 128 the first that needs
    // two. Both read back as themselves, which is the boundary the decode
    // gets wrong if the continuation bit is.
    TempStr buf = AllocStrTemp(300);
    for (int i = 0; i < len(buf); i++) {
        buf.s[i] = (char)('a' + (i % 26));
    }
    for (int len = 126; len <= 130; len++) {
        Arena* b = ArenaNew();
        uint64_t was = ArenaUsed(b);
        ArenaStr big = ArenaStrDup(b, Str(buf.s, len));
        utassert(ArenaStrLen(b, big) == (uint32_t)len);
        utassert(base::StrEq(ArenaStrGet(b, big), Str(buf.s, len)));
        int want = len < 128 ? 1 : 2;
        utassert(ArenaUsed(b) == was + (uint64_t)want + len + 1);
        ArenaDelete(b);
    }
    ArenaDelete(a);
}

// Growing past 127 characters is where the prefix needs a second byte and
// the characters shift over to make room. The string it names does not move.
static void GrowingPastTheOneByteLength() {
    Arena* a = ArenaNew();
    TempStr buf = AllocStrTemp(200);
    for (int i = 0; i < len(buf); i++) {
        buf.s[i] = (char)('a' + (i % 26));
    }
    ArenaStr s = ArenaStrDup(a, Str(buf.s, 120));
    ArenaStr was = s;
    uint64_t after = ArenaUsed(a);

    // 120 -> 130: one byte for the ten characters' worth of prefix growth,
    // ten for the characters.
    s = ArenaStrAppend(a, s, Str(buf.s + 120, 10));
    utassert(s == was);
    utassert(ArenaStrLen(a, s) == 130);
    utassert(base::StrEq(ArenaStrGet(a, s), Str(buf.s, 130)));
    utassert(ArenaUsed(a) == after + 1 + 10);

    Str got = ArenaStrGet(a, s);
    utassert(got.s[len(got)] == 0);
    ArenaDelete(a);
}

static void TheWordIsAnOffsetAndNothingElse() {
    Arena* a = ArenaNew();
    ArenaStr s = ArenaStrDup(a, StrL("abcd"));
    // The whole word is where the string starts; the length is in the arena
    // with the characters, not in the holder.
    utassert(s > 0);
    utassert(ArenaStrGet(a, s).s == (char*)ArenaAtOffset(a, s) + 1);
    // The size is the whole point, so it is checked at compile time. Four
    // bytes on every target, against a Str's pointer-plus-length: sixteen on
    // the 64-bit hosts, eight on wasm32, and smaller than either.
    static_assert(sizeof(ArenaStr) == 4, "ArenaStr is an offset");
    static_assert(sizeof(ArenaStr) < sizeof(Str), "and smaller than a Str");
    ArenaDelete(a);
}

// Growing the newest string in an arena costs only the bytes appended. The
// check is that it costs that and not more, since the whole point is the
// copies it does not make.
static void AppendingToTheNewestCostsOnlyTheBytes() {
    Arena* a = ArenaNew();
    ArenaStr s = ArenaStrDup(a, StrL("one"));
    uint64_t after = ArenaUsed(a);

    s = ArenaStrAppend(a, s, StrL(" two"));
    utassert(base::StrEq(ArenaStrGet(a, s), StrL("one two")));
    // Four characters more, and nothing else: no second copy of "one".
    utassert(ArenaUsed(a) == after + 4);

    s = ArenaStrAppend(a, s, StrL(" three"));
    utassert(base::StrEq(ArenaStrGet(a, s), StrL("one two three")));
    utassert(ArenaUsed(a) == after + 4 + 6);
    utassert(ArenaStrLen(a, s) == 13);

    // Still NUL-terminated, which the in-place path has to keep true or the
    // next append would find the wrong end.
    Str got = ArenaStrGet(a, s);
    utassert(got.s[len(got)] == 0);

    // Appending nothing is not an allocation.
    uint64_t before = ArenaUsed(a);
    s = ArenaStrAppend(a, s, Str{});
    s = ArenaStrAppend(a, s, StrL(""));
    utassert(ArenaUsed(a) == before);
    utassert(ArenaStrLen(a, s) == 13);

    // Appending to nothing is just storing it.
    ArenaStr fresh = ArenaStrAppend(a, kArenaStrNone, StrL("first"));
    utassert(base::StrEq(ArenaStrGet(a, fresh), StrL("first")));
    ArenaDelete(a);
}

// And when it is not the newest, it copies — which is what concatenating
// always did, so the answer is right either way.
static void AppendingToAnOlderStringCopies() {
    Arena* a = ArenaNew();
    ArenaStr first = ArenaStrDup(a, StrL("aa"));
    ArenaStr second = ArenaStrDup(a, StrL("bb"));

    // `first` is no longer at the end, so this cannot grow in place.
    first = ArenaStrAppend(a, first, StrL("cc"));
    utassert(base::StrEq(ArenaStrGet(a, first), StrL("aacc")));
    // The one behind it is untouched, which is the thing a wrong in-place
    // append would break.
    utassert(base::StrEq(ArenaStrGet(a, second), StrL("bb")));

    // And the copy is itself the newest now, so the next append is in place.
    uint64_t after = ArenaUsed(a);
    first = ArenaStrAppend(a, first, StrL("dd"));
    utassert(base::StrEq(ArenaStrGet(a, first), StrL("aaccdd")));
    utassert(ArenaUsed(a) == after + 2);
    ArenaDelete(a);
}

// An append to the newest string still has to fit somewhere, and when the
// block it is in has no room left the arena chains onto a new one. That is
// not contiguous with the characters already stored, so the append becomes a
// copy of both halves — and has to have asked for room for both halves, not
// just for the bytes appended. Getting that wrong hands out a string whose
// tail is the next allocation.
static void AppendingAtTheEndOfABlockAsksForBothHalves() {
    Arena* a = ArenaNew();

    // Walk to the end of a block. Nothing says how big one is, but the arena
    // says when it chained: the position jumps to the new block's base, which
    // is the block size, and blocks all reserve the same.
    const int kStep = 4096;
    uint64_t blockBase = 0;
    for (int i = 0; i < 100000; i++) {
        uint64_t before = ArenaUsed(a);
        Alloc(a, kStep);
        uint64_t after = ArenaUsed(a);
        if (after > before + kStep + kArenaHeaderSize) {
            blockBase = after - kStep - kArenaHeaderSize;
            break;
        }
    }
    utassert(blockBase > 0);
    uint64_t blockEnd = blockBase * 2;

    // Up to the end of this one, then to a hundred-odd bytes short of it.
    while (ArenaUsed(a) + kStep + 64 < blockEnd) {
        Alloc(a, kStep);
    }
    utassert(ArenaUsed(a) > blockBase && ArenaUsed(a) < blockEnd);
    uint64_t left = blockEnd - ArenaUsed(a);
    utassert(left > 200);
    Alloc(a, (int)(left - 200));

    // Two hundred bytes of block left, a string in the last of them, and an
    // append too big to follow it there.
    TempStr buf = AllocStrTemp(250);
    for (int i = 0; i < len(buf); i++) {
        buf.s[i] = (char)('a' + (i % 26));
    }
    ArenaStr was = ArenaStrDup(a, Str(buf.s, 5));
    ArenaStr s = ArenaStrAppend(a, was, Str(buf.s + 5, 245));
    // The append did chain — otherwise this test is not testing anything.
    utassert(s != was);
    utassert(base::StrEq(ArenaStrGet(a, s), Str(buf.s, 250)));

    // And the string owns every byte it reads back, so what the arena hands
    // out next starts after it rather than on top of its tail.
    ArenaStr next = ArenaStrDup(a, StrL("0123456789"));
    utassert(base::StrEq(ArenaStrGet(a, s), Str(buf.s, 250)));
    utassert(base::StrEq(ArenaStrGet(a, next), StrL("0123456789")));
    Str got = ArenaStrGet(a, s);
    utassert(got.s[len(got)] == 0);
    ArenaDelete(a);
}

// ─── ArenaPtr ────────────────────────────────────────────────────────────

struct PtrThing {
    int32_t a;
    int32_t b;
};

static void APointerGoesThereAndBack() {
    Arena* arena = ArenaNew();
    PtrThing* one = ArenaNew<PtrThing>(arena);
    one->a = 11;
    one->b = 22;
    PtrThing* two = ArenaNew<PtrThing>(arena);
    two->a = 33;

    ArenaPtr<PtrThing> p = ArenaPtrOf(arena, one);
    ArenaPtr<PtrThing> q = ArenaPtrOf(arena, two);
    utassert(p.IsSet() && q.IsSet());
    utassert(p != q);
    static_assert(sizeof(ArenaPtr<PtrThing>) == 4, "an ArenaPtr is an offset");

    utassert(ArenaPtrGet(arena, p) == one);
    utassert(ArenaPtrGet(arena, q) == two);
    utassert(ArenaPtrGet(arena, p)->a == 11);
    utassert(ArenaPtrGet(arena, p)->b == 22);

    // A default one is null, and reading it back is null rather than a
    // pointer at the arena's own header.
    ArenaPtr<PtrThing> none = {};
    utassert(!none.IsSet());
    utassert(none.off == kArenaPtrNone);
    utassert(ArenaPtrGet(arena, none) == nullptr);

    // Something that is not in this arena has no offset to name.
    PtrThing onTheStack = {};
    utassert(!ArenaPtrOf(arena, &onTheStack).IsSet());
    ArenaDelete(arena);
}

// The offset is into the chain's position space, not into one block, so it
// keeps meaning the same object once the arena has outgrown its first block.
static void APointerSurvivesTheArenaChainingOn() {
    Arena* arena = ArenaNew();
    PtrThing* early = ArenaNew<PtrThing>(arena);
    early->a = 7;
    ArenaPtr<PtrThing> p = ArenaPtrOf(arena, early);

    // Past any first block: the chain adds one, and `early` stays where it
    // is because an arena never moves what it has handed out.
    for (int i = 0; i < 200000; i++) {
        (void)ArenaNew<PtrThing>(arena);
    }
    PtrThing* late = ArenaNew<PtrThing>(arena);
    late->a = 9;
    ArenaPtr<PtrThing> q = ArenaPtrOf(arena, late);

    utassert(ArenaPtrGet(arena, p) == early);
    utassert(ArenaPtrGet(arena, p)->a == 7);
    utassert(ArenaPtrGet(arena, q) == late);
    utassert(ArenaPtrGet(arena, q)->a == 9);
    utassert(p.off < q.off);
    ArenaDelete(arena);
}

void TestArenaStr() {
    TestSuite("arena_str");
    APointerGoesThereAndBack();
    APointerSurvivesTheArenaChainingOn();
    AppendingToTheNewestCostsOnlyTheBytes();
    AppendingToAnOlderStringCopies();
    AppendingAtTheEndOfABlockAsksForBothHalves();
    WhatGoesInComesOut();
    ItSurvivesTheArenaChainingOn();
    TheLengthRidesAlongInOneByte();
    GrowingPastTheOneByteLength();
    TheWordIsAnOffsetAndNothingElse();
}
