// crates/component-shell/src/shell/structured/form.rs

#include "component_shell/families.h"
#include "component_shell/structured/mod.h"
#include "component_shell/typed_compound/mod.h"
#include "ui/form.h"

namespace gpui::component_shell::structured::form {

using typed_compound::TakeElementAs;
using typed_compound::TypedChildElement;

struct FieldPayload {};

struct FieldOp {
    enum Kind : uint8_t {
        Label,
        Description,
        Required,
        Visible,
        LabelIndent,
        Align,
        ColSpan,
    } kind = Label;
    Str text;
    bool flag = false;
    component::FieldAlign align = component::FieldAlign::Center;
    uint16_t span = 1;
};

struct FormPayload {
    bool horizontal = false;
};

struct FormOp {
    enum Kind : uint8_t {
        Columns,
        LabelWidth,
        Size,
    } kind = Columns;
    int columns = 1;
    float width = 0;
    UiSize size = UiSize::Medium;
};

static El* MaterializeField(MaterializeRequest* request) {
    if (!shell::PayloadIs<FieldPayload>(request->payload))
        return request->Fail(StrL("Field received an incompatible payload"));
    Ctx* cx = request->cx;
    component::Field* field = ArenaNew<component::Field>(cx->a);
    *field = component::Field::New();
    EachMethod<FieldOp>(request, [&](const FieldOp& op) {
        switch (op.kind) {
            case FieldOp::Label:
                field->Label(op.text);
                break;
            case FieldOp::Description:
                field->Description(op.text);
                break;
            case FieldOp::Required:
                field->Required(op.flag);
                break;
            case FieldOp::Visible:
                field->Visible(op.flag);
                break;
            case FieldOp::LabelIndent:
                field->LabelIndent(op.flag);
                break;
            case FieldOp::Align:
                field->Align(op.align);
                break;
            case FieldOp::ColSpan:
                field->ColSpan(op.span);
                break;
        }
    });
    field->refiner = request->TakeStyle();
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    field->Children(children, count);
    // A Field renders on its own when an ordinary parent holds it, with the
    // default props; a Form takes the field itself and renders it again.
    El* rendered = field->IntoEl(cx);
    return TypedChildElement(cx, shell::PayloadTag<component::Field>(), field,
                             rendered);
}

static El* MaterializeForm(MaterializeRequest* request) {
    const FormPayload* payload = request->PayloadAs<FormPayload>();
    if (!payload)
        return request->Fail(StrL("Form received an incompatible payload"));
    Ctx* cx = request->cx;
    component::Form* form =
        payload->horizontal ? component::h_form(cx) : component::v_form(cx);
    EachMethod<FormOp>(request, [&](const FormOp& op) {
        switch (op.kind) {
            case FormOp::Columns:
                form->Columns(op.columns);
                break;
            case FormOp::LabelWidth:
                form->LabelWidth(op.width);
                break;
            case FormOp::Size:
                form->WithSize(op.size);
                break;
        }
    });
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    static constexpr const char* kAllowed[] = {"Field"};
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "Form", children[i].componentName, kAllowed))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        component::Field* field =
            TakeElementAs<component::Field>(request, element, "Field");
        if (!field) return nullptr;
        form->Child(*field);
    }
    return request->ApplyStyle(form->IntoEl());
}

static bool ConstructField(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<FieldPayload>();
}

template <FieldOp::Kind K>
static bool RecordText(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(
            K == FieldOp::Label
                ? StrL("Field.label(label) expects a string")
                : StrL("Field.description(description) expects a string"));
    FieldOp* op = build->New<FieldOp>();
    op->kind = K;
    op->text = args[0].string;
    return true;
}

// bool_method("Field", name, ..).
template <FieldOp::Kind K>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean) {
        const char* name = K == FieldOp::Required  ? "required"
                           : K == FieldOp::Visible ? "visible"
                                                   : "label_indent";
        return build->Fail(fmt("Field.%s expects one boolean", Str(name)));
    }
    FieldOp* op = build->New<FieldOp>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

static bool RecordAlign(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build
            ->Fail(StrL("Field.align(align) expects an alignment literal"));
    Str value = args[0].string;
    component::FieldAlign align;
    if (StrEq(value, StrL("start")))
        align = component::FieldAlign::Start;
    else if (StrEq(value, StrL("center")))
        align = component::FieldAlign::Center;
    else if (StrEq(value, StrL("end")))
        align = component::FieldAlign::End;
    else
        return build->Fail(fmt("unsupported Field alignment `%s`", value));
    FieldOp* op = build->New<FieldOp>();
    op->kind = FieldOp::Align;
    op->align = align;
    return true;
}

