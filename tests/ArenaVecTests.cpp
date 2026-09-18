/* `ArenaVec` in src/base.h — the segmented array the markdown parser and the
 * widget builders fill. Not a port: the Rust side has no equivalent, and the
 * tests the tree already has only ever exercise vecs small enough to sit in
 * one segment. These are the cases past that edge — the segment boundary, the
 * pop that hands an earlier segment back the appends, and the iterator, which
 * has to end where the elements do and not where the segments do. */

#include "Test.h"

// Enough elements to cross the first three segment sizes (4, 16, 64) and land
// well inside the doubling that follows.
static const int kMany = 300;

static void AnEmptyVecHasNothing() {
    ArenaVec<int> v{};
    utassert(len(v) == 0);
    utassert(!v.first.IsSet() && !v.last.IsSet());
    utassert(v.Flatten(nullptr) == nullptr);
    utassert(!(v.begin() != v.end()));
}

static void TheIteratorWalksEverySegment() {
    Arena* a = ArenaNew();
    ArenaVec<int> v{};
    for (int i = 0; i < kMany; i++) {
        v.Append(a, i);
    }
    utassert(v.first != v.last);
    int seen = 0;
    for (int el : v) {
        utassert(el == seen);
        seen++;
    }
    utassert(seen == kMany);

    // Through a reference, which is how an event's point is rewritten in
    // place.
    for (int& el : v) {
        el += 1000;
    }
    utassert(v[0] == 1000);
    utassert(v[kMany - 1] == kMany - 1 + 1000);

    // A vec of one element, and one that never got a segment at all.
    ArenaVec<int> one{};
    one.Append(a, 7);
    seen = 0;
    for (int el : one) {
        utassert(el == 7);
        seen++;
    }
    utassert(seen == 1);
    ArenaVec<int> none{};
    for (int el : none) {
        (void)el;
        utassert(false);
    }
    ArenaDelete(a);
}

static void TheIteratorSkipsTheSegmentsATruncateEmptied() {
    Arena* a = ArenaNew();
    ArenaVec<int> v{};
    for (int i = 0; i < kMany; i++) {
        v.Append(a, i);
    }
    // Back into the first segment, with several empty ones still linked
    // behind it — the walk has to end at the last element and not at the
    // last segment.
    v.Truncate(3);
    utassert(ArenaPtrGet(a, v.last)->next.IsSet());
    int seen = 0;
    for (int el : v) {
        utassert(el == seen);
        seen++;
    }
    utassert(seen == 3);

    // And nothing at all left is still a walk that ends immediately.
    v.Truncate(0);
    for (int el : v) {
        (void)el;
        utassert(false);
    }
    ArenaDelete(a);
}

static void AppendsReadBackAcrossSegments() {
    Arena* a = ArenaNew();
    ArenaVec<int> v{};
    for (int i = 0; i < kMany; i++) {
        utassert(v.Append(a, i * 3));
        utassert(len(v) == i + 1);
    }
    // Forward. `Iter` is the way to do this; `v[i]` walks from the first
    // segment every time, which is what the rest of the tree uses it for and
    // what this is checking.
    for (int i = 0; i < kMany; i++) {
        utassert(v[i] == i * 3);
    }
    // Backwards, which restarts the walk at every segment boundary and has to
    // land in the same places.
    for (int i = kMany - 1; i >= 0; i--) {
        utassert(v[i] == i * 3);
    }
    // Jumping around.
    utassert(v[0] == 0);
    utassert(v[kMany - 1] == (kMany - 1) * 3);
    utassert(v[17] == 51);
    utassert(v[4] == 12);
    // More than one segment, or the rest of this is testing nothing.
    utassert(v.first != v.last);
    ArenaDelete(a);
}

static void ElementsDoNotMoveWhenTheVecGrows() {
    Arena* a = ArenaNew();
    ArenaVec<int> v{};
    v.Append(a, 1);
    int* firstEl = &v[0];
    for (int i = 1; i < kMany; i++) {
        v.Append(a, i + 1);
    }
    // The flat version reallocated and this pointer would be into an
    // abandoned block.
    utassert(firstEl == &v[0]);
    utassert(*firstEl == 1);
    ArenaDelete(a);
}

static void AppendManyFillsTheSameOrder() {
    Arena* a = ArenaNew();
    int src[kMany];
    for (int i = 0; i < kMany; i++) {
        src[i] = i;
    }
    ArenaVec<int> v{};
    // Two runs, so the second one starts partway into a segment.
    utassert(v.AppendMany(a, src, 7));
    utassert(v.AppendMany(a, src + 7, kMany - 7));
    utassert(len(v) == kMany);
    for (int i = 0; i < kMany; i++) {
        utassert(v[i] == i);
    }

    // Append is AppendMany of one; a one-at-a-time fill must match.
    ArenaVec<int> one{};
    for (int i = 0; i < kMany; i++) {
        utassert(one.Append(a, src[i]));
    }
    utassert(len(one) == len(v));
    for (int i = 0; i < kMany; i++) {
        utassert(one[i] == v[i]);
    }
    utassert(one.first != one.last && v.first != v.last);
    ArenaDelete(a);
}

