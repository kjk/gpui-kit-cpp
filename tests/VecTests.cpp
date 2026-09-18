/* Vec's borrowed storage and free-function API, kept aligned with Sumatra's
 * src/base/Vec.h. */

#include "Test.h"

template <typename T>
static int VecCap(const Vec<T>& v) {
    return v.cap < 0 ? -v.cap : v.cap;
}

static void AnInlineVecStartsInTheBufferAndDoesNotAllocate() {
    int buf[4] = {};
    Vec<int> v;
    VecUseExternalBuffer(v, buf);
    utassert(len(v) == 0);
    utassert(VecCap(v) == 4);
    utassert(v.els == buf);

    for (int i = 0; i < 4; i++) {
        utassert(VecAppend(v, i * 7));
    }
    utassert(len(v) == 4);
    // Still the caller's array: the elements were written straight into it,
    // and nothing was allocated to hold them.
    utassert(v.els == buf);
    for (int i = 0; i < 4; i++) {
        utassert(v[i] == i * 7);
        utassert(buf[i] == i * 7);
    }
}

static void TheAppendPastTheBufferMovesToTheHeapWithTheElements() {
    int buf[4] = {};
    Vec<int> v;
    VecUseExternalBuffer(v, buf);
    for (int i = 0; i < 4; i++) {
        VecAppend(v, i * 7);
    }
    utassert(VecAppend(v, 99));

    // Off the buffer and onto a block of its own, carrying everything that
    // was already there.
    utassert(v.els != buf);
    utassert(v.cap > 0);
    utassert(VecCap(v) >= 5);
    utassert(len(v) == 5);
    for (int i = 0; i < 4; i++) {
        utassert(v[i] == i * 7);
    }
    utassert(v[4] == 99);
    // The array it was lent is the caller's and was left as it was.
    for (int i = 0; i < 4; i++) {
        utassert(buf[i] == i * 7);
    }

    // And it goes on growing the ordinary way from there.
    for (int i = 0; i < 200; i++) {
        VecAppend(v, 1000 + i);
    }
    utassert(len(v) == 205);
    utassert(v[204] == 1199);
    utassert(v[0] == 0);
}

static void AReserveStraightPastTheBufferAlsoCarries() {
    int buf[4] = {};
    Vec<int> v;
    VecUseExternalBuffer(v, buf);
    VecAppend(v, 1);
    VecAppend(v, 2);
    // One jump, rather than an append at a time.
    utassert(VecReserve(v, 64) != nullptr);
    utassert(v.els != buf);
    utassert(VecCap(v) >= 64);
    utassert(len(v) == 2);
    utassert(v[0] == 1 && v[1] == 2);
}

static void ResetGivesTheBufferBackWithoutFreeingIt() {
    int buf[4] = {7, 7, 7, 7};
    Vec<int> v;
    VecUseExternalBuffer(v, buf);
    VecAppend(v, 1);
    // The interesting half: `Reset` must not hand a stack address to free().
    // Reaching the end of this test at all is the assertion; the rest says
    // the vec is empty afterwards and the array is untouched.
    VecReset(v);
    utassert(len(v) == 0);
    utassert(VecCap(v) == 0);
    utassert(v.els == nullptr);
    utassert(buf[0] == 1);

    // Empty and owning nothing, it allocates the way any other vec does.
    VecAppend(v, 5);
    utassert(len(v) == 1 && v[0] == 5);
    utassert(v.els != buf);
}

static void ADestructorOnABorrowedVecFreesNothing() {
    int buf[4] = {};
    {
        Vec<int> v;
        VecUseExternalBuffer(v, buf);
        VecAppend(v, 3);
        VecAppend(v, 4);
    }
    // Same: the point is that the scope closed without free() seeing the
    // stack array.
    utassert(buf[0] == 3 && buf[1] == 4);
}

static void ACopyOfABorrowedVecOwnsItsOwnElements() {
    int buf[4] = {};
    Vec<int> v;
    VecUseExternalBuffer(v, buf);
    VecAppend(v, 11);
    VecAppend(v, 22);

    Vec<int> copy = v;
    utassert(len(copy) == 2);
    utassert(copy[0] == 11 && copy[1] == 22);
    // The copy borrows nothing — it allocated — so writing through one is not
    // seen by the other, and the copy's own destructor has a block to free.
    utassert(copy.els != buf);
    utassert(copy.cap > 0);
    v[0] = 99;
    utassert(copy[0] == 11);
}

static void ClearOnABorrowedVecZeroesTheBuffer() {
    int buf[4] = {};
    Vec<int> v;
    VecUseExternalBuffer(v, buf);
    for (int i = 0; i < 4; i++) {
        VecAppend(v, i + 1);
    }
    // Clear zeroes the whole capacity, and the capacity here is the array —
    // the size of it is what the sign of `cap` has to be read through.
    VecClear(v);
    utassert(len(v) == 0);
    for (int i = 0; i < 4; i++) {
        utassert(buf[i] == 0);
    }
}

static void AnOrdinaryVecIsUnaffected() {
    Vec<int> v;
    utassert(VecCap(v) == 0);
    for (int i = 0; i < 100; i++) {
        VecAppend(v, i);
    }
    utassert(len(v) == 100);
    utassert(v.cap >= 100 && VecCap(v) == v.cap);
    utassert(v[99] == 99);
    VecReset(v);
    utassert(len(v) == 0 && v.cap == 0 && v.els == nullptr);
}

