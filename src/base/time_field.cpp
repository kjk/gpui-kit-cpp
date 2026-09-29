#include "base/time_field.h"
#include "base/actions.h"
#include "gpui/keymap.h"

namespace gpui {

Str TimePrecisionFormat(TimePrecision precision, HourCycle hourCycle) {
    if (hourCycle == HourCycle::H23) {
        return precision == TimePrecision::Minute ? StrL("%H:%M")
                                                  : StrL("%H:%M:%S");
    }
    return precision == TimePrecision::Minute ? StrL("%I:%M %p")
                                              : StrL("%I:%M:%S %p");
}

LocalTime TimePrecisionTruncate(TimePrecision precision, LocalTime time) {
    if (precision == TimePrecision::Minute) {
        time.second = 0;
    }
    return time;
}

Str TimeFormat(Arena* a, Str pattern, LocalTime time) {
    if (!a) {
        return {};
    }
    int hour12 = (time.hour + 11) % 12 + 1;
    const char* period = time.hour < 12 ? "AM" : "PM";
    StrBuilder out(a);
    for (int i = 0; i < len(pattern); i++) {
        char ch = pattern.s[i];
        if (ch != '%' || i + 1 >= len(pattern)) {
            out.AppendChar(ch);
            continue;
        }
        char directive = pattern.s[++i];
        switch (directive) {
            case '%':
                out.AppendChar('%');
                break;
            case 'H':
                out.Append(fmt("%02d", time.hour));
                break;
            case 'k':
                out.Append(fmt("%2d", time.hour));
                break;
            case 'I':
                out.Append(fmt("%02d", hour12));
                break;
            case 'l':
                out.Append(fmt("%2d", hour12));
                break;
            case 'M':
                out.Append(fmt("%02d", time.minute));
                break;
            case 'S':
                out.Append(fmt("%02d", time.second));
                break;
            case 'p':
                out.Append(Str(period));
                break;
            case 'P':
                out.Append(time.hour < 12 ? StrL("am") : StrL("pm"));
                break;
            case 'R':
                out.Append(fmt("%02d:%02d", time.hour, time.minute));
                break;
            case 'T':
            case 'X':
                out.Append(
                    fmt("%02d:%02d:%02d", time.hour, time.minute, time.second));
                break;
            default:
                out.AppendChar('%');
                out.AppendChar(directive);
                break;
        }
    }
    return out.TakeStr();
}

// ─── SegmentEditor ────────────────────────────────────────────────────────

int SegmentEditor::Segments(TimeSegment out[4]) const {
    int n = 0;
    out[n++] = TimeSegment::Hour;
    out[n++] = TimeSegment::Minute;
    if (precision == TimePrecision::Second) {
        out[n++] = TimeSegment::Second;
    }
    if (hourCycle == HourCycle::H12) {
        out[n++] = TimeSegment::Period;
    }
    return n;
}

bool SegmentEditor::HasSegment(TimeSegment s) const {
    TimeSegment all[4];
    int n = Segments(all);
    for (int i = 0; i < n; i++) {
        if (all[i] == s) {
            return true;
        }
    }
    return false;
}

void SegmentEditor::Bounds(TimeSegment s, int* min, int* max) const {
    switch (s) {
        case TimeSegment::Hour:
            *min = hourCycle == HourCycle::H23 ? 0 : 1;
            *max = hourCycle == HourCycle::H23 ? 23 : 12;
            return;
        case TimeSegment::Minute:
        case TimeSegment::Second:
            *min = 0;
            *max = 59;
            return;
        case TimeSegment::Period:
            *min = 0;
            *max = 1;
            return;
    }
}

int SegmentEditor::Value(TimeSegment s) const {
    switch (s) {
        case TimeSegment::Hour:
            return hourCycle == HourCycle::H23 ? time.hour
                                               : (time.hour + 11) % 12 + 1;
        case TimeSegment::Minute:
            return time.minute;
        case TimeSegment::Second:
            return time.second;
        case TimeSegment::Period:
            return time.hour / 12;
    }
    return 0;
}

Str SegmentEditor::Label(Arena* a, TimeSegment s) const {
    if (s == TimeSegment::Period) {
        return Value(s) == 0 ? StrL("AM") : StrL("PM");
    }
    return StrDup(a, fmt("%02d", Value(s)));
}

LocalTime SegmentEditor::WithValue(TimeSegment s, int value) const {
    LocalTime out = time;
    int pmOffset = time.hour / 12 * 12;
    switch (s) {
        case TimeSegment::Hour:
            if (hourCycle == HourCycle::H23) {
                if (value >= 0 && value <= 23) {
                    out.hour = value;
                }
            } else {
                // 12 AM is midnight and 12 PM is noon.
                out.hour = value % 12 + pmOffset;
            }
            break;
        case TimeSegment::Minute:
            if (value >= 0 && value <= 59) {
                out.minute = value;
            }
            break;
        case TimeSegment::Second:
            if (value >= 0 && value <= 59) {
                out.second = value;
            }
            break;
        case TimeSegment::Period:
            if (value == 0 || value == 1) {
                out.hour = time.hour % 12 + value * 12;
            }
            break;
    }
    return out;
}

void SegmentEditor::SetPrecision(TimePrecision value) {
    precision = value;
    time = TimePrecisionTruncate(value, time);
    ResetSegment();
}

void SegmentEditor::SetHourCycle(HourCycle value) {
    hourCycle = value;
    ResetSegment();
}

void SegmentEditor::ResetSegment() {
    if (!HasSegment(segment)) {
        segment = TimeSegment::Hour;
    }
    pendingDigit = -1;
}

bool SegmentEditor::SetTime(LocalTime value) {
    value = TimePrecisionTruncate(precision, value);
    if (time == value) {
        return false;
    }
    time = value;
    pendingDigit = -1;
    return true;
}

bool SegmentEditor::SelectSegment(TimeSegment s) {
    if (!HasSegment(s)) {
        return false;
    }
    segment = s;
    pendingDigit = -1;
    return true;
}

bool SegmentEditor::MoveSegment(int offset) {
    TimeSegment all[4];
    int n = Segments(all);
    int ix = 0;
    for (int i = 0; i < n; i++) {
        if (all[i] == segment) {
            ix = i;
            break;
        }
    }
    int next = ix + offset;
    if (next < 0 || next >= n) {
        return false;
    }
    return SelectSegment(all[next]);
}

bool SegmentEditor::Step(int delta) {
    pendingDigit = -1;
    int min = 0, max = 0;
    Bounds(segment, &min, &max);
    int span = max - min + 1;
    int value = (Value(segment) - min + delta) % span;
    if (value < 0) {
        value += span;
    }
    return ReplaceSegment(value + min);
}

bool SegmentEditor::InputDigit(int digit) {
    if (segment == TimeSegment::Period) {
        return false;
    }
    int min = 0, max = 0;
    Bounds(segment, &min, &max);
    int value = 0;
    bool complete = false;
    int first = pendingDigit;
    pendingDigit = -1;
    if (first >= 0 && first * 10 + digit >= min && first * 10 + digit <= max) {
        value = first * 10 + digit;
        complete = true;
    } else {
        // The two digits cannot form a valid value, so the new digit starts
        // over.
        value = digit;
        complete = digit * 10 > max;
    }
    if (!complete) {
        pendingDigit = digit;
    }
    bool changed = ReplaceSegment(value);
    if (complete) {
        MoveSegment(1);
    }
    return changed;
}

bool SegmentEditor::InputPeriod(bool pm) {
    if (segment != TimeSegment::Period) {
        return false;
    }
    return ReplaceSegment(pm ? 1 : 0);
}

bool SegmentEditor::ClearSegment() {
    pendingDigit = -1;
    return ReplaceSegment(0);
}

bool SegmentEditor::ReplaceSegment(int value) {
    LocalTime next = WithValue(segment, value);
    bool changed = next != time;
    time = next;
    return changed;
}

// ─── TimeFieldState ───────────────────────────────────────────────────────

static const char* kTimeFieldContext = "TimeField";

Str TimeFieldContext() {
    return StrL("TimeField");
}

uint32_t TimeFieldIncrement() {
    static uint32_t id = 0;
    if (!id) {
        id = ActionOf(StrL("number_input::Increment"));
    }
    return id;
}

uint32_t TimeFieldDecrement() {
    static uint32_t id = 0;
    if (!id) {
        id = ActionOf(StrL("number_input::Decrement"));
    }
    return id;
}

static uint32_t TimeFieldDelete() {
    static uint32_t id = 0;
    if (!id) {
        id = ActionOf(StrL("input::Delete"));
    }
    return id;
}

void TimeFieldInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    const char* ctx = kTimeFieldContext;
    KeyBinding bindings[] = {
        {"up", TimeFieldIncrement(), ctx},
        {"down", TimeFieldDecrement(), ctx},
        {"left", action::SelectLeft(), ctx},
        {"right", action::SelectRight(), ctx},
        {"tab", action::SelectNextColumn(), ctx},
        {"shift-tab", action::SelectPrevColumn(), ctx},
        {"backspace", TimeFieldDelete(), ctx},
        {"delete", TimeFieldDelete(), ctx},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

static void TimeFieldNotify(TimeFieldState* s, Ctx* cx) {
    if (s && cx && cx->app && s->self.IsValid()) {
        NotifyEntity(cx->app, s->self.id, cx->win);
    }
}

Entity<TimeFieldState> TimeFieldStateNew(Ctx* cx, TimePrecision precision,
                                         HourCycle hourCycle) {
    if (!cx || !cx->app) {
        return {};
    }
    Entity<TimeFieldState> out = EntityNewState<TimeFieldState>(cx->app);
    TimeFieldState* s = out.Get(cx);
    if (!s) {
        return {};
    }
    s->self = out;
    s->focus = FocusHandleNew(cx);
    s->editor.SetPrecision(precision);
    s->editor.SetHourCycle(hourCycle);
    return out;
}

void TimeFieldStateSetPrecision(TimeFieldState* s, TimePrecision precision,
                                Ctx* cx) {
    if (s && s->editor.precision != precision) {
        s->editor.SetPrecision(precision);
        TimeFieldNotify(s, cx);
    }
}

void TimeFieldStateSetHourCycle(TimeFieldState* s, HourCycle hourCycle,
                                Ctx* cx) {
    if (s && s->editor.hourCycle != hourCycle) {
        s->editor.SetHourCycle(hourCycle);
        TimeFieldNotify(s, cx);
    }
}

void TimeFieldStateSetTime(TimeFieldState* s, LocalTime time, Ctx* cx) {
    if (s && s->editor.SetTime(time)) {
        TimeFieldNotify(s, cx);
    }
}

void TimeFieldStateFocus(TimeFieldState* s, Window* win) {
    if (s && win) {
        FocusHandleFocus(win, s->focus);
    }
}

// TimeFieldState::edit: an edit that changed the time emits it; either way
// the field repaints.
static void TimeFieldEdited(TimeFieldState* s, Ctx* cx, bool changed) {
    if (changed && s->self.IsValid()) {
        TimeFieldEvent event = {TimeFieldEventKind::Change, s->editor.time};
        EntityEmit(cx->app, cx->win, s->self, &event);
    }
    Notify(cx);
}

void TimeFieldState::OnAction(TimeFieldState* self, Ctx* cx,
                              const ActionEvent* ev) {
    if (!self) {
        return;
    }
    uint32_t id = ev->action;
    if (id == TimeFieldIncrement()) {
        TimeFieldEdited(self, cx, self->editor.Step(1));
    } else if (id == TimeFieldDecrement()) {
        TimeFieldEdited(self, cx, self->editor.Step(-1));
    } else if (id == TimeFieldDelete()) {
        TimeFieldEdited(self, cx, self->editor.ClearSegment());
    } else if (id == action::SelectLeft()) {
        self->editor.MoveSegment(-1);
        Notify(cx);
    } else if (id == action::SelectRight()) {
        self->editor.MoveSegment(1);
        Notify(cx);
    } else if (id == action::SelectNextColumn() ||
               id == action::SelectPrevColumn()) {
        // Tab walks the segments first and leaves the field only from the
        // last one.
        int offset = id == action::SelectNextColumn() ? 1 : -1;
        if (self->editor.MoveSegment(offset)) {
            Notify(cx);
        } else {
            const_cast<ActionEvent*>(ev)->propagate = true;
        }
    } else {
        const_cast<ActionEvent*>(ev)->propagate = true;
    }
}

void TimeFieldState::OnKeyDown(TimeFieldState* self, Ctx* cx,
                               const KeyEvent* ev) {
    if (!self || !ev) {
        return;
    }
    // `modifiers.modified()`: shift counts too, so a shifted letter is not
    // typed into the period.
    if (ev->shift || ev->ctrl || ev->alt || ev->platform || ev->function) {
        return;
    }
    int vk = ev->vk;
    bool changed = false;
    if (vk >= '0' && vk <= '9') {
        changed = self->editor.InputDigit(vk - '0');
    } else if (vk >= 96 && vk <= 105) {
        // The keypad digits, which Rust's keystroke also names "0".."9".
        changed = self->editor.InputDigit(vk - 96);
    } else if (vk == 'A') {
        changed = self->editor.InputPeriod(false);
    } else if (vk == 'P') {
        changed = self->editor.InputPeriod(true);
    } else {
        return;
    }
    // window.prevent_default() and cx.stop_propagation().
    const_cast<KeyEvent*>(ev)->propagate = false;
    TimeFieldEdited(self, cx, changed);
}

void TimeFieldState::OnSegmentDown(TimeFieldState* self, Ctx* cx,
                                   const MouseDownEvent*, intptr_t segment) {
    if (!self) {
        return;
    }
    FocusHandleFocus(cx->win, self->focus);
    self->editor.SelectSegment((TimeSegment)segment);
    Notify(cx);
}

// ─── TimeField ────────────────────────────────────────────────────────────

void TimeFieldSegmentClearChildren(El* segment) {
    if (segment) {
        segment->first = nullptr;
        segment->last = nullptr;
    }
}

TimeField* TimeField::New(Ctx* cx, Str id, Entity<TimeFieldState> state) {
    TimeField* f = ArenaNew<TimeField>(cx->a);
    f->cx = cx;
    f->id = id;
    f->state = state;
    return f;
}

TimeField* TimeField::Disabled(bool v) {
    disabled = v;
    return this;
}

TimeField* TimeField::RenderSegment(TimeFieldSegmentRenderer fn, void* user) {
    segment = fn;
    segmentUser = user;
    return this;
}

TimeField* TimeField::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}

static Str TimeSegmentName(TimeSegment s) {
    switch (s) {
        case TimeSegment::Hour:
            return StrL("hour");
        case TimeSegment::Minute:
            return StrL("minute");
        case TimeSegment::Second:
            return StrL("second");
        case TimeSegment::Period:
            return StrL("period");
    }
    return {};
}

El* TimeField::IntoEl() {
    Arena* a = cx->a;
    TimeFieldState* st = state.Get(cx);
    if (!st) {
        return Div(a)->Id(id);
    }
    TimeFieldInitKeys();
    bool focused = FocusHandleIsFocused(cx->win, st->focus);
    const SegmentEditor& editor = st->editor;
    Str format = TimePrecisionFormat(editor.precision, editor.hourCycle);

    El* root = Div(a)
                   ->PathId(id)
                   ->Role(AccessibilityRole::TimeInput)
                   ->AriaValue(TimeFormat(a, format, editor.time))
                   ->TrackFocus(st->focus)
                   ->TabStop(!disabled);
    if (!disabled) {
        Listener onAction = ListenTo(state, &TimeFieldState::OnAction);
        root->KeyContext(TimeFieldContext())
            ->OnAction(TimeFieldIncrement(), onAction)
            ->OnAction(TimeFieldDecrement(), onAction)
            ->OnAction(TimeFieldDelete(), onAction)
            ->OnAction(action::SelectLeft(), onAction)
            ->OnAction(action::SelectRight(), onAction)
            ->OnAction(action::SelectNextColumn(), onAction)
            ->OnAction(action::SelectPrevColumn(), onAction)
            ->OnKeyDown(ListenTo(state, &TimeFieldState::OnKeyDown));
    }

    // The segments are named under the field, so "hour" in one field is not
    // "hour" in another.
    IdScope scope(cx, id);
    TimeSegment all[4];
    int n = editor.Segments(all);
    for (int ix = 0; ix < n; ix++) {
        TimeSegment s = all[ix];
        if (ix > 0 && s != TimeSegment::Period) {
            root->Child(Div(a)->Child(TextEl(a, StrL(":"))));
        }
        TimeFieldSegmentState segmentState;
        segmentState.segment = s;
        segmentState.value = editor.Value(s);
        segmentState.selected = focused && s == editor.segment;
        segmentState.disabled = disabled;
        El* item = Div(a)
                       ->PathClick(TimeSegmentName(s))
                       ->Child(TextEl(a, editor.Label(a, s)));
        if (!disabled) {
            item->OnMouseDown(
                ListenTo(state, &TimeFieldState::OnSegmentDown, (intptr_t)s));
        }
        if (segment) {
            item = segment(segmentUser, item, &segmentState, cx);
        }
        root->Child(item);
    }
    StyleApplyFields(&root->style, style, styleSet);
    return root;
}

} // namespace gpui
