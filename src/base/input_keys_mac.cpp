#include "base/input_keys.h"

namespace gpui {

void InputBindPlatformKeys(const char* ctx) {
    KeyBinding bindings[] = {
        {"ctrl-backspace", input::Backspace(), ctx},
        {"cmd-backspace", input::DeleteToBeginningOfLine(), ctx},
        {"cmd-delete", input::DeleteToEndOfLine(), ctx},
        {"alt-backspace", input::DeleteToPreviousWordStart(), ctx},
        {"alt-delete", input::DeleteToNextWordEnd(), ctx},
        {"cmd-]", input::Indent(), ctx},
        {"cmd-[", input::Outdent(), ctx},
        {"cmd-alt-up", input::AddCursorAbove(), ctx},
        {"cmd-alt-down", input::AddCursorBelow(), ctx},
        {"ctrl-shift-a", input::SelectToStartOfLine(), ctx},
        {"ctrl-shift-e", input::SelectToEndOfLine(), ctx},
        {"shift-cmd-left", input::SelectToStartOfLine(), ctx},
        {"shift-cmd-right", input::SelectToEndOfLine(), ctx},
        {"alt-shift-left", input::SelectToPreviousWordStart(), ctx},
        {"alt-shift-right", input::SelectToNextWordEnd(), ctx},
        {"cmd-a", input::SelectAll(), ctx},
        {"cmd-c", input::Copy(), ctx},
        {"cmd-x", input::Cut(), ctx},
        {"cmd-v", input::Paste(), ctx},
        {"ctrl-a", input::MoveHome(), ctx},
        {"cmd-left", input::MoveHome(), ctx},
        {"ctrl-e", input::MoveEnd(), ctx},
        {"cmd-right", input::MoveEnd(), ctx},
        {"cmd-z", input::Undo(), ctx},
        {"cmd-shift-z", input::Redo(), ctx},
        {"cmd-up", input::MoveToStart(), ctx},
        {"cmd-down", input::MoveToEnd(), ctx},
        {"alt-left", input::MoveToPreviousWord(), ctx},
        {"alt-right", input::MoveToNextWord(), ctx},
        {"cmd-shift-up", input::SelectToStart(), ctx},
        {"cmd-shift-down", input::SelectToEnd(), ctx},
        {"cmd-.", input::ToggleCodeActions(), ctx},
        {"cmd-f", input::Search(), ctx},
        {"cmd-shift-f", input::Replace(), ctx},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

} // namespace gpui