// The caller `VecUseExternalBuffer` was written for, driven past its buffer.
// Taffy's flex line list is the only one in the tree, and no layout anywhere in
// the tree — the taffy suite, the story gallery, the showcase, the benchmarks —
// produces more than one flex line, so nothing else would ever move it off
// the stack. A wrapping row twelve items wide makes six lines, and what is
// checked is that the lines the buffer already held came through the move
// with the rest.
static void TaffyFlexLinesSurviveOutgrowingTheBuffer() {
    taffy::TaffyTree tree;
    tree.Init(16);

    taffy::Style childStyle;
    childStyle.size = taffy::SizeDim::FromLengths(50.0f, 10.0f);

    const int kItems = 12; // two to a line, so six lines
    taffy::NodeId kids[kItems];
    for (int i = 0; i < kItems; i++) {
        kids[i] = tree.NewLeaf(childStyle);
    }

    taffy::Style rootStyle;
    rootStyle.size = taffy::SizeDim::FromLengths(100.0f, 60.0f);
    rootStyle.flexWrap = taffy::FlexWrap::Wrap;
    taffy::NodeId root = tree.NewWithChildren(rootStyle, kids, kItems);

    tree.ComputeLayout(root, taffy::SizeAvail::MaxContent());

    for (int i = 0; i < kItems; i++) {
        const taffy::Layout& l = tree.GetLayout(kids[i]);
        utassert(l.size.w == 50.0f);
        utassert(l.size.h == 10.0f);
        utassert(l.location.x == (i % 2 == 0 ? 0.0f : 50.0f));
        utassert(l.location.y == (float)(i / 2) * 10.0f);
    }
}

static void TheFreeFunctionSurfaceKeepsVecSemantics() {
    Vec<int> v;
    utassert(VecReserve(v, 0) == nullptr);
    utassert(VecAppend(v, 2));
    int more[] = {4, 6};
    utassert(VecAppendN(v, more, 2));
    utassert(VecInsertAt(v, 1, 3));
    utassert(len(v) == 4 && v[0] == 2 && v[1] == 3 && v[2] == 4 && v[3] == 6);
    utassert(VecFind(v, 4) == 2);
    utassert(VecContains(v, 6));
    utassert(VecLast(v) == 6);
    utassert(VecIsValidIndex(v, 3) && !VecIsValidIndex(v, 4));

    utassert(VecPopAt(v, 1) == 3);
    VecRemoveAtN(v, 0, 2);
    utassert(len(v) == 1 && v[0] == 6);
    utassert(VecAppend(v, 8));
    VecRemoveAtFast(v, 0);
    utassert(len(v) == 1 && v[0] == 8);
    VecRemoveLast(v);
    utassert(len(v) == 0);

    utassert(VecResize(v, 3));
    utassert(len(v) == 3 && v[0] == 0 && v[1] == 0 && v[2] == 0);
    v[0] = 10;
    v[1] = 20;
    v[2] = 30;
    utassert(VecPop(v) == 30);
    utassert(VecRemove(v, 10) == 0);
    utassert(len(v) == 1 && v[0] == 20);

    Vec<int> other;
    utassert(VecAppend(other, 40));
    utassert(VecAppendVec(v, other));
    utassert(len(v) == 2 && v[1] == 40);
    int* taken = VecTake(v);
    utassert(taken && taken[0] == 20 && taken[1] == 40);
    utassert(len(v) == 0 && v.cap == 0 && v.els == nullptr);
    Free(nullptr, taken);
}

static void TakingBorrowedStorageReturnsAnOwnedCopy() {
    int buf[2] = {};
    Vec<int> v;
    VecUseExternalBuffer(v, buf);
    VecAppend(v, 7);
    VecAppend(v, 9);
    int* taken = VecTake(v);
    utassert(taken && taken != buf && taken[0] == 7 && taken[1] == 9);
    utassert(len(v) == 0 && v.cap == 0 && v.els == nullptr);
    utassert(buf[0] == 7 && buf[1] == 9);
    Free(nullptr, taken);
}

static void HeapAndBorrowedGrowWithTheSamePolicy() {
    Vec<int> heap;
    utassert(VecAppend(heap, 1));
    utassert(VecAbsCap(heap.cap) == VecNextCap(0, 1, (int)sizeof(int)));

    int buf[1] = {};
    Vec<int> borrowed;
    VecUseExternalBuffer(borrowed, buf);
    utassert(VecAppend(borrowed, 1));
    utassert(borrowed.els == buf);
    utassert(VecAppend(borrowed, 2));
    utassert(borrowed.els != buf);
    utassert(VecAbsCap(borrowed.cap) == VecNextCap(1, 2, (int)sizeof(int)));
    utassert(borrowed[0] == 1 && borrowed[1] == 2);
}

void TestVec() {
    TestSuite("vec");
    AnInlineVecStartsInTheBufferAndDoesNotAllocate();
    TheAppendPastTheBufferMovesToTheHeapWithTheElements();
    AReserveStraightPastTheBufferAlsoCarries();
    ResetGivesTheBufferBackWithoutFreeingIt();
    ADestructorOnABorrowedVecFreesNothing();
    ACopyOfABorrowedVecOwnsItsOwnElements();
    ClearOnABorrowedVecZeroesTheBuffer();
    AnOrdinaryVecIsUnaffected();
    TaffyFlexLinesSurviveOutgrowingTheBuffer();
    TheFreeFunctionSurfaceKeepsVecSemantics();
    TakingBorrowedStorageReturnsAnOwnedCopy();
    HeapAndBorrowedGrowWithTheSamePolicy();
}
