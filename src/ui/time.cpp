#include "ui/i18n.h"
#include "ui/time.h"
#include "ui/button.h"

namespace gpui {

namespace component {

Calendar* Calendar::New(Ctx* cx) {
    Arena* a = cx->a;
    Calendar* c = ArenaNew<Calendar>(a);
    c->a = a;
    c->cx = cx;
    return c;
}
Calendar* Calendar::New(Ctx* cx, Entity<CalendarState> state) {
    Calendar* c = New(cx);
    c->state = state;
    return c;
}
Calendar* Calendar::Year(int y) {
    year = y;
    return this;
}
Calendar* Calendar::Month(int m) {
    month = m;
    return this;
}
Calendar* Calendar::Day(int d) {
    day = d;
    return this;
}
Calendar* Calendar::Selection(int y, int m, int d) {
    selectedYear = y;
    selectedMonth = m;
    day = d;
    return this;
}
Calendar* Calendar::RangeEnd(int y, int m, int d) {
    rangeEnd = {y, m, d};
    return this;
}
Calendar* Calendar::WithSize(UiSize s) {
    size = s;
    return this;
}
Calendar* Calendar::NumberOfMonths(int count) {
    numberOfMonths = std::max(1, count);
    return this;
}
Calendar* Calendar::FirstDayOfWeek(int weekday) {
    firstDayOfWeek = ((weekday % 7) + 7) % 7;
    return this;
}
Calendar* Calendar::View(CalendarView value) {
    view = value;
    return this;
}
Calendar* Calendar::YearRange(int minYear, int maxYear, int pageStart) {
    yearMin = minYear;
    yearMax = maxYear;
    yearPageStart = pageStart;
    return this;
}
Calendar* Calendar::DisabledMatcher(DateMatcher matcher) {
    disabledMatcher = matcher;
    return this;
}
Calendar* Calendar::Bare() {
    bare = true;
    return this;
}
Calendar* Calendar::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
Calendar* Calendar::OnDay(Listener fn) {
    onDay = fn;
    return this;
}
Calendar* Calendar::OnDate(Listener fn) {
    onDate = fn;
    return this;
}
Calendar* Calendar::OnPrev(Listener fn) {
    onPrev = fn;
    return this;
}
Calendar* Calendar::OnNext(Listener fn) {
    onNext = fn;
    return this;
}
Calendar* Calendar::OnMonthToggle(Listener fn) {
    onMonthToggle = fn;
    return this;
}
Calendar* Calendar::OnYearToggle(Listener fn) {
    onYearToggle = fn;
    return this;
}
Calendar* Calendar::OnMonth(Listener fn) {
    onMonth = fn;
    return this;
}
Calendar* Calendar::OnYear(Listener fn) {
    onYear = fn;
    return this;
}

static float CalendarCellSize(UiSize size) {
    if (size == UiSize::Small) {
        return 28;
    }
    if (size == UiSize::Large) {
        return 40;
    }
    return 32;
}

static float CalendarWidth(UiSize size) {
    // crates/ui/src/time/calendar.rs. These came down with the eight-column
    // fix: a month is seven cells wide now rather than however many the
    // wrapping fitted, so the panel is sized to seven of them plus its
    // padding instead of to what the wrap wanted.
    if (size == UiSize::Small) {
        return 220;
    }
    if (size == UiSize::Large) {
        return 304;
    }
    return 248;
}

// The themed item — crates/ui/src/time/calendar.rs. The calendar builds
// every slot and hands it over; what is left up here is the look and the
// label, which is all a theme has to say about a calendar.
static El* ThemedCalendarItem(void* user, Ctx* cx, El* item,
                              const CalendarItemState& st) {
    Calendar* self = (Calendar*)user;
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    // t!("Calendar.week.N"): the heads are the locale's, so a calendar in
    // Chinese reads 日 一 二 rather than Su Mo Tu.
    static const char* weekdays[] = {"Calendar.week.0", "Calendar.week.1",
                                     "Calendar.week.2", "Calendar.week.3",
                                     "Calendar.week.4", "Calendar.week.5",
                                     "Calendar.week.6"};
    // t!("Calendar.month.January"). One-based, so a month number indexes it.
    static const char* months[] = {
        "",
        "Calendar.month.January",
        "Calendar.month.February",
        "Calendar.month.March",
        "Calendar.month.April",
        "Calendar.month.May",
        "Calendar.month.June",
        "Calendar.month.July",
        "Calendar.month.August",
        "Calendar.month.September",
        "Calendar.month.October",
        "Calendar.month.November",
        "Calendar.month.December",
    };
    float cellSize = CalendarCellSize(self->size);
    switch (st.kind) {
        case CalendarItemKind::Previous:
        case CalendarItemKind::Next: {
            bool on = !st.disabled;
            item->Radius(th.radius)
                ->Child(IconEl(a,
                               st.kind == CalendarItemKind::Previous
                                   ? IconName::ChevronLeft
                                   : IconName::ChevronRight,
                               16)
                            ->Fg(on ? th.foreground : th.mutedFg));
            if (on) {
                item->HoverBg(th.secondaryHover)
                    ->FocusId(HashClickId(st.kind == CalendarItemKind::Previous
                                              ? StrL("cal-prev")
                                              : StrL("cal-next")));
            }
            return item;
        }
        case CalendarItemKind::MonthToggle:
        case CalendarItemKind::YearToggle: {
            bool isMonth = st.kind == CalendarItemKind::MonthToggle;
            // Several months at once are plain labels rather than toggles, and
            // the calendar says so by handing over a slot with no id.
            if (self->numberOfMonths > 1) {
                return item
                    ->Child(TextEl(a, isMonth ? Tr(months[st.value])
                                              : StrDup(a, fmt("%d", st.value)))
                                ->Font(14)
                                ->Semibold()
                                ->Fg(th.foreground));
            }
            item->Radius(th.radius);
            if (st.active) {
                item->Bg(th.tokens.primary);
            }
            item->FocusId(HashClickId(isMonth ? StrL("cal-month-toggle")
                                              : StrL("cal-year-toggle")));
            return item
                ->Child(TextEl(a, isMonth ? Tr(months[st.value])
                                          : StrDup(a, fmt("%d", st.value)))
                            ->Font(14)
                            ->Semibold()
                            ->Fg(st.active ? th.primaryFg : th.foreground));
        }
        case CalendarItemKind::Weekday:
            return item->Child(
                TextEl(a, Tr(weekdays[st.value]))->Font(12)->Fg(th.mutedFg));
        case CalendarItemKind::Day: {
            item->Radius(self->size == UiSize::Small   ? th.radius * 0.5f
                         : self->size == UiSize::Large ? th.radius * 2.f
                                                       : th.radius);
            Rgba fg = st.muted ? th.mutedFg : th.foreground;
            if (st.disabled) {
                // calendar.rs fades the whole cell rather than the ink, so a
                // day that is both picked and blocked shows its primary square
                // at half strength.
                item->Opacity(0.5f);
            }
            if (st.active) {
                item->Bg(th.tokens.primary);
                fg = th.primaryFg;
            } else if (st.inRange || st.today) {
                item->Bg(th.tokens.accent);
                fg = th.foreground;
            } else if (!st.disabled) {
                item->HoverBg(th.secondaryHover);
            }
            return item->Child(
                TextEl(a, StrDup(a, fmt("%d", st.value)))->Font(14)->Fg(fg));
        }
        case CalendarItemKind::Month:
            item->Radius(th.radius)->HoverBg(th.secondaryHover);
            if (st.active) {
                item->Bg(th.tokens.primary);
            }
            // `uses_compact_text`: a month option and nothing else, because
            // "September" is what overflows.
            return item
                ->Child(TextEl(a, Tr(months[st.value]))
                            ->Font(12)
                            ->Fg(st.active ? th.primaryFg : th.foreground));
        case CalendarItemKind::Year:
            item->Radius(th.radius)->HoverBg(th.secondaryHover);
            if (st.active) {
                item->Bg(th.tokens.primary);
            }
            return item
                ->Child(TextEl(a, StrDup(a, fmt("%d", st.value)))
                            ->Font(14)
                            ->Fg(st.active ? th.primaryFg : th.foreground));
    }
    (void)cellSize;
    return item;
}

El* Calendar::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // date_picker.rs sizes the calendar in its popup itself — 196 / 224 / 280
    // a month — because that one is built `border_0().rounded_none().p_0()`
    // and so has none of the padding the panel width above counts in. The
    // three pairs differ by exactly the 12 either side.
    float width =
        (CalendarWidth(size) - (bare ? 24.f : 0.f)) * (float)numberOfMonths;
    // The source-shaped path delegates all structure and behavior to Base's
    // retained calendar. The legacy controlled path below remains for
    // callers written against the first C++ surface.
    if (state.IsValid()) {
        gpui::Calendar* calendar =
            gpui::Calendar::New(cx, StrL("calendar"), state)
                ->NumberOfMonths(numberOfMonths)
                ->FirstDayOfWeek(firstDayOfWeek)
                // The themed item writes the label. Suppress Base's fallback
                // text so the facade does not produce two children per slot.
                ->Label(nullptr)
                ->Item(&ThemedCalendarItem, this);
        CalendarState* retained = state.Get(cx);
        if (retained) {
            retained->numberOfMonths = numberOfMonths;
        }
        El* root = calendar->IntoEl()->W(width);
        if (!bare) {
            root->Pad(12)->Border(1, th.border)->Radius(th.radiusLg);
        }
        StyleApplyFields(&root->style, style, styleSet);
        return root;
    }

