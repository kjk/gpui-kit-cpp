/* The taffy tree carried across frames, and what reconciling it has to get
 * right.
 *
 * The tree is rebuilt as elements every frame and reconciled against the
 * nodes of the last one by position, so what a frame costs is only what
 * actually changed. What it must never cost is a frame laid out wrong: a
 * node that ends up detached is never laid out at all, and everything under
 * it writes back a zero rectangle — a page drawn as nothing, and left that
 * way until the next thing that happens to redraw. */

#include "Test.h"

using namespace gpui;

// A child whose *kind* changed cannot keep the node: the reconcile builds a
// new one and puts it where the old one sat. Taking the old one out first
// would shorten the parent's child list under the index the new one is about
// to be written at — and where that index was the last, the new subtree is
// never attached and never laid out.
static void AChildOfAnotherKindIsStillLaidOut() {
    TestSuite("layout reuse");
    LayoutCache* lc = LayoutCacheNew();
    Arena* a = ArenaNew();

    // A parent with one child, which is what makes the index the last one.
    El* first = Div(a)->FlexCol()->W(kFill)->H(kFill);
    first->Child(Div(a)->W(100)->H(20));
    LayoutEl(nullptr, first, 0, 0, 400, 300, 14, Rgba{}, lc);
    utassert(first->first->w == 100 && first->first->h == 20);

    // The same position, a different kind of element: an icon measures a
    // constant, so its size says whether it was laid out at all.
    a->Reset();
    El* second = Div(a)->FlexCol()->W(kFill)->H(kFill);
    second->Child(IconEl(a, IconName::Check, 16));
    LayoutEl(nullptr, second, 0, 0, 400, 300, 14, Rgba{}, lc);
    utassert(second->first->w == 16 && second->first->h == 16);

    // And back the other way, which is the same swap in reverse.
    a->Reset();
    El* third = Div(a)->FlexCol()->W(kFill)->H(kFill);
    third->Child(Div(a)->W(120)->H(24));
    LayoutEl(nullptr, third, 0, 0, 400, 300, 14, Rgba{}, lc);
    utassert(third->first->w == 120 && third->first->h == 24);

    ArenaDelete(a);
    LayoutCacheFree(lc);
}

// A whole subtree replaced at once — a page switch, which is where this was
// found. Every box in the new page has to have a size.
static void APageSwitchLaysOutEveryBox() {
    LayoutCache* lc = LayoutCacheNew();
    Arena* a = ArenaNew();

    El* page = Div(a)->FlexCol()->W(kFill)->H(kFill);
    page->Child(
        Div(a)->FlexCol()->W(kFill)->Child(IconEl(a, IconName::Check, 16)));
    LayoutEl(nullptr, page, 0, 0, 400, 300, 14, Rgba{}, lc);

    // The next frame's page is deeper and wider than the one before it: the
    // reconcile keeps the top of the tree and builds the rest.
    a->Reset();
    El* other = Div(a)->FlexCol()->W(kFill)->H(kFill);
    El* row = Div(a)->FlexRow()->W(kFill)->H(40);
    for (int i = 0; i < 4; i++) {
        row->Child(Div(a)->W(50)->H(20));
    }
    other->Child(row);
    other->Child(Div(a)->W(80)->H(30));
    LayoutEl(nullptr, other, 0, 0, 400, 300, 14, Rgba{}, lc);

    utassert(row->w == 400 && row->h == 40);
    int n = 0;
    for (El* c = row->first; c; c = c->next) {
        utassert(c->w == 50 && c->h == 20);
        utassert(c->x == 50.f * n);
        n++;
    }
    utassert(n == 4);
    utassert(other->first->next->w == 80 && other->first->next->h == 30);

    ArenaDelete(a);
    LayoutCacheFree(lc);
}