static bool RecordColSpan(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(
            StrL("Field.col_span expects an exactly representable positive "
                 "integer"));
    uint16_t span = 0;
    Str error;
    if (!PositiveU16(args[0].number, StrL("Field.col_span"), &span, &error))
        return build->Fail(error);
    FieldOp* op = build->New<FieldOp>();
    op->kind = FieldOp::ColSpan;
    op->span = span;
    return true;
}

template <bool Horizontal>
static bool ConstructForm(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<FormPayload>()->horizontal = Horizontal;
    return true;
}

static bool RecordLabelWidth(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(StrL(
            "Form.label_width(width) expects a nonnegative finite number"));
    float width = 0;
    Str error;
    if (!NonnegativeF32(args[0].number, StrL("Form.label_width"), &width,
                        &error))
        return build->Fail(error);
    FormOp* op = build->New<FormOp>();
    op->kind = FormOp::LabelWidth;
    op->width = width;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(StrL("Form.size(size) expects a size literal"));
    bool known = false;
    for (const char* literal : kSizeLiterals)
        known = known || StrEq(args[0].string, Str(literal));
    if (!known)
        return build->Fail(fmt("unsupported Form size `%s`", args[0].string));
    FormOp* op = build->New<FormOp>();
    op->kind = FormOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kDescriptionArgs[] = {
    {"description", SchemaString()}};
static constexpr ArgumentDescriptor kRequiredArgs[] = {
    {"required", SchemaBoolean()}};
static constexpr ArgumentDescriptor kVisibleArgs[] = {
    {"visible", SchemaBoolean()}};
static constexpr ArgumentDescriptor kLabelIndentArgs[] = {
    {"label_indent", SchemaBoolean()}};
static constexpr const char* kAlignLiterals[] = {"start", "center", "end"};
static constexpr ArgumentDescriptor kAlignArgs[] = {
    {"align", SchemaEnum(kAlignLiterals)}};
static constexpr ArgumentDescriptor kSpanArgs[] = {{"span", SchemaNumber()}};

static constexpr ConstructorDescriptor kFieldConstructors[] = {
    {"Field", {}, &ConstructField}};
static constexpr MethodDescriptor kFieldMethods[] = {
    {"label", kLabelArgs, "Sets the field label.", &RecordText<FieldOp::Label>},
    {"description", kDescriptionArgs, "Sets supporting text below the control.",
     &RecordText<FieldOp::Description>},
    {"required", kRequiredArgs, "Marks the field as required.",
     &RecordBool<FieldOp::Required>},
    {"visible", kVisibleArgs, "Controls field visibility.",
     &RecordBool<FieldOp::Visible>},
    {"label_indent", kLabelIndentArgs,
     "Keeps unlabeled horizontal fields aligned with labeled fields.",
     &RecordBool<FieldOp::LabelIndent>},
    {"align", kAlignArgs, "Aligns the label and control within the field.",
     &RecordAlign},
    {"col_span", kSpanArgs, "Sets the field's grid-column span.",
     &RecordColSpan},
};
static constexpr ComponentDescriptor kField = {
    "Field", kFieldConstructors, kFieldMethods,
    "A typed form field containing ordinary control children.",
    &MaterializeField};

static constexpr ArgumentDescriptor kColumnsArgs[] = {
    {"columns", SchemaNumber()}};
static constexpr ArgumentDescriptor kWidthArgs[] = {{"width", SchemaNumber()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ConstructorDescriptor kFormConstructors[] = {
    {"Form", {}, &ConstructForm<false>},
    {"VForm", {}, &ConstructForm<false>},
    {"HForm", {}, &ConstructForm<true>},
};
static constexpr MethodDescriptor kFormMethods[] = {
    {"columns", kColumnsArgs, "Sets the form grid's column count.",
     &RecordFormColumns},
    {"label_width", kWidthArgs,
     "Sets the horizontal form label width in pixels.", &RecordLabelWidth},
    {"size", kSizeArgs, "Sets the form density.", &RecordSize},
};
static constexpr ComponentDescriptor kForm = {
    "Form", kFormConstructors, kFormMethods,
    "A vertical or horizontal form accepting Field children.",
    &MaterializeForm};

} // namespace gpui::component_shell::structured::form

namespace gpui::component_shell::structured {

// form_columns.
bool RecordFormColumns(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(StrL(
            "Form.columns expects an exactly representable positive integer"));
    uint16_t columns = 0;
    Str error;
    if (!PositiveU16(args[0].number, StrL("Form.columns"), &columns, &error))
        return build->Fail(error);
    form::FormOp* op = build->New<form::FormOp>();
    op->kind = form::FormOp::Columns;
    op->columns = columns;
    return true;
}

} // namespace gpui::component_shell::structured

namespace gpui::component_shell {

bool RegisterStructuredForm(shell::ComponentRegistry* registry,
                            shell::RegistryError* error) {
    return registry->Register(&structured::form::kField, error) &&
           registry->Register(&structured::form::kForm, error);
}

} // namespace gpui::component_shell
