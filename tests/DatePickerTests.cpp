/* Ported from crates/base/src/date_picker.rs.
 *
 * The root binds the same Confirm and Cancel actions a select does, and its
 * two handlers are what separate the pair: Enter toggles the popup, and the
 * Cancel handler does not look at `disabled` at all. */

#include "Test.h"

// The chord, resolved in the picker's context, read as what the picker does.
static DatePickerAction ForChord(const char* spec, bool open, bool disabled) {
    DatePickerInitKeys();
    KeyChord c = {};
    utassert(KeyChordParse(Str(spec), &c));
    uint32_t ctx = KeyContextOf(DatePickerContext());
    return DatePickerActionOf(KeymapMatch(c, &ctx, 1).action, open, disabled);
}

static void EnterOpensAndCloses() {
    utassert(ForChord("enter", false, false) == DatePickerAction::Open);
    // Already open, Enter closes it again once the value shown in the popup
    // is the one wanted (upstream f97b9eb3; it used to do nothing).
    utassert(ForChord("enter", true, false) == DatePickerAction::Dismiss);
}

static void EscapeOnlyCloses() {
    utassert(ForChord("escape", true, false) == DatePickerAction::Dismiss);
    // Closed, Rust propagates it.
    utassert(ForChord("escape", false, false) == DatePickerAction::None);
}

static void OnlyTheConfirmHandlerChecksDisabled() {
    utassert(ForChord("enter", false, true) == DatePickerAction::None);
    // Rust's Cancel handler has no disabled check, so Escape still closes a
    // disabled picker rather than trapping it open.
    utassert(ForChord("escape", true, true) == DatePickerAction::Dismiss);
}

// on_delete: Delete and Backspace clear the date, open or shut. Rust's
// handler has no disabled check, and neither does this.
static void DeleteClearsTheDate() {
    utassert(ForChord("delete", false, false) == DatePickerAction::Clear);
    utassert(ForChord("backspace", true, false) == DatePickerAction::Clear);
    utassert(ForChord("delete", false, true) == DatePickerAction::Clear);
}

static void OtherKeysAreNotThePickers() {
    utassert(ForChord("down", false, false) == DatePickerAction::None);
    utassert(ForChord("space", true, false) == DatePickerAction::None);
}

static LocalDate D(int year, int month, int day) {
    return {year, month, day};
}

