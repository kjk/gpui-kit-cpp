#ifndef GPUI_UI_CHECKBOX_H_
#define GPUI_UI_CHECKBOX_H_
/* Themed checkbox — crates/ui/src/checkbox.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// What a checkbox and a radio both hold: identity, the label column's text,
// the tooltip, state, size and the focus and Tab settings.
struct ChoiceFields {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Str label = {};
    // The announced name, when the visible label is not it.
    Str accessibilityLabel = {};
    Str hint = {};
    Str tooltip = {};
    int tooltipShowDelayMs = -1;
    bool checked = false;
    bool disabled = false;
    UiSize size = UiSize::Medium;
    bool focusRing = true;
    int tabIndex = 0;
    bool tabStop = true;
    Listener onClick;
};

// The row around the indicator, built from what gpui_base's Checkbox or
// Radio returned: Tab and focus settings, h_flex().gap_2().items_start(),
// and the announced name and tooltip. The announced name is the explicit
// one over the visible label, and only the name changes: what is drawn
// stays the label. The tooltip takes its own show delay when one is set.
El* ChoiceRow(Ctx* cx, El* row, const ChoiceFields& f);

// The setters of a checkbox and a radio, each returning its own builder.
template <typename Self>
struct ChoiceBuilder : ChoiceFields {
    static Self* New(Ctx* cx, Str id) {
        Self* self = ArenaNew<Self>(cx->a);
        self->a = cx->a;
        self->cx = cx;
        self->id = id;
        return self;
    }
    Self* This() { return static_cast<Self*>(this); }
    Self* Label(Str s) {
        label = s;
        return This();
    }
    // Set the name a screen reader announces, overriding the visible label.
    // This does not change the label drawn on screen. A control without a
    // label or text children has no accessible name, so set this whenever
    // it is drawn without visible text.
    Self* AccessibilityLabel(Str s) {
        accessibilityLabel = s;
        return This();
    }
    Self* Hint(Str s) {
        hint = s;
        return This();
    }
    Self* Tooltip(Str s) {
        tooltip = s;
        return This();
    }
    Self* TooltipShowDelay(int ms) {
        tooltipShowDelayMs = ms;
        return This();
    }
    Self* Checked(bool v) {
        checked = v;
        return This();
    }
    Self* Disabled(bool v) {
        disabled = v;
        return This();
    }
    Self* WithSize(UiSize s) {
        size = s;
        return This();
    }
    // FocusableExt::focus_ring: no focus appearance on this control.
    Self* FocusRing(bool v) {
        focusRing = v;
        return This();
    }
    Self* TabIndex(int v) {
        tabIndex = v;
        return This();
    }
    Self* TabStop(bool v) {
        tabStop = v;
        return This();
    }
    // Semantic controlled-value spelling. OnClick is the compatibility
    // alias; both replace the same callback.
    Self* OnChange(Listener fn) {
        onClick = fn;
        return This();
    }
    Self* OnClick(Listener fn) { return OnChange(fn); }
};

struct Checkbox : ChoiceBuilder<Checkbox> {
    // ParentElement: the element Rust's `.child(..)` puts under the label,
    // which the story fills with muted text or a markdown line.
    El* child = nullptr;
    AccessibilityRole accessibilityRole = AccessibilityRole::CheckBox;
    float w = 0;

    Checkbox* Child(El* e);
    Checkbox* W(float v);
    Checkbox* Role(AccessibilityRole role);
    El* IntoEl();
};

// v_flex().line_height(relative(1.25)).gap_1(): the label over the muted
// hint, with the indicator nudged down to the label's first line. The caller
// adds what goes under them and puts the column in the row. `wrapLabel` lets
// the label wrap, which a checkbox's does and a radio's does not.
El* ChoiceLabelColumn(Ctx* cx, El* indicator, float box, Str label, Str hint,
                      UiSize size, bool disabled, bool wrapLabel);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_CHECKBOX_H_
