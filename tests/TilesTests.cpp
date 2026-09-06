/* Ported from crates/ui/src/dock/tiles.rs.
 *
 * The tests below are Rust's own: an edge snaps to the nearest neighbour
 * edge strictly inside the grid size, and falls back to rounding onto the
 * grid when nothing is close. Which edge moves is read off which of the four
 * values a resize was given, and the pinned edge stays where it was. The move
 * side — the magnetic snap, the boundary and the order tiles paint in — has
 * no tests in Rust; the ones here follow the same rules its code states. */

#include "Test.h"

static Bounds B(float x, float y, float w, float h) {
    return {x, y, w, h};
}

static void SnapEdgeWithinThreshold() {
    const float candidates[] = {100.f, 300.f};
    float out = 0;
    // 102 is 2px from 100, which is inside 8.
    utassert(TileSnapEdge(102.f, candidates, 2, 8.f, &out));
    utassertnear(out, 100.f);
}

static void SnapEdgeOutsideThreshold() {
    const float candidates[] = {100.f, 300.f};
    float out = -1;
    // 120 is 20px from the nearest, so nothing snaps.
    utassert(!TileSnapEdge(120.f, candidates, 2, 8.f, &out));
}

static void SnapEdgePicksNearest() {
    const float candidates[] = {308.f, 300.f};
    float out = 0;
    utassert(TileSnapEdge(303.f, candidates, 2, 8.f, &out));
    utassertnear(out, 300.f);
}

static void SnapEdgeEmptyCandidates() {
    float out = -1;
    utassert(!TileSnapEdge(50.f, nullptr, 0, 8.f, &out));
}

// test_resize_right_edge_snaps_to_neighbor_left.
static void ResizeRightEdgeSnapsToNeighbour() {
    Bounds prev = B(0, 0, 196, 100);
    Bounds neighbour = B(200, 0, 100, 100);
    float w = 197;
    Bounds out = TileComputeResizedBounds(prev, nullptr, nullptr, &w, nullptr,
                                          &neighbour, 1, 8.f);
    utassertnear(out.x, 0.f);
    utassertnear(out.w, 200.f);
}

static void ResizeBottomEdgeSnapsToNeighbour() {
    Bounds prev = B(0, 0, 100, 196);
    Bounds neighbour = B(0, 200, 100, 100);
    float h = 197;
    Bounds out = TileComputeResizedBounds(prev, nullptr, nullptr, nullptr, &h,
                                          &neighbour, 1, 8.f);
    utassertnear(out.y, 0.f);
    utassertnear(out.h, 200.f);
}

// The left edge moving pins the right one: the width is whatever is left
// between the snapped left edge and where the right edge already was.
static void ResizeLeftEdgeSnapsAndPinsRight() {
    Bounds prev = B(200, 0, 100, 100);
    Bounds neighbour = B(0, 0, 100, 100);
    float x = 103;
    float w = 197;
    Bounds out = TileComputeResizedBounds(prev, &x, nullptr, &w, nullptr,
                                          &neighbour, 1, 8.f);
    utassertnear(out.x, 100.f);
    utassertnear(out.w, 200.f);
}

static void ResizeCornerSnapsBothEdges() {
    Bounds prev = B(0, 0, 196, 196);
    Bounds others[2] = {B(100, 0, 200, 100), B(0, 100, 100, 150)};
    float w = 298;
    float h = 248;
    Bounds out = TileComputeResizedBounds(prev, nullptr, nullptr, &w, &h,
                                          others, 2, 8.f);
    utassertnear(out.w, 300.f);
    utassertnear(out.h, 250.f);
}

// With no neighbour close, the edge lands on the grid instead.
static void ResizeGridRoundsWithNoNeighbour() {
    Bounds prev = B(0, 0, 100, 100);
    float w = 153;
    Bounds out = TileComputeResizedBounds(prev, nullptr, nullptr, &w, nullptr,
                                          nullptr, 0, 8.f);
    utassertnear(out.w, 152.f);
}

static void ResizeRespectsMinimumSize() {
    Bounds prev = B(0, 0, 100, 100);
    float w = 10;
    Bounds out = TileComputeResizedBounds(prev, nullptr, nullptr, &w, nullptr,
                                          nullptr, 0, 8.f);
    utassertnear(out.w, kTileMinW);
}

static void ResizeWithNothingGivenChangesNothing() {
    Bounds prev = B(0, 0, 100, 100);
    Bounds out = TileComputeResizedBounds(prev, nullptr, nullptr, nullptr,
                                          nullptr, nullptr, 0, 8.f);
    utassertnear(out.x, 0.f);
    utassertnear(out.y, 0.f);
    utassertnear(out.w, 100.f);
    utassertnear(out.h, 100.f);
}

