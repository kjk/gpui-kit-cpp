#ifndef GPUI_BASE_TABS_H_
#define GPUI_BASE_TABS_H_
/* Unstyled tabs — crates/base/src/tabs.rs */

#include "base/state_style.h"

namespace gpui {

struct TabStyles {
    StateStyle selected = {};
    StateStyle disabled = {};

    TabStyles& Selected(const StateStyle& style);
    TabStyles& Disabled(const StateStyle& style);
};

// A tab list is a single keyboard stop. Its enabled children rove focus
// along the requested axis and keep selection and activation controlled.
struct Tabs {
    static El* New(Ctx* cx, Str id, Axis axis = Axis::Horizontal);
};

// Resolve which tabs belong to each list after assigning stable element
// paths. Runs before layout so the focused tab can also draw its ring.
void TabsPrepareFocus(Window* win, El* root);

// A tab joins its enclosing list when present. Selection, naming and set
// position are provided by the caller, and activation runs its own listener.
struct Tab {
    static El* New(Ctx* cx, Str id, bool disabled = false,
                   Listener onClick = {}, bool selected = false,
                   Str accessibilityLabel = {}, int positionInSet = 0,
                   int sizeOfSet = 0, const TabStyles* styles = nullptr,
                   const StateStyle* instance = nullptr);
};
} // namespace gpui
#endif // GPUI_BASE_TABS_H_
