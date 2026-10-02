#ifndef GPUI_BASE_DATE_PICKER_H_
#define GPUI_BASE_DATE_PICKER_H_
/* Unstyled date picker — crates/base/src/date_picker.rs */

#include "base/calendar.h"

namespace gpui {

// What a keystroke asks a date picker to do. Rust binds the same Confirm and
// Cancel actions a select does, but its handlers are not the select's: Enter
// opens a closed picker, and closes an open one once the value shown in the
// popup is the one the user wants.
enum class DatePickerAction : uint8_t {
    None,
    Open,
    Dismiss,
    // Delete or Backspace: `on_delete`, which is the clear button's handler
    // reached from the keyboard.
    Clear
};

// date_picker.rs::init: enter, escape and the two delete keys in the
// "DatePicker" key context.
void DatePickerInitKeys();
Str DatePickerContext();

// The three handlers, whole. Note that only the Confirm one checks `disabled`:
// neither Rust's Cancel nor its Delete does, so Escape still closes a disabled
// picker that somehow got opened rather than trapping it that way.
DatePickerAction DatePickerActionOf(uint32_t id, bool open, bool disabled);

// Where a picker's two handlers wait between frames, the way DialogKeys does:
// Rust's DatePicker is a view that owns its open flag and its date, and the
// port's is a builder whose caller owns both.
struct DatePickerKeys {
    // on_toggle, which the trigger's click carries: it opens a closed picker
    // and closes an open one, which is what Rust's Confirm and Cancel do one
    // way each.
    Listener onToggle = {};
    // on_delete, which is the clear button's handler reached from the
    // keyboard.
    Listener onClear = {};
    bool open = false;
    bool disabled = false;

    static void OnAction(DatePickerKeys* self, Ctx* cx, const ActionEvent* ev);
};

void DatePickerBindKeys(Ctx* cx, El* root, Str name, Listener onToggle,
                        Listener onClear, bool open, bool disabled);

int64_t DatePickerDateKey(LocalDate date);
LocalDate DatePickerDateFromKey(int64_t key);

enum class DateSelectionResult : uint8_t {
    Rejected,
    Partial,
    Complete
};

// CalendarState::select_date. A complete range restarts on the next click; an
// earlier second endpoint also restarts. Only a complete value is emitted by
// Rust, which is why Partial and Complete are distinct here.
DateSelectionResult DatePickerSelectDate(bool range, LocalDate value,
                                         LocalDate* start, LocalDate* end,
                                         const DateMatcher& disabled);

// The picker takes focus even when disabled — Rust tracks the handle either
// way and only drops it out of tab traversal, so a click still lands on it.
struct DatePicker {
    static El* New(Ctx* cx, Str id, bool disabled = false, bool open = false,
                   Listener onOpenChange = {});
};
} // namespace gpui
#endif // GPUI_BASE_DATE_PICKER_H_
