/* Ported from crates/base/src/time_field.rs: the SegmentEditor rules, and
 * the state's keystrokes and actions reaching them. */

#include "Test.h"

static LocalTime Hms(int h, int m, int s) {
    LocalTime t;
    t.hour = h;
    t.minute = m;
    t.second = s;
    return t;
}

static SegmentEditor NewEditor(TimePrecision precision, HourCycle hourCycle) {
    SegmentEditor editor;
    editor.SetPrecision(precision);
    editor.SetHourCycle(hourCycle);
    return editor;
}

static void TypeKeys(SegmentEditor* editor, const char* keys) {
    for (const char* k = keys; *k; k++) {
        if (*k == 'a') {
            editor->InputPeriod(false);
        } else if (*k == 'p') {
            editor->InputPeriod(true);
        } else {
            editor->InputDigit(*k - '0');
        }
    }
}

// typing_fills_segments_and_advances
static void TypingFillsSegmentsAndAdvances() {
    SegmentEditor editor = NewEditor(TimePrecision::Second, HourCycle::H23);
    TypeKeys(&editor, "093015");
    utassert(editor.time == Hms(9, 30, 15));
    // The last segment stays selected once it is complete.
    utassert(editor.segment == TimeSegment::Second);
}

// a_digit_that_cannot_start_two_digits_completes_the_segment
static void ADigitThatCannotStartTwoDigitsCompletesTheSegment() {
    SegmentEditor editor = NewEditor(TimePrecision::Minute, HourCycle::H23);
    TypeKeys(&editor, "7");
    utassert(editor.time == Hms(7, 0, 0));
    utassert(editor.segment == TimeSegment::Minute);
    TypeKeys(&editor, "8");
    utassert(editor.time == Hms(7, 8, 0));
}

// an_out_of_range_pair_restarts_from_the_second_digit
static void AnOutOfRangePairRestartsFromTheSecondDigit() {
    SegmentEditor editor = NewEditor(TimePrecision::Minute, HourCycle::H23);
    // 2 then 5 cannot form 25 hours, so 5 starts a new entry and completes it.
    TypeKeys(&editor, "25");
    utassert(editor.time == Hms(5, 0, 0));
    utassert(editor.segment == TimeSegment::Minute);
}

// stepping_wraps_without_carry
static void SteppingWrapsWithoutCarry() {
    SegmentEditor editor = NewEditor(TimePrecision::Minute, HourCycle::H23);
    editor.SetTime(Hms(23, 59, 0));
    editor.Step(1);
    utassert(editor.time == Hms(0, 59, 0));
    editor.SelectSegment(TimeSegment::Minute);
    editor.Step(1);
    utassert(editor.time == Hms(0, 0, 0));
    editor.Step(-1);
    utassert(editor.time == Hms(0, 59, 0));
}

// segment_movement_stops_at_the_ends
static void SegmentMovementStopsAtTheEnds() {
    SegmentEditor editor = NewEditor(TimePrecision::Minute, HourCycle::H23);
    utassert(!editor.MoveSegment(-1));
    utassert(editor.MoveSegment(1));
    utassert(!editor.MoveSegment(1));
    utassert(!editor.SelectSegment(TimeSegment::Second));
    utassert(!editor.SelectSegment(TimeSegment::Period));
}

// precision_truncates_seconds
static void PrecisionTruncatesSeconds() {
    SegmentEditor editor = NewEditor(TimePrecision::Second, HourCycle::H23);
    editor.SetTime(Hms(9, 30, 15));
    editor.SelectSegment(TimeSegment::Second);
    editor.SetPrecision(TimePrecision::Minute);
    utassert(editor.time == Hms(9, 30, 0));
    utassert(editor.segment == TimeSegment::Hour);
}

// clearing_resets_only_the_selected_segment
static void ClearingResetsOnlyTheSelectedSegment() {
    SegmentEditor editor = NewEditor(TimePrecision::Minute, HourCycle::H23);
    editor.SetTime(Hms(9, 30, 0));
    editor.SelectSegment(TimeSegment::Minute);
    utassert(editor.ClearSegment());
    utassert(editor.time == Hms(9, 0, 0));
}

// twelve_hour_labels_map_midnight_and_noon_to_twelve
static void TwelveHourLabelsMapMidnightAndNoonToTwelve() {
    Arena* a = ArenaNew();
    SegmentEditor editor = NewEditor(TimePrecision::Minute, HourCycle::H12);
    struct Case {
        int hour;
        const char* label;
        const char* period;
    };
    const Case cases[] = {
        {0, "12", "AM"}, {9, "09", "AM"}, {12, "12", "PM"}, {23, "11", "PM"}};
    for (const Case& c : cases) {
        editor.SetTime(Hms(c.hour, 0, 0));
        utassert(base::StrEq(editor.Label(a, TimeSegment::Hour), Str(c.label)));
        utassert(
            base::StrEq(editor.Label(a, TimeSegment::Period), Str(c.period)));
    }
    ArenaDelete(a);
}

