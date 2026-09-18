#ifndef GPUI_BASE_INPUT_CORE_H_
#define GPUI_BASE_INPUT_CORE_H_
/* Source-shaped input modes and presentation seams —
   crates/base/src/input/base/{kind,mod,native,state}.rs.

   The runtime keeps one POD InputState rather than instantiating Rust's
   InputBaseState<M> three times. These records preserve the public split:
   mode markers describe which API is meaningful, presentation is a
   read-only snapshot, and NativeMenu is Base's action model rather than UI
   chrome. */

#include "gpui/gpui.h"

namespace gpui {

struct InputMode {
    static constexpr InputKind KIND = InputKind::Input;
    static constexpr bool MULTI_LINE = false;
    static constexpr bool CODE_EDITOR = false;
};

struct TextareaMode {
    static constexpr InputKind KIND = InputKind::Textarea;
    static constexpr bool MULTI_LINE = true;
    static constexpr bool CODE_EDITOR = false;
};

struct EditorMode {
    static constexpr InputKind KIND = InputKind::Editor;
    static constexpr bool MULTI_LINE = true;
    static constexpr bool CODE_EDITOR = true;
};

// Rust traits become closed value facades: the engine still rejects an
// operation whose runtime kind would not implement the corresponding trait.
struct InputModeKind {
    InputKind kind = InputKind::Input;

    static InputModeKind Of(const InputState* state);
    bool IsMultiLine() const;
    bool IsCodeEditor() const;
};

struct MultiLineMode {
    InputKind kind = InputKind::Textarea;

    static bool Includes(InputKind kind);
    bool IsEditor() const { return kind == InputKind::Editor; }
};

// What a renderer may read from the mode-specific portion of the flattened
// state. Plain input/textarea states answer empty collections naturally.
struct InputExtras {
    const InputState* state = nullptr;

    bool HasSemanticTokens() const;
    bool HasDocumentColors() const;
    bool HasHover() const;
    bool HasInlineCompletion() const;
};

struct EditorExtras : InputExtras {
    static EditorExtras Of(const InputState* state);
    bool HasDefinition() const;
    bool HasCodeActions() const;
};

// InputBaseState<M> is one runtime record here; the three source aliases are
// deliberately aliases too, so no state is copied at the facade boundary.
using InputBaseState = InputState;
using EditorState = InputState;
using TextareaState = InputState;

struct InputContextMenuCapabilities {
    bool disabled = false;
    bool readonly = false;
    bool codeEditor = false;
    bool selection = false;
    bool masked = false;
    bool goToDefinition = false;
    bool codeActions = false;

    static InputContextMenuCapabilities New() { return {}; }
    static InputContextMenuCapabilities Of(const InputState* state);
    InputContextMenuCapabilities Disabled(bool value) const;
    InputContextMenuCapabilities Readonly(bool value) const;
    InputContextMenuCapabilities CodeEditor(bool value) const;
    InputContextMenuCapabilities Selection(bool value) const;
    InputContextMenuCapabilities Masked(bool value) const;
    InputContextMenuCapabilities GoToDefinition(bool value) const;
    InputContextMenuCapabilities CodeActions(bool value) const;
    bool IsDisabled() const { return disabled; }
    bool IsReadonly() const { return readonly; }
    bool IsEditable() const { return !disabled && !readonly; }
    bool IsCodeEditor() const { return codeEditor; }
    bool HasSelection() const { return selection; }
    bool IsMasked() const { return masked; }
    bool IsCopyable() const { return selection && !masked; }
    bool HasDefinition() const { return goToDefinition; }
    bool HasCodeActions() const { return codeActions; }
};

struct InputPresentation {
    FocusHandle focus = {};
    bool disabled = false;
    bool readonly = false;
    bool loading = false;
    bool masked = false;
    bool multiLine = false;
    bool codeEditor = false;
    int textAlign = 0;
    Str placeholder = {};
    Str maskPlaceholder = {};

    static InputPresentation Of(Arena* a, const InputState* state);
    bool IsEditable() const { return !disabled && !readonly; }
};

// Semantic refinements on the unstyled frame. Fields are explicit because a
// default Style is meaningful; only named fields override the normal style.
struct InputStyles {
    Style focused = {};
    uint32_t focusedFields = 0;
    Style disabled = {};
    uint32_t disabledFields = 0;

    InputStyles& Focused(const Style& style, uint32_t fields);
    InputStyles& Disabled(const Style& style, uint32_t fields);
    void Apply(Style* style, bool isFocused, bool isDisabled) const;
};

enum class NativeMenuItemKind : uint8_t {
    Separator,
    Action
};

struct NativeMenuItem {
    NativeMenuItemKind kind = NativeMenuItemKind::Separator;
    Str label = {};
    bool disabled = false;
    InputAction action = InputAction::None;
    bool goToDefinition = false;
};

// Presentation-independent menu returned by an input's context-menu seam.
// Labels are arena-owned and items have no cap, matching Rust's Vec.
struct NativeMenu {
    Arena* arena = nullptr;
    Vec<NativeMenuItem> items;

    NativeMenu();
    NativeMenu(const NativeMenu&) = delete;
    NativeMenu& operator=(const NativeMenu&) = delete;
    ~NativeMenu();
    NativeMenu& Menu(Str label, InputAction action);
    NativeMenu& MenuWithDisabled(Str label, bool disabled, InputAction action);
    NativeMenu& Separator();
    bool IsEmpty() const { return len(items) == 0; }
};

void InputDefaultNativeMenu(const InputState* state, NativeMenu* out);
bool InputPerformNativeMenuItem(InputState* state, App* app, Window* win,
                                const NativeMenuItem& item);

} // namespace gpui
#endif // GPUI_BASE_INPUT_CORE_H_