static bool SameDate(LocalDate a, LocalDate b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

static bool FirstFiveDays(LocalDate date) {
    return date.day <= 5;
}

static void MatchersKeepTheirRustSemantics() {
    DateMatcher weekends = DateMatcherWeekdays((1u << 0) | (1u << 6));
    utassert(DateMatcherMatches(weekends, D(2025, 2, 9))); // Sunday
    utassert(!DateMatcherMatches(weekends, D(2025, 2, 10)));

    DateMatcher interval = DateMatcherInterval(D(2025, 2, 10), D(2025, 2, 15));
    utassert(DateMatcherMatches(interval, D(2025, 2, 9)));
    utassert(!DateMatcherMatches(interval, D(2025, 2, 12)));
    utassert(DateMatcherMatches(interval, D(2025, 2, 16)));

    DateMatcher range = DateMatcherRange(D(2025, 2, 10), D(2025, 2, 15));
    utassert(!DateMatcherMatches(range, D(2025, 2, 9)));
    utassert(DateMatcherMatches(range, D(2025, 2, 12)));
    utassert(!DateMatcherMatches(range, D(2025, 2, 16)));

    DateMatcher first = DateMatcherCustom(&FirstFiveDays);
    utassert(DateMatcherMatches(first, D(2025, 2, 5)));
    utassert(!DateMatcherMatches(first, D(2025, 2, 6)));
}

static void RangeSelectionRestartsAndCompletes() {
    LocalDate start = {};
    LocalDate end = {};
    DateMatcher none;
    utassert(DatePickerSelectDate(true, D(2025, 2, 10), &start, &end, none) ==
             DateSelectionResult::Partial);
    utassert(DatePickerSelectDate(true, D(2025, 2, 12), &start, &end, none) ==
             DateSelectionResult::Complete);
    utassert(start.day == 10 && end.day == 12);
    utassert(DatePickerSelectDate(true, D(2025, 2, 11), &start, &end, none) ==
             DateSelectionResult::Partial);
    utassert(start.day == 11 && end.day == 0);

    start = D(2025, 2, 12);
    utassert(DatePickerSelectDate(true, D(2025, 2, 10), &start, &end, none) ==
             DateSelectionResult::Partial);
    utassert(start.day == 10 && end.day == 0);

    DateMatcher disabled = DateMatcherRange(D(2025, 2, 1), D(2025, 2, 28));
    utassert(DatePickerSelectDate(false, D(2025, 2, 15), &start, &end,
                                  disabled) == DateSelectionResult::Rejected);
}

static El* FindNamedDp(El* root, const char* name);

struct DatePickerSink {
    int changes = 0;
    Date last = {};
    component::DateTime values[8] = {};

    static void OnChange(DatePickerSink* self, Ctx*,
                         const component::DatePickerEvent* ev) {
        if (self->changes < 8) {
            self->values[self->changes] = ev->value;
        }
        self->changes++;
        self->last = ev->value.DateValue();
    }
};

static void RetainedStateOwnsAndForwardsCalendar() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    Entity<component::DatePickerState> picker =
        component::DatePickerStateNew(&cx, true);
    component::DatePickerState* state = picker.Get(&app);
    utassert(state && state->date.kind == DateKind::Range);
    utassert(state && state->calendar.IsValid());
    // date_format is None until set; the display format is then the date
    // alone, since a range edits no times.
    utassert(state && !state->dateFormat.s);
    utassert(state && StrEqI(component::DatePickerStateDisplayFormat(a, state),
                             "%Y/%m/%d"));

    Entity<DatePickerSink> sink = EntityNewState<DatePickerSink>(&app);
    SubscribeTo(&app, picker, sink, &DatePickerSink::OnChange);
    CalendarState* calendar = state ? state->calendar.Get(&app) : nullptr;
    if (calendar) {
        cx.self = state->calendar.id;
        utassert(!CalendarStateSelectDate(calendar, D(2025, 2, 10), &cx));
        utassert(state->date.kind == DateKind::Range && state->date.start
                                                                .day == 0);
        utassert(CalendarStateSelectDate(calendar, D(2025, 2, 12), &cx));
    }
    DatePickerSink* received = sink.Get(&app);
    utassert(received && received->changes == 1);
    utassert(received && SameDate(received->last.start, D(2025, 2, 10)));
    utassert(received && SameDate(received->last.end, D(2025, 2, 12)));
    utassert(state && SameDate(state->date.start, D(2025, 2, 10)));
    utassert(state && SameDate(state->date.end, D(2025, 2, 12)));
    utassert(state && !state->open);

    cx.self = picker.id;
    component::DatePickerStateSetDateFormat(state, StrL("%A, %B %e, %Y"), &cx);
    Str formatted = component::DatePickerFormatValue(
        a, state->dateFormat, Date::Single(D(2025, 2, 10)));
    utassert(StrEqI(formatted, "Monday, February 10, 2025"));
    formatted = component::DatePickerFormatDate(
        a, StrL("%G-W%V %U %W %-j %_m %q %v"), D(2021, 1, 1));
    utassert(StrEqI(formatted, "2020-W53 00 00 1  1 1  1-Jan-2021"));
    component::DatePickerStateSetFirstDayOfWeek(state, 1, &cx);
    component::DatePickerStateSetDisabledMatcher(
        state, DateMatcherWeekdays(1u << 0), &cx);
    component::DatePickerStateSetYearRange(state, 1980, 2030, &cx);
    utassert(state->firstDayOfWeek == 1);
    utassert(calendar && calendar->disabledMatcher.weekdayMask == 1u);
    utassert(calendar && calendar->yearMin == 1980 &&
             calendar->yearMax == 2030);

    component::DateRangePreset preset = component::DateRangePreset::Range(
        StrL("week"), D(2025, 3, 1), D(2025, 3, 7));
    component::DatePickerStateSelectPreset(state, preset, &cx);
    utassert(SameDate(state->date.start, D(2025, 3, 1)));
    utassert(SameDate(state->date.end, D(2025, 3, 7)));
    utassert(received && received->changes == 2);

    EntityDropAll(&app);
    ArenaDelete(a);
    delete win;
}