// A parent that only *loses* a child changes in no other way, so nothing
// restyles it and nothing remeasures it. Taking a child out does not dirty a
// node in taffy — Rust's `remove` does not either, because Rust's callers
// reach for `set_children`, which does — so the reconcile has to say so
// itself, or the parent keeps the height it had when the child was there.
static void AParentThatLostAChildShrinks() {
    LayoutCache* lc = LayoutCacheNew();
    Arena* a = ArenaNew();

    // Inside a root, because a root is stretched to the space it was given
    // and would say nothing about its own content.
    El* root = Div(a)->FlexCol()->W(kFill)->H(kFill);
    El* column = Div(a)->FlexCol()->W(200);
    for (int i = 0; i < 3; i++) {
        column->Child(Div(a)->W(kFill)->H(20));
    }
    root->Child(column);
    LayoutEl(nullptr, root, 0, 0, 400, 300, 14, Rgba{}, lc);
    utassert(column->h == 60);

    a->Reset();
    El* root2 = Div(a)->FlexCol()->W(kFill)->H(kFill);
    El* shorter = Div(a)->FlexCol()->W(200);
    for (int i = 0; i < 2; i++) {
        shorter->Child(Div(a)->W(kFill)->H(20));
    }
    root2->Child(shorter);
    LayoutEl(nullptr, root2, 0, 0, 400, 300, 14, Rgba{}, lc);
    utassert(shorter->h == 40);
    utassert(LayoutCacheNodeCount(lc) < 10);

    ArenaDelete(a);
    LayoutCacheFree(lc);
}

// A virtualized column: a spacer, a window of rows, a spacer. Scrolling
// slides the window. The taffy tree must not grow — that was the editor's
// scroll leak, InsertNode allocating a NodeData for every newly visible row
// and never recycling the one that scrolled off.
//
// Rows are nested the way the editor's line-number band is (gutter + pane +
// text), so a spacer becoming a row is a whole subtree, not a leaf. That is
// the InsertNode stack scrolling still produced: LayoutSync AddChild of a
// band, then LayoutBuild of its children.
static El* SlidingRow(Arena* a) {
    El* band = Div(a)->FlexRow()->W(kFill)->H(20);
    band->Child(Div(a)->W(40)->Child(TextEl(a, StrL("1"))));
    band->Child(Div(a)->Flex1()->Child(TextEl(a, StrL("row"))));
    return band;
}

static El* SlidingList(Arena* a, int first, int visible, int total) {
    El* col = Div(a)->FlexCol()->W(kFill);
    if (first > 0) {
        col->Child(Div(a)->W(kFill)->H((float)first * 20));
    }
    for (int i = 0; i < visible; i++) {
        col->Child(SlidingRow(a));
    }
    int end = first + visible;
    if (end < total) {
        col->Child(Div(a)->W(kFill)->H((float)(total - end) * 20));
    }
    return Div(a)->FlexCol()->W(kFill)->H(kFill)->Child(col);
}

static void ASlidingWindowDoesNotGrowTheTaffyTree() {
    LayoutCache* lc = LayoutCacheNew();
    Arena* a = ArenaNew();
    const int kVisible = 10;
    const int kTotal = 80;
    int live = 0;
    int peakSlots = 0;
    for (int first = 0; first < 40; first++) {
        a->Reset();
        El* root = SlidingList(a, first, kVisible, kTotal);
        LayoutEl(nullptr, root, 0, 0, 400, 300, 14, Rgba{}, lc);
        int now = LayoutCacheNodeCount(lc);
        int slots = LayoutCacheSlotCount(lc);
        if (slots > peakSlots) {
            peakSlots = slots;
        }
        if (first == 1) {
            // first=0 has no top spacer; first>=1 does, and the bottom
            // spacer is there until the window hits the end.
            live = now;
        }
        if (first > 1) {
            utassert(now == live);
        }
    }
    utassert(live > 0);
    utassert(peakSlots > 0);

    // Back to the top: the spacer at index 0 becomes a row, and the last
    // row becomes a spacer. Building that row before dropping the last
    // one is what InsertNode'd a NodeData on every scroll-up — live count
    // stayed flat (the extras were freed at the end of the pass) while
    // the slot array kept growing.
    for (int first = 39; first >= 0; first--) {
        a->Reset();
        El* root = SlidingList(a, first, kVisible, kTotal);
        LayoutEl(nullptr, root, 0, 0, 400, 300, 14, Rgba{}, lc);
        if (first > 0) {
            utassert(LayoutCacheNodeCount(lc) == live);
        }
        utassert(LayoutCacheSlotCount(lc) <= peakSlots);
    }

    // Down again: the free list must absorb the same swap.
    for (int first = 0; first < 40; first++) {
        a->Reset();
        El* root = SlidingList(a, first, kVisible, kTotal);
        LayoutEl(nullptr, root, 0, 0, 400, 300, 14, Rgba{}, lc);
        if (first > 0) {
            utassert(LayoutCacheNodeCount(lc) == live);
        }
        utassert(LayoutCacheSlotCount(lc) <= peakSlots);
    }

    ArenaDelete(a);
    LayoutCacheFree(lc);
}

