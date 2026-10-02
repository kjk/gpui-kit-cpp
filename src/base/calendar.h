#ifndef GPUI_BASE_CALENDAR_H_
#define GPUI_BASE_CALENDAR_H_
/* Unstyled calendar — crates/base/src/calendar.rs */

#include "gpui/gpui.h"

namespace gpui {

// Which grid a calendar is showing. Rust's CalendarView, and what its header
// switches between.
enum class CalendarView : uint8_t {
    Day,
    Month,
    Year
};

// Date is the tagged POD projection of Rust's payload enum. An invalid
// LocalDate is Option::None; Range therefore keeps both optional endpoints
// without heap storage.
enum class DateKind : uint8_t {
    Single,
    Range
};

struct Date {
    DateKind kind = DateKind::Single;
    LocalDate start = {};
    LocalDate end = {};

    static Date Single(LocalDate value = {});
    static Date Range(LocalDate start = {}, LocalDate end = {});
    bool IsSome() const;
    bool IsComplete() const;
    bool IsSingle() const { return kind == DateKind::Single; }
    bool IsActive(LocalDate value) const;
    bool IsInRange(LocalDate value) const;
};

// Matcher is likewise a tagged POD value where Rust uses an enum carrying
// Vec/Rc/Box payloads. Seven weekdays fit in a mask and the dependency-free
// custom case is the function pointer convention used across Base.
struct IntervalMatcher {
    LocalDate before = {};
    LocalDate after = {};
};

struct RangeMatcher {
    LocalDate from = {};
    LocalDate to = {};
};

enum class MatcherKind : uint8_t {
    None,
    DayOfWeek,
    // Compatibility spelling from the first C++ surface.
    Weekdays = DayOfWeek,
    Interval,
    Range,
    Custom
};

struct Matcher {
    MatcherKind kind = MatcherKind::None;
    uint8_t weekdayMask = 0;
    IntervalMatcher interval = {};
    RangeMatcher range = {};
    // Compatibility view of the two payloads. Constructors fill both; a
    // value initialized through the older public fields is read as a fallback.
    LocalDate from = {};
    LocalDate to = {};
    bool (*custom)(LocalDate date) = nullptr;
};

// Compatibility names from the first C++ surface.
using DateMatcher = Matcher;
using DateMatcherKind = MatcherKind;

Matcher DateMatcherWeekdays(uint8_t weekdayMask);
Matcher DateMatcherInterval(LocalDate before, LocalDate after);
Matcher DateMatcherRange(LocalDate from, LocalDate to);
Matcher DateMatcherCustom(bool (*fn)(LocalDate date));
bool DateMatcherMatches(const Matcher& matcher, LocalDate date);
bool MatcherMatches(const Matcher& matcher, Date date);

// CalendarEvent::Selected(Date). One variant still carries its payload as an
// event struct so EntityEmit can pass it without allocation.
enum class CalendarEventKind : uint8_t {
    Selected
};

struct CalendarEvent {
    CalendarEventKind kind = CalendarEventKind::Selected;
    Date date = {};
};

// Rust's retained CalendarState: selection, view, navigation, year pages and
// the matcher all live in one entity. The public current/year-page fields are
// retained for compatibility with the original C++ pure helpers.
struct CalendarState {
    Entity<CalendarState> self = {};
    FocusHandle focus = {};
    CalendarView view = CalendarView::Day;
    Date date = {};
    int currentYear = 0;
    // 1..12, as Rust keeps it.
    int currentMonth = 1;
    int numberOfMonths = 1;
    // The year grid is paged, and `yearPageCount` is how many pages the year
    // range came to.
    int yearPage = 0;
    int yearPageCount = 0;
    int yearMin = 0;
    int yearMax = 0; // exclusive
    LocalDate today = {};
    Matcher disabledMatcher = {};

