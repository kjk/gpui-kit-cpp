#ifndef GPUI_BASE_TIME_FIELD_H_
#define GPUI_BASE_TIME_FIELD_H_
/* Unstyled segmented time field — crates/base/src/time_field.rs */

#include "gpui/gpui.h"

namespace gpui {

// The smallest unit a TimeField edits.
enum class TimePrecision : uint8_t {
    // Hours and minutes, e.g. `09:30`.
    Minute,
    // Hours, minutes and seconds, e.g. `09:30:15`.
    Second,
};

// How a TimeField counts the hours of a day. The names follow the Unicode
// `hourCycle` values.
enum class HourCycle : uint8_t {
    // A 24-hour clock from `00` to `23`.
    H23,
    // A 12-hour clock from `12` to `11`, with an AM/PM segment.
    H12,
};

// One editable component of a TimeField.
enum class TimeSegment : uint8_t {
    Hour,
    Minute,
    Second,
    // AM or PM, present with HourCycle::H12.
    Period,
};

// TimePrecision::format: the chrono format that displays a time at this
// precision and cycle. Static.
Str TimePrecisionFormat(TimePrecision precision, HourCycle hourCycle);
// TimePrecision::truncate: drop the components finer than this precision.
LocalTime TimePrecisionTruncate(TimePrecision precision, LocalTime time);
// NaiveTime::format for the time directives chrono gives it: %H %I %k %l
// %M %S %p %P %R %T %X and %%. Anything else is copied as written.
Str TimeFormat(Arena* a, Str pattern, LocalTime time);

enum class TimeFieldEventKind : uint8_t {
    // Keyboard editing changed the time.
    Change,
};

// TimeFieldEvent::Change(NaiveTime).
struct TimeFieldEvent {
    TimeFieldEventKind kind = TimeFieldEventKind::Change;
    LocalTime time = {};
};

// The pure editing rules of a TimeFieldState, kept apart from focus so they
// can be tested without a window. Rust keeps SegmentEditor private to the
// module and tests it there; here it is the seam the tests reach it by.
struct SegmentEditor {
    LocalTime time = {};
    TimePrecision precision = TimePrecision::Minute;
    HourCycle hourCycle = HourCycle::H23;
    TimeSegment segment = TimeSegment::Hour;
    // The first digit typed into the selected segment, awaiting a second.
    // -1 is None.
    int pendingDigit = -1;

    // The segments shown, in reading order. Writes up to four; returns how
    // many there are.
    int Segments(TimeSegment out[4]) const;
    bool HasSegment(TimeSegment s) const;
    // The inclusive range a segment's displayed value takes.
    void Bounds(TimeSegment s, int* min, int* max) const;
    // The displayed value of a segment: the hour follows the cycle, and the
    // period is 0 for AM and 1 for PM.
    int Value(TimeSegment s) const;
    // Two digits, or AM / PM. Static or in `a`.
    Str Label(Arena* a, TimeSegment s) const;
    LocalTime WithValue(TimeSegment s, int value) const;
    void SetPrecision(TimePrecision value);
    void SetHourCycle(HourCycle value);
    void ResetSegment();
    // Answers whether the time changed. Keeps a half-typed segment when the
    // owner echoes the same value back.
    bool SetTime(LocalTime value);
    bool SelectSegment(TimeSegment s);
    // Move the selected segment by `offset`; false at either end.
    bool MoveSegment(int offset);
    // Step the selected segment by `delta`, wrapping within the segment.
    bool Step(int delta);
    bool InputDigit(int digit);
    // Type `a` or `p` into the period segment.
    bool InputPeriod(bool pm);
    // Reset the selected segment to its first value: zero, or 12 / AM on a
    // 12-hour clock.
    bool ClearSegment();
    bool ReplaceSegment(int value);
};

// Retained editing behavior for a segmented time field.
//
// The field is one Tab stop. Inside it, one segment is selected at a time:
// Up/Down step the selected segment and wrap within it (no carry into the
// next unit), Left/Right and Tab/Shift-Tab move between segments, digits are
// typed with a two-digit buffer that advances to the next segment once no
// further digit could fit, `a`/`p` set the AM/PM segment, and
// Backspace/Delete reset the segment.
//
// The owner supplies the value with TimeFieldStateSetTime, which does not
// emit; user edits emit TimeFieldEvent::Change.
struct TimeFieldState {
    Entity<TimeFieldState> self = {};
    FocusHandle focus = {};
    SegmentEditor editor = {};

