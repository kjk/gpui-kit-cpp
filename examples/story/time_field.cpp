#include "Story.h"

// crates/story/src/stories/time_field_story.rs: five fields, all seeded at
// 09:30, and the value of the first one read back under it.
struct TimeFieldStory {
    Entity<TimeFieldState> minute = {};
    Entity<TimeFieldState> second = {};
    Entity<TimeFieldState> twelveHour = {};
    Entity<TimeFieldState> disabled = {};
    Entity<TimeFieldState> invalid = {};
    LocalTime value = {9, 30, 0};
    Subscription subscription = {};
    StoryToolbarState toolbar;
    bool seeded = false;

    static void OnChange(TimeFieldStory* self, Ctx* cx,
                         const TimeFieldEvent* ev);
    static El* Render(TimeFieldStory* self, Ctx* cx);
};

void TimeFieldStory::OnChange(TimeFieldStory* self, Ctx* cx,
                              const TimeFieldEvent* ev) {
    self->value = ev->time;
    Notify(cx);
}

static Entity<TimeFieldState> SeedTimeField(Ctx* cx, TimePrecision precision,
                                            HourCycle hourCycle,
                                            LocalTime value) {
    Entity<TimeFieldState> e = TimeFieldStateNew(cx, precision, hourCycle);
    if (TimeFieldState* s = e.Get(cx)) {
        TimeFieldStateSetTime(s, value, cx);
    }
    return e;
}

El* TimeFieldStory::Render(TimeFieldStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        LocalTime value = {9, 30, 0};
        self->minute =
            SeedTimeField(cx, TimePrecision::Minute, HourCycle::H23, value);
        self->second =
            SeedTimeField(cx, TimePrecision::Second, HourCycle::H23, value);
        self->twelveHour =
            SeedTimeField(cx, TimePrecision::Minute, HourCycle::H12, value);
        self->disabled =
            SeedTimeField(cx, TimePrecision::Minute, HourCycle::H23, value);
        self->invalid =
            SeedTimeField(cx, TimePrecision::Minute, HourCycle::H23, value);
        self->subscription =
            Subscribe(cx, self->minute, &TimeFieldStory::OnChange);
    }
    UiSize size = self->toolbar.size;
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    El* def = StorySection(
        cx, "Default",
        "Up/Down change the selected segment; digits type it and move to the "
        "next.");
    StorySectionBody(def)->FlexCol()->Gap(12);
    StorySectionAdd(def, component::TimeField::New(cx, self->minute)
                             ->WithSize(size)
                             ->IntoEl());
    // format!("Value: {}", NaiveTime): chrono's Display is HH:MM:SS.
    StorySectionAdd(
        def, StoryTxt(cx,
                      StoryFmt(cx, "Value: %02d:%02d:%02d", self->value.hour,
                               self->value.minute, self->value.second),
                      14, th.mutedFg));
    page->Child(def);

    El* seconds = StorySection(cx, "With seconds",
                               "Set the precision to edit seconds as well.");
    StorySectionAdd(seconds, component::TimeField::New(cx, self->second)
                                 ->WithSize(size)
                                 ->IntoEl());
    page->Child(seconds);

    El* twelve = StorySection(
        cx, "12-hour clock",
        "An AM/PM segment follows the time; type a or p to set it.");
    StorySectionAdd(twelve, component::TimeField::New(cx, self->twelveHour)
                                ->WithSize(size)
                                ->IntoEl());
    page->Child(twelve);

    El* disabled = StorySection(cx, "Disabled", nullptr);
    StorySectionAdd(disabled, component::TimeField::New(cx, self->disabled)
                                  ->WithSize(size)
                                  ->Disabled()
                                  ->IntoEl());
    page->Child(disabled);

    El* invalid =
        StorySection(cx, "Invalid", "Show a validation result from the owner.");
    StorySectionAdd(invalid, component::TimeField::New(cx, self->invalid)
                                 ->WithSize(size)
                                 ->Invalid()
                                 ->IntoEl());
    page->Child(invalid);
    return page;
}

STORY_PAGE(StoryTimeField, TimeFieldStory);
