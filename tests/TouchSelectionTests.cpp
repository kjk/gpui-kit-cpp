/* Ported from crates/base/src/touch_selection.rs. */

#include "Test.h"

static void SnapshotReportsBoundsAndEdges() {
    Bounds start = TouchCaretLineBox({10, 20}, 16);
    Bounds end = TouchCaretLineBox({80, 52}, 16);
    TouchSelectionSnapshot snapshot = TouchSelectionSnapshot::New(start, end)
                                          .WithMenuOpen(true)
                                          .WithDragging(SelectionEdge::End);
    utassert(snapshot.Edge(SelectionEdge::Start).x == 10);
    utassert(snapshot.Edge(SelectionEdge::End).x == 80);
    utassert(!snapshot.IsEmpty() && snapshot.menuOpen);
    utassert(snapshot.hasDragging && snapshot.dragging == SelectionEdge::End);
    Bounds bounds;
    utassert(snapshot.BoundsOfVisible(&bounds));
    utassertnear(bounds.x, 10);
    utassertnear(bounds.y, 20);
    utassertnear(bounds.w, 70);
    utassertnear(bounds.h, 48);
}

static void AScrolledAwayEndKeepsItsGeometry() {
    Bounds start = TouchCaretLineBox({10, -30}, 16);
    Bounds end = TouchCaretLineBox({80, 52}, 16);
    Bounds viewport = {0, 0, 200, 100};
    utassert(!TouchCaretInView(start, viewport));
    utassert(TouchCaretInView(end, viewport));
    TouchSelectionSnapshot snapshot =
        TouchSelectionSnapshot::New(start, end)
            .WithEdgeVisible(SelectionEdge::Start, false);
    Bounds bounds;
    utassert(snapshot.BoundsOfVisible(&bounds));
    utassertnear(bounds.x, end.x);
    snapshot = snapshot.WithEdgeVisible(SelectionEdge::End, false);
    utassert(!snapshot.BoundsOfVisible(&bounds));
}

static void EdgeDragKeepsTheFingerOffset() {
    Bounds caret = TouchCaretLineBox({100, 40}, 20);
    TouchEdgeDrag drag =
        TouchEdgeDrag::Begin(SelectionEdge::End, caret, {102, 72});
    Point text = drag.TextPosition({150, 90});
    utassertnear(text.x, 148);
    utassertnear(text.y, 68);
}

void TestTouchSelection() {
    TestSuite("touch_selection");
    SnapshotReportsBoundsAndEdges();
    AScrolledAwayEndKeepsItsGeometry();
    EdgeDragKeepsTheFingerOffset();
}