static LocalDate DT(int y, int m, int d) {
    return {y, m, d};
}

static LocalTime TM(int h, int m, int s) {
    LocalTime t;
    t.hour = h;
    t.minute = m;
    t.second = s;
    return t;
}

static bool SameTime(LocalTime a, LocalTime b) {
    return a == b;
}

// kit/tests/date_picker.rs date_time_picker_reports_each_edit_and_stays_open,
// through the state's own handlers rather than a window: picking a date
// keeps the popup open and reports the value, each time edit reports it, the
// display format follows the precision, and picking the selected date again
// closes the popup without a report.
static void DateTimePickerReportsEachEditAndStaysOpen() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;
    Entity<component::DatePickerState> picker =
        component::DatePickerStateNew(&cx);
    component::DatePickerState* state = picker.Get(&app);
    component::DatePickerStateSetTimePrecision(state, TimePrecision::Second);
    component::DatePickerStateSetDateTime(
        state, component::DateTime::Single({DT(2026, 9, 15), TM(8, 0, 0)}),
        &cx);
    utassert(StrEqI(component::DatePickerStateDateTime(state).Format(
                        a, component::DatePickerStateDisplayFormat(a, state)),
                    "2026/09/15 08:00:00"));
    Entity<DatePickerSink> sink = EntityNewState<DatePickerSink>(&app);
    SubscribeTo(&app, picker, sink, &DatePickerSink::OnChange);
    state->open = true;

    CalendarState* calendar = state->calendar.Get(&app);
    utassert(calendar);
    if (calendar) {
        cx.self = state->calendar.id;
        CalendarStateSelectDate(calendar, D(2026, 9, 16), &cx);
    }
    // Picking a date keeps the popup open so the time can be edited next.
    utassert(state->open);
    TimeFieldState* field = state->timeField.Get(&app);
    utassert(field && field->editor.precision == TimePrecision::Second);
    if (field) {
        cx.self = state->timeField.id;
        MouseDownEvent press = {};
        TimeFieldState::OnSegmentDown(field, &cx, &press,
                                      (intptr_t)TimeSegment::Minute);
        KeyEvent key = {};
        key.vk = '4';
        TimeFieldState::OnKeyDown(field, &cx, &key);
        key.vk = '5';
        key.propagate = true;
        TimeFieldState::OnKeyDown(field, &cx, &key);
        // The minute is complete, so the seconds segment is selected next.
        ActionEvent up = {};
        up.action = TimeFieldIncrement();
        TimeFieldState::OnAction(field, &cx, &up);
    }
    utassert(StrEqI(component::DatePickerStateDateTime(state).Format(
                        a, component::DatePickerStateDisplayFormat(a, state)),
                    "2026/09/16 08:45:01"));
    // Clicking the selected day again confirms it and closes the popup.
    if (calendar) {
        cx.self = state->calendar.id;
        CalendarStateSelectDate(calendar, D(2026, 9, 16), &cx);
    }
    utassert(!state->open);
    DatePickerSink* received = sink.Get(&app);
    utassert(received && received->changes == 4);
    if (received && received->changes == 4) {
        const LocalTime times[4] = {TM(8, 0, 0), TM(8, 4, 0), TM(8, 45, 0),
                                    TM(8, 45, 1)};
        for (int i = 0; i < 4; i++) {
            component::LocalDateTime at = {};
            utassert(received->values[i].Start(&at));
            utassert(SameDate(at.date, D(2026, 9, 16)));
            utassert(SameTime(at.time, times[i]));
        }
    }
    EntityDropAll(&app);
    ArenaDelete(a);
    delete win;
}

