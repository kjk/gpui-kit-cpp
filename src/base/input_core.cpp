#include "base/input_core.h"

namespace gpui {

InputModeKind InputModeKind::Of(const InputState* state) {
    return InputModeKind{state ? state->kind : InputKind::Input};
}

bool InputModeKind::IsMultiLine() const {
    return kind == InputKind::Textarea || kind == InputKind::Editor;
}

bool InputModeKind::IsCodeEditor() const {
    return kind == InputKind::Editor;
}

bool MultiLineMode::Includes(InputKind kind) {
    return kind == InputKind::Textarea || kind == InputKind::Editor;
}

bool InputExtras::HasSemanticTokens() const {
    return state && state->semanticTokens.len > 0;
}

bool InputExtras::HasDocumentColors() const {
    return state && state->documentColors.len > 0;
}

bool InputExtras::HasHover() const {
    return state && state->hoverText.len > 0;
}

bool InputExtras::HasInlineCompletion() const {
    return state && len(state->inlineCompletion.text) > 0;
}

EditorExtras EditorExtras::Of(const InputState* state) {
    EditorExtras extras;
    if (state && state->kind == InputKind::Editor) {
        extras.state = state;
    }
    return extras;
}

bool EditorExtras::HasDefinition() const {
    return state &&
           (state->definitionProvider || state->hoverDef.lastLocations.len > 0);
}

bool EditorExtras::HasCodeActions() const {
    return state &&
           (state->codeActionProvider || state->codeActionProviders.len > 0 ||
            state->codeActions.items.len > 0);
}

InputContextMenuCapabilities InputContextMenuCapabilities::Of(
    const InputState* state) {
    InputContextMenuCapabilities value;
    if (!state) {
        value.disabled = true;
        return value;
    }
    value.disabled = state->disabled;
    value.readonly = state->readonly;
    value.codeEditor = state->kind == InputKind::Editor;
    value.selection = !state->selectedRange.IsEmpty();
    value.masked = state->masked || state->maskPatternSet;
    value.goToDefinition = state->definitionProvider != nullptr ||
                           state->hoverDef.lastLocations.len > 0;
    value.codeActions = state->codeActionProvider != nullptr ||
                        state->codeActionProviders.len > 0 ||
                        state->codeActions.items.len > 0;
    return value;
}

#define GPUI_CAPABILITY_BUILDER(Method, Field)                         \
    InputContextMenuCapabilities InputContextMenuCapabilities::Method( \
        bool value) const {                                            \
        InputContextMenuCapabilities copy = *this;                     \
        copy.Field = value;                                            \
        return copy;                                                   \
    }

GPUI_CAPABILITY_BUILDER(Disabled, disabled)
GPUI_CAPABILITY_BUILDER(Readonly, readonly)
GPUI_CAPABILITY_BUILDER(CodeEditor, codeEditor)
GPUI_CAPABILITY_BUILDER(Selection, selection)
GPUI_CAPABILITY_BUILDER(Masked, masked)
GPUI_CAPABILITY_BUILDER(GoToDefinition, goToDefinition)
GPUI_CAPABILITY_BUILDER(CodeActions, codeActions)

#undef GPUI_CAPABILITY_BUILDER

InputPresentation InputPresentation::Of(Arena* a, const InputState* state) {
    InputPresentation value;
    if (!state) {
        value.disabled = true;
        return value;
    }
    value.focus = state->focus;
    value.disabled = state->disabled;
    value.readonly = state->readonly;
    value.loading = state->loading;
    value.masked = state->masked || state->maskPatternSet;
    value.multiLine = InputIsMultiLine(state);
    value.codeEditor = state->kind == InputKind::Editor;
    value.textAlign = state->align;
    value.placeholder = state->placeholder;
    if (a && state->maskPatternSet) {
        value.maskPlaceholder = MaskPlaceholder(a, state->maskPattern);
    }
    return value;
}

InputStyles& InputStyles::Focused(const Style& style, uint32_t fields) {
    focused = style;
    focusedFields = fields;
    return *this;
}

InputStyles& InputStyles::Disabled(const Style& style, uint32_t fields) {
    disabled = style;
    disabledFields = fields;
    return *this;
}

void InputStyles::Apply(Style* style, bool isFocused, bool isDisabled) const {
    if (!style) {
        return;
    }
    if (isFocused && focusedFields) {
        StyleApplyFields(style, focused, focusedFields);
    }
    if (isDisabled && disabledFields) {
        StyleApplyFields(style, disabled, disabledFields);
    }
}

NativeMenu::NativeMenu() {
    arena = ArenaNew();
}

NativeMenu::~NativeMenu() {
    VecReset(items);
    if (arena) {
        ArenaDelete(arena);
    }
}

NativeMenu& NativeMenu::Menu(Str label, InputAction action) {
    return MenuWithDisabled(label, false, action);
}

NativeMenu& NativeMenu::MenuWithDisabled(Str label, bool disabled,
                                         InputAction action) {
    NativeMenuItem item;
    item.kind = NativeMenuItemKind::Action;
    item.label = arena ? StrDup(arena, label) : label;
    item.disabled = disabled;
    item.action = action;
    VecAppend(items, item);
    return *this;
}

NativeMenu& NativeMenu::Separator() {
    if (items.len > 0 && items[items.len - 1]
                                 .kind != NativeMenuItemKind::Separator) {
        VecAppend(items, NativeMenuItem{});
    }
    return *this;
}

void InputDefaultNativeMenu(const InputState* state, NativeMenu* out) {
    if (!out) {
        return;
    }
    InputContextMenuCapabilities c = InputContextMenuCapabilities::Of(state);
    out->MenuWithDisabled(StrL("Copy"), !c.IsCopyable(), InputAction::Copy)
        .MenuWithDisabled(StrL("Cut"), !c.IsEditable() || !c.IsCopyable(),
                          InputAction::Cut)
        .MenuWithDisabled(StrL("Paste"), !c.IsEditable(), InputAction::Paste)
        .Separator()
        .MenuWithDisabled(StrL("Select All"),
                          !state || len(InputValue(state)) == 0,
                          InputAction::SelectAll);
    if (c.IsCodeEditor() && (c.HasDefinition() || c.HasCodeActions())) {
        out->Separator();
        if (c.HasDefinition()) {
            out->Menu(StrL("Go to Definition"), InputAction::None);
            out->items[out->items.len - 1].goToDefinition = true;
        }
        if (c.HasCodeActions()) {
            out->MenuWithDisabled(StrL("Code Actions"), !c.IsEditable(),
                                  InputAction::ToggleCodeActions);
        }
    }
}

bool InputPerformNativeMenuItem(InputState* state, App* app, Window* win,
                                const NativeMenuItem& item) {
    if (!state || item.kind != NativeMenuItemKind::Action || item.disabled) {
        return false;
    }
    if (item.goToDefinition) {
        if (!InputCanGoToDefinition(state)) {
            return false;
        }
        InputGoToDefinition(state, app, win);
        return true;
    }
    return InputPerform(state, app, win, item.action, false);
}

} // namespace gpui
