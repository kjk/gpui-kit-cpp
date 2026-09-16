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
    bool focusRing = true;
    AccessibilityRole accessibilityRole = AccessibilityRole::CheckBox;
    int tabIndex = 0;
    bool tabStop = true;
    float w = 0;
    Listener onClick;

    static Checkbox* New(Ctx* cx, Str id);
    Checkbox* Label(Str s);
    // Set the name a screen reader announces, overriding the visible label.
    // This does not change the label drawn on screen.
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
    Checkbox* OnClick(Listener fn);
    // Semantic controlled-value spelling. OnClick is the compatibility
    // alias; both replace the same callback.
    Checkbox* OnChange(Listener fn);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_CHECKBOX_H_
