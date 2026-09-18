/* Side placement for anchored popups — crates/base/src/positioner.rs, as
 * gpui-kit 81305ef4 started using it for dropdowns.
 *
 * `Positioner::side` takes the requested side when the popup fits there, the
 * opposite side when it does not, and the roomier of the two when neither
 * does; the result is then clamped into the viewport. The rule lives in the
 * layout pass here, where the anchor is applied, so these drive it through a
 * layout with a viewport rather than calling it directly. */

#include "Test.h"

namespace {

// A trigger at `y`, with an anchored popup `popupH` tall under it. Answers
// where the popup landed.
static float PopupTop(float viewH, float triggerY, float triggerH, float popupH,
                      bool flip) {
    Arena* a = ArenaNew();
    PaintCtx ctx = {};
    ctx.viewW = 400;
    ctx.viewH = viewH;

    El* root = Div(a)->FlexCol()->W(400)->H(viewH);
    // The space above the trigger, so the trigger sits where the case wants.
    root->Child(Div(a)->W(400)->H(triggerY));
    El* trigger = Div(a)->W(200)->H(triggerH);
    El* popup = Div(a)->W(200)->H(popupH)->AnchorBelow(4);
    if (flip) {
        popup->AnchorFlip();
    }
    trigger->Child(popup);
    root->Child(trigger);

    LayoutEl(&ctx, root, 0, 0, 400, viewH, 14, Rgba{});
    float top = popup->y;
    ArenaDelete(a);
    return top;
}

} // namespace

static void ThePreferredSideIsTakenWhenItFits() {
    // Room below: the popup goes under the trigger, 4 of gap and all.
    utassertnear(PopupTop(600, 100, 30, 100, true), 134.f);
    // And the same without the flip, which is the corner behaviour.
    utassertnear(PopupTop(600, 100, 30, 100, false), 134.f);
}

static void ItFlipsWhenThePreferredSideHasNoRoom() {
    // A trigger near the bottom: 600 - 560 - 4 of gap - 4 of margin leaves 32
    // for a popup of 100, and there is room above, so it opens upward — its
    // bottom edge 4 above the trigger's top.
    utassertnear(PopupTop(600, 530, 30, 100, true), 426.f);
}

// Without the flip it is clamped against the edge instead, which is what a
// corner-anchored popup does and what every dropdown did before 81305ef4.
static void WithoutTheFlipItIsClampedInstead() {
    float top = PopupTop(600, 530, 30, 100, false);
    utassert(top > 456.f);
    utassertnear(top, 600.f - 100.f - 4.f);
}

// Neither side fits: the roomier one wins, and the clamp does the rest.
static void TheRoomierSideWinsWhenNeitherFits() {
    // A 300 tall viewport and a popup of 250. With the trigger at 40 there is
    // 222 below and 32 above, so it goes below and is then pulled up to sit
    // against the bottom margin.
    utassertnear(PopupTop(300, 40, 30, 250, true), 46.f);
    // With the trigger low there is 12 below and 242 above, so it goes above
    // and the clamp holds it at the top margin.
    utassertnear(PopupTop(300, 250, 30, 250, true), 4.f);
}

void TestAnchorFlip() {
    TestSuite("anchor_flip");
    ThePreferredSideIsTakenWhenItFits();
    ItFlipsWhenThePreferredSideHasNoRoom();
    WithoutTheFlipItIsClampedInstead();
    TheRoomierSideWinsWhenNeitherFits();
}
