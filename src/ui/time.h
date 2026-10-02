#ifndef GPUI_SRC_UI_TIME_H_
#define GPUI_SRC_UI_TIME_H_
/* Themed calendar, date picker and time field — crates/ui/src/time/ */

#include "ui/sizing.h"

namespace gpui {

namespace component {

struct Calendar {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    int year = 2026;
    int month = 1;
    int day = 1;
    int selectedYear = 0;
    int selectedMonth = 0;
    LocalDate rangeEnd = {};
    UiSize size = UiSize::Medium;
    int numberOfMonths = 1;
    CalendarView view = CalendarView::Day;
    int yearMin = 0;
    int yearMax = 0; // exclusive
    int yearPageStart = 0;
    DateMatcher disabledMatcher = {};
    Entity<CalendarState> state = {};
    int firstDayOfWeek = 0;
    Style style = {};
    uint32_t styleSet = 0;
    // border_0().rounded_none().p_0(): what a DatePicker's popup asks for,
    // since the popup is the frame and a second one inside it would show.
    bool bare = false;
    Listener onDay;  // day of month
    Listener onDate; // DatePickerDateKey(LocalDate)
    Listener onPrev;
    Listener onNext;
    Listener onMonthToggle;
    Listener onYearToggle;
    Listener onMonth;
    Listener onYear;

    static Calendar* New(Ctx* cx);
    // The source-shaped facade: behavior stays in Base CalendarState and
    // this layer supplies only labels, sizes and theme tokens.
    static Calendar* New(Ctx* cx, Entity<CalendarState> state);
    Calendar* Year(int y);
    Calendar* Month(int m);
    Calendar* Day(int d);
    Calendar* Selection(int y, int m, int d);
    Calendar* RangeEnd(int y, int m, int d);
    Calendar* WithSize(UiSize s);
    Calendar* NumberOfMonths(int count);
    Calendar* FirstDayOfWeek(int weekday);
    Calendar* View(CalendarView value);
    Calendar* YearRange(int minYear, int maxYear, int pageStart);
    Calendar* DisabledMatcher(DateMatcher matcher);
    Calendar* Bare();
    Calendar* Refine(const Style& value, uint32_t fields);
    Calendar* OnDay(Listener fn);
    Calendar* OnDate(Listener fn);
    Calendar* OnPrev(Listener fn);
    Calendar* OnNext(Listener fn);
    Calendar* OnMonthToggle(Listener fn);
    Calendar* OnYearToggle(Listener fn);
    Calendar* OnMonth(Listener fn);
    Calendar* OnYear(Listener fn);
    El* IntoEl();
};

// time/time_field.rs re-exports Base's state and its vocabulary.
using ::gpui::HourCycle;
using ::gpui::TimeFieldEvent;
using ::gpui::TimeFieldState;
using ::gpui::TimePrecision;
using ::gpui::TimeSegment;

// A segmented time editor, e.g. 09:30, 09:30:15 or 09:30 PM. The value
// lives in TimeFieldState; see it for the keyboard model.
struct TimeField {
    Ctx* cx = nullptr;
    Str id = {};
    Entity<TimeFieldState> state = {};
    UiSize size = UiSize::Medium;
    Style style = {};
    uint32_t styleSet = 0;
    bool disabled = false;
    bool invalid = false;

    // The id defaults to ("time-field", state id).
    static TimeField* New(Ctx* cx, Entity<TimeFieldState> state);
    // with_id: the name a date picker gives its own field.
    TimeField* WithId(Str id);
    // Display the caller's validation result. This does not reject edits.
    TimeField* Invalid(bool v = true);
    TimeField* WithSize(UiSize s);
    TimeField* Disabled(bool v = true);
    TimeField* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

// A calendar date and its time of day: chrono's NaiveDateTime.
struct LocalDateTime {
    LocalDate date = {};
    LocalTime time = {};
};

// date_picker::DateTime: the selected date or dates combined with their
// times of day. A date of all zeros is None, as in Date. When the picker has
// no time precision, every time is its default time, 00:00 unless set.
struct DateTime {
    DateKind kind = DateKind::Single;
    LocalDate startDate = {};
    LocalTime startTime = {};
    LocalDate endDate = {};
    LocalTime endTime = {};

