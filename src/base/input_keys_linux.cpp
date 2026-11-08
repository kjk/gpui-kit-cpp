#include "base/input_keys.h"

namespace gpui {

void InputBindPlatformKeys(const char* ctx) {
    // Avoid Ctrl+Alt+arrows: desktops may reserve them. alt-shift selects a
    // word in addition to ctrl-shift.
    KeyBinding extra[] = {
        {"shift-alt-up", input::AddCursorAbove(), ctx},
        {"shift-alt-down", input::AddCursorBelow(), ctx},
        {"alt-shift-left", input::SelectToPreviousWordStart(), ctx},
        {"alt-shift-right", input::SelectToNextWordEnd(), ctx},
    };
    InputBindKeysNonMac(ctx, extra, (int)(sizeof(extra) / sizeof(extra[0])));
}

} // namespace gpui