// kit/tests/date_picker.rs twelve_hour_picker_types_the_period: midnight
// reads as 12 AM, and typing 0 9 3 0 p makes 9:30 PM.
static void TwelveHourPickerTypesThePeriod() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;
    Entity<component::DatePickerState> picker =
        component::DatePickerStateNew(&cx);
    component::DatePickerState* state = picker.Get(&app);
    component::DatePickerStateSetTimePrecision(state, TimePrecision::Minute);
    component::DatePickerStateSetHourCycle(state, HourCycle::H12);
    component::DatePickerStateSetDateTime(
        state, component::DateTime::Single({DT(2026, 9, 15), TM(0, 0, 0)}),
        &cx);
    utassert(StrEqI(component::DatePickerStateDateTime(state).Format(
                        a, component::DatePickerStateDisplayFormat(a, state)),
                    "2026/09/15 12:00 AM"));
    TimeFieldState* field = state->timeField.Get(&app);
    utassert(field && field->editor.hourCycle == HourCycle::H12);
    if (field) {
        cx.self = state->timeField.id;
        MouseDownEvent press = {};
        TimeFieldState::OnSegmentDown(field, &cx, &press,
                                      (intptr_t)TimeSegment::Hour);
        const int keys[] = {'0', '9', '3', '0', 'P'};
        for (int k : keys) {
            KeyEvent key = {};
            key.vk = k;
            TimeFieldState::OnKeyDown(field, &cx, &key);
        }
    }
    utassert(StrEqI(component::DatePickerStateDateTime(state).Format(
                        a, component::DatePickerStateDisplayFormat(a, state)),
                    "2026/09/15 09:30 PM"));
    component::LocalDateTime at = {};
    utassert(component::DatePickerStateDateTime(state).Start(&at) &&
             SameTime(at.time, TM(21, 30, 0)));
    EntityDropAll(&app);
    ArenaDelete(a);
    delete win;
}

// kit/tests/date_picker.rs range_picker_edits_dates_only: a range picker
// with a precision shows dates only, closes on a complete range, and keeps
// the times its owner set.
static void RangePickerEditsDatesOnly() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;
    Entity<component::DatePickerState> picker =
        component::DatePickerStateNew(&cx, true);
    component::DatePickerState* state = picker.Get(&app);
    component::DatePickerStateSetTimePrecision(state, TimePrecision::Minute);
    component::DatePickerStateSetDateTime(
        state,
        component::DateTime::Range({DT(2026, 9, 15), TM(9, 0, 0)},
                                   {DT(2026, 9, 15), TM(18, 0, 0)}),
        &cx);
    utassert(StrEqI(component::DatePickerStateDateTime(state).Format(
                        a, component::DatePickerStateDisplayFormat(a, state)),
                    "2026/09/15 - 2026/09/15"));
    Entity<DatePickerSink> sink = EntityNewState<DatePickerSink>(&app);
    SubscribeTo(&app, picker, sink, &DatePickerSink::OnChange);
    state->open = true;
    CalendarState* calendar = state->calendar.Get(&app);
    if (calendar) {
        cx.self = state->calendar.id;
        CalendarStateSelectDate(calendar, D(2026, 9, 16), &cx);
        CalendarStateSelectDate(calendar, D(2026, 9, 18), &cx);
    }
    // A complete range closes the popup, as in any date-only picker.
    utassert(!state->open);
    DatePickerSink* received = sink.Get(&app);
    utassert(received && received->changes == 1);
    if (received && received->changes == 1) {
        component::LocalDateTime start = {};
        component::LocalDateTime end = {};
        utassert(received->values[0].Start(&start) && received->values[0]
                                                          .End(&end));
        // The times set by the owner are kept.
        utassert(SameDate(start.date, D(2026, 9, 16)) &&
                 SameTime(start.time, TM(9, 0, 0)));
        utassert(SameDate(end.date, D(2026, 9, 18)) &&
                 SameTime(end.time, TM(18, 0, 0)));
    }
    EntityDropAll(&app);
    ArenaDelete(a);
    delete win;
}