// calculate_magnetic_snap: the top and left of the area come first, and a
// tile near either snaps flush to it.
static void TheAreaEdgesSnapFirst() {
    TilesState s;
    TilesAdd(&s, 0, B(0, 0, 100, 100));
    bool hasX = false;
    bool hasY = false;
    float x = 0;
    float y = 0;
    TilesMagneticSnap(&s, B(3, 5, 100, 100), 0, 8.f, &hasX, &x, &hasY, &y);
    utassert(hasX && TestNear(x, 0.f));
    utassert(hasY && TestNear(y, 0.f));

    // Far from both, and with no neighbour to catch it, nothing snaps.
    TilesMagneticSnap(&s, B(300, 300, 100, 100), 0, 8.f, &hasX, &x, &hasY, &y);
    utassert(!hasX && !hasY);
}

// A tile whose left edge is near a neighbour's right edge snaps flush to it,
// and the tile being dragged is never its own neighbour.
static void ATileSnapsToItsNeighbour() {
    TilesState s;
    TilesAdd(&s, 0, B(0, 200, 100, 100));
    int ix = TilesAdd(&s, 1, B(103, 200, 100, 100));
    bool hasX = false;
    bool hasY = false;
    float x = 0;
    float y = 0;
    TilesMagneticSnap(&s, B(103, 200, 100, 100), ix, 8.f, &hasX, &x, &hasY, &y);
    utassert(hasX && TestNear(x, 100.f));
    // The tops line up exactly, which is a snap of zero distance.
    utassert(hasY && TestNear(y, 200.f));
}

// apply_boundary_constraints: never above the top, and never so far left that
// less than 64px is left to grab.
static void ATileStaysReachable() {
    TilesState s;
    s.dragInitialBounds = B(0, 0, 200, 100);
    Point p = TilesConstrainOrigin(&s, {-500, -40});
    utassertnear(p.x, -136.f);
    utassertnear(p.y, 0.f);
    // Inside the area it is left alone.
    p = TilesConstrainOrigin(&s, {40, 60});
    utassertnear(p.x, 40.f);
    utassertnear(p.y, 60.f);
}

// sorted_panels: by z-index, and by the order they were added where two share
// one.
static void ThePaintOrderIsZIndexThenInsertion() {
    TilesState s;
    TilesAdd(&s, 0, B(0, 0, 10, 10));
    TilesAdd(&s, 1, B(0, 0, 10, 10));
    TilesAdd(&s, 2, B(0, 0, 10, 10));
    s.items[0].zIndex = 2;
    s.items[2].zIndex = 1;
    int order[16] = {};
    TilesPaintOrder(&s, order);
    utassert(order[0] == 1);
    utassert(order[1] == 2);
    utassert(order[2] == 0);

    // With nothing to separate them, they paint in the order they were added.
    s.items[0].zIndex = 0;
    s.items[2].zIndex = 0;
    TilesPaintOrder(&s, order);
    utassert(order[0] == 0 && order[1] == 1 && order[2] == 2);
}

// bring_to_front moves the tile to the end of the list, which is what puts it
// over the others, and the change is one the history can put back.
static void TheTileBroughtToFrontGoesLast() {
    TilesState s;
    TilesAdd(&s, 0, B(0, 0, 10, 10));
    TilesAdd(&s, 1, B(20, 0, 10, 10));
    TilesAdd(&s, 2, B(40, 0, 10, 10));
    utassert(TilesBringToFront(&s, 0) == 2);
    utassert(s.items[0].panel == 1);
    utassert(s.items[1].panel == 2);
    utassert(s.items[2].panel == 0);

    utassert(TilesCanUndo(&s));
    TilesUndo(&s);
    utassert(s.items[0].panel == 0);
    utassert(s.items[1].panel == 1);
    utassert(s.items[2].panel == 2);
    utassert(TilesCanRedo(&s));
    TilesRedo(&s);
    utassert(s.items[2].panel == 0);
}

// A drag records where the tile was and where it ended up, and the release is
// what lands it on the grid.
static void TheReleaseLandsTheTileOnTheGrid() {
    TilesState s;
    s.bounds = B(0, 0, 800, 600);
    TilesAdd(&s, 0, B(200, 200, 100, 100));
    TilesBeginMove(&s, 0, 250, 250);
    // Far from the area edges and with no neighbour, so nothing snaps.
    TilesUpdatePosition(&s, 253, 257);
    utassertnear(s.items[0].bounds.x, 203.f);
    utassertnear(s.items[0].bounds.y, 207.f);
    TilesMouseUp(&s);
    utassertnear(s.items[0].bounds.x, 200.f);
    utassertnear(s.items[0].bounds.y, 208.f);
    utassert(s.dragging < 0);

    // Undo puts back where the drag started.
    while (TilesCanUndo(&s)) {
        TilesUndo(&s);
    }
    utassertnear(s.items[0].bounds.x, 200.f);
    utassertnear(s.items[0].bounds.y, 200.f);
}

