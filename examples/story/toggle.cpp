#include "Story.h"

struct ToggleStory {
    int toggleSel = 1;
    bool toggles[10] = {};
    // Single-selection groups. -1 is no selection. Week starts checked.
    int period = 1;
    int density = -1;
    StoryToolbarState toolbar;

    static El* Render(ToggleStory* self, Ctx* cx);
};

static void OnPreview(ToggleStory* self, Ctx* cx, const ClickEvent*,
                      int64_t checked) {
    self->toggleSel = checked ? 1 : 0;
    Notify(cx);
}

static void OnFavorite(ToggleStory* self, Ctx* cx, const ClickEvent*,
                       int64_t checked) {
    self->toggles[0] = checked != 0;
    Notify(cx);
}

static void CopyToggleGroup(ToggleStory* self, Ctx* cx,
                            const component::ToggleGroupEvent* event,
                            int first) {
    int count = std::min(event->count, 10 - first);
    for (int i = 0; i < count; i++)
        self->toggles[first + i] = event->checked[i];
    Notify(cx);
}

static void OnGhostGroup(ToggleStory* self, Ctx* cx,
                         const component::ToggleGroupEvent* event) {
    CopyToggleGroup(self, cx, event, 1);
}

static void OnOutlineGroup(ToggleStory* self, Ctx* cx,
                           const component::ToggleGroupEvent* event) {
    CopyToggleGroup(self, cx, event, 4);
}

static void OnSegmentedGroup(ToggleStory* self, Ctx* cx,
                             const component::ToggleGroupEvent* event) {
    CopyToggleGroup(self, cx, event, 7);
}

static int FirstChecked(const component::ToggleGroupEvent* event) {
    for (int i = 0; event && i < event->count; i++) {
        if (event->checked[i]) return i;
    }
    return -1;
}

static void OnPeriod(ToggleStory* self, Ctx* cx,
                     const component::ToggleGroupEvent* event) {
    self->period = FirstChecked(event);
    Notify(cx);
}

static void OnDensity(ToggleStory* self, Ctx* cx,
                      const component::ToggleGroupEvent* event) {
    self->density = FirstChecked(event);
    Notify(cx);
}

