#ifndef GPUI_UI_CHECKBOX_H_
#define GPUI_UI_CHECKBOX_H_
/* Themed checkbox — crates/ui/src/checkbox.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

struct Checkbox {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Str label = {};
    // The announced name, when the visible label is not it.
    Str accessibilityLabel = {};
    Str hint = {};
    // ParentElement: the element Rust's `.child(..)` puts under the label,
    // which the story fills with muted text or a markdown line.
    El* child = nullptr;
    bool checked = false;
    bool disabled = false;
    UiSize size = UiSize::Medium;
    Str tooltip = {};
    int tooltipShowDelayMs = -1;
    bool focusRing = true;
    AccessibilityRole accessibilityRole = AccessibilityRole::CheckBox;
    int tabIndex = 0;
    bool tabStop = true;
    float w = 0;
    Listener onClick;

    static Checkbox* New(Ctx* cx, Str id);
    Checkbox* Label(Str s);
    // Set the name a screen reader announces, overriding the visible label.
    // This does not change the label drawn on screen. A checkbox without a
    // label or text children has no accessible name, so set this whenever
    // the checkbox is drawn without visible text.
    Checkbox* AccessibilityLabel(Str s);
    Checkbox* Hint(Str s);
    Checkbox* Child(El* e);
    Checkbox* Checked(bool v);
    Checkbox* Disabled(bool v);
    Checkbox* WithSize(UiSize s);
    Checkbox* W(float v);
    // FocusableExt::focus_ring: no focus appearance on this control.
    Checkbox* FocusRing(bool v);
    Checkbox* Role(AccessibilityRole role);
    Checkbox* TabIndex(int v);
    Checkbox* TabStop(bool v);
    Checkbox* Tooltip(Str s);
    Checkbox* TooltipShowDelay(int ms);
    Checkbox* OnClick(Listener fn);
    // Semantic controlled-value spelling. OnClick is the compatibility
    // alias; both replace the same callback.
    Checkbox* OnChange(Listener fn);
    El* IntoEl();
};

// What a checkbox and a radio both hang on the row around their indicator.
// The announced name is the explicit one over the visible label, and only
// the name changes: what is drawn stays the label. The tooltip takes its own
// show delay when one is set.
void ChoiceRowAnnounce(El* row, Str label, Str accessibilityLabel, Str tooltip,
                       int tooltipShowDelayMs);
// v_flex().line_height(relative(1.25)).gap_1(): the label over the muted
// hint, with the indicator nudged down to the label's first line. The caller
// adds what goes under them and puts the column in the row. `wrapLabel` lets
// the label wrap, which a checkbox's does and a radio's does not.
El* ChoiceLabelColumn(Ctx* cx, El* indicator, float box, Str label, Str hint,
                      UiSize size, bool disabled, bool wrapLabel);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_CHECKBOX_H_
