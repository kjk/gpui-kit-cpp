/* The input's keyboard, as the keymap holds it — the port of
   `crates/base/src/input/base/state.rs::init`.

   Every chord in that function is a `KeyBinding` here, in the `Input` key
   context an editable field declares, with the two platform sets Rust splits
   with `#[cfg(target_os = "macos")]` kept apart the way it keeps them. It had
   been a `switch` over the key code, which could say nothing about `ctrl-a`
   and `cmd-a` being different chords on a Mac and which no application could
   rebind.

   `Enter { secondary, shift }` is the one action here that carries a payload:
   bit 0 is `secondary`, bit 1 is `shift`. */

#include "base/input_keys.h"

namespace gpui {

namespace input {

// One accessor per action, since a name spelled two ways is two actions and
// nothing would say so. Rust's `actions!(input, [..])`, namespace included.
#define GPUI_INPUT_ACTION(name, spelled)              \
    uint32_t name() {                                 \
        static uint32_t id = ActionOf(StrL(spelled)); \
        return id;                                    \
    }

GPUI_INPUT_ACTION(AddCursorAbove, "input::AddCursorAbove")
GPUI_INPUT_ACTION(AddCursorBelow, "input::AddCursorBelow")
GPUI_INPUT_ACTION(Backspace, "input::Backspace")
GPUI_INPUT_ACTION(Copy, "input::Copy")
GPUI_INPUT_ACTION(Cut, "input::Cut")
GPUI_INPUT_ACTION(Delete, "input::Delete")
GPUI_INPUT_ACTION(DeleteToBeginningOfLine, "input::DeleteToBeginningOfLine")
GPUI_INPUT_ACTION(DeleteToEndOfLine, "input::DeleteToEndOfLine")
GPUI_INPUT_ACTION(DeleteToNextWordEnd, "input::DeleteToNextWordEnd")
GPUI_INPUT_ACTION(DeleteToPreviousWordStart, "input::DeleteToPreviousWordStart")
GPUI_INPUT_ACTION(Escape, "input::Escape")
GPUI_INPUT_ACTION(Indent, "input::Indent")
GPUI_INPUT_ACTION(IndentInline, "input::IndentInline")
GPUI_INPUT_ACTION(MoveDown, "input::MoveDown")
GPUI_INPUT_ACTION(MoveEnd, "input::MoveEnd")
GPUI_INPUT_ACTION(MoveHome, "input::MoveHome")
GPUI_INPUT_ACTION(MoveLeft, "input::MoveLeft")
GPUI_INPUT_ACTION(MovePageDown, "input::MovePageDown")
GPUI_INPUT_ACTION(MovePageUp, "input::MovePageUp")
GPUI_INPUT_ACTION(MoveRight, "input::MoveRight")
GPUI_INPUT_ACTION(MoveToEnd, "input::MoveToEnd")
GPUI_INPUT_ACTION(MoveToNextWord, "input::MoveToNextWord")
GPUI_INPUT_ACTION(MoveToPreviousWord, "input::MoveToPreviousWord")
GPUI_INPUT_ACTION(MoveToStart, "input::MoveToStart")
GPUI_INPUT_ACTION(MoveUp, "input::MoveUp")
GPUI_INPUT_ACTION(Outdent, "input::Outdent")
GPUI_INPUT_ACTION(OutdentInline, "input::OutdentInline")
GPUI_INPUT_ACTION(Paste, "input::Paste")
GPUI_INPUT_ACTION(Redo, "input::Redo")
GPUI_INPUT_ACTION(Replace, "input::Replace")
GPUI_INPUT_ACTION(Search, "input::Search")
GPUI_INPUT_ACTION(SelectAll, "input::SelectAll")
GPUI_INPUT_ACTION(SelectDown, "input::SelectDown")
GPUI_INPUT_ACTION(SelectLeft, "input::SelectLeft")
GPUI_INPUT_ACTION(SelectRight, "input::SelectRight")
GPUI_INPUT_ACTION(SelectToEnd, "input::SelectToEnd")
GPUI_INPUT_ACTION(SelectToEndOfLine, "input::SelectToEndOfLine")
GPUI_INPUT_ACTION(SelectToNextWordEnd, "input::SelectToNextWordEnd")
GPUI_INPUT_ACTION(SelectToPreviousWordStart, "input::SelectToPreviousWordStart")
GPUI_INPUT_ACTION(SelectToStart, "input::SelectToStart")
GPUI_INPUT_ACTION(SelectToStartOfLine, "input::SelectToStartOfLine")
GPUI_INPUT_ACTION(SelectUp, "input::SelectUp")
GPUI_INPUT_ACTION(Undo, "input::Undo")
GPUI_INPUT_ACTION(Enter, "input::Enter")
GPUI_INPUT_ACTION(ToggleCodeActions, "input::ToggleCodeActions")
#undef GPUI_INPUT_ACTION

} // namespace input

Str InputContext() {
    return StrL("Input");
}

void InputInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    const char* ctx = "Input";
    KeyBinding bindings[] = {

        {"backspace", input::Backspace(), ctx},
        {"shift-backspace", input::Backspace(), ctx},

#if GPUI_OS_MAC
        {"ctrl-backspace", input::Backspace(), ctx},
#endif
        {"delete", input::Delete(), ctx},
        {"shift-delete", input::Delete(), ctx},

#if GPUI_OS_MAC
        {"cmd-backspace", input::DeleteToBeginningOfLine(), ctx},
        {"cmd-delete", input::DeleteToEndOfLine(), ctx},
        {"alt-backspace", input::DeleteToPreviousWordStart(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-backspace", input::DeleteToPreviousWordStart(), ctx},
#endif
#if GPUI_OS_MAC
        {"alt-delete", input::DeleteToNextWordEnd(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-delete", input::DeleteToNextWordEnd(), ctx},
#endif
        {"enter", input::Enter(), ctx},
        {"shift-enter", input::Enter(), ctx, 2},
        {"secondary-enter", input::Enter(), ctx, 1},
        {"escape", input::Escape(), ctx},
        {"up", input::MoveUp(), ctx},
        {"down", input::MoveDown(), ctx},
        {"left", input::MoveLeft(), ctx},
        {"right", input::MoveRight(), ctx},
        {"pageup", input::MovePageUp(), ctx},
        {"pagedown", input::MovePageDown(), ctx},
        {"tab", input::IndentInline(), ctx},
        {"shift-tab", input::OutdentInline(), ctx},

#if GPUI_OS_MAC
        {"cmd-]", input::Indent(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-]", input::Indent(), ctx},
#endif
#if GPUI_OS_MAC
        {"cmd-[", input::Outdent(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-[", input::Outdent(), ctx},
#endif
        {"shift-left", input::SelectLeft(), ctx},
        {"shift-right", input::SelectRight(), ctx},
        {"shift-up", input::SelectUp(), ctx},
        {"shift-down", input::SelectDown(), ctx},
    // Avoid Ctrl+Alt+arrows on Linux, where desktops may reserve them.
#if GPUI_OS_MAC
        {"cmd-alt-up", input::AddCursorAbove(), ctx},
        {"cmd-alt-down", input::AddCursorBelow(), ctx},
#elif GPUI_OS_WINDOWS
        {"ctrl-alt-up", input::AddCursorAbove(), ctx},
        {"ctrl-alt-down", input::AddCursorBelow(), ctx},
#else
        {"shift-alt-up", input::AddCursorAbove(), ctx},
        {"shift-alt-down", input::AddCursorBelow(), ctx},
#endif
        {"home", input::MoveHome(), ctx},
        {"end", input::MoveEnd(), ctx},
        {"shift-home", input::SelectToStartOfLine(), ctx},
        {"shift-end", input::SelectToEndOfLine(), ctx},

#if GPUI_OS_MAC
        {"ctrl-shift-a", input::SelectToStartOfLine(), ctx},
        {"ctrl-shift-e", input::SelectToEndOfLine(), ctx},
        {"shift-cmd-left", input::SelectToStartOfLine(), ctx},
        {"shift-cmd-right", input::SelectToEndOfLine(), ctx},
        {"alt-shift-left", input::SelectToPreviousWordStart(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-shift-left", input::SelectToPreviousWordStart(), ctx},
#endif
#if GPUI_OS_MAC
        {"alt-shift-right", input::SelectToNextWordEnd(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-shift-right", input::SelectToNextWordEnd(), ctx},
#endif
#if GPUI_OS_MAC
        {"cmd-a", input::SelectAll(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-a", input::SelectAll(), ctx},
#endif
#if GPUI_OS_MAC
        {"cmd-c", input::Copy(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-c", input::Copy(), ctx},
#endif
#if GPUI_OS_MAC
        {"cmd-x", input::Cut(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-x", input::Cut(), ctx},
#endif
#if GPUI_OS_MAC
        {"cmd-v", input::Paste(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-v", input::Paste(), ctx},
#endif
#if GPUI_OS_MAC
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
#endif
#if !GPUI_OS_MAC
        {"ctrl-left", input::MoveToPreviousWord(), ctx},
        {"ctrl-right", input::MoveToNextWord(), ctx},
#endif
#if GPUI_OS_MAC
        {"cmd-shift-up", input::SelectToStart(), ctx},
        {"cmd-shift-down", input::SelectToEnd(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-z", input::Undo(), ctx},
        {"ctrl-y", input::Redo(), ctx},
#endif
#if GPUI_OS_MAC
        {"cmd-.", input::ToggleCodeActions(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-.", input::ToggleCodeActions(), ctx},
#endif
#if GPUI_OS_MAC
        {"cmd-f", input::Search(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-f", input::Search(), ctx},
#endif
#if GPUI_OS_MAC
        {"cmd-shift-f", input::Replace(), ctx},
#endif
#if !GPUI_OS_MAC
        {"ctrl-h", input::Replace(), ctx},
#endif
#if !GPUI_OS_MAC
        // Three chords state.rs does not bind off macOS, kept because this
        // tree had them and because they are what the platform means:
        //
        //   ctrl-home / ctrl-end   the document ends, which state.rs spells
        //                          cmd-up / cmd-down and binds on macOS only,
        //                          leaving a Windows field no way to reach
        //                          either end
        //   ctrl-shift-z           redo. Upstream's only non-macOS redo is
        //                          ctrl-y, which is bound above; this is the
        //                          spelling every other editor on the
        //                          platform also takes
        {"ctrl-home", input::MoveToStart(), ctx},
        {"ctrl-end", input::MoveToEnd(), ctx},
        {"ctrl-shift-home", input::SelectToStart(), ctx},
        {"ctrl-shift-end", input::SelectToEnd(), ctx},
        {"ctrl-shift-z", input::Redo(), ctx},
#endif
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

// The action the keymap resolved, read as the edit it names. Rust dispatches
// the type; this is the same table, spelled out once.
InputAction InputActionOf(uint32_t id, intptr_t arg) {
    (void)arg;
    if (!id) {
        return InputAction::None;
    }

    if (id == input::Backspace()) {
        return InputAction::Backspace;
    }
    if (id == input::Copy()) {
        return InputAction::Copy;
    }
    if (id == input::Cut()) {
        return InputAction::Cut;
    }
    if (id == input::Delete()) {
        return InputAction::Delete;
    }
    if (id == input::DeleteToBeginningOfLine()) {
        return InputAction::DeleteToBeginningOfLine;
    }
    if (id == input::DeleteToEndOfLine()) {
        return InputAction::DeleteToEndOfLine;
    }
    if (id == input::DeleteToNextWordEnd()) {
        return InputAction::DeleteToNextWordEnd;
    }
    if (id == input::DeleteToPreviousWordStart()) {
        return InputAction::DeleteToPreviousWordStart;
    }
    if (id == input::Escape()) {
        return InputAction::Escape;
    }
    if (id == input::Indent()) {
        return InputAction::Indent;
    }
    if (id == input::IndentInline()) {
        return InputAction::IndentInline;
    }
    if (id == input::MoveDown()) {
        return InputAction::MoveDown;
    }
    if (id == input::MoveEnd()) {
        return InputAction::MoveEnd;
    }
    if (id == input::MoveHome()) {
        return InputAction::MoveHome;
    }
    if (id == input::MoveLeft()) {
        return InputAction::MoveLeft;
    }
    if (id == input::MovePageDown()) {
        return InputAction::MovePageDown;
    }
    if (id == input::MovePageUp()) {
        return InputAction::MovePageUp;
    }
    if (id == input::MoveRight()) {
        return InputAction::MoveRight;
    }
    if (id == input::MoveToEnd()) {
        return InputAction::MoveToEnd;
    }
    if (id == input::MoveToNextWord()) {
        return InputAction::MoveToNextWord;
    }
    if (id == input::MoveToPreviousWord()) {
        return InputAction::MoveToPreviousWord;
    }
    if (id == input::MoveToStart()) {
        return InputAction::MoveToStart;
    }
    if (id == input::MoveUp()) {
        return InputAction::MoveUp;
    }
    if (id == input::Outdent()) {
        return InputAction::Outdent;
    }
    if (id == input::OutdentInline()) {
        return InputAction::OutdentInline;
    }
    if (id == input::Paste()) {
        return InputAction::Paste;
    }
    if (id == input::Redo()) {
        return InputAction::Redo;
    }
    if (id == input::Replace()) {
        return InputAction::Replace;
    }
    if (id == input::ToggleCodeActions()) {
        return InputAction::ToggleCodeActions;
    }
    if (id == input::Search()) {
        return InputAction::Search;
    }
    if (id == input::SelectAll()) {
        return InputAction::SelectAll;
    }
    if (id == input::SelectDown()) {
        return InputAction::SelectDown;
    }
    if (id == input::SelectLeft()) {
        return InputAction::SelectLeft;
    }
    if (id == input::SelectRight()) {
        return InputAction::SelectRight;
    }
    if (id == input::SelectToEnd()) {
        return InputAction::SelectToEnd;
    }
    if (id == input::SelectToEndOfLine()) {
        return InputAction::SelectToEndOfLine;
    }
    if (id == input::SelectToNextWordEnd()) {
        return InputAction::SelectToNextWordEnd;
    }
    if (id == input::SelectToPreviousWordStart()) {
        return InputAction::SelectToPreviousWordStart;
    }
    if (id == input::SelectToStart()) {
        return InputAction::SelectToStart;
    }
    if (id == input::SelectToStartOfLine()) {
        return InputAction::SelectToStartOfLine;
    }
    if (id == input::SelectUp()) {
        return InputAction::SelectUp;
    }
    if (id == input::AddCursorAbove()) {
        return InputAction::AddCursorAbove;
    }
    if (id == input::AddCursorBelow()) {
        return InputAction::AddCursorBelow;
    }
    if (id == input::Undo()) {
        return InputAction::Undo;
    }
    if (id == input::Enter()) {
        return InputAction::Enter;
    }
    return InputAction::None;
}

} // namespace gpui