// An undo does not record itself, which is what keeps undo and redo from
// growing the history.
static void AnUndoRecordsNothing() {
    TilesState s;
    s.bounds = B(0, 0, 800, 600);
    TilesAdd(&s, 0, B(200, 200, 100, 100));
    TilesBeginMove(&s, 0, 250, 250);
    TilesUpdatePosition(&s, 253, 257);
    int recorded = (s.history.undos.len + s.history.redos.len);
    TilesUndo(&s);
    utassert((s.history.undos.len + s.history.redos.len) == recorded);
    TilesRedo(&s);
    utassert((s.history.undos.len + s.history.redos.len) == recorded);
}

// The room the tiles take between them, which is what the area scrolls over.
// Rust folds it from an empty box at the origin, so the content never starts
// past the origin and is never smaller than the view's own corner.
static void TheContentIsWhateverTheTilesCover() {
    TilesState s;
    Size size = TilesContentSize(&s);
    utassertnear(size.w, 0.f);
    utassertnear(size.h, 0.f);

    TilesAdd(&s, 0, B(16, 16, 300, 200));
    TilesAdd(&s, 1, B(340, 16, 260, 160));
    size = TilesContentSize(&s);
    utassertnear(size.w, 600.f);
    utassertnear(size.h, 216.f);

    // A tile hanging off the left extends the content that way instead.
    TilesAdd(&s, 2, B(-40, 0, 100, 100));
    size = TilesContentSize(&s);
    utassertnear(size.w, 640.f);
    utassertnear(size.h, 216.f);
}

// The pointer is read in the coordinates the tiles are laid out in, which is
// the content's — a scrolled area moves the tiles under the view.
static void ADragReadsThePointerThroughTheScroll() {
    TilesState s;
    s.bounds = B(100, 50, 400, 300);
    s.scrollY = 120;
    TilesAdd(&s, 0, B(0, 200, 100, 100));
    TilesBeginMove(&s, 0, 150, 100);
    utassertnear(s.dragInitialMouse.x, 50.f);
    utassertnear(s.dragInitialMouse.y, 170.f);
    // A move of ten pixels down the window is ten pixels down the content.
    TilesUpdatePosition(&s, 150, 110);
    utassertnear(s.items[0].bounds.y, 210.f);
}

// a_resize_tracks_the_pointer_travel_not_its_window_position.
//
// A resize is resolved against the pointer's travel since the drag began,
// never against the pointer's position: pointer positions are window
// coordinates and tile bounds are canvas coordinates, and the two differ by
// the canvas's own offset in the window. Reading the position directly
// widened the tile by that offset the moment a drag started.
static void AResizeTracksThePointerTravelNotItsWindowPosition() {
    TilesState s;
    // The canvas sits offset in the window, as it always does.
    s.bounds = B(200, 150, 800, 600);
    TilesAdd(&s, 0, B(20, 20, 100, 100));

    // The pointer's window position is nowhere near the tile's canvas bounds.
    const float startX = 500;
    const float startY = 300;
    TilesBeginResize(&s, 0, TileSide::Right, startX, startY);
    TilesUpdateResize(&s, startX, startY);
    utassertnear(s.items[0].bounds.w, 100.f);

    // 30px of travel: 100 + 30 puts the right edge at 150, and GRID_SIZE
    // rounds it to 152, so the width lands at 132. (Upstream's own case is
    // the same arithmetic against the ten-pixel grid its story uses; the
    // crate's GRID_SIZE, which this port pins, is eight.)
    TilesUpdateResize(&s, startX + 30.f, startY);
    utassertnear(s.items[0].bounds.w, 132.f);
    // The pinned edge never moved.
    utassertnear(s.items[0].bounds.x, 20.f);
}

void TestTiles() {
    TestSuite("tiles/snap");
    AResizeTracksThePointerTravelNotItsWindowPosition();
    SnapEdgeWithinThreshold();
    SnapEdgeOutsideThreshold();
    SnapEdgePicksNearest();
    SnapEdgeEmptyCandidates();

    TestSuite("tiles/resize");
    ResizeRightEdgeSnapsToNeighbour();
    ResizeBottomEdgeSnapsToNeighbour();
    ResizeLeftEdgeSnapsAndPinsRight();
    ResizeCornerSnapsBothEdges();
    ResizeGridRoundsWithNoNeighbour();
    ResizeRespectsMinimumSize();
    ResizeWithNothingGivenChangesNothing();

    TestSuite("tiles/move");
    TheAreaEdgesSnapFirst();
    ATileSnapsToItsNeighbour();
    ATileStaysReachable();
    TheReleaseLandsTheTileOnTheGrid();

    TestSuite("tiles/scroll");
    TheContentIsWhateverTheTilesCover();
    ADragReadsThePointerThroughTheScroll();

    TestSuite("tiles/order");
    ThePaintOrderIsZIndexThenInsertion();
    TheTileBroughtToFrontGoesLast();
    AnUndoRecordsNothing();
}
