/* Ported from crates/base/src/positioner.rs, mod tests.
 *
 * Two groups, as upstream has them: the positioner's own cases, and the ones
 * that moved in from the tooltip module when its private placement logic was
 * merged into the shared one. */

#include "Test.h"

static const float kMargin = 4.f;
static const float kViewW = 500.f;
static const float kViewH = 400.f;

static Bounds Trigger(float x, float y, float w, float h) {
    Bounds r = {x, y, w, h};
    return r;
}

// Named apart from gpui::Side, the enum.
static Positioned AtSide(Bounds trigger, const Placement* placement,
                         PopupAlign align, float popupW, float popupH) {
    return PositionSide(trigger, {popupW, popupH}, {kViewW, kViewH}, kMargin,
                        placement, align, 0);
}

static void PrefersTheRequestedSideWhenItFits() {
    Placement top = Placement::Top;
    Positioned p =
        AtSide(Trigger(200, 200, 40, 20), &top, PopupAlign::Center, 80, 30);

    utassert(p.hasPlacement && p.placement == Placement::Top);
    utassertnear(p.bounds.Bottom(), 200.f);
}

static void FlipsToTheOppositeSideWhenThePreferredSideDoesNotFit() {
    Placement top = Placement::Top;
    Positioned p =
        AtSide(Trigger(200, 10, 40, 20), &top, PopupAlign::Center, 80, 60);

    utassert(p.hasPlacement && p.placement == Placement::Bottom);
    utassertnear(p.bounds.y, 30.f);
}

static void ClampsIntoTheViewportWhileKeepingTheFlippedSide() {
    Placement bottom = Placement::Bottom;
    Positioned p =
        AtSide(Trigger(480, 200, 40, 20), &bottom, PopupAlign::Center, 120, 30);

    utassert(p.hasPlacement && p.placement == Placement::Bottom);
    utassertnear(p.bounds.Right(), kViewW - kMargin);
}

static void AlignmentSelectsTheLeadingCenterOrTrailingEdge() {
    Bounds trigger = Trigger(200, 200, 100, 20);
    Placement bottom = Placement::Bottom;

    Positioned start = AtSide(trigger, &bottom, PopupAlign::Start, 40, 30);
    Positioned center = AtSide(trigger, &bottom, PopupAlign::Center, 40, 30);
    Positioned end = AtSide(trigger, &bottom, PopupAlign::End, 40, 30);

    utassertnear(start.bounds.x, 200.f);
    utassertnear(center.bounds.x, 230.f);
    utassertnear(end.bounds.x, 260.f);
}

static void SideOffsetAddsAGapBetweenTriggerAndPopup() {
    Placement bottom = Placement::Bottom;
    Positioned p =
        PositionSide(Trigger(200, 200, 40, 20), {40, 30}, {kViewW, kViewH},
                     kMargin, &bottom, PopupAlign::Center, 8);

    utassertnear(p.bounds.y, 228.f);
}

static void CornerPositioningPlacesTheNamedCornerAndNeverReportsASide() {
    Positioned p = PositionCorner(Anchor::TopLeft, {100, 100}, {40, 30},
                                  {kViewW, kViewH}, kMargin);

    utassert(!p.hasPlacement);
    utassertnear(p.bounds.x, 100.f);
    utassertnear(p.bounds.y, 100.f);
}

static void CornerPositioningSupportsGpuisWholeAnchorVocabulary() {
    Positioned center = PositionCorner(Anchor::BottomCenter, {100, 100},
                                       {40, 30}, {kViewW, kViewH}, kMargin);
    utassertnear(center.bounds.x, 80.f);
    utassertnear(center.bounds.y, 70.f);

    Positioned side = PositionCorner(Anchor::RightCenter, {100, 100}, {40, 30},
                                     {kViewW, kViewH}, kMargin);
    utassertnear(side.bounds.x, 60.f);
    utassertnear(side.bounds.y, 85.f);
}

static void CornerPositioningClampsButDoesNotFlip() {
    Positioned p = PositionCorner(Anchor::TopLeft, {480, 390}, {40, 30},
                                  {kViewW, kViewH}, kMargin);

    utassert(!p.hasPlacement);
    utassertnear(p.bounds.Right(), kViewW - kMargin);
    utassertnear(p.bounds.Bottom(), kViewH - kMargin);
}

