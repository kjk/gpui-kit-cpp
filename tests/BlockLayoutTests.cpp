/* GPUI's `div()` is `display: block`, and `.flex()` — which `h_flex()` and
 * `v_flex()` call for you — is what turns the flex model on. The difference
 * shows up whenever the content is taller than the box holding it: a block
 * container hands each child the height it asked for and lets the column
 * overflow, which is what a scroll container is for, while a flex container
 * reads the same thing as a deficit and shrinks every child to close it.
 *
 * The tree this pins is `examples/table_in_scrollable`, cut down to its
 * shape: a 700x700 scrolling page holding a column of 400 + 300 + 800. It
 * squashed the 400 to 112 while every box in this tree was a flex
 * container. */

#include "Test.h"

// The page from the example: root (block, the size of the window, scrolling)
// > column (flex, padded and gapped) > three fixed-height fillers.
static El* ScrollPage(Arena* a, El** outCol, El* fillers[3]) {
    El* root = Div(a)->SizeFull()->ClipY();
    El* col = Div(a)->FlexCol()->Pad(16)->Gap(16);
    for (int i = 0; i < 3; i++) {
        col->Child(fillers[i]);
    }
    root->Child(col);
    *outCol = col;
    return root;
}

static void ABlockPageOverflowsInsteadOfSquashing() {
    TestSuite("gpui block layout");

    Arena* a = ArenaNew();
    El* fillers[3] = {Div(a)->H(400)->W(kFill), Div(a)->H(300)->W(kFill),
                      Div(a)->H(800)->W(kFill)};
    El* col = nullptr;
    El* root = ScrollPage(a, &col, fillers);
    LayoutEl(nullptr, root, 0, 0, 700, 700, 14, Rgba{});

    // The window's box is the window's box.
    utassertnear(root->w, 700.f);
    utassertnear(root->h, 700.f);
    // 16 + 400 + 16 + 300 + 16 + 800 + 16 — taller than the page, and meant
    // to be.
    utassertnear(col->h, 1564.f);
    utassertnear(fillers[0]->h, 400.f);
    utassertnear(fillers[1]->h, 300.f);
    utassertnear(fillers[2]->h, 800.f);
    // A block child fills the container's content box across.
    utassertnear(col->w, 700.f);
    // 16 of padding, then the first filler, then a 16 gap.
    utassertnear(fillers[0]->y, 16.f);
    utassertnear(fillers[1]->y, 432.f);
    ArenaDelete(a);
}

// The same tree with the page made a flex container, which is what every box
// in this tree used to be. The column and the fillers clip, so their automatic
// minimum size is zero — CSS's rule for a scroll container — and there is
// nothing to stop flexbox sharing the 864px deficit out among them. That is the
// reading the fix moved away from, and it is here so the two stay told apart
// rather than rediscovered.
static void AFlexPageSharesTheDeficitOut() {
    Arena* a = ArenaNew();
    El* fillers[3] = {Div(a)->H(400)->W(kFill)->ClipY(),
                      Div(a)->H(300)->W(kFill)->ClipY(),
                      Div(a)->H(800)->W(kFill)->ClipY()};
    El* col = nullptr;
    El* root = ScrollPage(a, &col, fillers);
    root->FlexCol();
    col->ClipY();
    LayoutEl(nullptr, root, 0, 0, 700, 700, 14, Rgba{});

    utassertnear(col->h, 700.f);
    utassert(fillers[0]->h < 400.f);
    ArenaDelete(a);
}

// A bare Div is a block container; an alignment, a justification or a gap is
// this tree's way of saying `.flex()`, since that is what its callers wrote
// when every box was already one.
static void AnAlignmentTurnsTheFlexModelOn() {
    Arena* a = ArenaNew();
    utassert(Div(a)->style.display == Display::Block);
    utassert(Div(a)->W(10)->style.display == Display::Block);
    utassert(Div(a)->Flex()->style.display == Display::Flex);
    utassert(Div(a)->FlexRow()->style.display == Display::Flex);
    utassert(Div(a)->FlexCol()->style.display == Display::Flex);
    utassert(Div(a)->ItemsCenter()->style.display == Display::Flex);
    utassert(Div(a)->JustifyBetween()->style.display == Display::Flex);
    utassert(Div(a)->Gap(4)->style.display == Display::Flex);
    ArenaDelete(a);
}

void TestGpuiBlockLayout() {
    ABlockPageOverflowsInsteadOfSquashing();
    AFlexPageSharesTheDeficitOut();
    AnAlignmentTurnsTheFlexModelOn();
}
