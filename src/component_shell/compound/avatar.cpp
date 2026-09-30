// crates/component-shell/src/shell/compound/avatar.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/avatar.h"

namespace gpui::component_shell::compound::avatar {

struct AvatarPayload {};

struct AvatarOp {
    enum Kind : uint8_t {
        Name,
        Size,
    } kind = Name;
    Str name;
    UiSize size = UiSize::Medium;
};

static bool Construct(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<AvatarPayload>();
}

static bool RecordName(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    AvatarOp* op = build->New<AvatarOp>();
    op->kind = AvatarOp::Name;
    op->name = args[0].string;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    AvatarOp* op = build->New<AvatarOp>();
    op->kind = AvatarOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

// AvatarMaterializer::component: the payload check, then every recorded
// operation folded into the builder in script order.
static component::Avatar* Component(MaterializeRequest* request) {
    if (!shell::PayloadIs<AvatarPayload>(request->payload)) {
        request->Fail(StrL("Avatar received an incompatible payload"));
        return nullptr;
    }
    component::Avatar* avatar = component::Avatar::New(request->cx);
    EachMethod<AvatarOp>(request, [&](const AvatarOp& op) {
        switch (op.kind) {
            case AvatarOp::Name:
                avatar->Name(op.name);
                break;
            case AvatarOp::Size:
                avatar->WithSize(op.size);
                break;
        }
    });
    return avatar;
}

static El* Materialize(MaterializeRequest* request) {
    if (request->ChildrenLen() != 0)
        return request->Fail(StrL("Avatar does not accept children"));
    component::Avatar* avatar = Component(request);
    if (!avatar) return nullptr;
    El* wrapper = Div(request->cx->a)->Child(avatar->IntoEl());
    return request->ApplyStyle(wrapper);
}

static constexpr ArgumentDescriptor kNameArgs[] = {{"name", SchemaString()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Avatar", {}, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"name", kNameArgs,
     "Sets the person's name and generated initials fallback.", &RecordName},
    {"size", kSizeArgs, "Sets the avatar's semantic size.", &RecordSize},
};
static constexpr ComponentDescriptor kAvatar = {
    "Avatar", kConstructors, kMethods,
    "A circular avatar with a name-derived fallback.", &Materialize};

} // namespace gpui::component_shell::compound::avatar

namespace gpui::component_shell {

bool RegisterCompoundAvatar(shell::ComponentRegistry* registry,
                            shell::RegistryError* error) {
    return registry->Register(&compound::avatar::kAvatar, error);
}

} // namespace gpui::component_shell