El* ToggleStory::Render(ToggleStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    UiSize size = self->toolbar.size;
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    El* def = StorySection(cx, "Default",
                           "Text and icon toggles with clear selected states.");
    StorySectionBody(def)->FlexCol()->W(512)->ItemsCenter()->Gap(12);
    El* defRow = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    defRow->Child(component::Toggle::New(cx, StrL("preview"))
                      ->Label(StrL("Preview"))
                      ->WithSize(size)
                      ->Checked(self->toggleSel == 1)
                      ->OnClick(Listen(cx, &OnPreview))
                      ->IntoEl());
    defRow->Child(component::Toggle::New(cx, StrL("favorite"))
                      ->Icon(IconName::Star)
                      ->WithSize(size)
                      ->Checked(self->toggles[0])
                      ->OnClick(Listen(cx, &OnFavorite))
                      ->IntoEl());
    StorySectionAdd(def, defRow);
    page->Child(def);

    El* vars = StorySection(
        cx, "Variants", "Ghost and outline treatments for different surfaces.");
    StorySectionBody(vars)->W(512)->FlexCol()->ItemsCenter()->Gap(16);
    StorySectionAdd(vars, StoryTxt(cx, StrL("Ghost"), 14, th.foreground)
                              ->Medium());
    component::ToggleGroup* ghost =
        component::ToggleGroup::New(cx, StrL("ghost-group"))
            ->WithSize(size)
            ->OnClick(Listen(cx, &OnGhostGroup));
    ghost->Child(component::Toggle::New(cx, StrL("ghost-bell"))
                     ->Icon(IconName::Bell)
                     ->Checked(self->toggles[1]));
    ghost->Child(component::Toggle::New(cx, StrL("ghost-inbox"))
                     ->Icon(IconName::Inbox)
                     ->Checked(self->toggles[2]));
    ghost->Child(component::Toggle::New(cx, StrL("ghost-check"))
                     ->Icon(IconName::Check)
                     ->Checked(self->toggles[3]));
    StorySectionAdd(vars, ghost->IntoEl());
    StorySectionAdd(vars, StoryTxt(cx, StrL("Outline"), 14, th.foreground)
                              ->Medium());
    component::ToggleGroup* outline =
        component::ToggleGroup::New(cx, StrL("outline-group"))
            ->Outline()
            ->WithSize(size)
            ->OnClick(Listen(cx, &OnOutlineGroup));
    outline->Child(component::Toggle::New(cx, StrL("outline-bell"))
                       ->Icon(IconName::Bell)
                       ->Checked(self->toggles[4]));
    outline->Child(component::Toggle::New(cx, StrL("outline-inbox"))
                       ->Icon(IconName::Inbox)
                       ->Checked(self->toggles[5]));
    outline->Child(component::Toggle::New(cx, StrL("outline-check"))
                       ->Icon(IconName::Check)
                       ->Checked(self->toggles[6]));
    StorySectionAdd(vars, outline->IntoEl());
    page->Child(vars);

    El* grp = StorySection(cx, "Group",
                           "Connected toggles keep related choices together.");
    StorySectionBody(grp)->W(512)->FlexCol()->ItemsCenter();
    component::ToggleGroup* segmented =
        component::ToggleGroup::New(cx, StrL("segmented-group"))
            ->Segmented()
            ->Outline()
            ->WithSize(size)
            ->OnClick(Listen(cx, &OnSegmentedGroup));
    segmented->Child(component::Toggle::New(cx, StrL("bold"))
                         ->Label(StrL("Bold"))
                         ->Checked(self->toggles[7]));
    segmented->Child(component::Toggle::New(cx, StrL("italic"))
                         ->Label(StrL("Italic"))
                         ->Checked(self->toggles[8]));
    segmented->Child(component::Toggle::New(cx, StrL("code"))
                         ->Label(StrL("Code"))
                         ->Checked(self->toggles[9]));
    StorySectionAdd(grp, segmented->IntoEl());
    page->Child(grp);

    El* single = StorySection(
        cx, "Single selection",
        "One item at a time. Selecting the chosen item again clears it.");
    StorySectionBody(single)->FlexCol()->W(512)->ItemsCenter()->Gap(12);
    component::ToggleGroup* period =
        component::ToggleGroup::New(cx, StrL("period"))
            ->Single()
            ->Segmented()
            ->Outline()
            ->WithSize(size)
            ->OnClick(Listen(cx, &OnPeriod));
    period->Child(component::Toggle::New(cx, StrL("day"))
                      ->Label(StrL("Day"))
                      ->Checked(self->period == 0));
    period->Child(component::Toggle::New(cx, StrL("week"))
                      ->Label(StrL("Week"))
                      ->Checked(self->period == 1));
    period->Child(component::Toggle::New(cx, StrL("month"))
                      ->Label(StrL("Month"))
                      ->Checked(self->period == 2));
    StorySectionAdd(single, period->IntoEl());
    component::ToggleGroup* density =
        component::ToggleGroup::New(cx, StrL("density"))
            ->Single()
            ->WithSize(size)
            ->OnClick(Listen(cx, &OnDensity));
    density->Child(component::Toggle::New(cx, StrL("compact"))
                       ->Label(StrL("Compact"))
                       ->Checked(self->density == 0));
    density->Child(component::Toggle::New(cx, StrL("comfortable"))
                       ->Label(StrL("Comfortable"))
                       ->Checked(self->density == 1));
    density->Child(component::Toggle::New(cx, StrL("spacious"))
                       ->Label(StrL("Spacious"))
                       ->Checked(self->density == 2));
    StorySectionAdd(single, density->IntoEl());
    page->Child(single);
    return page;
}

STORY_PAGE(StoryToggle, ToggleStory);