static El* LayoutPositioner(Positioner* positioner, Arena* a, PaintCtx* paint,
                            float viewW = kViewW, float viewH = kViewH) {
    El* placed = positioner->IntoEl();
    El* root = Div(a)->W(viewW)->H(viewH)->Child(placed);
    paint->viewW = viewW;
    paint->viewH = viewH;
    LayoutEl(paint, root, 0, 0, viewW, viewH, 14, Rgba{});
    return placed;
}

static void PublicPositionerMeasuresAndPlacesItsChildGroup() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    PaintCtx paint = {};

    El* placed =
        LayoutPositioner(Positioner::Side(&cx, Trigger(200, 200, 40, 20))
                             ->Placement(Placement::Bottom)
                             ->Align(Align::Start)
                             ->Offset(8)
                             ->Margin(kMargin)
                             ->Child(Div(a)->W(40)->H(30)),
                         a, &paint);
    utassertnear(placed->x, 200.f);
    utassertnear(placed->y, 228.f);
    utassertnear(placed->w, 40.f);
    utassertnear(placed->h, 30.f);

    ArenaDelete(a);
}

static void PublicPositionerFlipsAllFourSidesAfterMeasurement() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    PaintCtx paint = {};

    El* vertical =
        LayoutPositioner(Positioner::Side(&cx, Trigger(200, 10, 40, 20))
                             ->Placement(Placement::Top)
                             ->Child(Div(a)->W(80)->H(60)),
                         a, &paint);
    utassertnear(vertical->y, 30.f);

    El* horizontal =
        LayoutPositioner(Positioner::Side(&cx, Trigger(260, 60, 32, 32))
                             ->Placement(Placement::Right)
                             ->Align(Align::Center)
                             ->Child(Div(a)->W(240)->H(30)),
                         a, &paint, 300, 200);
    utassertnear(horizontal->x, 20.f);
    utassertnear(horizontal->y, 61.f);

    ArenaDelete(a);
}

static void PublicCornerPositionerUsesTheMeasuredGroupCorner() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    PaintCtx paint = {};

    El* placed = LayoutPositioner(
        Positioner::Corner(&cx, Anchor::BottomRight, {100, 100})
            ->Margin(kMargin)
            ->Child(Div(a)->W(40)->H(30)),
        a, &paint);
    utassertnear(placed->x, 60.f);
    utassertnear(placed->y, 70.f);

    ArenaDelete(a);
}

static void PublicPositionerChildrenHaveNoPortOnlyCapacity() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    PaintCtx paint = {};
    Positioner* positioner = Positioner::Corner(&cx, Anchor::TopLeft, {20, 20});
    for (int i = 0; i < 80; i++) {
        positioner->Child(Div(a)->W(1)->H(2));
    }
    El* placed = LayoutPositioner(positioner, a, &paint);
    utassertnear(placed->x, 20.f);
    utassertnear(placed->y, 20.f);
    utassertnear(placed->w, 80.f);
    utassertnear(placed->h, 2.f);

    PositionerState state = {};
    int stateSize = (int)sizeof(state);
    utassert(stateSize > 0);
    ArenaDelete(a);
}

static void PublicPositionerAddsTheWindowClientInsetToItsMargin() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    PaintCtx paint = {};
    paint.clientInset = 20;

    El* placed =
        LayoutPositioner(Positioner::Corner(&cx, Anchor::TopLeft, {0, 0})
                             ->Margin(kMargin)
                             ->Child(Div(a)->W(40)->H(30)),
                         a, &paint);
    utassertnear(placed->x, 24.f);
    utassertnear(placed->y, 24.f);
    ArenaDelete(a);
}

// ─── migrated from the tooltip module ─────────────────────────────────────
//
// These passed unchanged across that move, which is what proves the merge
// preserved tooltip placement behavior — and here, that our tooltip did not
// change meaning when it moved onto the shared positioner.

static const float kWindowMargin = 4.f;

static Positioned TooltipPlacement(Bounds trigger, float popupW, float popupH,
                                   float viewW, float viewH, float margin,
                                   const Placement* placement) {
    return PositionSide(trigger, {popupW, popupH}, {viewW, viewH}, margin,
                        placement, PopupAlign::Center, 0);
}