static void ReserveSkipsTheClimb() {
    Arena* a = ArenaNew();
    ArenaVec<int> v{};
    utassert(v.Reserve(a, kMany));
    for (int i = 0; i < kMany; i++) {
        v.Append(a, i);
    }
    // One segment took all of it, so `Flatten` is free and the fast path in
    // `operator[]` is the one being read.
    utassert(v.first == v.last);
    utassert(len(v) == kMany);
    utassert(v[kMany - 1] == kMany - 1);
    ArenaDelete(a);
}

static void PopAndTruncateGiveTheRoomBack() {
    Arena* a = ArenaNew();
    ArenaVec<int> v{};
    for (int i = 0; i < kMany; i++) {
        v.Append(a, i);
    }
    v.Pop();
    utassert(len(v) == kMany - 1);
    utassert(v[len(v) - 1] == kMany - 2);

    // One element past the end of the first segment, so an earlier segment is
    // the active one again and the ones after it are empty.
    const int past = ArenaVec<int>::CapFor(kArenaVecCap0, kArenaVecBytes0) + 1;
    v.Truncate(past);
    utassert(len(v) == past);
    utassert(v[past - 1] == past - 1);
    utassert(v.first != v.last);

    // Truncating to nothing keeps the segments, and refilling reuses them:
    // no arena is spent on the second pass.
    v.Truncate(0);
    utassert(len(v) == 0);
    uint64_t before = a->pos;
    for (int i = 0; i < kMany; i++) {
        v.Append(a, i * 2);
    }
    utassert(a->pos == before);
    utassert(len(v) == kMany);
    for (int i = 0; i < kMany; i++) {
        utassert(v[i] == i * 2);
    }

    // A truncate longer than the vec is not a resize.
    v.Truncate(kMany + 100);
    utassert(len(v) == kMany);
    ArenaDelete(a);
}

static void PopAtASegmentBoundaryDoesNotAllocate() {
    Arena* a = ArenaNew();
    ArenaVec<int> v{};
    // Fill the first segment exactly — asked for rather than written out,
    // since the first step is a count capped by a byte budget — so the next
    // append is the one that takes a second segment.
    const int cap0 = ArenaVec<int>::CapFor(kArenaVecCap0, kArenaVecBytes0);
    for (int i = 0; i < cap0; i++) {
        v.Append(a, i);
    }
    v.Append(a, 4);
    utassert(v.first != v.last);
    uint64_t before = a->pos;
    for (int i = 0; i < 50; i++) {
        v.Pop();
        v.Append(a, 4);
    }
    utassert(a->pos == before);
    utassert(len(v) == cap0 + 1);
    utassert(v[cap0] == 4);
    ArenaDelete(a);
}

static void FlattenIsOneArrayEitherWay() {
    Arena* a = ArenaNew();
    ArenaVec<int> few{};
    for (int i = 0; i < 3; i++) {
        few.Append(a, i);
    }
    // One segment: the elements are already an array, so this is a pointer
    // into it and not a copy.
    int* flat = few.Flatten(a);
    utassert(flat == &few[0]);
    utassert(flat[2] == 2);

    ArenaVec<int> many{};
    for (int i = 0; i < kMany; i++) {
        many.Append(a, i);
    }
    utassert(many.first != many.last);
    int* copy = many.Flatten(a);
    utassert(copy != nullptr);
    for (int i = 0; i < kMany; i++) {
        utassert(copy[i] == i);
    }
    ArenaDelete(a);
}

static void ACopyOfTheHandleSeesTheSameElements() {
    Arena* a = ArenaNew();
    ArenaVec<int> v{};
    for (int i = 0; i < kMany; i++) {
        v.Append(a, i);
    }
    // The parser passes these around by value — a copy is a handle onto the
    // same segments, and reading through one is what the other reads.
    ArenaVec<int> copy = v;
    utassert(len(copy) == len(v));
    for (int i = 0; i < kMany; i++) {
        utassert(copy[i] == i);
    }
    v[5] = 500;
    utassert(copy[5] == 500);
    ArenaDelete(a);
}

void TestArenaVec() {
    TestSuite("arena_vec");
    AnEmptyVecHasNothing();
    TheIteratorWalksEverySegment();
    TheIteratorSkipsTheSegmentsATruncateEmptied();
    AppendsReadBackAcrossSegments();
    ElementsDoNotMoveWhenTheVecGrows();
    AppendManyFillsTheSameOrder();
    ReserveSkipsTheClimb();
    PopAndTruncateGiveTheRoomBack();
    PopAtASegmentBoundaryDoesNotAllocate();
    FlattenIsOneArrayEitherWay();
    ACopyOfTheHandleSeesTheSameElements();
}
