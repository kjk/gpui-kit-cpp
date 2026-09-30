// crates/component-shell/src/shell/empty.rs

#include "component_shell/families.h"
#include "component_shell/typed_compound/mod.h"
#include "ui/empty.h"

namespace gpui::component_shell::empty {

using typed_compound::FinishPart;
using typed_compound::TakeElementAs;
using typed_compound::TypedChildElementOf;

enum class Part : uint8_t {
    Root,
    Header,
    Media,
    Title,
    Description,
    Content,
};

enum class Slot : uint8_t {
    Media,
    Title,
    Description,
};

struct PartPayload {
    Part part = Part::Root;
};

struct EmptyOp {
    enum Kind : uint8_t {
        Slot,
        Variant,
    } kind = Slot;
    empty::Slot slot = empty::Slot::Media;
    ComponentArgument argument = {};
    component::EmptyMediaVariant variant =
        component::EmptyMediaVariant::Default;
};

// Resolve only the final value: overwritten slots must not be materialized.
template <class T>
static bool SlotValue(MaterializeRequest* request, Slot slot, const char* name,
                      T** out) {
    *out = nullptr;
    const ComponentArgument* last = nullptr;
    EachMethod<EmptyOp>(request, [&](const EmptyOp& op) {
        if (op.kind == EmptyOp::Slot && op.slot == slot) last = &op.argument;
    });
    if (!last) return true;
    El* element = request->ResolveElement(*last);
    if (!element) return false;
    *out = TakeElementAs<T>(request, element, name);
    return *out != nullptr;
}

// `header` and `content` are slots the prelude installs on every element, so
// they arrive through the common slot lane rather than as recorded methods.
// Resolve only the final value: overwritten slots must not be rendered.
template <class T>
static bool CommonSlot(MaterializeRequest* request, const char* slot,
                       const char* name, T** out) {
    *out = nullptr;
    // TakeSlot drains one value per call, so draining to the end is what
    // finds the last.
    El* last = nullptr;
    while (El* element = request->TakeSlot(slot)) last = element;
    if (!last) return true;
    *out = TakeElementAs<T>(request, last, name);
    return *out != nullptr;
}

static El* Materialize(MaterializeRequest* request) {
    const PartPayload* payload = request->PayloadAs<PartPayload>();
    if (!payload)
        return request->Fail(StrL("Empty received an incompatible payload"));
    Ctx* cx = request->cx;
    switch (payload->part) {
        case Part::Root: {
            component::EmptyHeader* header = nullptr;
            component::EmptyContent* content = nullptr;
            if (!CommonSlot(request, "header", "EmptyHeader", &header))
                return nullptr;
            if (!CommonSlot(request, "content", "EmptyContent", &content))
                return nullptr;
            component::Empty* empty = component::Empty::New(cx);
            if (header) empty->Header(header);
            if (content) empty->Content(content);
            // request.finish: ordinary children follow the header and
            // content, and the style refines the root.
            El** children = nullptr;
            int count = 0;
            if (!request->TakeChildren(&children, &count)) return nullptr;
            for (int i = 0; i < count; i++) empty->Child(children[i]);
            return request->ApplyStyle(empty->IntoEl());
        }
        case Part::Header: {
            if (request->ChildrenLen() != 0)
                return request->Fail(
                    StrL("EmptyHeader does not accept children; use media, "
                         "title, and description"));
            component::EmptyMedia* media = nullptr;
            component::EmptyTitle* title = nullptr;
            component::EmptyDescription* description = nullptr;
            if (!SlotValue(request, Slot::Media, "EmptyMedia", &media))
                return nullptr;
            if (!SlotValue(request, Slot::Title, "EmptyTitle", &title))
                return nullptr;
            if (!SlotValue(request, Slot::Description, "EmptyDescription",
                           &description))
                return nullptr;
            component::EmptyHeader* header = component::EmptyHeader::New(cx);
            if (media) header->Media(media);
            if (title) header->Title(title);
            if (description) header->Description(description);
            header->refiner = request->TakeStyle();
            return TypedChildElementOf(cx, header);
        }
        case Part::Media: {
            bool hasVariant = false;
            component::EmptyMediaVariant variant =
                component::EmptyMediaVariant::Default;
            EachMethod<EmptyOp>(request, [&](const EmptyOp& op) {
                if (op.kind != EmptyOp::Variant) return;
                hasVariant = true;
                variant = op.variant;
            });
            component::EmptyMedia* media = component::EmptyMedia::New(cx);
            if (hasVariant) media->WithVariant(variant);
            return FinishPart(request, media);
        }
        case Part::Title:
            return FinishPart(request, component::EmptyTitle::New(cx));
        case Part::Description:
            return FinishPart(request, component::EmptyDescription::New(cx));
        case Part::Content:
            return FinishPart(request, component::EmptyContent::New(cx));
    }
    return nullptr;
}

template <Part P>
static bool Construct(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<PartPayload>()->part = P;
    return true;
}

template <Slot S>
static bool RecordSlot(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    const char* name = S == Slot::Media   ? "media"
                       : S == Slot::Title ? "title"
                                          : "description";
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Element)
        return build
            ->Fail(fmt("%s expects one registered Empty part", Str(name)));
    EmptyOp* op = build->New<EmptyOp>();
    op->kind = EmptyOp::Slot;
    op->slot = S;
    op->argument = args[0];
    return true;
}