    static DateTime Single(LocalDateTime value);
    static DateTime Single();
    static DateTime Range(LocalDateTime start, LocalDateTime end);
    bool IsSome() const;
    bool IsComplete() const;
    bool Start(LocalDateTime* out) const;
    bool End(LocalDateTime* out) const;
    // The date part of this value.
    Date DateValue() const;
    // Format a complete value, joining a range with " - ". Empty when it is
    // not complete, which is Rust's None.
    Str Format(Arena* a, Str pattern) const;
};

enum class DatePickerEventKind : uint8_t {
    Change
};

// DatePickerEvent::Change(DateTime): the user changed the value. With a time
// precision set, this is emitted on every edit while the popup stays open.
struct DatePickerEvent {
    DatePickerEventKind kind = DatePickerEventKind::Change;
    DateTime value = {};
};

enum class DateRangePresetValueKind : uint8_t {
    Single,
    Range,
    DateTime
};

struct DateRangePresetValue {
    DateRangePresetValueKind kind = DateRangePresetValueKind::Single;
    LocalDate start = {};
    LocalDate end = {};
    // DateRangePresetValue::DateTime: a date with its time, or a range of
    // them. The times are kept as given, even by a range picker.
    ::gpui::component::DateTime dateTime = {};

    static DateRangePresetValue Single(LocalDate date);
    static DateRangePresetValue Range(LocalDate start, LocalDate end);
    static DateRangePresetValue WithDateTime(::gpui::component::DateTime value);
    Date IntoDate() const;
};

struct DateRangePreset {
    Str label = {};
    DateRangePresetValue value = {};
    // Compatibility fields from the earlier controlled builder. New code
    // uses `value`; old aggregate initialization continues to render.
    LocalDate start = {};
    LocalDate end = {};
    int64_t arg = 0;

    static DateRangePreset Single(Str label, LocalDate date, int64_t arg = 0);
    static DateRangePreset Range(Str label, LocalDate start, LocalDate end,
                                 int64_t arg = 0);
    // DateRangePreset::date_time: a preset with a date and time, or a range
    // of them.
    static DateRangePreset WithDateTime(Str label,
                                        ::gpui::component::DateTime value);
};

// The two formats the story uses: %Y/%m/%d (the default) and %Y-%m-%d.
enum class DateFormat : uint8_t {
    Slash,
    Dash
};

// The retained state in crates/ui/src/time/date_picker.rs. The picker owns
// its Base calendar entity and forwards its completed selections as
// DatePickerEvent::Change.
struct DatePickerState {
    Entity<DatePickerState> self = {};
    FocusHandle focus = {};
    Date date = {};
    bool open = false;
    Entity<CalendarState> calendar = {};
    // Heap-owned because this state outlives every frame arena. Empty is
    // Rust's None: the display format then follows the time precision.
    Str dateFormat = {};
    // time_precision: None (false) edits dates only.
    bool hasTimePrecision = false;
    TimePrecision timePrecision = TimePrecision::Minute;
    HourCycle hourCycle = HourCycle::H23;
    LocalTime defaultTime = {};
    LocalTime startTime = {};
    // The time of a range's end. Only DatePickerStateSetDateTime sets it,
    // since a range picker edits dates only.
    LocalTime endTime = {};
    Entity<TimeFieldState> timeField = {};
    bool timeFieldPushed = false;
    Subscription timeFieldSubscription = {};
    int numberOfMonths = 1;
    Matcher disabledMatcher = {};
    Subscription calendarSubscription = {};
    int firstDayOfWeek = 0;
    Bounds bounds = {};

    ~DatePickerState();

