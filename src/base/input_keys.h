#ifndef GPUI_BASE_INPUT_KEYS_H_
#define GPUI_BASE_INPUT_KEYS_H_
/* The input's key bindings and the actions they name — the declarations for
   `input_keys.cpp`, which is `state.rs::init`. */

#include "gpui/gpui.h"
#include "gpui/keymap.h"

namespace gpui {

namespace input {

uint32_t Backspace();
uint32_t Copy();
uint32_t Cut();
uint32_t Delete();
uint32_t DeleteToBeginningOfLine();
uint32_t DeleteToEndOfLine();
uint32_t DeleteToNextWordEnd();
uint32_t DeleteToPreviousWordStart();
uint32_t Escape();
uint32_t Indent();
uint32_t IndentInline();
uint32_t MoveDown();
uint32_t MoveEnd();
uint32_t MoveHome();
uint32_t MoveLeft();
uint32_t MovePageDown();
uint32_t MovePageUp();
uint32_t MoveRight();
uint32_t MoveToEnd();
uint32_t MoveToNextWord();
uint32_t MoveToPreviousWord();
uint32_t MoveToStart();
uint32_t MoveUp();
uint32_t Outdent();
uint32_t OutdentInline();
uint32_t Paste();
uint32_t Redo();
uint32_t Replace();
uint32_t Search();
uint32_t SelectAll();
uint32_t SelectDown();
uint32_t SelectLeft();
uint32_t SelectRight();
uint32_t SelectToEnd();
uint32_t SelectToEndOfLine();
uint32_t SelectToNextWordEnd();
uint32_t AddCursorAbove();
uint32_t AddCursorBelow();
uint32_t SelectToPreviousWordStart();
uint32_t SelectToStart();
uint32_t SelectToStartOfLine();
uint32_t SelectUp();
uint32_t Undo();
uint32_t Enter();

} // namespace input

// The key context an editable field declares, which is what scopes every
// binding below to a focused input.
Str InputContext();

// Bind them, once per keymap. A field's element calls this as it is built,
// the way each component's `init` does.
void InputInitKeys();

// The edit an action names. `arg` is the action's payload — for
// `input::Enter` it is bit 0 `secondary`, bit 1 `shift`.
InputAction InputActionOf(uint32_t id, intptr_t arg = 0);

// Whether `arg` on an `input::Enter` says the shift variant.
constexpr bool InputEnterShift(intptr_t arg) {
    return (arg & 2) != 0;
}

} // namespace gpui
#endif // GPUI_BASE_INPUT_KEYS_H_
