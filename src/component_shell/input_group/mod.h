#ifndef GPUI_COMPONENT_SHELL_INPUT_GROUP_MOD_H_
#define GPUI_COMPONENT_SHELL_INPUT_GROUP_MOD_H_

// crates/component-shell/src/shell/input_group/mod.rs: what its binding.rs
// and content_type.rs share with it — the one operation type every part
// records, and the two submodules' methods.

#include "component_shell/support.h"
#include "ui/input.h"

namespace gpui::component_shell::input_group {

// binding.rs TextareaLayout: Rows(rows) or AutoGrow(min, max).
struct TextareaLayout {
    bool autoGrow = false;
    int rows = 0;
    int minRows = 0;
    int maxRows = 0;

    bool operator==(const TextareaLayout& other) const {
        return autoGrow == other.autoGrow && rows == other.rows &&
               minRows == other.minRows && maxRows == other.maxRows;
    }
    bool operator!=(const TextareaLayout& other) const {
        return !(*this == other);
    }
};

// Op, one variant per field group.
struct Op {
    enum Kind : uint8_t {
        Addon,
        Align,
        Size,
        Variant,
        Outline,
        Readonly,
        Invalid,
        FocusRing,
        Masked,
        Loading,
        Label,
        Icon,
        AriaLabel,
        AccessibilityId,
        Tooltip,
        Value,
        Placeholder,
        ContentType,
        Layout,
        OnChange,
    } kind = Addon;
    // Addon's element, OnChange's callback.
    ComponentArgument argument = {};
    Str text;
    bool flag = false;
    UiSize size = UiSize::Medium;
    component::ButtonVariant variant = component::ButtonVariant::Default;
    component::InputGroupAddonAlignment align =
        component::InputGroupAddonAlignment::InlineStart;
    component::InputContentType contentType = component::InputContentType::Name;
    TextareaLayout layout = {};
};

} // namespace gpui::component_shell::input_group

// ─── binding.rs ────────────────────────────────────────────────────────────

namespace gpui::component_shell::input_group::binding {

bool RecordRows(PayloadBuild* build, const ComponentArgument* args, int count);
bool RecordAutoGrow(PayloadBuild* build, const ComponentArgument* args,
                    int count);

// rows(): a positive whole row count, or "textarea rows must be a positive
// integer". Exposed for the tests.
bool Rows(const ComponentArgument& argument, int* out, Str* error);

inline constexpr ArgumentDescriptor kRowsArguments[] = {
    {"rows", SchemaNumber()}};
inline constexpr ArgumentDescriptor kAutoGrowArguments[] = {
    {"min_rows", SchemaNumber()},
    {"max_rows", SchemaNumber()}};

// layout_methods().
inline constexpr MethodDescriptor kRowsMethod = {
    "rows", kRowsArguments,
    "Sets a fixed row count on TextareaState. Use h(...) for an explicit "
    "viewport height.",
    &RecordRows};
inline constexpr MethodDescriptor kAutoGrowMethod = {
    "auto_grow", kAutoGrowArguments,
    "Grows the native textarea between the minimum and maximum row counts.",
    &RecordAutoGrow};

// What `prepare` reads from a control's operations: the last of each.
struct Binding {
    InputState* state = nullptr;
    uint64_t handle = 0;
    // The control's place in the description, which with the state names
    // its change host, as Rust's keyed state is the path and the state.
    Str elementId;
    bool textarea = false;
    shell::ComponentCallback change = {};
    bool hasValue = false;
    Str value;
    bool hasPlaceholder = false;
    Str placeholder;
    bool hasLayout = false;
    TextareaLayout layout = {};
    bool hasMasked = false;
    bool masked = false;
};

bool Prepare(MaterializeRequest* request, InputState* state, uint64_t handle,
             bool textarea, Binding* out);
// Binding::apply: retains the change subscription and reasserts the
// controlled value, placeholder, masking and layout.
void Apply(const Binding& binding, Ctx* cx);

} // namespace gpui::component_shell::input_group::binding

// ─── content_type.rs ───────────────────────────────────────────────────────

namespace gpui::component_shell::input_group::content_type {

bool Record(PayloadBuild* build, const ComponentArgument* args, int count);

extern const char* const kLiterals[45];

inline constexpr ArgumentDescriptor kArguments[] = {
    {"content_type", SchemaEnum(kLiterals)}};

// method().
inline constexpr MethodDescriptor kMethod = {
    "content_type", kArguments,
    "Sets the native content type; names follow InputContentType in "
    "snake_case.",
    &Record};

} // namespace gpui::component_shell::input_group::content_type
#endif // GPUI_COMPONENT_SHELL_INPUT_GROUP_MOD_H_