// twelve_hour_typing_keeps_the_period_until_it_is_typed
static void TwelveHourTypingKeepsThePeriodUntilItIsTyped() {
    SegmentEditor editor = NewEditor(TimePrecision::Minute, HourCycle::H12);
    // "1" may start 10-12, so it waits; "2" makes 12, which is midnight in AM.
    TypeKeys(&editor, "12");
    utassert(editor.time == Hms(0, 0, 0));
    TypeKeys(&editor, "30p");
    utassert(editor.time == Hms(12, 30, 0));
    utassert(editor.segment == TimeSegment::Period);
    editor.SelectSegment(TimeSegment::Hour);
    TypeKeys(&editor, "9");
    utassert(editor.time == Hms(21, 30, 0));
    // "00" is not an hour on a 12-hour clock.
    editor.SelectSegment(TimeSegment::Hour);
    TypeKeys(&editor, "00");
    utassert(editor.segment == TimeSegment::Hour);
}

// twelve_hour_stepping_wraps_within_the_period
static void TwelveHourSteppingWrapsWithinThePeriod() {
    SegmentEditor editor = NewEditor(TimePrecision::Minute, HourCycle::H12);
    editor.SetTime(Hms(11, 0, 0));
    editor.Step(1);
    utassert(editor.time == Hms(0, 0, 0)); // 11 AM steps to 12 AM
    editor.SelectSegment(TimeSegment::Period);
    editor.Step(1);
    utassert(editor.time == Hms(12, 0, 0));
    editor.Step(1);
    utassert(editor.time == Hms(0, 0, 0));
    // Digits do not edit the period.
    utassert(!editor.InputDigit(1));
}

// The formats a field shows its value in, and the aria value it carries.
static void TheFormatFollowsPrecisionAndCycle() {
    Arena* a = ArenaNew();
    utassert(
        base::StrEq(TimePrecisionFormat(TimePrecision::Second, HourCycle::H12),
                    StrL("%I:%M:%S %p")));
    utassert(base::StrEq(TimeFormat(a, StrL("%H:%M:%S"), Hms(8, 5, 9)),
                         StrL("08:05:09")));
    utassert(base::StrEq(TimeFormat(a, StrL("%I:%M %p"), Hms(0, 0, 0)),
                         StrL("12:00 AM")));
    utassert(base::StrEq(TimeFormat(a, StrL("%I:%M %p"), Hms(21, 30, 0)),
                         StrL("09:30 PM")));
    ArenaDelete(a);
}

struct TimeFieldRecorder {
    LocalTime changes[8] = {};
    int count = 0;

    static void OnEvent(TimeFieldRecorder* self, Ctx*,
                        const TimeFieldEvent* ev) {
        if (self->count < 8) {
            self->changes[self->count++] = ev->time;
        }
    }
};

