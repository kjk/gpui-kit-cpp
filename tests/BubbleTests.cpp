/* Ported from the tests in crates/ui/src/bubble.rs: test_bubble_builder. */

#include "Test.h"

using namespace gpui::component;

static void TheBuilderCarriesAlignmentVariantAndReactions() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Bubble* bubble =
        Bubble::New(&cx)
            ->Alignment(MessageAlignment::End)
            ->WithVariant(BubbleVariant::Outline)
            ->Content(BubbleContent::New(&cx)->Child(TextEl(a, StrL("Hello"))))
            ->Reactions(BubbleReactions::New(&cx)
                            ->Child(TextEl(a, StrL("👍"))));

    utassert(bubble->hasAlignment);
    utassert(bubble->alignment == MessageAlignment::End);
    utassert(bubble->variant == BubbleVariant::Outline);
    utassert(len(bubble->content->children) == 1);
    utassert(bubble->reactions != nullptr);
    utassert(!bubble->IsGhost());
    utassert(Bubble::New(&cx)->WithVariant(BubbleVariant::Ghost)->IsGhost());

    // Direct children survive a later Content(..) call and stay in front of
    // the new surface's own children.
    Bubble* reordered =
        Bubble::New(&cx)
            ->Child(TextEl(a, StrL("Existing")))
            ->Content(BubbleContent::New(&cx)
                          ->Child(TextEl(a, StrL("Configured"))));
    utassert(len(reordered->content->children) == 2);
    utassert(
        base::StrEq(reordered->content->children[0]->text, StrL("Existing")));
    utassert(
        base::StrEq(reordered->content->children[1]->text, StrL("Configured")));

    BubbleGroup* group = BubbleGroup::New(&cx)
                             ->Child(TextEl(a, StrL("First")))
                             ->Child(TextEl(a, StrL("Second")));
    utassert(len(group->children) == 2);

    BubbleReactions* reactions = BubbleReactions::New(&cx)
                                     ->Side(BubbleReactionSide::Top)
                                     ->Alignment(MessageAlignment::Start)
                                     ->Child(TextEl(a, StrL("👍 2")));
    utassert(reactions->side == BubbleReactionSide::Top);
    utassert(reactions->alignment == MessageAlignment::Start);
    utassert(len(reactions->children) == 1);

    BubbleReactions* mixed =
        BubbleReactions::New(&cx)
            ->Action(component::Button::New(&cx, StrL("reaction-action")))
            ->Child(TextEl(a, StrL("👍")));
    utassert(len(mixed->children) == 2);
    utassert(mixed->children[0].action != nullptr);
    utassert(mixed->children[1].element != nullptr);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// The surface each variant paints, which is the half the builder cannot show.
static void EachVariantPaintsItsOwnSurface() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;
    const Theme& th = ThemeNow(&app);

    El* filled = Bubble::New(&cx)->IntoEl();
    // The root owns the row layout; the surface is its one child.
    utassertnear(filled->style.maxWFrac, 0.8f);
    El* surface = filled->first;
    utassert(surface != nullptr);
    utassertnear(surface->style.pad.left, 12.f);
    utassertnear(surface->style.pad.top, 8.f);
    utassertnear(surface->style.fontSize, 14.f);
    utassertnear(surface->style.lineHeight, 1.625f);
    utassert(RgbaEq(surface->style.bg.color, th.primary));
    utassert(RgbaEq(surface->style.color, th.primaryFg));

    // `secondary` uses the near-background muted tier, not the button role.
    El* secondary =
        Bubble::New(&cx)->WithVariant(BubbleVariant::Secondary)->IntoEl();
    utassert(RgbaEq(secondary->first->style.bg.color, th.muted));
    utassert(RgbaEq(secondary->first->style.color, th.secondaryFg));

    El* outline =
        Bubble::New(&cx)->WithVariant(BubbleVariant::Outline)->IntoEl();
    utassertnear(outline->first->style.border, 1.f);
    utassert(RgbaEq(outline->first->style.borderColor, th.border));

    // Ghost takes the whole row, drops the surface and squares its corners.
    El* ghost = Bubble::New(&cx)->WithVariant(BubbleVariant::Ghost)->IntoEl();
    utassertnear(ghost->style.maxWFrac, 1.f);
    utassertnear(ghost->first->style.radius, 0.f);
    utassertnear(ghost->first->style.pad.left, 0.f);
    utassert(ghost->first->style.bg.color.a == 0);

    // A reaction pill hangs off the named edge, three quarters outside.
    El* withReactions =
        Bubble::New(&cx)
            ->Reactions(BubbleReactions::New(&cx)->Child(TextEl(a, StrL("x"))))
            ->IntoEl();
    El* pill = withReactions->first->next;
    utassert(pill != nullptr);
    utassert(pill->style.absolute);
    utassertnear(pill->style.absBottom, -20.f);
    utassertnear(pill->style.absRight, 12.f);
    utassertnear(pill->style.border, 3.f);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

void TestBubble() {
    TestSuite("bubble");
    TheBuilderCarriesAlignmentVariantAndReactions();
    EachVariantPaintsItsOwnSurface();
}
