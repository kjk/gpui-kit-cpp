// crates/component-shell/src/shell/controls/text.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "shell/capability.h"
#include "shell/view.h"
#include "ui/kbd.h"
#include "ui/label.h"
#include "ui/link.h"
#include "ui/live_region.h"

namespace gpui::component_shell::controls::text {

struct StringPayload {
    Str value;
};

struct LiveRegionPayload {
    Str id;
    Str text;
};

struct LabelOp {
    enum Kind : uint8_t {
        Secondary,
        Masked,
        Highlights,
    } kind = Secondary;
    Str text;
    bool masked = false;
};

struct LinkOp {
    Str href;
};

struct KbdOp {
    enum Kind : uint8_t {
        Appearance,
        Outline,
    } kind = Appearance;
    bool appearance = true;
};

// string_constructor(export, argument): one non-empty string.
static bool ConstructString(PayloadBuild* build, const ComponentArgument* args,
                            const char* exportName, const char* argument) {
    if (len(args[0].string) == 0)
        return build->Fail(
            fmt("%s %s must not be empty", Str(exportName), Str(argument)));
    build->New<StringPayload>()->value = args[0].string;
    return true;
}

static bool ConstructLabel(PayloadBuild* build, const ComponentArgument* args,
                           int) {
    return ConstructString(build, args, "Label", "text");
}

static bool ConstructLiveRegion(PayloadBuild* build,
                                const ComponentArgument* args, int) {
    if (len(args[0].string) == 0) {
        return build->Fail(StrL("LiveRegion id must not be empty"));
    }
    *build->New<LiveRegionPayload>() = {args[0].string, args[1].string};
    return true;
}

static bool RecordAssertive(PayloadBuild* build, const ComponentArgument*,
                            int) {
    *build->New<bool>() = true;
    return true;
}

static bool ConstructLink(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    return ConstructString(build, args, "Link", "id");
}

static bool ConstructKbd(PayloadBuild* build, const ComponentArgument* args,
                         int) {
    component::Keystroke stroke;
    if (!component::KeystrokeParse(build->a, args[0].string, &stroke))
        return build->Fail(
            fmt("invalid Kbd keystroke: %s",
                Str(component::KeystrokeParseErrorTemp(args[0].string))));
    *build->New<component::Keystroke>() = stroke;
    return true;
}

template <LabelOp::Kind K>
static bool RecordLabelString(PayloadBuild* build,
                              const ComponentArgument* args, int) {
    LabelOp* op = build->New<LabelOp>();
    op->kind = K;
    op->text = args[0].string;
    return true;
}

static bool RecordMasked(PayloadBuild* build, const ComponentArgument* args,
                         int) {
    LabelOp* op = build->New<LabelOp>();
    op->kind = LabelOp::Masked;
    op->masked = args[0].boolean;
    return true;
}

static bool RecordHref(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    if (!IsOpenableUrl(args[0].string)) {
        return build->Fail(
            StrL("Link.href(url) expects an absolute HTTP(S) URL with a host"));
    }
    build->New<LinkOp>()->href = args[0].string;
    return true;
}

static bool RecordAppearance(PayloadBuild* build, const ComponentArgument* args,
                             int) {
    KbdOp* op = build->New<KbdOp>();
    op->kind = KbdOp::Appearance;
    op->appearance = args[0].boolean;
    return true;
}

static bool RecordKbdOutline(PayloadBuild* build, const ComponentArgument*,
                             int) {
    build->New<KbdOp>()->kind = KbdOp::Outline;
    return true;
}

// LabelMaterializer. Label declares on_click and disabled, and Rust's
// materializer reads neither; neither does this one.
static El* MaterializeLabel(MaterializeRequest* request) {
    const StringPayload* text = request->PayloadAs<StringPayload>();
    if (!text)
        return request->Fail(StrL("Label received an incompatible payload"));
    Ctx* cx = request->cx;
    component::Label* label = component::Label::New(cx, text->value)
                                  ->Id(request->elementId);
    EachMethod<LabelOp>(request, [&](const LabelOp& op) {
        switch (op.kind) {
            case LabelOp::Secondary:
                label->Secondary(op.text);
                break;
            case LabelOp::Masked:
                label->Masked(op.masked);
                break;
            case LabelOp::Highlights:
                label->Highlights(op.text);
                break;
        }
    });
    return request->Finish(Div(cx->a)->Child(label->IntoEl()));
}

static El* MaterializeLiveRegion(MaterializeRequest* request) {
    if (request->ChildrenLen() != 0) {
        return request
            ->Fail(StrL("LiveRegion does not accept children; pass its text to "
                        "the constructor"));
    }
    const auto* payload = request->PayloadAs<LiveRegionPayload>();
    if (!payload) {
        return request
            ->Fail(StrL("LiveRegion received an incompatible payload"));
    }
    auto* region =
        component::LiveRegion::New(request->cx, payload->id, payload->text);
    EachMethod<bool>(request, [&](bool assertive) {
        if (assertive) region->Assertive();
    });
    return request->Finish(region->IntoEl());
}

struct OpenHref {
    Str href;
};

static void RunOpenHref(OpenHref* open) {
    if (open && open->href.s) OpenUrl(open->href);
}

static El* MaterializeLink(MaterializeRequest* request) {
    const StringPayload* id = request->PayloadAs<StringPayload>();
    if (!id)
        return request->Fail(StrL("Link received an incompatible payload"));
    Ctx* cx = request->cx;
    component::Link* link = component::Link::New(cx, id->value)
                                ->Disabled(request->disabled);
    EachMethod<LinkOp>(request, [&](const LinkOp& op) { link->Href(op.href); });
    if (request->onClick)
        link->OnOpen(
            Listen(cx, &ScriptView::OnClick, (int64_t)request->onClick));
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) link->Child(children[i]);
    El* element = link->IntoEl();
    // link.rs opens the href itself before running on_click; base's Link
    // leaves navigation to its caller. `disabled` is inert there, so a
    // disabled link opens it too.
    if (link->href.s) {
        OpenHref* open = ArenaNew<OpenHref>(cx->a);
        open->href = link->href;
        element->OnClick(MkFunc0(&RunOpenHref, open));
    }
    return request->ApplyStyle(element);
}