// time/time_field.rs: test_time_field_builder.
static void TimeFieldBuilder() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;
    Entity<TimeFieldState> state =
        TimeFieldStateNew(&cx, TimePrecision::Second, HourCycle::H12);
    component::TimeField* field = component::TimeField::New(&cx, state)
                                      ->WithId(StrL("start"))
                                      ->WithSize(UiSize::Large)
                                      ->Disabled(true)
                                      ->Invalid(true);
    utassert(StrEqI(field->id, "start"));
    utassert(field->size == UiSize::Large);
    utassert(field->disabled);
    utassert(field->invalid);
    EntityDropAll(&app);
    ArenaDelete(a);
    delete win;
}

static void RetainedFacadeUsesTheStateIdentity() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;
    Entity<component::DatePickerState> picker =
        component::DatePickerStateNew(&cx);
    component::DatePickerState* state = picker.Get(&app);
    component::DatePickerStateSetDate(state, Date::Single(D(2025, 8, 3)), &cx);
    state->open = true;
    component::DateRangePreset preset =
        component::DateRangePreset::Single(StrL("Tomorrow"), D(2025, 8, 4));
    El* root = component::DatePicker::New(&cx, picker)
                   ->Cleanable()
                   ->NumberOfMonths(2)
                   ->Presets(&preset, 1)
                   ->IntoEl();
    utassert(root && root->style.focusId == state->focus.id);
    utassert(root && root->accessibility.role == AccessibilityRole::ComboBox);
    utassert(FindNamedDp(root, "clean") != nullptr);
    // Popup captures its trigger on the first frame and mounts deferred
    // content on the second, as the upstream Positioner does.
    root = component::DatePicker::New(&cx, picker)
               ->Cleanable()
               ->NumberOfMonths(2)
               ->Presets(&preset, 1)
               ->IntoEl();
    utassert(FindNamedDp(root, "date-preset-0") != nullptr);
    utassert(FindNamedDp(root, "calendar") != nullptr);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    ArenaDelete(a);
    delete win;
}

static El* FindNamedDp(El* root, const char* name) {
    if (!root) {
        return nullptr;
    }
    if (root->id.s && base::StrEqI(root->id, name)) {
        return root;
    }
    for (El* c = root->first; c; c = c->next) {
        if (El* hit = FindNamedDp(c, name)) {
            return hit;
        }
    }
    return nullptr;
}

// The trigger, the clear and the popup are `input`, `clean` and `pop` in
// every picker; the picker's own name over them is what tells two of them
// apart, the way GPUI's element id stack does.
static void TwoPickersHaveTwoTriggers() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    El* page = Div(a);
    El* one = component::DatePicker::New(&cx)
                  ->Id(StrL("one"))
                  ->Year(2025)
                  ->Month(2)
                  ->Day(10)
                  ->Cleanable()
                  ->IntoEl();
    El* two = component::DatePicker::New(&cx)
                  ->Id(StrL("two"))
                  ->Year(2025)
                  ->Month(2)
                  ->Day(11)
                  ->Cleanable()
                  ->IntoEl();
    page->Child(one)->Child(two);
    IdsCollect(page);

    El* inOne = FindNamedDp(one, "input");
    El* inTwo = FindNamedDp(two, "input");
    utassert(inOne && inTwo);
    if (inOne && inTwo) {
        utassert(inOne->clickId != 0 && inTwo->clickId != 0);
        utassert(inOne->clickId != inTwo->clickId);
        // The trigger is what the keyboard reaches, and it is reached
        // separately in each picker.
        utassert(inOne->style.focusId != inTwo->style.focusId);
    }
    El* clearOne = FindNamedDp(one, "clean");
    El* clearTwo = FindNamedDp(two, "clean");
    utassert(clearOne && clearTwo);
    if (clearOne && clearTwo) {
        utassert(clearOne->clickId != clearTwo->clickId);
    }

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

static const uint8_t kTabular = 1 + (uint8_t)gpui::FontFeatures::TabularFigures;

static int CountTabular(const El* e) {
    if (!e) {
        return 0;
    }
    int n = e->style.fontFeatures == kTabular ? 1 : 0;
    for (const El* c = e->first; c; c = c->next) {
        n += CountTabular(c);
    }
    return n;
}

