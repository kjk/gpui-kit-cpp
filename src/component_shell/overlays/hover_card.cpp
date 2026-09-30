// crates/component-shell/src/shell/overlays/hover_card.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "shell/view.h"
#include "ui/hover_card.h"
#include "ui/popover.h"

#include <math.h>

namespace gpui::component_shell::overlays::hover_card {

struct HoverCardPayload {
    Str id;
};

struct HoverCardOp {
    enum Kind : uint8_t {
        Trigger,
        Anchor,
        OpenDelay,
        CloseDelay,
        Appearance,
        OnOpenChange,
    } kind = Trigger;
    ComponentArgument argument = {};
    gpui::Anchor anchor = gpui::Anchor::TopCenter;
    // A Duration, in milliseconds.
    double milliseconds = 0;
    bool appearance = true;
};

// What the lazy content builder needs: the deferred slot, and the parts
// HoverCard's own render lays onto the content surface.
struct CardContent {
    DeferredSlot* slot = nullptr;
    bool appearance = true;
    ElRefiner style = {};
    El** children = nullptr;
    int count = 0;
};

// HoverCard::render's content: Popover::render_popover_content (the
// surface; the top_1/bottom_1 inset is component::HoverCard's own) with
// overflow_hidden, the lazy content, the card's children and its style.
static El* BuildContent(void* user, Ctx* cx) {
    CardContent* content = (CardContent*)user;
    El* surface = Div(cx->a)->Id(StrL("content"))->FlexCol();
    if (content->appearance) component::PopoverSurface(cx, surface)->Pad(12);
    surface->ClipX()->ClipY();
    surface->Child(BuildDeferredSlot(content->slot, cx));
    for (int i = 0; i < content->count; i++)
        surface->Child(content->children[i]);
    content->style.Apply(surface);
    return surface;
}

static void RunOpenChange(const shell::ComponentEventBinding* binding,
                          ScriptView* view, Ctx* cx, const void* event) {
    const HoverCardOpenChangeEvent* ev = (const HoverCardOpenChangeEvent*)event;
    shell::ComponentDataValue open =
        shell::ComponentDataValue::Boolean(ev && ev->open);
    binding->callback.InvokeAndReport(view->runtime,
                                      "HoverCard.on_open_change callback "
                                      "failed",
                                      &open, 1, cx->win, cx->app);
}

static El* Materialize(MaterializeRequest* request) {
    const HoverCardPayload* payload = request->PayloadAs<HoverCardPayload>();
    if (!payload)
        return request
            ->Fail(StrL("HoverCard received an incompatible payload"));
    // trigger_argument: the first trigger_element.
    const HoverCardOp* trigger = nullptr;
    EachMethod<HoverCardOp>(request, [&](const HoverCardOp& op) {
        if (!trigger && op.kind == HoverCardOp::Trigger) trigger = &op;
    });
    if (!trigger)
        return request
            ->Fail(StrL("HoverCard requires "
                        "trigger_element(element)"));
    El* triggerElement = request->ResolveElement(trigger->argument);
    if (!triggerElement) return nullptr;
    shell::ComponentElementFactory factory = request
                                                 ->TakeSlotFactory("content");
    if (!factory.IsSet())
        return request->Fail(StrL("HoverCard requires content(element)"));
    Ctx* cx = request->cx;
    CardContent* content = ArenaNew<CardContent>(cx->a);
    content->slot =
        NewDeferredSlot(request, factory, "Failed to render HoverCard content");
    component::HoverCard* card = component::HoverCard::New(cx, payload->id)
                                     ->Trigger(triggerElement)
                                     ->ContentBuilder(&BuildContent, content);
    EachMethod<HoverCardOp>(request, [&](const HoverCardOp& op) {
        switch (op.kind) {
            case HoverCardOp::Trigger:
                break;
            case HoverCardOp::Anchor:
                card->Anchor(op.anchor);
                break;
            // Duration here is whole milliseconds, which is what the port's
            // timers count; Rust keeps the fraction.
            case HoverCardOp::OpenDelay:
                card->OpenDelay((int)lround(op.milliseconds));
                break;
            case HoverCardOp::CloseDelay:
                card->CloseDelay((int)lround(op.milliseconds));
                break;
            case HoverCardOp::Appearance:
                content->appearance = op.appearance;
                break;
            case HoverCardOp::OnOpenChange:
                card->OnOpenChange(shell::ComponentListener(
                    cx, &RunOpenChange, request->ResolveCallback(op.argument)));
                break;
        }
    });
    // request.finish(card): HoverCard is Styled and a ParentElement, and both
    // land on its content surface — taken now, laid on when it is built.
    content->style = request->TakeStyle();
    if (!request->TakeChildren(&content->children, &content->count))
        return nullptr;
    return card->IntoEl();
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(StrL("HoverCard(id) expects a string"));
    // non_empty_id.
    if (len(StrTrimAscii(args[0].string)) == 0)
        return build->Fail(StrL("HoverCard id must not be empty"));
    build->New<HoverCardPayload>()->id = args[0].string;
    return true;
}

static bool RecordTrigger(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Element)
        return build
            ->Fail(StrL("HoverCard.trigger_element(element) expects an "
                        "element"));
    HoverCardOp* op = build->New<HoverCardOp>();
    op->kind = HoverCardOp::Trigger;
    op->argument = args[0];
    return true;
}

