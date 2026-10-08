// crates/component-shell/src/shell/media/diff.rs

#include "component_shell/families.h"
#include "component_shell/media/mod.h"
#include "ui/diff.h"

namespace gpui::component_shell::media::diff {

struct DiffStateValue {
    Entity<component::DiffState> state;
};

struct Op {
    enum Kind : uint8_t {
        LineNumber,
        SyntaxHighlight,
        HeaderVisible,
        ChangeBackground,
        SoftWrap,
        HunkSeparator,
        ChangeIndicator,
    } kind = LineNumber;
    bool flag = false;
    component::DiffHunkSeparator separator =
        component::DiffHunkSeparator::Metadata;
    component::DiffChangeIndicator indicator =
        component::DiffChangeIndicator::Signs;
};

bool RequireLeaf(int children, Str* error) {
    if (children == 0) return true;
    *error = StrL("Diff does not accept children");
    return false;
}

static El* Materialize(MaterializeRequest* request) {
    const ComponentArgument* argument = request->PayloadAs<ComponentArgument>();
    if (!argument)
        return request->Fail(StrL("Diff received an incompatible payload"));
    DiffStateValue* held =
        request->StateAs<DiffStateValue>(*argument, "DiffState");
    if (!held) return nullptr;
    Ctx* cx = request->cx;
    component::Diff* view = component::Diff::New(cx, held->state);
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::LineNumber:
                view->LineNumber(op.flag);
                break;
            case Op::SyntaxHighlight:
                view->SyntaxHighlight(op.flag);
                break;
            case Op::HeaderVisible:
                view->HeaderVisible(op.flag);
                break;
            case Op::ChangeBackground:
                view->ChangeBackground(op.flag);
                break;
            case Op::SoftWrap:
                view->SoftWrap(op.flag);
                break;
            case Op::HunkSeparator:
                view->HunkSeparator(op.separator);
                break;
            case Op::ChangeIndicator:
                view->ChangeIndicator(op.indicator);
                break;
        }
    });
    Str error;
    if (!RequireLeaf(request->ChildrenLen(), &error))
        return request->Fail(error);
    return request->ApplyStyle(view->IntoEl());
}

static bool NewDiffState(shell::StateBuild* build,
                         const ComponentArgument* args, int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::Optional) {
        return build->Fail(StrL(
            "DiffState expects a unified or Git patch and an optional mode"));
    }
    component::DiffMode mode = component::DiffMode::Unified;
    if (const ComponentArgument* named = args[1].Some()) {
        if (named->kind != shell::ComponentArgumentKind::Enum)
            return build->Fail(StrL("DiffState mode expects unified or split"));
        if (StrEq(named->string, "split"))
            mode = component::DiffMode::Split;
        else if (!StrEq(named->string, "unified"))
            return build->Fail(StrL("DiffState mode expects unified or split"));
    }
    Vec<component::DiffFile*> files;
    component::DiffParseError error;
    if (!component::DiffFile::Parse(args[0].string, &files, &error)) {
        for (int i = 0; i < len(files); i++) delete files[i];
        return build
            ->Fail(error.message.len ? error.message : StrL("invalid diff"));
    }
    DiffStateValue* held = build->New<DiffStateValue>();
    held->state = EntityNew<component::DiffState>(build->app);
    component::DiffState* state = held->state.Get(build->app);
    state->Bind(build->app, held->state);
    state->WithMode(mode);
    state->SetFiles(files.els, len(files), nullptr);
    return true;
}

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Entity)
        return build->Fail(StrL("Diff expects one DiffState entity"));
    *build->New<ComponentArgument>() = args[0];
    return true;
}

template <Op::Kind K>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    const char* name = "line_number";
    if constexpr (K == Op::SyntaxHighlight)
        name = "syntax_highlight";
    else if constexpr (K == Op::HeaderVisible)
        name = "header_visible";
    else if constexpr (K == Op::ChangeBackground)
        name = "change_background";
    else if constexpr (K == Op::SoftWrap)
        name = "soft_wrap";
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(fmt("Diff.%s expects one boolean", Str(name)));
    Op* op = build->New<Op>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

static bool RecordSeparator(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(
            StrL("Diff.hunk_separator expects metadata, line_info or simple"));
    Op* op = build->New<Op>();
    op->kind = Op::HunkSeparator;
    if (StrEq(args[0].string, "metadata"))
        op->separator = component::DiffHunkSeparator::Metadata;
    else if (StrEq(args[0].string, "line_info"))
        op->separator = component::DiffHunkSeparator::LineInfo;
    else if (StrEq(args[0].string, "simple"))
        op->separator = component::DiffHunkSeparator::Simple;
    else
        return build->Fail(
            StrL("Diff.hunk_separator expects metadata, line_info or simple"));
    return true;
}

