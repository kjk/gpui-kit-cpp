// crates/component-shell/src/shell/media/editor.rs

#include "component_shell/families.h"
#include "component_shell/media/mod.h"
#include "ui/input.h"

namespace gpui::component_shell::media::editor {

struct Op {
    enum Kind : uint8_t {
        Appearance,
        Bordered,
        Readonly,
        AriaLabel,
    } kind = Appearance;
    bool flag = false;
    Str text;
};

bool RequireLeaf(int children, Str* error) {
    if (children == 0) return true;
    *error = StrL("Editor does not accept children");
    return false;
}

static El* Materialize(MaterializeRequest* request) {
    const ComponentArgument* argument = request->PayloadAs<ComponentArgument>();
    if (!argument)
        return request->Fail(StrL("Editor received an incompatible payload"));
    EditorStateValue* state =
        request->StateAs<EditorStateValue>(*argument, "EditorState");
    if (!state) return nullptr;
    Ctx* cx = request->cx;
    const ComponentArgument* value = argument->Some();
    uint64_t handle = value ? value->handle : 0;
    // Rust's Editor takes its element identity from the state entity; the
    // state's handle is that identity here.
    component::Editor* editor = component::Editor::New(
        cx,
        StrDup(cx->a,
               fmt("gpui-component-editor-%llu", (unsigned long long)handle)),
        &state->text.input);
    editor->Disabled(request->disabled);
    if (state->language) editor->Language(Str(state->language));
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::Appearance:
                editor->Appearance(op.flag);
                break;
            case Op::Bordered:
                editor->Bordered(op.flag);
                break;
            case Op::Readonly:
                editor->Readonly(op.flag);
                break;
            case Op::AriaLabel:
                editor->AriaLabel(op.text);
                break;
        }
    });
    Str error;
    if (!RequireLeaf(request->ChildrenLen(), &error))
        return request->Fail(error);
    return request->ApplyStyle(editor->IntoEl());
}

// ─── State and recorders ───────────────────────────────────────────────────

static bool NewEditorState(shell::StateBuild* build,
                           const ComponentArgument* args, int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::Optional) {
        return build->Fail(
            StrL("EditorState expects initial text and optional rust or json "
                 "language"));
    }
    const char* language = nullptr;
    if (const ComponentArgument* named = args[1].Some()) {
        if (named->kind != shell::ComponentArgumentKind::Enum)
            return build
                ->Fail(StrL("EditorState language expects rust or json"));
        // The literal outlives the call; the state keeps the static spelling.
        language = StrEq(named->string, "json") ? "json" : "rust";
    }
    EditorStateValue* state = build->New<EditorStateValue>();
    // EditorState::new(window, cx).default_value(value): the editor mode is
    // set before the value so its lines are kept whole.
    state->text.app = build->app;
    InputState* input = &state->text.input;
    input->kind = InputKind::Editor;
    input->mode.kind = LayoutModeKind::CodeEditor;
    input->focus = FocusHandleNew(build->app);
    if (len(args[0].string)) InputSetValue(input, args[0].string);
    state->language = language;
    return true;
}

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Entity)
        return build->Fail(StrL("Editor expects one EditorState entity"));
    *build->New<ComponentArgument>() = args[0];
    return true;
}

// media's bool_method("Editor", name, ...).
template <Op::Kind K>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    const char* name = K == Op::Appearance ? "appearance"
                       : K == Op::Bordered ? "bordered"
                                           : "readonly";
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(fmt("Editor.%s expects one boolean", Str(name)));
    Op* op = build->New<Op>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

static bool RecordAriaLabel(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        len(StrTrim(args[0].string)) == 0) {
        return build->Fail(StrL("Editor.aria_label expects non-empty text"));
    }
    Op* op = build->New<Op>();
    op->kind = Op::AriaLabel;
    op->text = args[0].string;
    return true;
}

// ─── Descriptors ───────────────────────────────────────────────────────────

static constexpr const char* kLanguageLiterals[] = {"rust", "json"};
static constexpr ArgumentSchema kLanguage = SchemaEnum(kLanguageLiterals);
static constexpr ArgumentDescriptor kStateArgs[] = {
    {"initial_value", SchemaString()},
    {"language", SchemaOptional(&kLanguage)}};

static constexpr shell::StateDescriptor kState = {
    "EditorState",
    "EditorState",
    kStateArgs,
    "Retained source-editor state initialized with local text and a syntax "
    "language.",
    &NewEditorState,
    {}};

static constexpr ArgumentDescriptor kConstructorArgs[] = {
    {"state", SchemaEntity("EditorState")}};
static constexpr ConstructorDescriptor kConstructors[] = {
    {"Editor", kConstructorArgs, &Construct}};

static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kAppearanceArgs[] = {
    {"appearance", SchemaBoolean()}};
static constexpr ArgumentDescriptor kBorderedArgs[] = {
    {"bordered", SchemaBoolean()}};
static constexpr ArgumentDescriptor kReadonlyArgs[] = {
    {"readonly", SchemaBoolean()}};
static constexpr ArgumentDescriptor kAriaLabelArgs[] = {
    {"label", SchemaString()}};

static constexpr MethodDescriptor kMethods[] = {
    // Rust records `()`: `disabled` is a common behavior the materializer
    // reads from the request.
    {"disabled", kDisabledArgs, "Disables the editor.", &RecordCommonBehavior},
    {"appearance", kAppearanceArgs, "Controls the editor appearance.",
     &RecordBool<Op::Appearance>},
    {"bordered", kBorderedArgs, "Controls the editor border.",
     &RecordBool<Op::Bordered>},
    {"readonly", kReadonlyArgs, "Controls read-only mode.",
     &RecordBool<Op::Readonly>},
    {"aria_label", kAriaLabelArgs, "Sets the editor accessibility label.",
     &RecordAriaLabel},
};

static constexpr ComponentDescriptor kEditor = {
    "Editor", kConstructors, kMethods,
    "A retained native source editor. Shell style and common disabled state "
    "are honored; children are rejected.",
    &Materialize};

} // namespace gpui::component_shell::media::editor

namespace gpui::component_shell {

bool RegisterMediaEditor(shell::ComponentRegistry* registry,
                         shell::RegistryError* error) {
    return registry->RegisterState(&media::editor::kState, error) &&
           registry->Register(&media::editor::kEditor, error);
}

} // namespace gpui::component_shell
