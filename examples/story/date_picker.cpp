#include "Story.h"

// The DatePickerState entities in the Rust story, in declaration order.
enum {
    DpDefault = 0,
    DpSmall,
    DpLarge,
    DpCustom,
    DpDateRange,
    DpEmptyRange,
    DpBirthday,
    DpNoAppearance,
    DpDateTime,
    DpDateTimeSecond,
    DpDateTime12h,
    DpCount
};

struct DatePickerStory {
    Entity<component::DatePickerState> pickers[DpCount] = {};
    Subscription subscriptions[6] = {};
    // format!("Value: {:?}") of the subscribed Option<String>.
    char value[96] = "None";
    // The same for the three date-and-time pickers.
    char dateTimeValue[96] = "None";
    StoryToolbarState toolbar;
    bool seeded = false;

    static void OnChange(DatePickerStory* self, Ctx* cx,
                         const component::DatePickerEvent* ev);
    static void OnDateTimeChange(DatePickerStory* self, Ctx* cx,
                                 const component::DatePickerEvent* ev);
    static El* Render(DatePickerStory* self, Ctx* cx);
};

static bool FirstFiveDays(LocalDate date) {
    // Rust's custom matcher is `date.day0() < 5`.
    return date.day <= 5;
}

void DatePickerStory::OnChange(DatePickerStory* self, Ctx* cx,
                               const component::DatePickerEvent* ev) {
    Date date = ev->value.DateValue();
    if (!date.IsComplete()) {
        StrCopyZ(self->value, (int)sizeof(self->value), "None");
    } else if (date.kind == DateKind::Range) {
        TempStr value = fmt("Some(\"%d-%02d-%02d - %d-%02d-%02d\")",
                            date.start.year, date.start.month, date.start.day,
                            date.end.year, date.end.month, date.end.day);
        StrCopyZ(self->value, (int)sizeof(self->value), value.s);
    } else {
        TempStr value = fmt("Some(\"%d-%02d-%02d\")", date.start.year,
                            date.start.month, date.start.day);
        StrCopyZ(self->value, (int)sizeof(self->value), value.s);
    }
    Notify(cx);
}

// value.format("%Y-%m-%d %H:%M:%S"), as the Option it is.
void DatePickerStory::OnDateTimeChange(DatePickerStory* self, Ctx* cx,
                                       const component::DatePickerEvent* ev) {
    Str value = ev->value.Format(cx->a, StrL("%Y-%m-%d %H:%M:%S"));
    if (!value.s) {
        StrCopyZ(self->dateTimeValue, (int)sizeof(self->dateTimeValue), "None");
    } else {
        TempStr text = fmt("Some(\"%s\")", value);
        StrCopyZ(self->dateTimeValue, (int)sizeof(self->dateTimeValue), text.s);
    }
    Notify(cx);
}

static void InitializeStory(DatePickerStory* self, Ctx* cx) {
    if (self->seeded) {
        return;
    }
    self->seeded = true;
    LocalDate now = DateToday();
    for (int i = 0; i < DpCount; i++) {
        self->pickers[i] = component::DatePickerStateNew(cx, i == DpEmptyRange);
    }

    component::DatePickerState* state = self->pickers[DpDefault].Get(cx);
    component::DatePickerStateSetDisabledMatcher(
        state, DateMatcherWeekdays((1u << 0) | (1u << 6)), cx);
    component::DatePickerStateSetDate(state, Date::Single(now), cx);

    state = self->pickers[DpSmall].Get(cx);
    component::DatePickerStateSetDisabledMatcher(
        state, DateMatcherInterval(now, DateAddDays(now, 5)), cx);
    component::DatePickerStateSetDate(state, Date::Single(now), cx);

    state = self->pickers[DpLarge].Get(cx);
    component::DatePickerStateSetDateFormat(state, StrL("%Y-%m-%d"), cx);
    component::DatePickerStateSetDisabledMatcher(
        state, DateMatcherRange(now, DateAddDays(now, 7)), cx);
    component::DatePickerStateSetDate(state, Date::Single(DateAddDays(now, -1)),
                                      cx);

    state = self->pickers[DpCustom].Get(cx);
    component::DatePickerStateSetDisabledMatcher(
        state, DateMatcherCustom(&FirstFiveDays), cx);
    component::DatePickerStateSetDate(state, Date::Single(now), cx);

    state = self->pickers[DpDateRange].Get(cx);
    component::DatePickerStateSetDate(
        state, Date::Range(now, DateAddDays(now, 4)), cx);

    state = self->pickers[DpBirthday].Get(cx);
    component::DatePickerStateSetYearRange(state, 1927, now.year + 1, cx);

    self->subscriptions[0] =
        Subscribe(cx, self->pickers[DpDefault], &DatePickerStory::OnChange);
    self->subscriptions[1] =
        Subscribe(cx, self->pickers[DpDateRange], &DatePickerStory::OnChange);
    self->subscriptions[2] =
        Subscribe(cx, self->pickers[DpEmptyRange], &DatePickerStory::OnChange);

    // Date and time: the first starts at now; the other two are empty until
    // picked and give a date 09:00 until its time is edited.
    LocalTime nine = {9, 0, 0};
    state = self->pickers[DpDateTime].Get(cx);
    component::DatePickerStateSetTimePrecision(state, TimePrecision::Minute);
    component::DatePickerStateSetDateTime(
        state, component::DateTime::Single({now, TimeOfDayNow()}), cx);
    state = self->pickers[DpDateTimeSecond].Get(cx);
    component::DatePickerStateSetTimePrecision(state, TimePrecision::Second);
    component::DatePickerStateSetDefaultTime(state, nine);
    state = self->pickers[DpDateTime12h].Get(cx);
    component::DatePickerStateSetTimePrecision(state, TimePrecision::Minute);
    component::DatePickerStateSetHourCycle(state, HourCycle::H12);
    component::DatePickerStateSetDefaultTime(state, nine);
    for (int i = 0; i < 3; i++) {
        self->subscriptions[3 + i] =
            Subscribe(cx, self->pickers[DpDateTime + i],
                      &DatePickerStory::OnDateTimeChange);
    }
}