static El* MaterializeKbd(MaterializeRequest* request) {
    const component::Keystroke* stroke =
        request->PayloadAs<component::Keystroke>();
    if (!stroke)
        return request->Fail(StrL("Kbd received an incompatible payload"));
    Ctx* cx = request->cx;
    component::Kbd* kbd = component::Kbd::New(cx, *stroke);
    EachMethod<KbdOp>(request, [&](const KbdOp& op) {
        switch (op.kind) {
            case KbdOp::Appearance:
                kbd->Appearance(op.appearance);
                break;
            case KbdOp::Outline:
                kbd->Outline();
                break;
        }
    });
    return request->Finish(Div(cx->a)->Child(kbd->IntoEl()));
}

static constexpr ArgumentDescriptor kTextArgs[] = {{"text", SchemaString()}};
static constexpr ArgumentDescriptor kKeystrokeArgs[] = {
    {"keystroke", SchemaString()}};
static constexpr ArgumentDescriptor kSecondaryArgs[] = {
    {"secondary", SchemaString()}};
static constexpr ArgumentDescriptor kMaskedArgs[] = {
    {"masked", SchemaBoolean()}};
static constexpr ArgumentDescriptor kHighlightsArgs[] = {
    {"highlights", SchemaString()}};
static constexpr ArgumentDescriptor kHrefArgs[] = {{"href", SchemaString()}};
static constexpr ArgumentDescriptor kAppearanceArgs[] = {
    {"appearance", SchemaBoolean()}};

static constexpr ConstructorDescriptor kLabelConstructors[] = {
    {"Label", kTextArgs, &ConstructLabel}};
static constexpr MethodDescriptor kLabelMethods[] = {
    kOnClickMethod,
    kDisabledMethod,
    {"secondary", kSecondaryArgs, "Adds muted secondary text.",
     &RecordLabelString<LabelOp::Secondary>},
    {"masked", kMaskedArgs, "Controls whether the main text is masked.",
     &RecordMasked},
    {"highlights", kHighlightsArgs, "Highlights matching text fragments.",
     &RecordLabelString<LabelOp::Highlights>},
};
static constexpr ComponentDescriptor kLabel = {
    "Label", kLabelConstructors, kLabelMethods,
    "A text label with optional secondary, masking, and highlight "
    "presentation.",
    &MaterializeLabel};

static constexpr ArgumentDescriptor kLiveRegionArgs[] = {
    {"id", SchemaString()},
    {"text", SchemaString()}};
static constexpr ConstructorDescriptor kLiveRegionConstructors[] = {
    {"LiveRegion", kLiveRegionArgs, &ConstructLiveRegion}};
static constexpr MethodDescriptor kLiveRegionMethods[] = {
    {"assertive",
     {},
     "Interrupts current speech instead of waiting for it.",
     &RecordAssertive}};
static constexpr ComponentDescriptor kLiveRegion = {
    "LiveRegion", kLiveRegionConstructors, kLiveRegionMethods,
    "Text that screen readers announce whenever it changes. Keep the id "
    "stable.",
    &MaterializeLiveRegion};

static constexpr ConstructorDescriptor kLinkConstructors[] = {
    {"Link", kIdArguments, &ConstructLink}};
static constexpr MethodDescriptor kLinkMethods[] = {
    kOnClickMethod,
    kDisabledMethod,
    {"href", kHrefArgs, "Sets the absolute HTTP(S) URL opened when activated.",
     &RecordHref},
};
static constexpr ComponentDescriptor kLink = {
    "Link", kLinkConstructors, kLinkMethods,
    "An external-resource link. Ordinary children, shell style, disabled "
    "state, and on_click are honored.",
    &MaterializeLink};

static constexpr ConstructorDescriptor kKbdConstructors[] = {
    {"Kbd", kKeystrokeArgs, &ConstructKbd}};
static constexpr MethodDescriptor kKbdMethods[] = {
    {"appearance", kAppearanceArgs,
     "Controls whether the keystroke uses keycap presentation.",
     &RecordAppearance},
    {"outline",
     {},
     "Uses the outlined keycap presentation.",
     &RecordKbdOutline},
};
static constexpr ComponentDescriptor kKbd = {
    "Kbd", kKbdConstructors, kKbdMethods,
    "A platform-formatted keyboard shortcut keycap.", &MaterializeKbd};

} // namespace gpui::component_shell::controls::text

namespace gpui::component_shell {

bool RegisterControlsText(shell::ComponentRegistry* registry,
                          shell::RegistryError* error) {
    return registry->Register(&controls::text::kLabel, error) &&
           registry->Register(&controls::text::kLink, error) &&
           registry->Register(&controls::text::kKbd, error) &&
           registry->Register(&controls::text::kLiveRegion, error);
}

} // namespace gpui::component_shell