static void PrefersAboveWhenSpaceAllows() {
    Bounds trigger = Trigger(100, 80, 80, 24);
    Positioned p =
        TooltipPlacement(trigger, 120, 30, 300, 200, kWindowMargin, nullptr);

    utassert(p.placement == Placement::Top);
    utassertnear(p.bounds.x, 80.f);
    utassertnear(p.bounds.y, 50.f);
}

static void FlipsAndClampsOnEachAxis() {
    Positioned top = TooltipPlacement(Trigger(24, 4, 120, 32), 240, 32, 520,
                                      260, kWindowMargin, nullptr);
    utassert(top.placement == Placement::Bottom);

    Placement right = Placement::Right;
    Positioned toLeft = TooltipPlacement(Trigger(260, 60, 32, 32), 120, 30, 300,
                                         200, kWindowMargin, &right);
    utassert(toLeft.placement == Placement::Left);

    Positioned leftEdge = TooltipPlacement(Trigger(4, 80, 24, 24), 120, 30, 300,
                                           200, kWindowMargin, nullptr);
    utassertnear(leftEdge.bounds.x, kWindowMargin);
}

static void PlacesTooltipToTheRight() {
    Bounds trigger = Trigger(20, 60, 32, 32);
    Placement right = Placement::Right;
    Positioned p =
        TooltipPlacement(trigger, 120, 30, 300, 200, kWindowMargin, &right);

    utassert(p.placement == Placement::Right);
    utassertnear(p.bounds.x, trigger.Right());
    utassertnear(p.bounds.CenterY(), trigger.CenterY());
}

static void RightPlacementClampsVerticalEdges() {
    Bounds trigger = Trigger(20, 2, 32, 20);
    Placement right = Placement::Right;
    Positioned p =
        TooltipPlacement(trigger, 120, 40, 300, 200, kWindowMargin, &right);

    utassert(p.placement == Placement::Right);
    utassertnear(p.bounds.y, kWindowMargin);
    utassertnear(p.bounds.x, trigger.Right());
}

static void UsesLargerSideWhenNeitherVerticalSideFits() {
    Positioned p = TooltipPlacement(Trigger(120, 20, 40, 20), 160, 120, 300,
                                    100, kWindowMargin, nullptr);

    utassert(p.placement == Placement::Bottom);
    utassertnear(p.bounds.y, kWindowMargin);
    utassertnear(p.bounds.x, 60.f);
}

// tooltip.rs `tooltip_priority_exceeds_popup_layer`: a tip floats above the
// dialog and popup layers. Rust sorts deferred elements by priority; the
// layers here are painted in order, so what has to hold is the same relation.
static void ATooltipPaintsAboveThePopupLayer() {
    // Read through a variable: the compiler can see the answer to a compare
    // between two constants, and /W4 says so.
    int tree = kPaintLayerTree;
    int popup = kPaintLayerPopup;
    int tooltip = kPaintLayerTooltip;
    int inspector = kPaintLayerInspector;
    utassert(tooltip > popup);
    utassert(popup > tree);
    utassert(inspector > tooltip);
}

void TestPositioner() {
    TestSuite("positioner");
    PrefersTheRequestedSideWhenItFits();
    FlipsToTheOppositeSideWhenThePreferredSideDoesNotFit();
    ClampsIntoTheViewportWhileKeepingTheFlippedSide();
    AlignmentSelectsTheLeadingCenterOrTrailingEdge();
    SideOffsetAddsAGapBetweenTriggerAndPopup();
    CornerPositioningPlacesTheNamedCornerAndNeverReportsASide();
    CornerPositioningSupportsGpuisWholeAnchorVocabulary();
    CornerPositioningClampsButDoesNotFlip();
    PublicPositionerMeasuresAndPlacesItsChildGroup();
    PublicPositionerFlipsAllFourSidesAfterMeasurement();
    PublicCornerPositionerUsesTheMeasuredGroupCorner();
    PublicPositionerChildrenHaveNoPortOnlyCapacity();
    PublicPositionerAddsTheWindowClientInsetToItsMargin();

    TestSuite("positioner/tooltip");
    PrefersAboveWhenSpaceAllows();
    FlipsAndClampsOnEachAxis();
    PlacesTooltipToTheRight();
    RightPlacementClampsVerticalEdges();
    UsesLargerSideWhenNeitherVerticalSideFits();
    ATooltipPaintsAboveThePopupLayer();
}