static bool RecordVariant(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    Str value = args[0].string;
    component::EmptyMediaVariant variant;
    if (StrEq(value, "default")) {
        variant = component::EmptyMediaVariant::Default;
    } else if (StrEq(value, "icon")) {
        variant = component::EmptyMediaVariant::Icon;
    } else {
        return build->Fail(StrL("EmptyMedia.variant expects default or icon"));
    }
    EmptyOp* op = build->New<EmptyOp>();
    op->kind = EmptyOp::Variant;
    op->variant = variant;
    return true;
}

static constexpr ArgumentDescriptor kMediaArgs[] = {{"media", SchemaElement()}};
static constexpr ArgumentDescriptor kTitleArgs[] = {{"title", SchemaElement()}};
static constexpr ArgumentDescriptor kDescriptionArgs[] = {
    {"description", SchemaElement()}};
static constexpr const char* kVariantLiterals[] = {"default", "icon"};
static constexpr ArgumentDescriptor kVariantArgs[] = {
    {"variant", SchemaEnum(kVariantLiterals)}};

static constexpr ConstructorDescriptor kRootConstructors[] = {
    {"Empty", {}, &Construct<Part::Root>}};
static constexpr ConstructorDescriptor kHeaderConstructors[] = {
    {"EmptyHeader", {}, &Construct<Part::Header>}};
static constexpr ConstructorDescriptor kMediaConstructors[] = {
    {"EmptyMedia", {}, &Construct<Part::Media>}};
static constexpr ConstructorDescriptor kTitleConstructors[] = {
    {"EmptyTitle", {}, &Construct<Part::Title>}};
static constexpr ConstructorDescriptor kDescriptionConstructors[] = {
    {"EmptyDescription", {}, &Construct<Part::Description>}};
static constexpr ConstructorDescriptor kContentConstructors[] = {
    {"EmptyContent", {}, &Construct<Part::Content>}};

static constexpr MethodDescriptor kHeaderMethods[] = {
    {"media", kMediaArgs, "Sets an EmptyMedia, replacing the previous media.",
     &RecordSlot<Slot::Media>},
    {"title", kTitleArgs, "Sets an EmptyTitle, replacing the previous title.",
     &RecordSlot<Slot::Title>},
    {"description", kDescriptionArgs,
     "Sets an EmptyDescription, replacing the previous description.",
     &RecordSlot<Slot::Description>},
};
static constexpr MethodDescriptor kMediaMethods[] = {
    {"variant", kVariantArgs,
     "Sets the unframed default treatment or a muted icon frame.",
     &RecordVariant},
};

static constexpr ComponentDescriptor kParts[] = {
    {"Empty",
     kRootConstructors,
     {},
     "A stateless empty state. Ordinary children follow the header and "
     "content slots.",
     &Materialize},
    {"EmptyHeader", kHeaderConstructors, kHeaderMethods,
     "An independently styled header. Accepts only named media, title, and "
     "description slots, in that order.",
     &Materialize},
    {"EmptyMedia", kMediaConstructors, kMediaMethods,
     "A centered media part accepting arbitrary children such as an icon, "
     "avatar, or image.",
     &Materialize},
    {"EmptyTitle",
     kTitleConstructors,
     {},
     "An independently styled title with arbitrary children.",
     &Materialize},
    {"EmptyDescription",
     kDescriptionConstructors,
     {},
     "An independently styled, wrapping description with arbitrary children.",
     &Materialize},
    {"EmptyContent",
     kContentConstructors,
     {},
     "An independently styled content column for application-owned controls "
     "and actions.",
     &Materialize},
};

} // namespace gpui::component_shell::empty

namespace gpui::component_shell {

bool RegisterEmpty(shell::ComponentRegistry* registry,
                   shell::RegistryError* error) {
    for (const ComponentDescriptor& part : empty::kParts) {
        if (!registry->Register(&part, error)) return false;
    }
    return true;
}

} // namespace gpui::component_shell