// time_field.rs tabular_figures(): the TimeField row asks for `tnum`, and a
// DatePicker's trigger asks for it only while its time is being edited — the
// value there updates live as it is typed (date_picker.rs).
static void TabularFiguresWhereATimeIsTyped() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;
    Entity<TimeFieldState> time =
        TimeFieldStateNew(&cx, TimePrecision::Minute, HourCycle::H23);
    El* field = component::TimeField::New(&cx, time)->IntoEl();
    utassert(field && field->style.fontFeatures == kTabular);

    Entity<component::DatePickerState> picker =
        component::DatePickerStateNew(&cx);
    component::DatePickerState* state = picker.Get(&app);
    component::DatePickerStateSetDate(state, Date::Single(D(2025, 8, 3)), &cx);
    El* dateOnly = component::DatePicker::New(&cx, picker)->IntoEl();
    utassert(CountTabular(dateOnly) == 0);
    component::DatePickerStateSetTimePrecision(state, TimePrecision::Minute);
    El* withTime = component::DatePicker::New(&cx, picker)->IntoEl();
    utassert(CountTabular(withTime) == 1);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    ArenaDelete(a);
    delete win;
}

// font_features is text style, so it cascades like the weight: the runs
// under a row that asks for `tnum` are shaped with it unless they name their
// own, and FontFeatures::default() is one way to name them. A shaped run with
// it gives every digit one width.
static void FontFeaturesCascadeAndShapeTabularDigits() {
    App app = {};
    app.paint = PaintAppNew();
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    win->paint.pa = app.paint;
    Arena* a = ArenaNew();
    El* inherits = TextEl(a, StrL("10:11"));
    El* optsOut = TextEl(a, StrL("10:11"));
    El* root = Div(a)
                   ->FlexCol()
                   ->FontFeatures(gpui::FontFeatures::TabularFigures)
                   ->Child(Div(a)->Child(inherits))
                   ->Child(Div(a)
                               ->FontFeatures(gpui::FontFeatures::Default)
                               ->Child(optsOut));
    LayoutEl(&win->paint, root, 0, 0, 400, 200, 14, Rgba{});
    utassert(inherits->style.fontFeatures == kTabular);
    utassert(optsOut->style
                 .fontFeatures == 1 + (uint8_t)gpui::FontFeatures::Default);

    Size ones =
        MeasureText(&win->paint, StrL("1111"), 14, 0, false, kFontTabularNums);
    Size zeros =
        MeasureText(&win->paint, StrL("0000"), 14, 0, false, kFontTabularNums);
    // The browser's canvas has no font-variant-numeric (paint_wasm.cpp).
#if !GPUI_OS_WASM
    utassert(ones.w > 0 && ones.w > zeros.w - 0.5f && ones.w < zeros.w + 0.5f);
#if GPUI_OS_MAC
    // The system font's own figures are proportional, so there the flag is
    // what evens them out. Segoe UI and DejaVu Sans figures are tabular
    // already.
    Size bareOnes = MeasureText(&win->paint, StrL("1111"), 14, 0, false, 0);
    Size bareZeros = MeasureText(&win->paint, StrL("0000"), 14, 0, false, 0);
    utassert(bareOnes.w < bareZeros.w - 0.5f);
#endif
#else
    utassert(ones.w > 0 && zeros.w > 0);
#endif

    ArenaDelete(a);
    delete win;
    PaintAppFree(app.paint);
    app.paint = nullptr;
}

void TestDatePicker() {
    TestSuite("date_picker");
    EnterOpensAndCloses();
    EscapeOnlyCloses();
    OnlyTheConfirmHandlerChecksDisabled();
    DeleteClearsTheDate();
    OtherKeysAreNotThePickers();
    MatchersKeepTheirRustSemantics();
    RangeSelectionRestartsAndCompletes();
    RetainedStateOwnsAndForwardsCalendar();
    RetainedFacadeUsesTheStateIdentity();
    DateTimePickerReportsEachEditAndStaysOpen();
    TwelveHourPickerTypesThePeriod();
    RangePickerEditsDatesOnly();
    TimeFieldBuilder();
    TwoPickersHaveTwoTriggers();
    TabularFiguresWhereATimeIsTyped();
    FontFeaturesCascadeAndShapeTabularDigits();
}