    static void OnDate(CalendarState* self, Ctx* cx, const ClickEvent* ev,
                       int64_t dateKey);
    static void OnPrev(CalendarState* self, Ctx* cx, const ClickEvent* ev);
    static void OnNext(CalendarState* self, Ctx* cx, const ClickEvent* ev);
    static void OnMonthToggle(CalendarState* self, Ctx* cx,
                              const ClickEvent* ev);
    static void OnYearToggle(CalendarState* self, Ctx* cx,
                             const ClickEvent* ev);
    static void OnMonth(CalendarState* self, Ctx* cx, const ClickEvent* ev,
                        int64_t month);
    static void OnYear(CalendarState* self, Ctx* cx, const ClickEvent* ev,
                       int64_t year);
};

void CalendarStateInit(CalendarState* s, Ctx* cx, Date date = Date::Single());
Entity<CalendarState> CalendarStateNew(Ctx* cx, Date date = Date::Single());
bool CalendarStateApplyDate(CalendarState* s, Date date);
void CalendarStateSetDate(CalendarState* s, Date date, Ctx* cx,
                          bool emit = false);
bool CalendarStateSelectDate(CalendarState* s, LocalDate value, Ctx* cx,
                             bool emit = true);
void CalendarStateSetDisabledMatcher(CalendarState* s, Matcher matcher,
                                     Ctx* cx = nullptr);
void CalendarStateSetYearRange(CalendarState* s, int minYear, int maxYear,
                               Ctx* cx = nullptr);

// prev_month / next_month. Stepping off either end of the year carries into
// the next one, which is the whole reason these are not `month += 1`.
void CalendarPrevMonth(CalendarState* s);
void CalendarNextMonth(CalendarState* s);

// The year grid's paging. Rust's movers answer whether they moved, so a header
// can leave its arrow disabled at the ends.
bool CalendarHasPrevYearPage(const CalendarState* s);
bool CalendarHasNextYearPage(const CalendarState* s);
bool CalendarPrevYearPage(CalendarState* s);
bool CalendarNextYearPage(CalendarState* s);

// days_in_month: where the grid for a month starts, as a day offset back from
// its first. Sunday-first, as Rust's default weekday is, and the grid then
// runs in whole weeks — which is what keeps a six-week month six weeks.
//
// `firstWeekday` is the weekday of the 1st, 0 for Sunday.
int CalendarGridOffset(int firstWeekday);
// How many cells the grid needs: whole weeks covering the month from that
// offset. Rust's div_ceil(7) * 7.
int CalendarGridCells(int offset, int daysInMonth);

// CalendarItemKind: what a slot in the calendar stands for. Every element a
// calendar builds is one of these, and every one of them is handed to the
// caller to decorate.
enum class CalendarItemKind : uint8_t {
    Previous,
    MonthToggle,
    YearToggle,
    Next,
    Weekday,
    Day,
    Month,
    Year
};

// CalendarItemState: the slot, and everything about it a look could depend
// on. Rust keeps the fields private behind readers so a new one can be added
// without breaking the item slots; the same care applies here, which is why
// a caller is handed this rather than the six flags.
struct CalendarItemState {
    CalendarItemKind kind = CalendarItemKind::Day;
    // The day of the month, the month, the year, or the weekday's index. The
    // two arrows have none.
    int value = 0;
    // Day only: the date the cell stands for, which may be in a neighbouring
    // month.
    LocalDate date = {};
    bool active = false;
    bool inRange = false;
    bool muted = false;
    bool disabled = false;
    bool today = false;
};

// `Calendar::item(|item, state, window, cx| ...)`. The calendar builds the
// slot — its size, its place in the grid, its id and its click — and hands it
// over to be decorated; what comes back is what goes in the grid, which is
// normally the same element with a look and a label on it. A function and a
// user pointer, since an element here holds no closures.
using CalendarItemFn = El* (*)(void* user, Ctx* cx, El* item,
                               const CalendarItemState& st);
using CalendarLabelFn = Str (*)(void* user, Ctx* cx, CalendarItemKind kind,
                                int value);

// What the calendar is looking at and what it may do — everything except how
// it looks, which is the item function's.
struct CalendarOpts {
    int year = 0;
    int month = 1;
    int numberOfMonths = 1;
    CalendarView view = CalendarView::Day;
    // The square a day, a weekday head and an arrow are drawn in.
    float cellSize = 32;
    // The selection, and the far end of it for a range. A `day` of 0 is no
    // selection at all.
    LocalDate selected = {};
    LocalDate rangeEnd = {};
    // Today, for the cell that is outlined. The caller passes it rather than
    // the calendar asking, so a test can say what day it is.
    LocalDate today = {};
    DateMatcher disabledMatcher = {};
    // chrono::Weekday::num_days_from_sunday: 0 is Sunday, 1 Monday.
    int firstDayOfWeek = 0;
    // The year grid's range and which page of it is up.
    int yearMin = 0;
    int yearMax = 0; // exclusive
    int yearPageStart = 0;
    Listener onDay = {};  // day of month
    Listener onDate = {}; // DatePickerDateKey(LocalDate)
    Listener onPrev = {};
    Listener onNext = {};
    Listener onMonthToggle = {};
    Listener onYearToggle = {};
    Listener onMonth = {};
    Listener onYear = {};
    CalendarItemFn item = nullptr;
    void* user = nullptr;
    CalendarLabelFn label = nullptr;
    void* labelUser = nullptr;
};

struct Calendar {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<CalendarState> state = {};
    CalendarOpts opts = {};
    Style style = {};
    uint32_t styleSet = 0;

    // Source-shaped retained-state builder.
    static Calendar* New(Ctx* cx, Str id, Entity<CalendarState> state);
    Calendar* NumberOfMonths(int count);
    Calendar* FirstDayOfWeek(int weekday);
    Calendar* Item(CalendarItemFn fn, void* user = nullptr);
    Calendar* Label(CalendarLabelFn fn, void* user = nullptr);
    Calendar* Refine(const Style& v, uint32_t fields);
    El* IntoEl();

    // The bare box, for a caller that builds the grid itself.
    static El* New(Ctx* cx, Str id);
    // The calendar: the header with its two arrows and its month and year
    // toggles, and below it the day grid, the month picker or the year
    // picker. The root carries no padding or border of its own — the caller
    // chains those onto what comes back.
    static El* New(Ctx* cx, Str id, const CalendarOpts& o);
};

// The month a calendar `offset` months on from the one it is looking at.
// Stepping off either end of the year carries, which is the whole reason this
// is not `month + offset`.
void CalendarOffsetMonth(int year, int month, int offset, int* outYear,
                         int* outMonth);
// The weekday of a date, 0 for Sunday — Sakamoto, which is what puts the 1st
// under the right head.
int CalendarWeekday(int year, int month, int day);
// How many days a month has, leap years counted.
int CalendarDaysInMonth(int year, int month);

struct CalendarItem {
    static El* New(Ctx* cx, Str id = {}, Listener onClick = {});
};

template <>
struct EventEmitter<CalendarState, CalendarEvent> {};
} // namespace gpui
#endif // GPUI_BASE_CALENDAR_H_