    static void OnCalendar(DatePickerState* self, Ctx* cx,
                           const CalendarEvent* ev);
    static void OnTimeField(DatePickerState* self, Ctx* cx,
                            const TimeFieldEvent* ev);
    static void OnToggle(DatePickerState* self, Ctx* cx, const ClickEvent* ev);
    static void OnOpenChange(DatePickerState* self, Ctx* cx,
                             const ClickEvent* ev, int64_t open);
    static void OnDismiss(DatePickerState* self, Ctx* cx,
                          const MouseUpEvent* ev);
    static void OnClear(DatePickerState* self, Ctx* cx, const ClickEvent* ev);
};

Entity<DatePickerState> DatePickerStateNew(Ctx* cx, bool range = false);
inline Entity<DatePickerState> DatePickerStateRange(Ctx* cx) {
    return DatePickerStateNew(cx, true);
}
void DatePickerStateSetDate(DatePickerState* state, Date date, Ctx* cx,
                            bool emit = false);
void DatePickerStateSetDateFormat(DatePickerState* state, Str format,
                                  Ctx* cx = nullptr);
void DatePickerStateSetNumberOfMonths(DatePickerState* state, int count,
                                      Ctx* cx = nullptr);
void DatePickerStateSetFirstDayOfWeek(DatePickerState* state, int weekday,
                                      Ctx* cx = nullptr);
void DatePickerStateSetDisabledMatcher(DatePickerState* state, Matcher matcher,
                                       Ctx* cx = nullptr);
void DatePickerStateSetYearRange(DatePickerState* state, int minYear,
                                 int maxYear, Ctx* cx = nullptr);
void DatePickerStateSelectPreset(DatePickerState* state,
                                 const DateRangePreset& preset, Ctx* cx,
                                 bool emit = true);
// time_precision: edit the time of day as well as the date, down to
// precision. Selecting a date then keeps the popup open, and every change to
// the date or time is reported as it happens; clicking the selected date
// again closes the popup. A range picker edits dates only.
void DatePickerStateSetTimePrecision(DatePickerState* state,
                                     TimePrecision precision);
// hour_cycle: how the time field counts hours, default H23.
void DatePickerStateSetHourCycle(DatePickerState* state, HourCycle hourCycle);
// default_time: the time given to a date before the user edits it, 00:00
// unless set.
void DatePickerStateSetDefaultTime(DatePickerState* state, LocalTime time);
// date_time: the value, combining the date with its time of day.
DateTime DatePickerStateDateTime(const DatePickerState* state);
// set_date_time: the date and the time of day. Does not emit.
void DatePickerStateSetDateTime(DatePickerState* state, DateTime value,
                                Ctx* cx);
// The format the trigger shows: date_format, or %Y/%m/%d followed by the
// time at the precision and hour cycle while the picker edits times.
Str DatePickerStateDisplayFormat(Arena* a, const DatePickerState* state);

// chrono's formatting seam, kept dependency-free. It covers the numeric,
// name and weekday directives used by gpui-kit, the time directives Base's
// TimeFormat knows for time, and copies unknown directives literally instead
// of silently changing the requested pattern.
Str DatePickerFormatDate(Arena* a, Str pattern, LocalDate date,
                         LocalTime time = {});
Str DatePickerFormatValue(Arena* a, Str pattern, Date date);

struct DatePicker {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    int year = 2026;
    int month = 1;
    int day = 1; // 0: no date picked, so the placeholder shows
    int viewYear = 0;
    int viewMonth = 0;
    // The end of a range; year2 == 0 means a single date.
    int year2 = 0;
    int month2 = 0;
    int day2 = 0;
    Str placeholder = {};
    DateFormat format = DateFormat::Slash;
    UiSize size = UiSize::Medium;
    float width = kFill;
    // cleanable swaps the calendar icon for a clear button once a date is set.
    bool cleanable = false;
    bool appearance = true;
    bool focusRing = true;
    bool disabled = false;
    bool range = false;
    bool open = false;
    int numberOfMonths = 1;
    CalendarView calendarView = CalendarView::Day;
    int yearMin = 0;
    int yearMax = 0;
    int yearPageStart = 0;
    DateMatcher disabledMatcher = {};
    const DateRangePreset* presets = nullptr;
    int presetsCount = 0;
    Listener onToggle;
    Listener onDay;
    Listener onDate;
    Listener onClear;
    Listener onPrev;
    Listener onNext;
    Listener onMonthToggle;
    Listener onYearToggle;
    Listener onMonth;
    Listener onYear;
    Listener onPreset;
    Entity<DatePickerState> state = {};
    Style style = {};
    uint32_t styleSet = 0;

    static DatePicker* New(Ctx* cx);
    static DatePicker* New(Ctx* cx, Entity<DatePickerState> state);
    DatePicker* Id(Str value);
    DatePicker* Year(int y);
    DatePicker* Month(int m);
    DatePicker* Day(int d);
    DatePicker* View(int y, int m);
    DatePicker* RangeEnd(int y, int m, int d);
    DatePicker* Placeholder(Str s);
    DatePicker* Format(DateFormat f);
    DatePicker* WithSize(UiSize s);
    DatePicker* W(float v);
    DatePicker* Cleanable(bool v = true);
    DatePicker* Appearance(bool v);
    // FocusableExt::focus_ring: no focus appearance on this control.
    DatePicker* FocusRing(bool v);
    DatePicker* Refine(const Style& value, uint32_t fields);
    DatePicker* Disabled(bool v = true);
    DatePicker* Range(bool v = true);
    DatePicker* NumberOfMonths(int count);
    DatePicker* CalendarMode(CalendarView value);
    DatePicker* YearRange(int minYear, int maxYear, int pageStart);
    DatePicker* DisabledMatcher(DateMatcher matcher);
    DatePicker* Presets(const DateRangePreset* values, int count,
                        Listener onSelect = {});
    DatePicker* Open(bool v);
    DatePicker* OnToggle(Listener fn);
    DatePicker* OnDay(Listener fn);
    DatePicker* OnDate(Listener fn);
    DatePicker* OnClear(Listener fn);
    DatePicker* OnPrev(Listener fn);
    DatePicker* OnNext(Listener fn);
    DatePicker* OnMonthToggle(Listener fn);
    DatePicker* OnYearToggle(Listener fn);
    DatePicker* OnMonth(Listener fn);
    DatePicker* OnYear(Listener fn);
    El* IntoEl();
};

} // namespace component

template <>
struct EventEmitter<component::DatePickerState, component::DatePickerEvent> {};

} // namespace gpui
#endif // GPUI_SRC_UI_TIME_H_
