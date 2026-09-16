#ifndef GPUI_SRC_UI_SWITCH_H_
#define GPUI_SRC_UI_SWITCH_H_
/* Themed switch — crates/ui/src/switch.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

struct Switch {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Str label = {};
    // The announced name, when the visible label is not it.
    Str accessibilityLabel = {};
    bool checked = false;
    bool disabled = false;
    UiSize size = UiSize::Medium;
    Rgba color = {};
    bool hasColor = false;
    Listener onClick;

    static Switch* New(Ctx* cx, Str id);
    Switch* Label(Str s);
    // Set the name a screen reader announces, when the visible label is not
    // it. A switch's name comes from its Label by default; setting this
    // replaces the announced name without changing what is displayed.
    Switch* AccessibilityLabel(Str s);
    Switch* Checked(bool v);
    Switch* Disabled(bool v);
    Switch* WithSize(UiSize s);
    Switch* Color(Rgba c);
    Switch* OnClick(Listener fn);
    // Semantic controlled-value spelling. OnClick is the compatibility
    // alias; both replace the same callback.
    Switch* OnChange(Listener fn);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_SWITCH_H_
