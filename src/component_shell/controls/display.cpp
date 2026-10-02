// crates/component-shell/src/shell/controls/display.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/badge.h"
#include "ui/tag.h"

#include <limits.h>

namespace gpui::component_shell::controls::display {

struct UnitPayload {};

struct BadgeOp {
    enum Kind : uint8_t {
        Size,
        Dot,
        Count,
        Max,
        Color,
    } kind = Size;
    UiSize size = UiSize::Medium;
    // Count and Max: Rust's usize.
    double number = 0;
    Rgba color = {};
};

struct TagOp {
    enum Kind : uint8_t {
        Size,
        Variant,
        Outline,
        RoundedFull,
    } kind = Size;
    UiSize size = UiSize::Medium;
    component::TagVariant variant = component::TagVariant::Secondary;
};

static bool Nullary(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<UnitPayload>();
}

// parse_natural: a finite, non-negative whole number below `usize::MAX as
// f64`, which rounds to 2^64 and so is itself refused.
static bool ParseNatural(double value) {
    return value == value && value >= 0.0 && value < 18446744073709551616.0 &&
           value == (double)(uint64_t)value;
}

template <BadgeOp::Kind K>
static bool RecordNatural(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    double value = args[0].number;
    if (!ParseNatural(value))
        return build->Fail(fmt("Badge.%s expects a non-negative integer",
                               Str(K == BadgeOp::Count ? "count" : "max")));
    BadgeOp* op = build->New<BadgeOp>();
    op->kind = K;
    op->number = value;
    return true;
}

static bool RecordDot(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<BadgeOp>()->kind = BadgeOp::Dot;
    return true;
}

static bool RecordBadgeColor(PayloadBuild* build, const ComponentArgument* args,
                             int) {
    Rgba color = {};
    if (!ParseColorArgument(build, "Badge", args[0].string, &color))
        return false;
    BadgeOp* op = build->New<BadgeOp>();
    op->kind = BadgeOp::Color;
    op->color = color;
    return true;
}

static bool RecordBadgeSize(PayloadBuild* build, const ComponentArgument* args,
                            int) {
    BadgeOp* op = build->New<BadgeOp>();
    op->kind = BadgeOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static bool RecordTagSize(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    TagOp* op = build->New<TagOp>();
    op->kind = TagOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static bool RecordTagVariant(PayloadBuild* build, const ComponentArgument* args,
                             int) {
    using component::TagVariant;
    Str value = args[0].string;
    TagOp* op = build->New<TagOp>();
    op->kind = TagOp::Variant;
    op->variant = StrEq(value, "primary")   ? TagVariant::Primary
                  : StrEq(value, "danger")  ? TagVariant::Danger
                  : StrEq(value, "success") ? TagVariant::Success
                  : StrEq(value, "warning") ? TagVariant::Warning
                  : StrEq(value, "info")    ? TagVariant::Info
                                            : TagVariant::Secondary;
    return true;
}

template <TagOp::Kind K>
static bool RecordTagFlag(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<TagOp>()->kind = K;
    return true;
}

// badge.rs's usize, which the registry has already checked is one.
static uint64_t BadgeCount(double value) {
    return value >= 18446744073709551616.0 ? UINT64_MAX : (uint64_t)value;
}

static El* MaterializeBadge(MaterializeRequest* request) {
    if (!shell::PayloadIs<UnitPayload>(request->payload))
        return request->Fail(StrL("Badge received an incompatible payload"));
    Ctx* cx = request->cx;
    component::Badge* badge = component::Badge::New(cx);
    EachMethod<BadgeOp>(request, [&](const BadgeOp& op) {
        switch (op.kind) {
            case BadgeOp::Size:
                badge->WithSize(op.size);
                break;
            case BadgeOp::Dot:
                badge->Dot();
                break;
            case BadgeOp::Count:
                badge->Count(BadgeCount(op.number));
                break;
            case BadgeOp::Max:
                badge->Max(BadgeCount(op.number));
                break;
            case BadgeOp::Color:
                badge->Color(op.color);
                break;
        }
    });
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) badge->Child(children[i]);
    // The wrapper, not the badge, takes the script's style.
    return request->ApplyStyle(Div(cx->a)->Child(badge->IntoEl()));
}

static El* MaterializeTag(MaterializeRequest* request) {
    if (!shell::PayloadIs<UnitPayload>(request->payload))
        return request->Fail(StrL("Tag received an incompatible payload"));
    component::Tag* tag = component::Tag::New(request->cx, Str{});
    EachMethod<TagOp>(request, [&](const TagOp& op) {
        switch (op.kind) {
            case TagOp::Size:
                tag->WithSize(op.size);
                break;
            case TagOp::Variant:
                tag->variant = op.variant;
                break;
            case TagOp::Outline:
                tag->Outline();
                break;
            case TagOp::RoundedFull:
                // tag.rs rounded_full: `rems(1.)`, one rem at the default
                // 16 px rem size.
                tag->Radius(16);
                break;
        }
    });
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) tag->Child(children[i]);
    return request->ApplyStyle(tag->IntoEl());
}

static constexpr const char* kTagVariantLiterals[] = {
    "primary", "secondary", "danger", "success", "warning", "info"};
static constexpr ArgumentDescriptor kCountArgs[] = {{"count", SchemaNumber()}};
static constexpr ArgumentDescriptor kMaxArgs[] = {{"max", SchemaNumber()}};
static constexpr ArgumentDescriptor kColorArgs[] = {{"color", SchemaString()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ArgumentDescriptor kVariantArgs[] = {
    {"variant", SchemaEnum(kTagVariantLiterals)}};

static constexpr ConstructorDescriptor kBadgeConstructors[] = {
    {"Badge", {}, &Nullary}};
static constexpr MethodDescriptor kBadgeMethods[] = {
    {"dot", {}, "Displays a dot instead of a numeric count.", &RecordDot},
    {"count", kCountArgs,
     "Sets the displayed count; zero hides a numeric badge.",
     &RecordNatural<BadgeOp::Count>},
    {"max", kMaxArgs,
     "Sets the largest count displayed before the plus suffix.",
     &RecordNatural<BadgeOp::Max>},
    {"color", kColorArgs,
     "Sets the badge background from a supported color token.",
     &RecordBadgeColor},
    {"size", kSizeArgs, "Sets the semantic component size.", &RecordBadgeSize},
};
static constexpr ComponentDescriptor kBadge = {
    "Badge", kBadgeConstructors, kBadgeMethods,
    "A count or dot badge positioned over its ordinary children.",
    &MaterializeBadge};

static constexpr ConstructorDescriptor kTagConstructors[] = {
    {"Tag", {}, &Nullary}};
static constexpr MethodDescriptor kTagMethods[] = {
    {"variant", kVariantArgs, "Sets the semantic tag variant.",
     &RecordTagVariant},
    {"outline",
     {},
     "Uses the outline presentation.",
     &RecordTagFlag<TagOp::Outline>},
    {"rounded_full",
     {},
     "Uses pill-shaped corners.",
     &RecordTagFlag<TagOp::RoundedFull>},
    {"size", kSizeArgs, "Sets the semantic component size.", &RecordTagSize},
};
static constexpr ComponentDescriptor kTag = {
    "Tag", kTagConstructors, kTagMethods,
    "A compact semantic status tag that renders ordinary children.",
    &MaterializeTag};

} // namespace gpui::component_shell::controls::display

namespace gpui::component_shell {

bool RegisterControlsDisplay(shell::ComponentRegistry* registry,
                             shell::RegistryError* error) {
    return registry->Register(&controls::display::kBadge, error) &&
           registry->Register(&controls::display::kTag, error);
}

} // namespace gpui::component_shell
