#include "Story.h"

// crates/story/src/stories/bubble_story.rs

// section(..).max_w(rems(42.5)) — 680 at the 16px root.
static const float kBubbleSectionMaxW = 680;

struct BubbleStory {
    bool expanded = false;

    static void OnToggle(BubbleStory* self, Ctx* cx, const ClickEvent*) {
        self->expanded = !self->expanded;
        Notify(cx);
    }

    static El* Render(BubbleStory* self, Ctx* cx);
};

static El* BubbleSection(Ctx* cx, const char* title, const char* desc,
                         float gap) {
    El* section = StorySection(cx, title, desc);
    StorySectionBody(section)->FlexCol()->Gap(gap)->MaxW(kBubbleSectionMaxW);
    return section;
}

// The story's `start_bubble()`.
static component::Bubble* StartBubble(Ctx* cx) {
    return component::Bubble::New(cx)
        ->Alignment(component::MessageAlignment::Start);
}

static El* BubbleText(Ctx* cx, const char* text) {
    return TextEl(cx->a, Str(text))->Wrap();
}

El* BubbleStory::Render(BubbleStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->W(kFill)->Gap(16);

    El* variants = BubbleSection(
        cx, "Variants",
        "Semantic variants match the Base UI bubble treatments.", 16);
    StorySectionAdd(variants,
                    StartBubble(cx)
                        ->Child(BubbleText(cx, "A strong primary bubble."))
                        ->IntoEl());
    StorySectionAdd(variants,
                    StartBubble(cx)
                        ->WithVariant(component::BubbleVariant::Secondary)
                        ->Child(BubbleText(cx, "The neutral secondary bubble."))
                        ->IntoEl());
    StorySectionAdd(
        variants, StartBubble(cx)
                      ->WithVariant(component::BubbleVariant::Muted)
                      ->Child(BubbleText(cx, "A lower-emphasis muted bubble."))
                      ->IntoEl());
    StorySectionAdd(
        variants, StartBubble(cx)
                      ->WithVariant(component::BubbleVariant::Tinted)
                      ->Child(BubbleText(cx, "A softly tinted primary bubble."))
                      ->IntoEl());
    StorySectionAdd(
        variants,
        StartBubble(cx)
            ->WithVariant(component::BubbleVariant::Outline)
            ->Child(BubbleText(cx, "A bordered bubble for rich content."))
            ->IntoEl());
    StorySectionAdd(
        variants,
        StartBubble(cx)
            ->WithVariant(component::BubbleVariant::Destructive)
            ->Child(BubbleText(cx, "A failed action with its reason in text."))
            ->IntoEl());
    StorySectionAdd(
        variants, StartBubble(cx)
                      ->WithVariant(component::BubbleVariant::Ghost)
                      ->Child(BubbleText(
                          cx,
                          "Ghost content is unframed and can use the full row "
                          "width."))
                      ->IntoEl());
    page->Child(variants);

    El* alignment = BubbleSection(
        cx, "Alignment", "Use the same alignment value as Message.", 12);
    StorySectionAdd(alignment,
                    StartBubble(cx)
                        ->WithVariant(component::BubbleVariant::Secondary)
                        ->Child(BubbleText(cx, "Incoming message"))
                        ->IntoEl());
    StorySectionAdd(alignment, component::Bubble::New(cx)
                                   ->Alignment(component::MessageAlignment::End)
                                   ->Child(BubbleText(cx, "Outgoing message"))
                                   ->IntoEl());
    page->Child(alignment);

    El* reactions = BubbleSection(
        cx, "Reactions",
        "Use action for integrated Button controls; child keeps emoji and "
        "arbitrary reactions composable.",
        32);
    StorySectionBody(reactions)->PadY(24);
    StorySectionAdd(
        reactions,
        StartBubble(cx)
            ->WithVariant(component::BubbleVariant::Outline)
            ->Child(BubbleText(cx, "This bubble has reaction feedback."))
            ->Reactions(component::BubbleReactions::New(cx)->Action(
                component::Button::New(cx, StrL("bubble-like"))
                    ->Ghost()
                    ->WithSize(UiSize::Small)
                    ->Label(StrL("👍 2"))
                    ->Tooltip(StrL("Like this message"))))
            ->IntoEl());
    StorySectionAdd(
        reactions,
        component::Bubble::New(cx)
            ->Alignment(component::MessageAlignment::End)
            ->Child(BubbleText(cx, "Reactions can attach to any edge."))
            ->Reactions(component::BubbleReactions::New(cx)
                            ->Side(component::BubbleReactionSide::Top)
                            ->Alignment(component::MessageAlignment::Start)
                            ->Child(TextEl(a, StrL("✨ 1"))))
            ->IntoEl());
    page->Child(reactions);

    El* group = BubbleSection(
        cx, "Group",
        "Group consecutive bubbles using the shared spacing scale.", 20);
    StorySectionAdd(
        group,
        component::BubbleGroup::New(cx)
            ->Child(StartBubble(cx)
                        ->WithVariant(component::BubbleVariant::Secondary)
                        ->Child(BubbleText(cx,
                                           "Can you tell me what "
                                           "changed?"))
                        ->IntoEl())
            ->Child(StartBubble(cx)
                        ->WithVariant(component::BubbleVariant::Secondary)
                        ->Child(BubbleText(cx,
                                           "The registry route was "
                                           "stale."))
                        ->IntoEl())
            ->IntoEl()
            ->W(kFill));
    StorySectionAdd(
        group,
        component::BubbleGroup::New(cx)
            ->Child(component::Bubble::New(cx)
                        ->Alignment(component::MessageAlignment::End)
                        ->Child(BubbleText(cx, "I removed the stale route."))
                        ->IntoEl())
            ->Child(component::Bubble::New(cx)
                        ->Alignment(component::MessageAlignment::End)
                        ->Child(BubbleText(cx,
                                           "The updated registry is ready "
                                           "to review."))
                        ->IntoEl())
            ->IntoEl()
            ->W(kFill));
    page->Child(group);

    El* links = BubbleSection(
        cx, "Links and buttons",
        "Compose external links and application actions inside a bubble.", 12);
    StorySectionAdd(
        links,
        StartBubble(cx)
            ->WithVariant(component::BubbleVariant::Outline)
            ->Content(component::BubbleContent::New(cx)->Child(
                Div(a)
                    ->FlexCol()
                    ->Gap(8)
                    ->Child(BubbleText(
                        cx, "The implementation guide is available online."))
                    ->Child(component::Link::New(
                                cx, StrL("bubble-documentation-link"))
                                ->Href(StrL("https://gpui-kit.com/"))
                                ->Text(StrL("Open the component documentation"))
                                ->IntoEl())))
            ->IntoEl());
    StorySectionAdd(
        links,
        StartBubble(cx)
            ->WithVariant(component::BubbleVariant::Muted)
            ->Content(component::BubbleContent::New(cx)->Child(
                Div(a)
                    ->FlexRow()
                    ->ItemsCenter()
                    ->Gap(12)
                    ->Child(BubbleText(cx, "The generated report is ready."))
                    ->Child(
                        component::Button::New(cx, StrL("bubble-open-report"))
                            ->Ghost()
                            ->WithSize(UiSize::Small)
                            ->Label(StrL("Open report"))
                            ->IntoEl())))
            ->IntoEl());
    page->Child(links);

    El* collapsible = BubbleSection(
        cx, "Collapsible content",
        "Keep long responses readable with an explicit disclosure action.", 0);
    StorySectionAdd(
        collapsible,
        StartBubble(cx)
            ->WithVariant(component::BubbleVariant::Muted)
            ->Content(component::BubbleContent::New(cx)->Child(
                component::Collapsible::New(cx)
                    ->Gap(8)
                    ->Open(self->expanded)
                    ->Trigger(Div(a)
                                  ->FlexCol()
                                  ->Gap(8)
                                  ->Child(BubbleText(
                                      cx,
                                      "The accessibility review found two "
                                      "focus states that need more contrast."))
                                  ->Child(component::Button::New(
                                              cx, StrL("bubble-toggle-details"))
                                              ->Ghost()
                                              ->WithSize(UiSize::Small)
                                              ->Icon(IconName::ChevronsUpDown)
                                              ->Label(self->expanded
                                                          ? StrL("Show less")
                                                          : StrL("Show more"))
                                              ->OnClick(Listen(
                                                  cx, &BubbleStory::OnToggle))
                                              ->IntoEl()))
                    ->Content(Div(a)
                                  ->FlexCol()
                                  ->Gap(8)
                                  ->Child(BubbleText(
                                      cx,
                                      "Dialog and sheet controls already keep "
                                      "their focus rings visible."))
                                  ->Child(BubbleText(
                                      cx,
                                      "Update the menu focus token separately "
                                      "from its pointer hover state.")))
                    ->IntoEl()))
            ->IntoEl());
    page->Child(collapsible);

    El* tooltip = BubbleSection(
        cx, "Tooltip",
        "Label icon-only reaction controls with their concrete meaning.", 0);
    StorySectionBody(tooltip)->PadY(16);
    StorySectionAdd(
        tooltip,
        component::Bubble::New(cx)
            ->Alignment(component::MessageAlignment::End)
            ->Child(BubbleText(cx, "The updated registry route is live."))
            ->Reactions(component::BubbleReactions::New(cx)->Action(
                component::Button::New(cx, StrL("bubble-delivery-details"))
                    ->Ghost()
                    ->WithSize(UiSize::XSmall)
                    ->Icon(IconName::CircleCheck)
                    ->Tooltip(StrL("Read today at 4:32 PM"))))
            ->IntoEl());
    page->Child(tooltip);

    El* popover = BubbleSection(
        cx, "Popover", "Use a semantic popover for contextual failure details.",
        0);
    StorySectionBody(popover)->PadY(16);
    StorySectionAdd(
        popover,
        StartBubble(cx)
            ->WithVariant(component::BubbleVariant::Destructive)
            ->Child(BubbleText(cx, "The build command could not finish."))
            ->Reactions(
                component::BubbleReactions::New(cx)
                    ->Refine(Style{}, 0)
                    ->Child(
                        component::Popover::New(cx,
                                                StrL("bubble-error-popover"))
                            ->Trigger(component::Button::New(
                                          cx, StrL("bubble-show-error"))
                                          ->Ghost()
                                          ->WithSize(UiSize::XSmall)
                                          ->Icon(IconName::Info)
                                          ->Rounded(th.radiusFull)
                                          ->Tooltip(StrL("Show error details"))
                                          ->IntoEl())
                            ->Content(
                                Div(a)
                                    ->FlexCol()
                                    ->W(256)
                                    ->Gap(8)
                                    ->Child(
                                        BubbleText(cx, "Build command failed"))
                                    ->Child(BubbleText(
                                        cx,
                                        "The workspace lockfile could not "
                                        "be found.")))
                            ->IntoEl()))
            ->IntoEl());
    page->Child(popover);

    El* rich = BubbleSection(
        cx, "Rich content",
        "Any GPUI element can be placed directly in the surface.", 12);
    StorySectionAdd(
        rich,
        StartBubble(cx)
            ->Content(component::BubbleContent::New(cx)->Child(
                Div(a)
                    ->FlexRow()
                    ->ItemsCenter()
                    ->Gap(12)
                    ->Child(Div(a)->W(40)->H(40)->Radius(th.radius)->Bg(
                        RgbaOpacity(th.primaryFg, 0.18f)))
                    ->Child(
                        Div(a)
                            ->FlexCol()
                            ->Child(BubbleText(cx, "design-notes.pdf"))
                            ->Child(Div(a)->Opacity(0.75f)->Child(
                                TextEl(a, StrL("2.4 MB · PDF"))->Font(12))))))
            ->IntoEl());
    StorySectionAdd(rich,
                    StartBubble(cx)
                        ->WithVariant(component::BubbleVariant::Secondary)
                        ->Child(BubbleText(
                            cx,
                            "A longer message wraps naturally within the "
                            "bubble's available width, preserving the shared "
                            "text scale, comfortable reading rhythm, and "
                            "leading alignment."))
                        ->IntoEl());
    page->Child(rich);

    El* custom = BubbleSection(cx, "Custom style",
                               "Caller refinements override the surface "
                               "defaults.",
                               0);
    Style surface = {};
    surface.radius = th.radius;
    surface.bg = Background(RgbaOpacity(th.success, 0.15f));
    surface.color = th.success;
    surface.borderColor = RgbaOpacity(th.success, 0.35f);
    StorySectionAdd(
        custom,
        StartBubble(cx)
            ->Content(component::BubbleContent::New(cx)
                          ->Refine(surface, StyleFieldRadius | StyleFieldBg |
                                                StyleFieldColor |
                                                StyleFieldBorderColor)
                          ->Child(BubbleText(cx, "Custom semantic color")))
            ->IntoEl());
    page->Child(custom);
    return page;
}

STORY_PAGE(StoryBubble, BubbleStory);
