// crates/component-shell/src/shell/display/group_box.rs

#include "component_shell/families.h"
#include "component_shell/display/common.h"
#include "ui/group_box.h"

namespace gpui::component_shell::display::group_box {

using component::GroupBoxVariant;

struct GroupBoxPayload {};

struct GroupBoxOp {
    enum Kind : uint8_t {
        Title,
        Variant,
    } kind = Title;
    Str title;
    GroupBoxVariant variant = GroupBoxVariant::Normal;
};

static bool Construct(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<GroupBoxPayload>();
}

static bool RecordTitle(PayloadBuild* build, const ComponentArgument* args,
                        int) {
    GroupBoxOp* op = build->New<GroupBoxOp>();
    op->kind = GroupBoxOp::Title;
    op->title = args[0].string;
    return true;
}

static bool RecordVariant(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    GroupBoxOp* op = build->New<GroupBoxOp>();
    op->kind = GroupBoxOp::Variant;
    op->variant = StrEq(args[0].string, "fill")      ? GroupBoxVariant::Fill
                  : StrEq(args[0].string, "outline") ? GroupBoxVariant::Outline
                                                     : GroupBoxVariant::Normal;
    return true;
}

static component::GroupBox* Component(MaterializeRequest* request) {
    if (!shell::PayloadIs<GroupBoxPayload>(request->payload)) {
        request->Fail(StrL("GroupBox received an incompatible payload"));
        return nullptr;
    }
    component::GroupBox* box = component::GroupBox::New(request->cx);
    EachMethod<GroupBoxOp>(request, [&](const GroupBoxOp& op) {
        switch (op.kind) {
            case GroupBoxOp::Title:
                // GroupBox::title(String): the text is the title element.
                box->title = op.title;
                box->titleEl = nullptr;
                box->hasTitle = true;
                break;
            case GroupBoxOp::Variant:
                box->WithVariant(op.variant);
                break;
        }
    });
    return box;
}

static El* Materialize(MaterializeRequest* request) {
    component::GroupBox* box = Component(request);
    if (!box) return nullptr;
    // request.finish(component): the children fill the group's content and
    // the style refines its root.
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) box->Child(children[i]);
    return request->ApplyStyle(box->IntoEl());
}

static constexpr const char* kVariantLiterals[] = {"normal", "fill", "outline"};
static constexpr ArgumentDescriptor kTitleArgs[] = {{"title", SchemaString()}};
static constexpr ArgumentDescriptor kVariantArgs[] = {
    {"variant", SchemaEnum(kVariantLiterals)}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"GroupBox", {}, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"title", kTitleArgs, "Sets the group title.", &RecordTitle},
    {"variant", kVariantArgs, "Sets the normal, fill, or outline presentation.",
     &RecordVariant},
};
static constexpr ComponentDescriptor kGroupBox = {
    "GroupBox", kConstructors, kMethods,
    "A titled container for grouping related content.", &Materialize};

} // namespace gpui::component_shell::display::group_box

namespace gpui::component_shell {

bool RegisterDisplayGroupBox(shell::ComponentRegistry* registry,
                             shell::RegistryError* error) {
    return registry->Register(&display::group_box::kGroupBox, error);
}

} // namespace gpui::component_shell
