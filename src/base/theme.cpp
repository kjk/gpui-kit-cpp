#include "base/theme.h"

namespace gpui {

base_theme::Theme base_theme::Theme::Global(const App* app) {
    const Theme* installed = AppGlobalGet<Theme>(app);
    return installed ? *installed : Theme{};
}

base_theme::Theme* base_theme::Theme::GlobalMut(App* app) {
    return AppGlobalEnsure<Theme>(app);
}

BaseTheme* BaseThemeGlobal(App* app) {
    return AppGlobalEnsure<BaseTheme>(app);
}

const BaseTheme* BaseThemeGlobal(const App* app) {
    return AppGlobalGet<BaseTheme>(app);
}

void BaseThemeSet(App* app, const BaseTheme& theme) {
    BaseTheme* current = BaseThemeGlobal(app);
    if (!current) {
        return;
    }
    CursorBlink before = current->cursorBlink;
    *current = theme;
    // An unrelated theme write keeps the phase. Only a normalized policy
    // change reaches carets that are already running.
    CursorBlink after = current->cursorBlink.Normalized();
    CursorBlink was = before.Normalized();
    if (was.intervalMs != after.intervalMs ||
        was.idleTimeoutMs != after.idleTimeoutMs) {
        BlinkRetarget(app, after);
    }
}

} // namespace gpui