    // The calendar itself is the base one; what is set here is what it is
    // looking at, and the item function that gives it a look.
    CalendarOpts o;
    o.year = year;
    o.month = month;
    o.numberOfMonths = numberOfMonths;
    o.view = view;
    o.cellSize = CalendarCellSize(size);
    o.selected = {selectedYear ? selectedYear : year,
                  selectedMonth ? selectedMonth : month, day};
    o.rangeEnd = rangeEnd;
    o.today = DateToday();
    o.disabledMatcher = disabledMatcher;
    o.firstDayOfWeek = firstDayOfWeek;
    o.yearMin = yearMin;
    o.yearMax = yearMax;
    o.yearPageStart = yearPageStart;
    o.onDay = onDay;
    o.onDate = onDate;
    o.onPrev = onPrev;
    o.onNext = onNext;
    o.onMonthToggle = onMonthToggle;
    o.onYearToggle = onYearToggle;
    o.onMonth = onMonth;
    o.onYear = onYear;
    o.item = &ThemedCalendarItem;
    o.user = this;
    El* root = gpui::Calendar::New(cx, StrL("calendar"), o)->W(width);
    if (!bare) {
        root->Pad(12)->Border(1, th.border)->Radius(th.radiusLg);
    }
    StyleApplyFields(&root->style, style, styleSet);
    return root;
}

static bool UiDateValid(LocalDate date) {
    return date.year != 0 && date.month != 0 && date.day != 0;
}

DateRangePresetValue DateRangePresetValue::Single(LocalDate date) {
    DateRangePresetValue out;
    out.kind = DateRangePresetValueKind::Single;
    out.start = date;
    return out;
}

DateRangePresetValue DateRangePresetValue::Range(LocalDate start,
                                                 LocalDate end) {
    DateRangePresetValue out;
    out.kind = DateRangePresetValueKind::Range;
    out.start = start;
    out.end = end;
    return out;
}

Date DateRangePresetValue::IntoDate() const {
    return kind == DateRangePresetValueKind::Range ? Date::Range(start, end)
                                                   : Date::Single(start);
}

DateRangePreset DateRangePreset::Single(Str label, LocalDate date,
                                        intptr_t arg) {
    DateRangePreset out;
    out.label = label;
    out.value = DateRangePresetValue::Single(date);
    out.start = date;
    out.arg = arg;
    return out;
}

DateRangePreset DateRangePreset::Range(Str label, LocalDate start,
                                       LocalDate end, intptr_t arg) {
    DateRangePreset out;
    out.label = label;
    out.value = DateRangePresetValue::Range(start, end);
    out.start = start;
    out.end = end;
    out.arg = arg;
    return out;
}

static Date PresetDate(const DateRangePreset& preset) {
    // Aggregates written against the old fields leave `value` empty. Prefer
    // them only in that compatibility case; the source-shaped constructors
    // always fill both views.
    if (!UiDateValid(preset.value.start) && UiDateValid(preset.start)) {
        return UiDateValid(preset.end) ? Date::Range(preset.start, preset.end)
                                       : Date::Single(preset.start);
    }
    return preset.value.IntoDate();
}

static void AppendDateNumber(StrBuilder* out, int value, int digits) {
    if (digits == 2) {
        out->Append(fmt("%02d", value));
    } else if (digits == 3) {
        out->Append(fmt("%03d", value));
    } else if (digits == 4) {
        out->Append(fmt("%04d", value));
    } else {
        out->Append(fmt("%d", value));
    }
}

static void AppendDateNumeric(StrBuilder* out, int value, int digits,
                              char defaultPad, char modifier) {
    char pad = modifier == '-'   ? 0
               : modifier == '_' ? ' '
               : modifier == '0' ? '0'
                                 : defaultPad;
    if (!pad || digits <= 1) {
        AppendDateNumber(out, value, 1);
        return;
    }
    // fmt() takes a literal width, while this date pattern supplies it at
    // runtime, so build the concrete format before rendering the value.
    // Keep output padding.
    TempStr format = pad == '0' ? fmt("%%0%dd", digits) : fmt("%%%dd", digits);
    out->Append(fmt(format.s, value));
}

static int DateYearDay(LocalDate date) {
    int day = date.day;
    for (int month = 1; month < date.month; month++) {
        day += CalendarDaysInMonth(date.year, month);
    }
    return day;
}

static void DateIsoWeek(LocalDate date, int* year, int* week) {
    int weekday = CalendarWeekday(date.year, date.month, date.day);
    int isoWeekday = weekday ? weekday : 7;
    LocalDate thursday = DateAddDays(date, 4 - isoWeekday);
    *year = thursday.year;
    *week = (DateYearDay(thursday) - 1) / 7 + 1;
}

Str DatePickerFormatDate(Arena* a, Str pattern, LocalDate date) {
    if (!a || !UiDateValid(date)) {
        return {};
    }
    static const char* shortMonths[] = {
        "",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    static const char* longMonths[] = {
        "",        "January",  "February", "March",  "April",
        "May",     "June",     "July",     "August", "September",
        "October", "November", "December",
    };
    static const char* shortDays[] = {"Sun", "Mon", "Tue", "Wed",
                                      "Thu", "Fri", "Sat"};
    static const char* longDays[] = {"Sunday",    "Monday",   "Tuesday",
                                     "Wednesday", "Thursday", "Friday",
                                     "Saturday"};
    StrBuilder out(a);
    int weekday = CalendarWeekday(date.year, date.month, date.day);
    int yearDay = DateYearDay(date);
    int isoYear = 0, isoWeek = 0;
    DateIsoWeek(date, &isoYear, &isoWeek);
    for (int i = 0; i < len(pattern); i++) {
        char ch = pattern.s[i];
        if (ch != '%' || i + 1 >= len(pattern)) {
            out.AppendChar(ch);
            continue;
        }
        char directive = pattern.s[++i];
        char modifier = 0;
        if ((directive == '-' || directive == '_' || directive == '0') &&
            i + 1 < len(pattern)) {
            modifier = directive;
            directive = pattern.s[++i];
        }
        switch (directive) {
            case '%':
                out.AppendChar('%');
                break;
            case 'Y':
                AppendDateNumeric(&out, date.year, 4, '0', modifier);
                break;
            case 'y':
                AppendDateNumeric(&out, date.year % 100, 2, '0', modifier);
                break;
            case 'C':
                AppendDateNumeric(&out, date.year / 100, 2, '0', modifier);
                break;
            case 'q':
                AppendDateNumeric(&out, (date.month - 1) / 3 + 1, 1, 0,
                                  modifier);
                break;
            case 'm':
                AppendDateNumeric(&out, date.month, 2, '0', modifier);
                break;
            case 'b':
            case 'h':
                out.Append(Str(shortMonths[date.month]));
                break;
            case 'B':
                out.Append(Str(longMonths[date.month]));
                break;
            case 'd':
                AppendDateNumeric(&out, date.day, 2, '0', modifier);
                break;
            case 'e':
                AppendDateNumeric(&out, date.day, 2, ' ', modifier);
                break;
            case 'j':
                AppendDateNumeric(&out, yearDay, 3, '0', modifier);
                break;
            case 'a':
                out.Append(Str(shortDays[weekday]));
                break;
            case 'A':
                out.Append(Str(longDays[weekday]));
                break;
            case 'w':
                AppendDateNumeric(&out, weekday, 1, 0, modifier);
                break;
            case 'u':
                AppendDateNumeric(&out, weekday ? weekday : 7, 1, 0, modifier);
                break;
            case 'U':
                AppendDateNumeric(&out, (yearDay - 1 + 7 - weekday) / 7, 2, '0',
                                  modifier);
                break;
            case 'W': {
                int mondayWeekday = (weekday + 6) % 7;
                AppendDateNumeric(&out, (yearDay - 1 + 7 - mondayWeekday) / 7,
                                  2, '0', modifier);
                break;
            }
            case 'G':
                AppendDateNumeric(&out, isoYear, 4, '0', modifier);
                break;
            case 'g':
                AppendDateNumeric(&out, isoYear % 100, 2, '0', modifier);
                break;
            case 'V':
                AppendDateNumeric(&out, isoWeek, 2, '0', modifier);
                break;
            case 'F':
                out.Append(
                    fmt("%04d-%02d-%02d", date.year, date.month, date.day));
                break;
            case 'D':
            case 'x':
                out.Append(fmt("%02d/%02d/%02d", date.month, date.day,
                               date.year % 100));
                break;
            case 'v':
                AppendDateNumeric(&out, date.day, 2, ' ', modifier);
                out.AppendChar('-');
                out.Append(Str(shortMonths[date.month]));
                out.AppendChar('-');
                AppendDateNumeric(&out, date.year, 4, '0', modifier);
                break;
            case 't':
                out.AppendChar('\t');
                break;
            case 'n':
                out.AppendChar('\n');
                break;
            default:
                // Keep an unsupported chrono directive visible and stable.
                out.AppendChar('%');
                out.AppendChar(directive);
                break;
        }
    }
    return out.TakeStr();
}

Str DatePickerFormatValue(Arena* a, Str pattern, Date date) {
    if (!date.IsComplete()) {
        return {};
    }
    Str start = DatePickerFormatDate(a, pattern, date.start);
    if (date.kind == DateKind::Single) {
        return start;
    }
    Str end = DatePickerFormatDate(a, pattern, date.end);
    return StrDup(a, fmt("%s - %s", start, end));
}

DatePickerState::~DatePickerState() {
    StrFree(dateFormat);
}

static void NotifyDatePicker(DatePickerState* state, Ctx* cx) {
    if (state && cx && state->self.IsValid()) {
        NotifyEntity(cx->app, state->self.id, cx->win);
    }
}

Entity<DatePickerState> DatePickerStateNew(Ctx* cx, bool range) {
    if (!cx || !cx->app) {
        return {};
    }
    Entity<DatePickerState> out = EntityNewState<DatePickerState>(cx->app);
    DatePickerState* state = out.Get(cx);
    if (!state) {
        return {};
    }
    state->self = out;
    state->focus = FocusHandleNew(cx);
    state->date = range ? Date::Range() : Date::Single();
    state->dateFormat = StrDup(StrL("%Y/%m/%d"));
    state->calendar = CalendarStateNew(cx, state->date);
    state->calendarSubscription = SubscribeTo(cx->app, state->calendar, out,
                                              &DatePickerState::OnCalendar);
    return out;
}

void DatePickerStateSetDate(DatePickerState* state, Date date, Ctx* cx,
                            bool emit) {
    if (!state) {
        return;
    }
    state->date = date;
    if (cx) {
        if (CalendarState* calendar = state->calendar.Get(cx)) {
            CalendarStateSetDate(calendar, date, nullptr, false);
        }
    }
    state->open = false;
    if (emit && cx && state->self.IsValid()) {
        DatePickerEvent event = {DatePickerEventKind::Change, date};
        EntityEmit(cx->app, cx->win, state->self, &event);
    }
    NotifyDatePicker(state, cx);
}

void DatePickerStateSetDateFormat(DatePickerState* state, Str format, Ctx* cx) {
    if (!state) {
        return;
    }
    Str copy = StrDup(format.s ? format : StrL("%Y/%m/%d"));
    StrFree(state->dateFormat);
    state->dateFormat = copy;
    NotifyDatePicker(state, cx);
}

void DatePickerStateSetNumberOfMonths(DatePickerState* state, int count,
                                      Ctx* cx) {
    if (!state) {
        return;
    }
    state->numberOfMonths = std::max(1, count);
    if (cx) {
        if (CalendarState* calendar = state->calendar.Get(cx)) {
            calendar->numberOfMonths = state->numberOfMonths;
        }
    }
    NotifyDatePicker(state, cx);
}

void DatePickerStateSetFirstDayOfWeek(DatePickerState* state, int weekday,
                                      Ctx* cx) {
    if (!state) {
        return;
    }
    state->firstDayOfWeek = ((weekday % 7) + 7) % 7;
    NotifyDatePicker(state, cx);
}

void DatePickerStateSetDisabledMatcher(DatePickerState* state, Matcher matcher,
                                       Ctx* cx) {
    if (!state) {
        return;
    }
    state->disabledMatcher = matcher;
    if (cx) {
        if (CalendarState* calendar = state->calendar.Get(cx)) {
            CalendarStateSetDisabledMatcher(calendar, matcher);
        }
    }
    NotifyDatePicker(state, cx);
}

void DatePickerStateSetYearRange(DatePickerState* state, int minYear,
                                 int maxYear, Ctx* cx) {
    if (!state) {
        return;
    }
    if (cx) {
        if (CalendarState* calendar = state->calendar.Get(cx)) {
            CalendarStateSetYearRange(calendar, minYear, maxYear);
        }
    }
    NotifyDatePicker(state, cx);
}

void DatePickerStateSelectPreset(DatePickerState* state,
                                 const DateRangePreset& preset, Ctx* cx,
                                 bool emit) {
    DatePickerStateSetDate(state, PresetDate(preset), cx, emit);
}

void DatePickerState::OnCalendar(DatePickerState* self, Ctx* cx,
                                 const CalendarEvent* ev) {
    if (!self || !ev || ev->kind != CalendarEventKind::Selected) {
        return;
    }
    // CalendarState already owns this value; update the facade, close, emit,
    // and return focus to the input exactly once.
    self->date = ev->date;
    self->open = false;
    DatePickerEvent event = {DatePickerEventKind::Change, ev->date};
    EntityEmit(cx->app, cx->win, self->self, &event);
    FocusHandleFocus(cx->win, self->focus);
    Notify(cx);
}

void DatePickerState::OnToggle(DatePickerState* self, Ctx* cx,
                               const ClickEvent*) {
    self->open = !self->open;
    Notify(cx);
}

void DatePickerState::OnOpenChange(DatePickerState* self, Ctx* cx,
                                   const ClickEvent*, intptr_t open) {
    if (!open && self->open &&
        FocusHandleContainsFocused(cx->win, self->focus)) {
        FocusHandleFocus(cx->win, self->focus);
    }
    self->open = open != 0;
    Notify(cx);
}

void DatePickerState::OnDismiss(DatePickerState* self, Ctx* cx,
                                const MouseUpEvent*) {
    if (!self->open) {
        return;
    }
    if (FocusHandleContainsFocused(cx->win, self->focus)) {
        FocusHandleFocus(cx->win, self->focus);
    }
    self->open = false;
    Notify(cx);
}

void DatePickerState::OnClear(DatePickerState* self, Ctx* cx,
                              const ClickEvent*) {
    WindowStopPropagation(cx);
    Date empty =
        self->date.kind == DateKind::Range ? Date::Range() : Date::Single();
    DatePickerStateSetDate(self, empty, cx, true);
}

DatePicker* DatePicker::New(Ctx* cx) {
    Arena* a = cx->a;
    DatePicker* d = ArenaNew<DatePicker>(a);
    d->a = a;
    d->cx = cx;
    d->id = StrL("date-picker");
    return d;
}
DatePicker* DatePicker::New(Ctx* cx, Entity<DatePickerState> state) {
    DatePicker* d = New(cx);
    d->state = state;
    d->id =
        StrDup(cx->a, fmt("date-picker-%d-%u", state.id.index, state.id.gen));
    if (DatePickerState* retained = state.Get(cx)) {
        d->numberOfMonths = retained->numberOfMonths;
    }
    return d;
}
DatePicker* DatePicker::Id(Str value) {
    id = value;
    return this;
}
DatePicker* DatePicker::Year(int y) {
    year = y;
    return this;
}
DatePicker* DatePicker::Month(int m) {
    month = m;
    return this;
}
DatePicker* DatePicker::Day(int d) {
    day = d;
    return this;
}
DatePicker* DatePicker::View(int y, int m) {
    viewYear = y;
    viewMonth = m;
    return this;
}
DatePicker* DatePicker::RangeEnd(int y, int m, int d) {
    year2 = y;
    month2 = m;
    day2 = d;
    return this;
}
DatePicker* DatePicker::Format(DateFormat f) {
    format = f;
    return this;
}
DatePicker* DatePicker::WithSize(UiSize s) {
    size = s;
    return this;
}
DatePicker* DatePicker::W(float v) {
    width = v;
    return this;
}
DatePicker* DatePicker::Cleanable(bool v) {
    cleanable = v;
    return this;
}
DatePicker* DatePicker::Appearance(bool v) {
    appearance = v;
    return this;
}
DatePicker* DatePicker::FocusRing(bool v) {
    focusRing = v;
    return this;
}
DatePicker* DatePicker::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
DatePicker* DatePicker::Disabled(bool v) {
    disabled = v;
    return this;
}
DatePicker* DatePicker::Range(bool v) {
    range = v;
    return this;
}
DatePicker* DatePicker::NumberOfMonths(int count) {
    numberOfMonths = std::max(1, count);
    return this;
}
DatePicker* DatePicker::CalendarMode(CalendarView value) {
    calendarView = value;
    return this;
}
DatePicker* DatePicker::YearRange(int minYear, int maxYear, int pageStart) {
    yearMin = minYear;
    yearMax = maxYear;
    yearPageStart = pageStart;
    return this;
}
DatePicker* DatePicker::DisabledMatcher(DateMatcher matcher) {
    disabledMatcher = matcher;
    return this;
}
DatePicker* DatePicker::Presets(const DateRangePreset* values, int count,
                                Listener onSelect) {
    presets = values;
    presetsCount = count;
    onPreset = onSelect;
    return this;
}
DatePicker* DatePicker::OnClear(Listener fn) {
    onClear = fn;
    return this;
}
DatePicker* DatePicker::Placeholder(Str s) {
    placeholder = s;
    return this;
}
DatePicker* DatePicker::Open(bool v) {
    open = v;
    return this;
}
DatePicker* DatePicker::OnToggle(Listener fn) {
    onToggle = fn;
    return this;
}
DatePicker* DatePicker::OnDay(Listener fn) {
    onDay = fn;
    return this;
}
DatePicker* DatePicker::OnDate(Listener fn) {
    onDate = fn;
    return this;
}
DatePicker* DatePicker::OnPrev(Listener fn) {
    onPrev = fn;
    return this;
}
DatePicker* DatePicker::OnNext(Listener fn) {
    onNext = fn;
    return this;
}
DatePicker* DatePicker::OnMonthToggle(Listener fn) {
    onMonthToggle = fn;
    return this;
}
DatePicker* DatePicker::OnYearToggle(Listener fn) {
    onYearToggle = fn;
    return this;
}
DatePicker* DatePicker::OnMonth(Listener fn) {
    onMonth = fn;
    return this;
}
DatePicker* DatePicker::OnYear(Listener fn) {
    onYear = fn;
    return this;
}

static Str FormatDate(Arena* a, DateFormat f, int y, int m, int d) {
    const char* sep = f == DateFormat::Dash ? "-" : "/";
    return StrDup(a, fmt("%d%s%02d%s%02d", y, Str(sep), m, Str(sep), d));
}

struct DatePresetAction {
    Entity<DatePickerState> picker = {};
    Date value = {};

    static void OnClick(DatePresetAction* self, Ctx* cx, const ClickEvent*) {
        if (!self) {
            return;
        }
        if (DatePickerState* picker = self->picker.Get(cx)) {
            DatePickerStateSetDate(picker, self->value, cx, true);
        }
    }
};

static El* RetainedDatePickerIntoEl(DatePicker* self) {
    Ctx* cx = self->cx;
    Arena* a = self->a;
    DatePickerState* state = self->state.Get(cx);
    if (!state) {
        return Div(a)->Id(self->id);
    }
    const Theme& th = ThemeNow(cx->app);
    if (CalendarState* calendarState = state->calendar.Get(cx)) {
        // Rust synchronizes this Option<Rc<Matcher>> at render time. This is
        // a POD value here, so assignment is the entire shared update.
        calendarState->disabledMatcher = state->disabledMatcher;
    }

    bool hasDate = state->date.IsSome();
    bool complete = state->date.IsComplete();
    Str title = DatePickerFormatValue(a, state->dateFormat, state->date);
    if (!title.s) {
        title = self->placeholder.s ? self->placeholder
                                    : Tr("DatePicker.placeholder");
    }
    Listener toggle = ListenTo(self->state, &DatePickerState::OnToggle);
    Listener setOpen = ListenTo(self->state, &DatePickerState::OnOpenChange);
    Listener clear = ListenTo(self->state, &DatePickerState::OnClear);

    float height = 32, padX = 10, font = 14;
    if (self->size == UiSize::Large) {
        height = 44;
        padX = 12;
        font = 16;
    } else if (self->size == UiSize::Small) {
        height = 24;
        padX = 8;
    } else if (self->size == UiSize::XSmall) {
        height = 20;
        padX = 4;
        font = 12;
    }

    bool focused = FocusHandleContainsFocused(cx->win, state->focus);
    El* trigger = Div(a)
                      ->FlexRow()
                      ->W(kFill)
                      ->H(height)
                      ->PadX(padX)
                      ->Gap(4)
                      ->ItemsCenter()
                      ->JustifyBetween();
    if (self->appearance) {
        trigger->Radius(th.radius)
            ->Bg(th.inputBg)
            ->Border(1, focused ? th.ring : th.inputBorder);
        if (self->disabled) {
            trigger->Opacity(0.5f);
        }
    }
    if (focused && self->appearance && !self->disabled) {
        trigger->FocusRing(self->focusRing);
    } else {
        trigger->FocusRing(false);
    }

    El* text = TextEl(a, title)
                   ->Font(font)
                   ->Fg(complete ? th.foreground : th.mutedFg)
                   ->Flex1()
                   ->MinW(0)
                   ->Truncate();
    El* triggerRow = Div(a)
                         ->FlexRow()
                         ->W(kFill)
                         ->MinW(0)
                         ->Gap(4)
                         ->ItemsCenter()
                         ->JustifyBetween()
                         ->Child(text);
    if (!self->disabled) {
        if (self->cleanable && hasDate) {
            triggerRow->Child(Button::New(cx, StrL("clean"))
                                  ->Text()
                                  ->WithSize(UiSize::XSmall)
                                  ->Icon(IconName::X)
                                  ->OnClick(clear)
                                  ->IntoEl()
                                  ->StopClick());
        } else {
            triggerRow
                ->Child(IconEl(a, IconName::Calendar, 12)->Fg(th.mutedFg));
        }
    }
    trigger->Child(triggerRow);
    if (!state->open && !self->disabled) {
        BindClick(trigger, StrL("date-picker-input"), toggle);
    } else {
        trigger->Id(StrL("date-picker-input"));
    }
    // BindClick names its path-derived default focus. The retained handle is
    // the explicit source value, so apply it after the id just as
    // BaseDatePicker::new(id, &focus_handle) does.
    trigger->TrackFocus(state->focus);

    El* popup = nullptr;
    if (state->open) {
        El* content = Div(a)->FlexRow()->Gap(12)->ItemsStart();
        if (self->presets && self->presetsCount > 0) {
            El* list = Div(a)->FlexCol()->Gap(8)->PadY(4)->JustifyEnd();
            for (int i = 0; i < self->presetsCount; i++) {
                const DateRangePreset& preset = self->presets[i];
                uint32_t key =
                    (uint32_t)(self->state.id.index + 1) * 2654435761u;
                key ^= self->state.id.gen * 2246822519u;
                key ^= (uint32_t)(i + 1) * 3266489917u;
                Entity<DatePresetAction> action = KeyedEntity<DatePresetAction>(
                    cx, KeyedKey(key, HashClickId(StrL("DatePresetAction"))));
                if (DatePresetAction* astate = action.Get(cx)) {
                    astate->picker = self->state;
                    astate->value = PresetDate(preset);
                }
                list->Child(
                    Button::New(cx, StrDup(a, fmt("date-preset-%d", i)))
                        ->WithSize(UiSize::Small)
                        ->Ghost()
                        ->TabStop(false)
                        ->Label(preset.label)
                        ->OnClick(ListenTo(action, &DatePresetAction::OnClick))
                        ->IntoEl());
            }
            content->Child(list);
        }
        Calendar* calendar = Calendar::New(cx, state->calendar)
                                 ->WithSize(self->size)
                                 ->NumberOfMonths(self->numberOfMonths)
                                 ->FirstDayOfWeek(state->firstDayOfWeek)
                                 ->Bare();
        content->Child(calendar->IntoEl());
        popup = Div(a)
                    ->Pad(12)
                    ->Border(1, th.border)
                    ->Radius(std::min(th.radius * 2.f, 8.f))
                    ->Bg(th.tokens.background)
                    ->Fg(th.foreground)
                    ->OnMouseUpOut(
                        ListenTo(self->state, &DatePickerState::OnDismiss))
                    ->Child(content);
    }

    El* root = gpui::DatePicker::New(cx, self->id, self->disabled, state->open,
                                     setOpen)
                   ->TrackFocus(state->focus)
                   ->TabStop(!self->disabled)
                   ->FlexNone()
                   ->W(self->width)
                   ->Child(Popup::New(cx, StrL("pop"), trigger)
                               ->Content(DropdownPlaceContent(popup))
                               ->IntoEl());
    StyleApplyFields(&root->style, self->style, self->styleSet);
    DatePickerBindKeys(cx, root, self->id, toggle, clear, state->open,
                       self->disabled);
    return root;
}

El* DatePicker::IntoEl() {
    if (state.IsValid()) {
        return RetainedDatePickerIntoEl(this);
    }
    const Theme& th = ThemeNow(cx->app);
    bool hasDate = day > 0;
    bool rangeMode = range || year2 > 0;
    bool complete = hasDate && (!rangeMode || day2 > 0);
    Str title;
    if (!complete) {
        title = placeholder.s ? placeholder : Tr("DatePicker.placeholder");
    } else if (rangeMode) {
        title =
            StrDup(a, fmt("%s - %s", FormatDate(a, format, year, month, day),
                          FormatDate(a, format, year2, month2, day2)));
    } else {
        title = FormatDate(a, format, year, month, day);
    }
    // The trigger is input-shaped: the date (or placeholder) with a calendar
    // icon, or the clear button when there is something to clear.
    float height = 32, padX = 10, font = 14;
    if (size == UiSize::Large) {
        height = 44;
        padX = 12;
        font = 16;
    } else if (size == UiSize::Small) {
        height = 24;
        padX = 8;
    } else if (size == UiSize::XSmall) {
        height = 20;
        padX = 4;
        font = 12;
    }
    El* trigger = Div(a)
                      ->FlexRow()
                      ->W(width)
                      ->H(height)
                      ->PadX(padX)
                      ->Gap(4)
                      ->ItemsCenter()
                      ->JustifyBetween();
    if (appearance) {
        trigger->Radius(th.radius)
            ->Bg(th.inputBg)
            ->Border(1, open ? th.ring : th.inputBorder);
    }
    if (disabled) {
        trigger->Opacity(0.5f);
    }
    trigger->Child(TextEl(a, title)->Font(font)->Fg(complete ? th.foreground
                                                             : th.mutedFg));
    if (cleanable && hasDate && !disabled) {
        trigger->Child(Button::New(cx, StrL("clean"))
                           ->Text()
                           ->WithSize(UiSize::XSmall)
                           ->Icon(IconName::X)
                           ->OnClick(onClear)
                           ->IntoEl()
                           ->StopClick());
    } else if (open) {
        trigger->Child(IconEl(a, IconName::Calendar, 12)->Fg(th.mutedFg));
    }
    if (!open && !disabled) {
        BindClick(trigger, StrL("input"), onToggle);
        trigger->FocusRing(focusRing);
    } else {
        // The trigger stops taking the press while the calendar is up — the
        // popup's own mouse-out is what closes it — but it keeps the focus
        // handle it was given when it was pressed. Rust's track_focus is
        // unconditional for the same reason: the picker's key context has to
        // stay over whatever has the focus, or escape belongs to nobody.
        trigger->PathFocus(StrL("input"))->FocusRing(false);
    }
    El* popup = nullptr;
    if (open) {
        Calendar* calendar = Calendar::New(cx)
                                 ->Year(viewYear ? viewYear : year)
                                 ->Month(viewMonth ? viewMonth : month)
                                 ->Selection(year, month, day)
                                 ->RangeEnd(year2, month2, day2)
                                 ->WithSize(size)
                                 ->NumberOfMonths(numberOfMonths)
                                 ->View(calendarView)
                                 ->YearRange(yearMin, yearMax, yearPageStart)
                                 ->DisabledMatcher(disabledMatcher)
                                 ->Bare()
                                 ->OnDay(onDay)
                                 ->OnDate(onDate)
                                 ->OnPrev(onPrev)
                                 ->OnNext(onNext)
                                 ->OnMonthToggle(onMonthToggle)
                                 ->OnYearToggle(onYearToggle)
                                 ->OnMonth(onMonth)
                                 ->OnYear(onYear);
        El* content = Div(a)->FlexRow()->Gap(12)->ItemsStart();
        if (presets && presetsCount > 0) {
            El* list = Div(a)->FlexCol()->Gap(8)->PadY(4)->JustifyEnd();
            for (int i = 0; i < presetsCount; i++) {
                const DateRangePreset& preset = presets[i];
                list->Child(component::Button::New(
                                cx, StrDup(a, fmt("date-preset-%d", i)))
                                ->WithSize(UiSize::Small)
                                ->Ghost()
                                ->Label(preset.label)
                                ->OnClick(ListenerArg(onPreset, preset.arg))
                                ->IntoEl());
            }
            content->Child(list);
        }
        content->Child(calendar->IntoEl());
        popup = Div(a)
                    ->Pad(12)
                    ->Border(1, th.border)
                    ->Radius(std::min(th.radius * 2.f, 8.f))
                    ->Bg(th.tokens.background)
                    ->Fg(th.foreground)
                    ->OnMouseUpOut(onToggle)
                    ->Child(content);
    }
    // Both halves of the picker take focus — the outer element and the
    // trigger inside it — so the opt-out has to reach both.
    El* root = gpui::DatePicker::New(cx, id, disabled, open, onToggle)
                   ->FocusRing(focusRing)
                   ->W(width)
                   ->Child(Popup::New(cx, StrL("pop"), trigger)
                               ->Content(DropdownPlaceContent(popup))
                               ->IntoEl());
    StyleApplyFields(&root->style, style, styleSet);
    // date_picker.rs::init binds enter, escape and the two delete keys in the
    // picker's context; the toggle and the clear the caller gave are what
    // they run, which is what Rust's on_action handlers reach for too.
    DatePickerBindKeys(cx, root, id, onToggle, onClear, open, disabled);
    return root;
}

} // namespace component
} // namespace gpui
