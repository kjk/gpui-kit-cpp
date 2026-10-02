#include "Story.h"

// calendar_story.rs holds three CalendarStates, one per section, so each
// navigates on its own. Each starts on the current month with nothing
// selected; the calendar marks today itself.
struct CalMonthState {
    int year = 0;
    int month = 0;
    int day = 0;
};

enum {
    CalSingle = 0,
    CalWide,
    CalDisabled,
    CalCount
};

struct CalendarStory {
    CalMonthState cal[CalCount] = {};
    static El* Render(CalendarStory* self, Ctx* cx);
};

// gpui_base::CalendarState carries the month a calendar is looking at, and
// stepping off either end of the year carries into the next one — which is
// the whole reason prev_month and next_month exist rather than `month += 1`.
static CalendarState CalStateOf(const CalMonthState& m) {
    CalendarState s;
    s.currentYear = m.year;
    s.currentMonth = m.month;
    return s;
}
static void CalPrev(CalendarStory* self, Ctx* cx, const ClickEvent*,
                    int64_t which) {
    CalMonthState& m = self->cal[which];
    CalendarState s = CalStateOf(m);
    CalendarPrevMonth(&s);
    m.year = s.currentYear;
    m.month = s.currentMonth;
    Notify(cx);
}
static void CalNext(CalendarStory* self, Ctx* cx, const ClickEvent*,
                    int64_t which) {
    CalMonthState& m = self->cal[which];
    CalendarState s = CalStateOf(m);
    CalendarNextMonth(&s);
    m.year = s.currentYear;
    m.month = s.currentMonth;
    Notify(cx);
}
// The calendar fills the listener's value with the day it was given, so
// which calendar asked has to come from the handler itself — one per section,
// the way Rust's three closures each capture their own entity.
static void CalDayInto(CalendarStory* self, Ctx* cx, int which, int64_t d) {
    self->cal[which].day = (int)d;
    Notify(cx);
}
static void CalDaySingle(CalendarStory* self, Ctx* cx, const ClickEvent*,
                         int64_t d) {
    CalDayInto(self, cx, CalSingle, d);
}
static void CalDayWide(CalendarStory* self, Ctx* cx, const ClickEvent*,
                       int64_t d) {
    CalDayInto(self, cx, CalWide, d);
}
static void CalDayDisabled(CalendarStory* self, Ctx* cx, const ClickEvent*,
                           int64_t d) {
    CalDayInto(self, cx, CalDisabled, d);
}

static component::Calendar* CalFor(CalendarStory* self, Ctx* cx, int which) {
    CalMonthState& m = self->cal[which];
    return component::Calendar::New(cx)
        ->Year(m.year)
        ->Month(m.month)
        ->Day(m.day)
        ->OnPrev(Listen(cx, &CalPrev, which))
        ->OnNext(Listen(cx, &CalNext, which))
        ->OnDay(which == CalSingle ? Listen(cx, &CalDaySingle)
                : which == CalWide ? Listen(cx, &CalDayWide)
                                   : Listen(cx, &CalDayDisabled));
}

El* CalendarStory::Render(CalendarStory* self, Ctx* cx) {
    Arena* a = cx->a;
    LocalDate now = DateToday();
    for (int i = 0; i < CalCount; i++) {
        if (self->cal[i].month == 0) {
            self->cal[i].year = now.year;
            self->cal[i].month = now.month;
        }
    }
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);

    El* single = StorySection(cx, "Single month", "Single-date selection.");
    StorySectionBody(single)->W(512);
    StorySectionAdd(single, CalFor(self, cx, CalSingle)->IntoEl());
    page->Child(single);

    // number_of_months(3): one calendar showing three months, not three
    // calendars each with their own header.
    El* multi =
        StorySection(cx, "Multiple months", "Three months shown together.");
    StorySectionBody(multi)->W(512);
    StorySectionAdd(multi,
                    CalFor(self, cx, CalWide)->NumberOfMonths(3)->IntoEl());
    page->Child(multi);

    // disabled_matcher(vec![0, 3, 6]): Sunday, Wednesday and Saturday are
    // never selectable.
    El* dis =
        StorySection(cx, "Disabled dates", "Recurring unavailable weekdays.");
    StorySectionBody(dis)->W(512);
    StorySectionAdd(dis, CalFor(self, cx, CalDisabled)
                             ->DisabledMatcher(DateMatcherWeekdays(
                                 (1 << 0) | (1 << 3) | (1 << 6)))
                             ->IntoEl());
    page->Child(dis);
    return page;
}

STORY_PAGE(StoryCalendar, CalendarStory);