    LocalTime Time() const { return editor.time; }
    // The segment that keyboard editing applies to.
    TimeSegment SelectedSegment() const { return editor.segment; }

    // The "TimeField" key context's actions and its own keystrokes.
    static void OnAction(TimeFieldState* self, Ctx* cx, const ActionEvent* ev);
    static void OnKeyDown(TimeFieldState* self, Ctx* cx, const KeyEvent* ev);
    // A press on a segment focuses the field and selects that segment.
    static void OnSegmentDown(TimeFieldState* self, Ctx* cx,
                              const MouseDownEvent* ev, intptr_t segment);
};

// TimeFieldState::new, with the precision and hour cycle builders folded in.
Entity<TimeFieldState> TimeFieldStateNew(
    Ctx* cx, TimePrecision precision = TimePrecision::Minute,
    HourCycle hourCycle = HourCycle::H23);
// set_precision / set_hour_cycle / set_time: none of them emits.
void TimeFieldStateSetPrecision(TimeFieldState* s, TimePrecision precision,
                                Ctx* cx = nullptr);
void TimeFieldStateSetHourCycle(TimeFieldState* s, HourCycle hourCycle,
                                Ctx* cx = nullptr);
void TimeFieldStateSetTime(TimeFieldState* s, LocalTime time,
                           Ctx* cx = nullptr);
void TimeFieldStateFocus(TimeFieldState* s, Window* win);

// time_field.rs::init: the arrows, tab and the two delete keys in the
// "TimeField" key context.
void TimeFieldInitKeys();
Str TimeFieldContext();
uint32_t TimeFieldIncrement();
uint32_t TimeFieldDecrement();

// TimeFieldSegmentState: what a segment slot is told for decoration.
struct TimeFieldSegmentState {
    TimeSegment segment = TimeSegment::Hour;
    int value = 0;
    bool selected = false;
    bool disabled = false;

    TimeSegment Segment() const { return segment; }
    // The displayed value: the hour on the configured clock, the minute or
    // second, or 0 for AM and 1 for PM.
    int Value() const { return value; }
    // Keyboard editing applies to this segment: it is the selected one and
    // the field has focus.
    bool IsSelected() const { return selected; }
    bool IsDisabled() const { return disabled; }
};

// The segment slot. Rust passes a pre-wired TimeFieldSegment the renderer
// styles and returns; here it is that El, already carrying its id, its press
// handler and its label, and TimeFieldSegmentClearChildren is
// `clear_children()`.
using TimeFieldSegmentRenderer = El* (*)(void* user, El* segment,
                                         const TimeFieldSegmentState* state,
                                         Ctx* cx);
void TimeFieldSegmentClearChildren(El* segment);

// Unstyled segmented time editor. Base owns focus, the keyboard model and
// pointer segment selection; the presentation lays out and decorates the
// field through Refine and each segment through RenderSegment. A `:`
// separates the numeric segments; the AM/PM segment follows without one.
struct TimeField {
    Ctx* cx = nullptr;
    Str id = {};
    Entity<TimeFieldState> state = {};
    bool disabled = false;
    Style style = {};
    uint32_t styleSet = 0;
    TimeFieldSegmentRenderer segment = nullptr;
    void* segmentUser = nullptr;

    static TimeField* New(Ctx* cx, Str id, Entity<TimeFieldState> state);
    TimeField* Disabled(bool v = true);
    // Decorate each segment. The segment already carries its label: two
    // digits, or AM/PM.
    TimeField* RenderSegment(TimeFieldSegmentRenderer fn, void* user = nullptr);
    TimeField* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

template <>
struct EventEmitter<TimeFieldState, TimeFieldEvent> {};

} // namespace gpui
#endif // GPUI_BASE_TIME_FIELD_H_
