/* Ported from crates/base/src/date_picker.rs.
 *
 * The root binds the same Confirm and Cancel actions a select does, and its
 * two handlers are what separate the pair: Enter only ever opens, and the
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

static void EnterOnlyOpens() {
    utassert(ForChord("enter", false, false) == DatePickerAction::Open);
    // Already open, Enter does nothing: choosing a date is the calendar's
    // business, not the root's. A select would confirm here.
    utassert(ForChord("enter", true, false) == DatePickerAction::None);
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

    static void OnChange(DatePickerSink* self, Ctx*,
                         const component::DatePickerEvent* ev) {
        self->changes++;
        self->last = ev->date;
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
    utassert(state && state->dateFormat.s &&
             StrEqI(state->dateFormat, "%Y/%m/%d"));

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

void TestDatePicker() {
    TestSuite("date_picker");
    EnterOnlyOpens();
    EscapeOnlyCloses();
    OnlyTheConfirmHandlerChecksDisabled();
    DeleteClearsTheDate();
    OtherKeysAreNotThePickers();
    MatchersKeepTheirRustSemantics();
    RangeSelectionRestartsAndCompletes();
    RetainedStateOwnsAndForwardsCalendar();
    RetainedFacadeUsesTheStateIdentity();
    TwoPickersHaveTwoTriggers();
}