static bool RecordIndicator(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build
            ->Fail(StrL("Diff.change_indicator expects signs, bars or none"));
    Op* op = build->New<Op>();
    op->kind = Op::ChangeIndicator;
    if (StrEq(args[0].string, "signs"))
        op->indicator = component::DiffChangeIndicator::Signs;
    else if (StrEq(args[0].string, "bars"))
        op->indicator = component::DiffChangeIndicator::Bars;
    else if (StrEq(args[0].string, "none"))
        op->indicator = component::DiffChangeIndicator::None;
    else
        return build
            ->Fail(StrL("Diff.change_indicator expects signs, bars or none"));
    return true;
}

static constexpr const char* kModeLiterals[] = {"unified", "split"};
static constexpr ArgumentSchema kMode = SchemaEnum(kModeLiterals);
static constexpr ArgumentDescriptor kStateArgs[] = {
    {"patch", SchemaString()},
    {"mode", SchemaOptional(&kMode)},
};
static constexpr shell::StateDescriptor kState = {
    "DiffState",
    "DiffState",
    kStateArgs,
    "Retained readonly patch state parsed from unified or Git diff text, in "
    "unified or split mode.",
    &NewDiffState,
    {},
};

static constexpr ArgumentDescriptor kConstructorArgs[] = {
    {"state", SchemaEntity("DiffState")}};
static constexpr ConstructorDescriptor kConstructors[] = {
    {"Diff", kConstructorArgs, &Construct}};

static constexpr ArgumentDescriptor kLineArgs[] = {
    {"line_number", SchemaBoolean()}};
static constexpr ArgumentDescriptor kSyntaxArgs[] = {
    {"syntax_highlight", SchemaBoolean()}};
static constexpr ArgumentDescriptor kHeaderArgs[] = {
    {"header_visible", SchemaBoolean()}};
static constexpr ArgumentDescriptor kBgArgs[] = {
    {"change_background", SchemaBoolean()}};
static constexpr ArgumentDescriptor kWrapArgs[] = {
    {"soft_wrap", SchemaBoolean()}};
static constexpr const char* kSeparatorLiterals[] = {"metadata", "line_info",
                                                     "simple"};
static constexpr ArgumentSchema kSeparator = SchemaEnum(kSeparatorLiterals);
static constexpr ArgumentDescriptor kSeparatorArgs[] = {
    {"hunk_separator", kSeparator}};
static constexpr const char* kIndicatorLiterals[] = {"signs", "bars", "none"};
static constexpr ArgumentSchema kIndicator = SchemaEnum(kIndicatorLiterals);
static constexpr ArgumentDescriptor kIndicatorArgs[] = {
    {"change_indicator", kIndicator}};

static constexpr MethodDescriptor kMethods[] = {
    {"line_number", kLineArgs, "Shows line numbers.",
     &RecordBool<Op::LineNumber>},
    {"syntax_highlight", kSyntaxArgs, "Emphasizes syntax.",
     &RecordBool<Op::SyntaxHighlight>},
    {"header_visible", kHeaderArgs, "Shows a header above each file.",
     &RecordBool<Op::HeaderVisible>},
    {"change_background", kBgArgs, "Tints changed lines.",
     &RecordBool<Op::ChangeBackground>},
    {"soft_wrap", kWrapArgs, "Wraps long lines at the column width.",
     &RecordBool<Op::SoftWrap>},
    {"hunk_separator", kSeparatorArgs, "Marks the start of each hunk.",
     &RecordSeparator},
    {"change_indicator", kIndicatorArgs, "Marks changed lines beside the code.",
     &RecordIndicator},
};

static constexpr ComponentDescriptor kDiff = {
    "Diff",
    kConstructors,
    kMethods,
    "A readonly patch viewer for one or more files. Shell style is honored; "
    "children are rejected.",
    &Materialize,
};

} // namespace gpui::component_shell::media::diff

namespace gpui::component_shell {

bool RegisterMediaDiff(shell::ComponentRegistry* registry,
                       shell::RegistryError* error) {
    return registry->RegisterState(&media::diff::kState, error) &&
           registry->Register(&media::diff::kDiff, error);
}

} // namespace gpui::component_shell
