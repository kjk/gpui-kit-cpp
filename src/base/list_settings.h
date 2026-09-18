#ifndef GPUI_BASE_LIST_SETTINGS_H_
#define GPUI_BASE_LIST_SETTINGS_H_
/* List presentation settings — crates/base/src/list_settings.rs */

#include "gpui/gpui.h"

namespace gpui {

struct ListSettings {
    // Whether a selected row takes the active highlight — the translucent
    // list.active tint — or the plain `accent` block. Rust defaults it on.
    // The outline around a selected item was dropped in upstream #3108.
    bool activeHighlight = true;
};

const ListSettings& ListSettingsNow(App* app);
void ListSettingsSet(App* app, ListSettings s);

// The pair a selected row paints with: the fill, and the rule drawn over the
// row's own box so the highlight does not move anything. `active` is the theme
// pair for a list or for a table, which fall back to the same colors.
struct ListActiveStyle {
    Background bg = {};
    Rgba border = {};
    bool hasBorder = false;
};
ListActiveStyle ListActiveStyleOf(const ListSettings& settings,
                                  Background active, Rgba activeBorder,
                                  Background accent, bool selected);

// The rule itself: an absolutely positioned child that covers the row.
El* ListActiveOverlay(Arena* a, Rgba border, float radius);

} // namespace gpui
#endif // GPUI_BASE_LIST_SETTINGS_H_
