#include "base/input_keys.h"

namespace gpui {

void InputBindPlatformKeys(const char* ctx) {
    KeyBinding extra[] = {
        {"shift-alt-left", input::SelectLeft(), ctx},
        {"shift-alt-right", input::SelectRight(), ctx},
        {"ctrl-alt-up", input::AddCursorAbove(), ctx},
        {"ctrl-alt-down", input::AddCursorBelow(), ctx},
    };
    InputBindKeysNonMac(ctx, extra, (int)(sizeof(extra) / sizeof(extra[0])));
}

} // namespace gpui