static void ASecondIdenticalFrameMakesNoNodes() {
    LayoutCache* lc = LayoutCacheNew();
    Arena* a = ArenaNew();
    El* first = Div(a)->FlexCol()->W(kFill)->H(kFill);
    first->Child(Div(a)->W(100)->H(20)->Child(TextEl(a, StrL("a"))));
    first->Child(Div(a)->W(100)->H(20)->Child(TextEl(a, StrL("b"))));
    LayoutEl(nullptr, first, 0, 0, 400, 300, 14, Rgba{}, lc);
    int live = LayoutCacheNodeCount(lc);
    utassert(LayoutCacheLastStats(lc).made > 0);

    a->Reset();
    El* second = Div(a)->FlexCol()->W(kFill)->H(kFill);
    second->Child(Div(a)->W(100)->H(20)->Child(TextEl(a, StrL("a"))));
    second->Child(Div(a)->W(100)->H(20)->Child(TextEl(a, StrL("b"))));
    LayoutEl(nullptr, second, 0, 0, 400, 300, 14, Rgba{}, lc);
    utassert(LayoutCacheNodeCount(lc) == live);
    utassert(LayoutCacheLastStats(lc).made == 0);

    ArenaDelete(a);
    LayoutCacheFree(lc);
}

static void AFixedOverlayIsReusedNotRebuilt() {
    LayoutCache* lc = LayoutCacheNew();
    Arena* a = ArenaNew();
    int live = 0;
    for (int f = 0; f < 12; f++) {
        a->Reset();
        El* root = Div(a)->FlexCol()->W(kFill)->H(kFill);
        root->Child(Div(a)->W(kFill)->H(20)->Child(TextEl(a, StrL("row"))));
        root->Child(Div(a)->Fixed()->Left(0)->Top(0)->W(40)->H(20)->Child(
            TextEl(a, StrL("tip"))));
        LayoutEl(nullptr, root, 0, 0, 400, 300, 14, Rgba{}, lc);
        if (f == 1) {
            live = LayoutCacheNodeCount(lc);
        }
        if (f > 1) {
            utassert(LayoutCacheLastStats(lc).made == 0);
            utassert(LayoutCacheNodeCount(lc) == live);
        }
    }
    utassert(live > 0);

    ArenaDelete(a);
    LayoutCacheFree(lc);
}

static void AKindChangeRecyclesBeforeBuilding() {
    LayoutCache* lc = LayoutCacheNew();
    Arena* a = ArenaNew();
    int peak = 0;
    for (int f = 0; f < 24; f++) {
        a->Reset();
        El* root = Div(a)->FlexCol()->W(kFill)->H(kFill);
        El* col = Div(a)->FlexCol()->W(kFill);
        for (int i = 0; i < 8; i++) {
            if ((f + i) % 2 == 0) {
                col->Child(TextEl(a, StrL("x")));
            } else {
                col->Child(Div(a)
                               ->W(kFill)
                               ->H(16)
                               ->Child(TextEl(a, StrL("a")))
                               ->Child(TextEl(a, StrL("b"))));
            }
        }
        root->Child(col);
        LayoutEl(nullptr, root, 0, 0, 400, 300, 14, Rgba{}, lc);
        int n = LayoutCacheNodeCount(lc);
        if (n > peak) {
            peak = n;
        }
    }
    // 8 nested rows (3 nodes each) plus wrappers, not 8 × 24 frames.
    utassert(peak < 50);

    ArenaDelete(a);
    LayoutCacheFree(lc);
}

