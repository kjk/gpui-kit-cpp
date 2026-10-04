#include "base/input_keys.h"

namespace gpui {

void InputBindPlatformKeys(const char* ctx) {
    KeyBinding bindings[] = {
        {"ctrl-backspace", input::DeleteToPreviousWordStart(), ctx},
        {"ctrl-delete", input::DeleteToNextWordEnd(), ctx},
        {"ctrl-]", input::Indent(), ctx},
        {"ctrl-[", input::Outdent(), ctx},
        {"shift-alt-left", input::SelectLeft(), ctx},
        {"shift-alt-right", input::SelectRight(), ctx},
        {"ctrl-alt-up", input::AddCursorAbove(), ctx},
        {"ctrl-alt-down", input::AddCursorBelow(), ctx},
        {"ctrl-shift-left", input::SelectToPreviousWordStart(), ctx},
        {"ctrl-shift-right", input::SelectToNextWordEnd(), ctx},
        {"ctrl-a", input::SelectAll(), ctx},
        {"ctrl-c", input::Copy(), ctx},
        {"ctrl-x", input::Cut(), ctx},
        {"ctrl-v", input::Paste(), ctx},
        {"ctrl-left", input::MoveToPreviousWord(), ctx},
        {"ctrl-right", input::MoveToNextWord(), ctx},
        {"ctrl-z", input::Undo(), ctx},
        {"ctrl-y", input::Redo(), ctx},
        {"ctrl-.", input::ToggleCodeActions(), ctx},
        {"ctrl-f", input::Search(), ctx},
        {"ctrl-h", input::Replace(), ctx},
        {"ctrl-home", input::MoveToStart(), ctx},
        {"ctrl-end", input::MoveToEnd(), ctx},
        {"ctrl-shift-home", input::SelectToStart(), ctx},
        {"ctrl-shift-end", input::SelectToEnd(), ctx},
        {"ctrl-shift-z", input::Redo(), ctx},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

} // namespace gpui