static constexpr const char* kAnchorLiterals[] = {
    "top_left",      "top_center",   "top_right",   "bottom_left",
    "bottom_center", "bottom_right", "left_center", "right_center"};

// anchor_operation.
static bool RecordAnchor(PayloadBuild* build, const ComponentArgument* args,
                         int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build
            ->Fail(StrL("HoverCard.card_anchor(anchor) expects an "
                        "anchor literal"));
    for (int i = 0; i < 8; i++) {
        if (!StrEq(args[0].string, kAnchorLiterals[i])) continue;
        HoverCardOp* op = build->New<HoverCardOp>();
        op->kind = HoverCardOp::Anchor;
        op->anchor = (gpui::Anchor)i;
        return true;
    }
    return build
        ->Fail(fmt("unsupported HoverCard anchor `%s`", args[0].string));
}

// duration_operation.
template <HoverCardOp::Kind Kind>
static bool RecordDelay(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    const char* method =
        Kind == HoverCardOp::OpenDelay ? "open_delay" : "close_delay";
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(
            fmt("HoverCard.%s(milliseconds) expects a number", Str(method)));
    double milliseconds = args[0].number;
    if (!isfinite(milliseconds) || milliseconds < 0.0 || milliseconds > 60000.0)
        return build
            ->Fail(fmt("HoverCard.%s(milliseconds) expects a finite "
                       "value from 0 through 60000",
                       Str(method)));
    HoverCardOp* op = build->New<HoverCardOp>();
    op->kind = Kind;
    op->milliseconds = milliseconds;
    return true;
}

static bool RecordAppearance(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build
            ->Fail(StrL("HoverCard.appearance(appearance) expects a "
                        "boolean"));
    HoverCardOp* op = build->New<HoverCardOp>();
    op->kind = HoverCardOp::Appearance;
    op->appearance = args[0].boolean;
    return true;
}

// open_change_operation.
static bool RecordOpenChange(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Callback)
        return build
            ->Fail(StrL("HoverCard.on_open_change(callback) expects a "
                        "callback"));
    HoverCardOp* op = build->New<HoverCardOp>();
    op->kind = HoverCardOp::OnOpenChange;
    op->argument = args[0];
    return true;
}

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kTriggerArgs[] = {
    {"element", SchemaElement()}};
static constexpr ArgumentDescriptor kAnchorArgs[] = {
    {"anchor", SchemaEnum(kAnchorLiterals)}};
static constexpr ArgumentDescriptor kDelayArgs[] = {
    {"milliseconds", SchemaNumber()}};
static constexpr ArgumentDescriptor kAppearanceArgs[] = {
    {"appearance", SchemaBoolean()}};
static constexpr ArgumentDescriptor kOpenChangeArgs[] = {
    {"callback", SchemaCallback("(open: boolean, cx: Context) => void")}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"HoverCard", kIdArgs, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"trigger_element", kTriggerArgs,
     "Sets the element that owns the hover interaction.", &RecordTrigger},
    {"card_anchor", kAnchorArgs, "Positions the card relative to its trigger.",
     &RecordAnchor},
    {"open_delay", kDelayArgs,
     "Sets the hover-open delay in milliseconds (0\xe2\x80\x93"
     "60000).",
     &RecordDelay<HoverCardOp::OpenDelay>},
    {"close_delay", kDelayArgs,
     "Sets the hover-close delay in milliseconds (0\xe2\x80\x93"
     "60000).",
     &RecordDelay<HoverCardOp::CloseDelay>},
    {"appearance", kAppearanceArgs,
     "Controls the component's popover surface styling.", &RecordAppearance},
    {"on_open_change", kOpenChangeArgs,
     "Runs when pointer interaction opens or closes the card.",
     &RecordOpenChange},
};
static constexpr ComponentDescriptor kHoverCard = {
    "HoverCard", kConstructors, kMethods,
    "A hover-triggered card. Supply trigger_element(element) and lazy "
    "content(element).",
    &Materialize};

} // namespace gpui::component_shell::overlays::hover_card

namespace gpui::component_shell {

bool RegisterOverlaysHoverCard(shell::ComponentRegistry* registry,
                               shell::RegistryError* error) {
    return registry->Register(&overlays::hover_card::kHoverCard, error);
}

} // namespace gpui::component_shell
