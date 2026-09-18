#include "base/list_settings.h"

namespace gpui {

const ListSettings& ListSettingsNow(App* app) {
    return *AppGlobalEnsure<ListSettings>(app);
}

void ListSettingsSet(App* app, ListSettings s) {
    ListSettings* current = AppGlobalEnsure<ListSettings>(app);
    if (current) {
        *current = s;
    }
}

ListActiveStyle ListActiveStyleOf(const ListSettings& settings,
                                  Background active, Rgba activeBorder,
                                  Background accent, bool selected) {
    ListActiveStyle out;
    bool highlight = settings.activeHighlight;
    // list_item.rs: the tint is for the selection proper — a row marked by a
    // right press takes `accent` either way. The outline around a selected
    // item, row or cell was dropped in upstream #3108.
    out.bg = (selected && highlight) ? active : accent;
    out.border = activeBorder;
    out.hasBorder = false;
    return out;
}

El* ListActiveOverlay(Arena* a, Rgba border, float radius) {
    return Div(a)
        ->Absolute()
        ->Top(0)
        ->Left(0)
        ->Right(0)
        ->Bottom(0)
        ->Radius(radius)
        ->Border(1, border);
}

} // namespace gpui
