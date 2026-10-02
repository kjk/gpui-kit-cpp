#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

// crates/base/examples/showcase/components/calendar.rs. The page is
// `Calendar::new("example-calendar", &self.calendar).w(px(250.)).p_3()` with
// an `item(..)` closure that decorates whatever slot the calendar hands it.
// The grid, the weekday the month starts on, the flanking days, the two
// arrows and the month and year pickers all belong to the calendar — this
// page used to work out every one of them.
static const char* kMon[] = {"",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char* kWd[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

static void EnsureCalendarDate(ShowcaseApp* app) {
    if (app->cal.currentYear > 0) {
        return;
    }
    LocalDate st = DateToday();
    app->cal.currentYear = st.year;
    app->cal.currentMonth = st.month;
}

static void CalPrev(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    if (app->cal.view == CalendarView::Year) {
        CalendarPrevYearPage(&app->cal);
    } else {
        CalendarPrevMonth(&app->cal);
    }
    Notify(cx);
}

static void CalNext(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    if (app->cal.view == CalendarView::Year) {
        CalendarNextYearPage(&app->cal);
    } else {
        CalendarNextMonth(&app->cal);
    }
    Notify(cx);
}

// The two toggles switch the grid below, and picking from it switches back —
// `CalendarEvent` in Rust, which the state answers itself.
static void CalMonthToggle(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->cal.view = app->cal.view == CalendarView::Month ? CalendarView::Day
                                                         : CalendarView::Month;
    Notify(cx);
}

static void CalYearToggle(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->cal.view = app->cal.view == CalendarView::Year ? CalendarView::Day
                                                        : CalendarView::Year;
    Notify(cx);
}

static void CalPickMonth(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                         int64_t m) {
    app->cal.currentMonth = (int)m;
    app->cal.view = CalendarView::Day;
    Notify(cx);
}

static void CalPickYear(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                        int64_t y) {
    app->cal.currentYear = (int)y;
    app->cal.view = CalendarView::Day;
    Notify(cx);
}

// The day the cell stands for, which the calendar hands over — a cell in a
// flanking month carries the neighbouring month's date, so nothing here has
// to work out which month a click landed in.
static void CalPickDate(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                        int64_t key) {
    LocalDate d = DatePickerDateFromKey(key);
    app->cal.currentYear = d.year;
    app->cal.currentMonth = d.month;
    app->calDay = d.day;
    Notify(cx);
}

// The look, and nothing else: `item(|item, state, _, _| match state.kind())`.
static El* CalItem(void* user, Ctx* cx, El* item, const CalendarItemState& st) {
    auto* app = (ShowcaseApp*)user;
    switch (st.kind) {
        case CalendarItemKind::Previous:
        case CalendarItemKind::Next:
            return item->W(28)->H(28)->HoverBg(ScHover())->Child(ScTxt(
                cx,
                st.kind == CalendarItemKind::Previous ? StrL("‹") : StrL("›"),
                14, ScInk()));
        case CalendarItemKind::MonthToggle:
            return item->H(28)
                ->PadX(4)
                ->Child(ScTxt(cx, Str(kMon[st.value]), 12, ScInk()));
        case CalendarItemKind::YearToggle:
            return item->H(28)
                ->PadX(4)
                ->Child(ScTxt(cx, DupFmt(cx, "%d", st.value), 12, ScInk()));
        case CalendarItemKind::Weekday:
            return item->Child(ScTxt(cx, Str(kWd[st.value]), 12, ScMutedC()));
        case CalendarItemKind::Day: {
            bool active = st.active;
            if (active) {
                return item->Bg(ScInk())->Child(
                    ScTxt(cx, DupFmt(cx, "%d", st.value), 12, ScWhite()));
            }
            if (st.today) {
                item->Border(1, ScBorder());
            }
            if (!st.disabled) {
                item->HoverBg(ScHover());
            }
            return item->Child(ScTxt(cx, DupFmt(cx, "%d", st.value), 12,
                                     st.muted ? ScSilver() : ScInk()));
        }
        case CalendarItemKind::Month:
        case CalendarItemKind::Year: {
            Str label = st.kind == CalendarItemKind::Month
                            ? Str(kMon[st.value])
                            : DupFmt(cx, "%d", st.value);
            item->W(74)->H(28);
            if (st.active) {
                return item->Bg(ScInk())
                    ->Child(ScTxt(cx, label, 12, ScWhite()));
            }
            return item->HoverBg(ScHover())
                ->Child(ScTxt(cx, label, 12, ScInk()));
        }
    }
    (void)app;
    return item;
}

El* ShowcaseCalendarGrid(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    EnsureCalendarDate(app);
    CalendarOpts o;
    o.year = app->cal.currentYear;
    o.month = app->cal.currentMonth;
    o.view = app->cal.view;
    o.cellSize = 32;
    o.today = DateToday();
    if (app->calDay > 0) {
        o.selected = {app->cal.currentYear, app->cal.currentMonth, app->calDay};
    }
    // The year grid pages twenty at a time from wherever the state left it.
    o.yearMin = app->cal.currentYear - 50;
    o.yearMax = app->cal.currentYear + 50;
    o.yearPageStart = app->cal.yearPage ? app->cal.yearPage : o.yearMin;
    o.onDate = Listen(cx, &CalPickDate);
    o.onPrev = Listen(cx, &CalPrev);
    o.onNext = Listen(cx, &CalNext);
    o.onMonthToggle = Listen(cx, &CalMonthToggle);
    o.onYearToggle = Listen(cx, &CalYearToggle);
    o.onMonth = Listen(cx, &CalPickMonth);
    o.onYear = Listen(cx, &CalPickYear);
    o.item = &CalItem;
    o.user = app;
    (void)a;
    return Calendar::New(cx, StrL("example-calendar"), o)
        ->W(250)
        ->Pad(12)
        ->Border(1, ScBorder());
}

El* ShowcaseCalendar(ShowcaseApp* app, Ctx* cx) {
    return ShowcaseCalendarGrid(app, cx);
}

SHOWCASE_PAGE(CompCalendar, ShowcaseCalendar);
