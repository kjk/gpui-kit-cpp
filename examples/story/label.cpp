#include "Story.h"

// TogglePrefix: the page's one Options row.
enum {
    LabelActPrefix = 3400
};

struct LabelStory {
    bool labelMasked = false;
    // HighlightsMatch::Prefix rather than Full.
    bool prefix = false;
    // The Highlighting section is driven by a search field.
    InputState search;
    StoryToolbarState toolbar;
    bool seeded = false;

    static El* Render(LabelStory* self, Ctx* cx);
};

static void LabelAct(LabelStory* self, Ctx* cx, const ClickEvent*,
                     int64_t act) {
    if (act == LabelActPrefix) {
        self->prefix = !self->prefix;
    } else {
        StoryToolbarApply(&self->toolbar, nullptr, (int)act);
    }
    Notify(cx);
}

static void FocusSearch(LabelStory* self, Ctx* cx, const ClickEvent*) {
    self->search.focused = true;
    Notify(cx);
}

// The Highlighting section's Mask toggle hands over the state it lands on.
static void SetMask(LabelStory* self, Ctx* cx, const ClickEvent*,
                    int64_t checked) {
    self->labelMasked = checked != 0;
    Notify(cx);
}

static void ToggleMask(LabelStory* self, Ctx* cx, const ClickEvent*) {
    self->labelMasked = !self->labelMasked;
    Notify(cx);
}

El* LabelStory::Render(LabelStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->W(kFill)->ItemsCenter()->Gap(24);
    StoryToolbarOpt opts[1] = {{"Prefix Match", self->prefix, LabelActPrefix}};
    page->Child(
        StoryToolbarOptions(cx, self, opts, 1, Listen(cx, &LabelAct), false));

    El* def =
        StorySection(cx, "Default",
                     "Present primary text with optional supporting context.");
    StorySectionBody(def)->W(560)->ItemsCenter();
    El* defCol = Div(a)->FlexCol()->Gap(16)->W(320);
    defCol->Child(component::Label::New(cx, StrL("Account details"))->IntoEl());
    defCol->Child(component::Label::New(cx, StrL("Company address"))
                      ->Secondary(StrL("Optional"))
                      ->IntoEl());
    defCol->Child(component::Label::New(cx, StrL("Workspace owner"))
                      ->Secondary(StrL("Administrator"))
                      ->Semibold()
                      ->IntoEl());
    StorySectionAdd(def, defCol);
    page->Child(def);

    El* hi = StorySection(cx, "Highlighting",
                          "Find matching text across Latin and CJK content.");
    StorySectionBody(hi)->W(560)->ItemsCenter();
    if (!self->seeded) {
        self->seeded = true;
        InputSetPlaceholder(&self->search, StrL("Search labels"));
    }
    if (self->search.focused) {
        cx->win->input = &self->search;
    }
    El* hiCol = Div(a)->FlexCol()->Gap(12)->W(320);
    hiCol->Child(
        Div(a)
            ->FlexRow()
            ->W(kFill)
            ->Gap(8)
            ->Child(Div(a)->Flex1()->Child(
                component::Input::New(cx, StrL("label-search"), &self->search)
                    ->OnFocus(Listen(cx, &FocusSearch))
                    ->IntoEl()))
            ->Child(component::Toggle::New(cx, StrL("highlight-mask"))
                        ->Label(StrL("Mask"))
                        ->Checked(self->labelMasked)
                        ->OnClick(Listen(cx, &SetMask))
                        ->IntoEl()));
    El* hiBox = Div(a)
                    ->FlexCol()
                    ->Gap(12)
                    ->W(kFill)
                    ->Pad(16)
                    ->Border(1, th.border)
                    ->Radius(th.radiusLg);
    Str needle = InputValue(&self->search);
    hiBox->Child(component::Label::New(cx, StrL("Design system documentation"))
                     ->Highlights(needle, self->prefix)
                     ->Masked(self->labelMasked)
                     ->IntoEl());
    // Keeps the mixed ASCII/CJK matching regression visible.
    hiBox->Child(component::Label::New(cx, StrL("AAA中文BB"))
                     ->Highlights(needle, self->prefix)
                     ->Masked(self->labelMasked)
                     ->IntoEl());
    hiCol->Child(hiBox);
    StorySectionAdd(hi, hiCol);
    page->Child(hi);

    El* lay = StorySection(cx, "Layout",
                           "Labels support alignment and natural wrapping.");
    StorySectionBody(lay)->W(560)->ItemsCenter();
    El* layCol = Div(a)->FlexCol()->Gap(16)->W(320);
    El* align = Div(a)
                    ->FlexCol()
                    ->Gap(8)
                    ->Pad(16)
                    ->W(kFill)
                    ->Radius(th.radiusLg)
                    ->Bg(RgbaOpacity(th.muted, 0.4f));
    align->Child(component::Label::New(cx, StrL("Start aligned"))->IntoEl());
    align->Child(component::Label::New(cx, StrL("Center aligned"))
                     ->TextCenter()
                     ->IntoEl());
    align->Child(
        component::Label::New(cx, StrL("End aligned"))->TextRight()->IntoEl());
    layCol->Child(align);
    layCol->Child(Div(a)->W(220)->Child(
        StoryTxt(cx,
                 StrL("Long labels wrap cleanly inside constrained layouts."),
                 14, th.foreground)
            ->Wrap()
            ->LineHeight(1.5f)));
    StorySectionAdd(lay, layCol);
    page->Child(lay);

    El* mask = StorySection(cx, "Masked",
                            "Reveal or conceal sensitive values in place.");
    StorySectionBody(mask)->W(560)->ItemsCenter();
    El* maskRow = Div(a)
                      ->FlexRow()
                      ->W(320)
                      ->ItemsCenter()
                      ->JustifyBetween()
                      ->Pad(16)
                      ->Border(1, th.border)
                      ->Radius(th.radiusLg);
    El* bal = Div(a)->FlexCol()->Gap(4);
    bal->Child(StoryTxt(cx, StrL("Available balance"), 12, th.mutedFg));
    bal->Child(component::Label::New(cx, StrL("$9,182.10"))
                   ->Masked(self->labelMasked)
                   ->Semibold()
                   ->Font(24)
                   ->IntoEl());
    maskRow->Child(bal);
    maskRow
        ->Child(component::Button::New(cx, StrL("btn-mask"))
                    ->Ghost()
                    ->Icon(self->labelMasked ? IconName::EyeOff : IconName::Eye)
                    ->IntoEl()
                    ->OnClick(Listen(cx, &ToggleMask)));
    StorySectionAdd(mask, maskRow);
    page->Child(mask);
    return page;
}

STORY_PAGE(StoryLabel, LabelStory);
