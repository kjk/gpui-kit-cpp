#include "Story.h"

struct TooltipStory {
    bool tipRemoved = false;
    static El* Render(TooltipStory* self, Ctx* cx);
};

static void RemoveTip(TooltipStory* self, Ctx* cx, const ClickEvent*) {
    self->tipRemoved = true;
    Notify(cx);
}

El* TooltipStory::Render(TooltipStory* self, Ctx* cx) {
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);

    El* btn = StorySection(cx, "Button",
                           "Prefer the left, bottom, or right side, with an "
                           "optional keyboard shortcut hint.");
    El* btnRow = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->Wrap();
    btnRow->Child(component::Button::New(cx, StrL("btn0"))
                      ->Label(StrL("Search"))
                      ->Primary()
                      ->Tooltip(StrL("This is a search Button."))
                      ->TooltipPlacement(Placement::Left)
                      ->IntoEl());
    btnRow->Child(component::Button::New(cx, StrL("btn1"))
                      ->Label(StrL("Info"))
                      ->Tooltip(StrL("This is a tooltip with Action for "
                                     "display keybinding."))
                      ->TooltipPlacement(Placement::Bottom)
                      ->IntoEl());
    btnRow->Child(component::Button::New(cx, StrL("btn2"))
                      ->Label(StrL("Hover me"))
                      ->Tooltip(StrL("This tooltip prefers the right side."))
                      ->TooltipPlacement(Placement::Right)
                      ->IntoEl());
    StorySectionAdd(btn, btnRow);
    page->Child(btn);

    El* chk =
        StorySection(cx, "Checkbox", "Tooltips work on selection controls.");
    StorySectionAdd(chk, component::Checkbox::New(cx, StrL("check"))
                             ->Label(StrL("Remember me"))
                             ->Checked(true)
                             ->Tooltip(StrL("This is a tooltip"))
                             ->IntoEl());
    page->Child(chk);

    El* rad = StorySection(cx, "Radio", "Explain an individual radio option.");
    StorySectionAdd(rad, component::Radio::New(cx, StrL("radio"))
                             ->Label(StrL("Radio with tooltip"))
                             ->Checked(true)
                             ->IntoEl()
                             ->Tip(StrL("This is a radio button")));
    page->Child(rad);

    El* sw = StorySection(cx, "Switch",
                          "Add context without extending the visible label.");
    StorySectionAdd(sw, component::Switch::New(cx, StrL("switch"))
                            ->Checked(true)
                            ->IntoEl()
                            ->Tip(StrL("This is a switch")));
    page->Child(sw);

    El* tog =
        StorySection(cx, "Toggle", "Describe text and icon-only toggles.");
    El* togRow = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    // An unselected Toggle has no frame of its own.
    togRow->Child(component::Button::New(cx, StrL("toggle1"))
                      ->Label(StrL("Bold"))
                      ->Ghost()
                      ->Tooltip(StrL("Toggle bold"))
                      ->IntoEl());
    togRow->Child(component::Button::New(cx, StrL("toggle2"))
                      ->Icon(IconName::Heart)
                      ->Ghost()
                      ->Tooltip(StrL("Toggle favorite"))
                      ->IntoEl());
    StorySectionAdd(tog, togRow);
    page->Child(tog);

    El* clip = StorySection(cx, "Clipboard", "Clarify the copy action.");
    StorySectionAdd(clip, component::Clipboard::New(cx, StrL("clip1"))
                              ->Value(StrL("Hello, World!"))
                              ->Tooltip(StrL("Copy to clipboard"))
                              ->IntoEl());
    page->Child(clip);

    El* custom = StorySection(cx, "Custom content",
                              "Build tooltip content with an action hint.");
    StorySectionAdd(
        custom, StoryTxt(cx, StrL("Hover me"), 14, ThemeNow(cx->app).foreground)
                    ->Tip(StrL("This is a default tooltip style "
                               "by GPUI.")));
    page->Child(custom);

    El* rem = StorySection(cx, "Removed trigger",
                           "Dismiss cleanly when the trigger leaves the view.");
    if (self->tipRemoved) {
        StorySectionAdd(rem, StoryTxt(cx, StrL("Trigger removed"), 13,
                                      ThemeNow(cx->app).mutedFg));
    } else {
        StorySectionAdd(
            rem,
            component::Button::New(cx, StrL("remove-tooltip-trigger"))
                ->Danger()
                ->Label(StrL("Remove me"))
                ->Tooltip(StrL("Clicking this button removes the trigger."))
                ->IntoEl()
                ->OnClick(Listen(cx, &RemoveTip)));
    }
    page->Child(rem);
    return page;
}

STORY_PAGE(StoryTooltip, TooltipStory);
