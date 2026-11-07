// crates/component-shell/src/shell/media/editor.rs

#include "component_shell/families.h"
#include "component_shell/media/mod.h"
#include "component_shell/text_editing.h"
#include "ui/input.h"

namespace gpui::component_shell::media::editor {

using text_editing::Op;

static constexpr char kName[] = "Editor";

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
    text_editing::ApplyOps(request, editor);
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
    return text_editing::RecordState(build, kName, "EditorState", args, count);
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
using text_editing::kAppearanceArgs;
using text_editing::kAriaLabelArgs;
using text_editing::kBorderedArgs;
using text_editing::kReadonlyArgs;

static constexpr MethodDescriptor kMethods[] = {
    // Rust records `()`: `disabled` is a common behavior the materializer
    // reads from the request.
    {"disabled", kDisabledArgs, "Disables the editor.", &RecordCommonBehavior},
    {"appearance", kAppearanceArgs, "Controls the editor appearance.",
     &text_editing::RecordBoolFor<kName, Op::Appearance>},
    {"bordered", kBorderedArgs, "Controls the editor border.",
     &text_editing::RecordBoolFor<kName, Op::Bordered>},
    {"readonly", kReadonlyArgs, "Controls read-only mode.",
     &text_editing::RecordBoolFor<kName, Op::Readonly>},
    {"aria_label", kAriaLabelArgs, "Sets the editor accessibility label.",
     &text_editing::RecordAriaLabelFor<kName>},
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