// __layout_reuse=off is the rust-shaped path: rebuild the taffy tree every
// frame. An identical second tree then still makes nodes.
static void ReuseOffRebuildsEveryFrame() {
    LayoutReuseTakeArg(StrL("__layout_reuse=off"));
    utassert(!LayoutReuseOn());

    LayoutCache* lc = LayoutCacheNew();
    Arena* a = ArenaNew();
    El* first = Div(a)->FlexCol()->W(kFill)->H(kFill);
    first->Child(Div(a)->W(100)->H(20)->Child(TextEl(a, StrL("a"))));
    LayoutEl(nullptr, first, 0, 0, 400, 300, 14, Rgba{}, lc);
    utassert(LayoutCacheLastStats(lc).made > 0);
    utassert(LayoutCacheLastStats(lc).allocs > 0);
    int slots = LayoutCacheSlotCount(lc);

    a->Reset();
    El* second = Div(a)->FlexCol()->W(kFill)->H(kFill);
    second->Child(Div(a)->W(100)->H(20)->Child(TextEl(a, StrL("a"))));
    LayoutEl(nullptr, second, 0, 0, 400, 300, 14, Rgba{}, lc);
    utassert(LayoutCacheLastStats(lc).made > 0);
    utassert(LayoutCacheLastStats(lc).allocs == 0);
    utassert(LayoutCacheSlotCount(lc) == slots);

    ArenaDelete(a);
    LayoutCacheFree(lc);
    LayoutReuseTakeArg(StrL("__layout_reuse=on"));
    utassert(LayoutReuseOn());
}

// What a virtual list does at prepaint: lay out a row of its own, from inside
// the write-back of the frame.
static void LayOutARowAtPrepaint(PaintCtx* ctx, El*, void* user) {
    Arena* a = (Arena*)user;
    El* row = Div(a)->W(100)->H(20)->Child(TextEl(a, StrL("row")));
    LayoutEl(ctx, row, 0, 0, 100, 20, 14, Rgba{}, nullptr);
}

// A layout nested in the write-back starts the fixed elements over; the
// frame's own fixed overlay — a dialog over a page with a list in it — still
// has to get its box.
static void AFixedOverlayOutlivesANestedLayout() {
    LayoutCache* lc = LayoutCacheNew();
    Arena* a = ArenaNew();
    El* root = Div(a)->FlexCol()->W(kFill)->H(kFill);
    El* list = Div(a)->W(kFill)->H(40);
    list->prePaint = &LayOutARowAtPrepaint;
    list->customUser = a;
    root->Child(list);
    El* overlay = Div(a)->Fixed()->Left(10)->Top(10)->W(40)->H(20);
    root->Child(overlay);
    LayoutEl(nullptr, root, 0, 0, 400, 300, 14, Rgba{}, lc);
    utassert(overlay->w == 40 && overlay->h == 20);
    utassert(overlay->x == 10 && overlay->y == 10);

    ArenaDelete(a);
    LayoutCacheFree(lc);
}

void TestLayoutReuse() {
    AFixedOverlayOutlivesANestedLayout();
    AChildOfAnotherKindIsStillLaidOut();
    APageSwitchLaysOutEveryBox();
    AParentThatLostAChildShrinks();
    ASlidingWindowDoesNotGrowTheTaffyTree();
    ASecondIdenticalFrameMakesNoNodes();
    AFixedOverlayIsReusedNotRebuilt();
    AKindChangeRecyclesBeforeBuilding();
    ReuseOffRebuildsEveryFrame();
}