// The state's own handlers: a digit keystroke, the step and delete actions
// and a press on a segment all reach the editor, and every edit that changed
// the time emits it. set_time does not.
static void TheStateEmitsUserEditsOnly() {
    App app = {};
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    Entity<TimeFieldState> state =
        TimeFieldStateNew(&cx, TimePrecision::Second, HourCycle::H23);
    Entity<TimeFieldRecorder> recorder =
        EntityNewState<TimeFieldRecorder>(&app);
    Subscription sub =
        SubscribeTo(&app, state, recorder, &TimeFieldRecorder::OnEvent);
    TimeFieldState* s = state.Get(&app);
    TimeFieldRecorder* r = recorder.Get(&app);
    utassert(s && r);
    if (s && r) {
        TimeFieldStateSetTime(s, Hms(8, 0, 0), &cx);
        utassert(r->count == 0);

        MouseDownEvent press = {};
        TimeFieldState::OnSegmentDown(s, &cx, &press,
                                      (int64_t)TimeSegment::Minute);
        utassert(s->SelectedSegment() == TimeSegment::Minute);
        KeyEvent four = {};
        four.vk = '4';
        four.down = true;
        TimeFieldState::OnKeyDown(s, &cx, &four);
        utassert(!four.propagate);
        KeyEvent five = four;
        five.vk = '5';
        five.propagate = true;
        TimeFieldState::OnKeyDown(s, &cx, &five);
        // The minute is complete, so the seconds segment is selected next.
        utassert(s->SelectedSegment() == TimeSegment::Second);
        ActionEvent up = {};
        up.action = TimeFieldIncrement();
        TimeFieldState::OnAction(s, &cx, &up);
        utassert(s->Time() == Hms(8, 45, 1));
        utassert(r->count == 3);
        utassert(r->changes[0] == Hms(8, 4, 0));
        utassert(r->changes[2] == Hms(8, 45, 1));

        // Tab leaves the field from its last segment.
        ActionEvent tab = {};
        tab.action = action::SelectNextColumn();
        tab.propagate = false;
        TimeFieldState::OnAction(s, &cx, &tab);
        utassert(tab.propagate);

        // A shifted letter is not typed into the period.
        KeyEvent shifted = {};
        shifted.vk = 'P';
        shifted.shift = true;
        TimeFieldState::OnKeyDown(s, &cx, &shifted);
        utassert(shifted.propagate);
    }
    EntityUnsubscribe(&app, sub);
    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

// The element: a time input carrying its value, a `:` between the numeric
// segments and none before AM/PM, and each segment named so a press finds it.
static void TheFieldNamesItsSegments() {
    App app = {};
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    Entity<TimeFieldState> state =
        TimeFieldStateNew(&cx, TimePrecision::Minute, HourCycle::H12);
    if (TimeFieldState* s = state.Get(&app)) {
        TimeFieldStateSetTime(s, Hms(21, 30, 0), &cx);
    }
    El* root = TimeField::New(&cx, StrL("time"), state)->IntoEl();
    utassert(root->accessibility.role == AccessibilityRole::TimeInput);
    utassert(base::StrEq(root->accessibility.value, StrL("09:30 PM")));
    int children = 0;
    for (El* c = root->first; c; c = c->next) {
        children++;
    }
    // hour, ":", minute, period.
    utassert(children == 4);
    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

// pressing_outside_the_segments_focuses_the_first_segment. The component
// test: a 200px field, a press in the empty part selects the hour and
// focuses the field, a press on the minute selects that segment, and a
// press in the empty part again returns to the hour.
struct TimePressView {
    Entity<TimeFieldState> state = {};

    static El* Render(TimePressView* self, Ctx* cx) {
        Style wide = {};
        wide.width = 200;
        return Div(cx->a)->Child(component::TimeField::New(cx, self->state)
                                     ->WithId(StrL("time"))
                                     ->Refine(wide, StyleFieldWidth)
                                     ->IntoEl());
    }
};

static Bounds HitPath(Window* win, uint32_t path) {
    if (!win) {
        return {};
    }
    for (int i = 0; i < win->paint.hits.len; i++) {
        if (win->paint.hits[i].pathId == path) {
            return win->paint.hits[i].bounds;
        }
    }
    return {};
}

static void PressingOutsideTheSegmentsFocusesTheFirstSegment() {
    App* app = TestAppNew();
    component::Init(app);
    Entity<TimePressView> view = EntityNew<TimePressView>(app);
    TimePressView* harness = view.Get(app);
    Ctx made = {app, nullptr, GetTempArena(), {}};
    if (harness) {
        harness->state = TimeFieldStateNew(&made);
    }
    Window* win = TestWindowOpen(app, view);
    TestDraw(win);
    TimeFieldState* state = harness ? harness->state.Get(app) : nullptr;
    uint32_t field = IdFoldName(0, StrL("time"));
    Bounds hour = HitPath(win, IdFoldName(field, StrL("hour")));
    Bounds minute = HitPath(win, IdFoldName(field, StrL("minute")));
    utassert(state && hour.w > 0 && minute.w > 0);
    if (state && hour.w > 0 && minute.w > 0) {
        Point blank = {180.f, hour.y + hour.h * 0.5f};
        TestSimulateClick(win, blank);
        utassert(FocusHandleIsFocused(win, state->focus));
        utassert(state->SelectedSegment() == TimeSegment::Hour);

        TestSimulateClick(
            win, {minute.x + minute.w * 0.5f, minute.y + minute.h * 0.5f});
        utassert(state->SelectedSegment() == TimeSegment::Minute);

        TestSimulateClick(win, blank);
        utassert(state->SelectedSegment() == TimeSegment::Hour);
    }
    TestAppFree(app);
}

void TestTimeField() {
    TestSuite("time_field");
    TypingFillsSegmentsAndAdvances();
    ADigitThatCannotStartTwoDigitsCompletesTheSegment();
    AnOutOfRangePairRestartsFromTheSecondDigit();
    SteppingWrapsWithoutCarry();
    SegmentMovementStopsAtTheEnds();
    PrecisionTruncatesSeconds();
    ClearingResetsOnlyTheSelectedSegment();
    TwelveHourLabelsMapMidnightAndNoonToTwelve();
    TwelveHourTypingKeepsThePeriodUntilItIsTyped();
    TwelveHourSteppingWrapsWithinThePeriod();
    TheFormatFollowsPrecisionAndCycle();
    TheStateEmitsUserEditsOnly();
    TheFieldNamesItsSegments();
    PressingOutsideTheSegmentsFocusesTheFirstSegment();
}