static component::DatePicker* Picker(DatePickerStory* self, Ctx* cx,
                                     int picker) {
    return component::DatePicker::New(cx, self->pickers[picker])
        ->WithSize(self->toolbar.size)
        ->W(280);
}

El* DatePickerStory::Render(DatePickerStory* self, Ctx* cx) {
    InitializeStory(self, cx);
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    LocalDate now = DateToday();

    component::DateRangePreset singlePresets[] = {
        component::DateRangePreset::Single(StrL("Yesterday"),
                                           DateAddDays(now, -1)),
        component::DateRangePreset::Single(StrL("Last Week"),
                                           DateAddDays(now, -7)),
        component::DateRangePreset::Single(StrL("Last Month"),
                                           DateAddDays(now, -30)),
    };
    component::DateRangePreset rangePresets[] = {
        component::DateRangePreset::Range(StrL("Last 7 Days"),
                                          DateAddDays(now, -7), now),
        component::DateRangePreset::Range(StrL("Last 14 Days"),
                                          DateAddDays(now, -14), now),
        component::DateRangePreset::Range(StrL("Last 30 Days"),
                                          DateAddDays(now, -30), now),
        component::DateRangePreset::Range(StrL("Last 90 Days"),
                                          DateAddDays(now, -90), now),
    };

    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    El* defaults = StorySection(
        cx, "Default", "Single-date selection with presets and clear action.");
    StorySectionBody(defaults)->FlexCol()->W(512)->Gap(12);
    StorySectionAdd(defaults, Picker(self, cx, DpDefault)
                                  ->Cleanable()
                                  ->Presets(singlePresets, 3)
                                  ->IntoEl());
    StorySectionAdd(
        defaults,
        StoryTxt(cx, StoryFmt(cx, "Value: %s", self->value), 14, th.mutedFg));
    page->Child(defaults);

    El* dateTime = StorySection(cx, "Date and time",
                                "Edit the time of day below the calendar; "
                                "changes apply as you make them.");
    StorySectionBody(dateTime)->FlexCol()->W(512)->Gap(12);
    StorySectionAdd(dateTime, Picker(self, cx, DpDateTime)->IntoEl());
    StorySectionAdd(dateTime, Picker(self, cx, DpDateTimeSecond)
                                  ->Placeholder(StrL("With seconds"))
                                  ->Cleanable()
                                  ->IntoEl());
    StorySectionAdd(dateTime, Picker(self, cx, DpDateTime12h)
                                  ->Placeholder(StrL("12-hour clock"))
                                  ->Cleanable()
                                  ->IntoEl());
    StorySectionAdd(dateTime,
                    StoryTxt(cx, StoryFmt(cx, "Value: %s", self->dateTimeValue),
                             14, th.mutedFg));
    page->Child(dateTime);

    El* disabled =
        StorySection(cx, "Disabled dates",
                     "Matchers can block intervals, ranges, or custom dates.");
    StorySectionBody(disabled)->FlexCol()->W(512)->Gap(12);
    StorySectionAdd(disabled, Picker(self, cx, DpSmall)->IntoEl());
    StorySectionAdd(disabled, Picker(self, cx, DpLarge)->IntoEl());
    StorySectionAdd(disabled, Picker(self, cx, DpCustom)->IntoEl());
    page->Child(disabled);

    El* range =
        StorySection(cx, "Date range", "Two months with range presets.");
    StorySectionBody(range)->W(512);
    StorySectionAdd(range, Picker(self, cx, DpDateRange)
                               ->NumberOfMonths(2)
                               ->Cleanable()
                               ->Presets(rangePresets, 4)
                               ->IntoEl());
    page->Child(range);

    El* empty = StorySection(cx, "Empty range", "Empty range with presets.");
    StorySectionBody(empty)->W(512);
    StorySectionAdd(empty, Picker(self, cx, DpEmptyRange)
                               ->Placeholder(StrL("Range mode picker"))
                               ->Cleanable()
                               ->Presets(rangePresets, 4)
                               ->IntoEl());
    page->Child(empty);

    El* birthday = StorySection(cx, "Year range", "Custom year range.");
    StorySectionBody(birthday)->W(512);
    StorySectionAdd(birthday, Picker(self, cx, DpBirthday)
                                  ->Placeholder(StrL("Select birthday"))
                                  ->Cleanable()
                                  ->IntoEl());
    page->Child(birthday);

    El* custom = StorySection(cx, "Custom style", "Appearance-free input.");
    StorySectionBody(custom)->W(512);
    StorySectionAdd(custom,
                    Div(a)
                        ->W(280)
                        ->Bg(th.tokens.secondary)
                        ->Child(Picker(self, cx, DpNoAppearance)
                                    ->Appearance(false)
                                    ->Placeholder(StrL("Without appearance"))
                                    ->IntoEl()));
    page->Child(custom);
    return page;
}

STORY_PAGE(StoryDatePicker, DatePickerStory);
