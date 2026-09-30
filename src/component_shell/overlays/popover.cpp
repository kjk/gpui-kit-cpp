// crates/component-shell/src/shell/overlays/popover.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "shell/view.h"
#include "ui/button.h"
#include "ui/popover.h"

namespace gpui::component_shell::overlays::popover {

struct PopoverPayload {
    Str id;
    Str label;
};

struct PopoverOp {
    enum Kind : uint8_t {
        Anchor,
        DefaultOpen,
        Open,
        Appearance,
        OverlayClosable,
        OnOpenChange,
    } kind = Anchor;
    gpui::Anchor anchor = gpui::Anchor::TopLeft;
    bool value = false;
    ComponentArgument callback = {};
};

// The content closure's captures and the surface parts Popover's render lays
// on it.
struct PopoverContent {
    DeferredSlot* slot = nullptr;
    bool appearance = true;
    ElRefiner style = {};
    El** children = nullptr;
    int count = 0;
};

// Popover::render's content: v_flex().id("content"), popover_style + p_3
// when `appearance`, the lazy content, the children and the style. Occlusion
// and the tab group are component::Popover's own layering here.
static El* BuildContent(void* user, Ctx* cx) {
    PopoverContent* content = (PopoverContent*)user;
    El* surface = Div(cx->a)->Id(StrL("content"))->FlexCol();
    if (content->appearance) component::PopoverSurface(cx, surface)->Pad(12);
    surface->Child(BuildDeferredSlot(content->slot, cx));
    for (int i = 0; i < content->count; i++)
        surface->Child(content->children[i]);
    content->style.Apply(surface);
    return surface;
}

static void RunOpenChange(const shell::ComponentEventBinding* binding,
                          ScriptView* view, Ctx* cx, const void* event) {
    const PopoverOpenChangeEvent* ev = (const PopoverOpenChangeEvent*)event;
    shell::ComponentDataValue open =
        shell::ComponentDataValue::Boolean(ev && ev->open);
    binding->callback.InvokeAndReport(view->runtime,
                                      "Popover.on_open_change callback failed",
                                      &open, 1, cx->win, cx->app);
}

static El* Materialize(MaterializeRequest* request) {
    const PopoverPayload* payload = request->PayloadAs<PopoverPayload>();
    if (!payload)
        return request->Fail(StrL("Popover received an incompatible payload"));
    shell::ComponentElementFactory factory = request
                                                 ->TakeSlotFactory("content");
    if (!factory.IsSet())
        return request->Fail(StrL("Popover requires content(element)"));
    Ctx* cx = request->cx;
    PopoverContent* content = ArenaNew<PopoverContent>(cx->a);
    content->slot =
        NewDeferredSlot(request, factory, "Failed to render Popover content");
    component::Popover* popover = component::Popover::New(cx, payload->id);
    popover
        ->Trigger(component::Button::New(
                      cx, StrDup(cx->a, fmt("popover-trigger:%s", payload->id)))
                      ->Ghost()
                      ->Label(payload->label));
    EachMethod<PopoverOp>(request, [&](const PopoverOp& op) {
        switch (op.kind) {
            case PopoverOp::Anchor:
                popover->Anchor(op.anchor);
                break;
            case PopoverOp::DefaultOpen:
                popover->DefaultOpen(op.value);
                break;
            case PopoverOp::Open:
                popover->Open(op.value);
                break;
            case PopoverOp::Appearance:
                content->appearance = op.value;
                break;
            case PopoverOp::OverlayClosable:
                popover->OverlayClosable(op.value);
                break;
            case PopoverOp::OnOpenChange:
                popover->OnOpenChange(shell::ComponentListener(
                    cx, &RunOpenChange, request->ResolveCallback(op.callback)));
                break;
        }
    });
    popover->ContentBuilder(&BuildContent, content);
    // request.finish(popover): its style and children belong to the content
    // surface, which is built only while the popover is open.
    content->style = request->TakeStyle();
    if (!request->TakeChildren(&content->children, &content->count))
        return nullptr;
    return popover->IntoEl();
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::String)
        return build->Fail(StrL("Popover(id, label) expects two strings"));
    if (len(StrTrimAscii(args[0].string)) == 0 ||
        len(StrTrimAscii(args[1].string)) == 0)
        return build->Fail(StrL("Popover id and label must not be empty"));
    PopoverPayload* payload = build->New<PopoverPayload>();
    payload->id = args[0].string;
    payload->label = args[1].string;
    return true;
}

static constexpr const char* kAnchorLiterals[] = {
    "top_left",      "top_center",   "top_right",   "bottom_left",
    "bottom_center", "bottom_right", "left_center", "right_center"};

// anchor_op.
static bool RecordAnchor(PayloadBuild* build, const ComponentArgument* args,
                         int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build
            ->Fail(StrL("Popover.card_anchor(anchor) expects an anchor "
                        "literal"));
    for (int i = 0; i < 8; i++) {
        if (!StrEq(args[0].string, kAnchorLiterals[i])) continue;
        PopoverOp* op = build->New<PopoverOp>();
        op->kind = PopoverOp::Anchor;
        op->anchor = (gpui::Anchor)i;
        return true;
    }
    return build->Fail(fmt("unsupported Popover anchor `%s`", args[0].string));
}

// boolean_op(name, ..).
template <PopoverOp::Kind Kind>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean) {
        const char* name = Kind == PopoverOp::DefaultOpen  ? "default_open"
                           : Kind == PopoverOp::Open       ? "open"
                           : Kind == PopoverOp::Appearance ? "appearance"
                                                           : "overlay_closable";
        return build
            ->Fail(fmt("Popover.%s(value) expects a boolean", Str(name)));
    }
    PopoverOp* op = build->New<PopoverOp>();
    op->kind = Kind;
    op->value = args[0].boolean;
    return true;
}

static bool RecordOpenChange(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Callback)
        return build
            ->Fail(StrL("Popover.on_open_change(callback) expects a "
                        "callback"));
    PopoverOp* op = build->New<PopoverOp>();
    op->kind = PopoverOp::OnOpenChange;
    op->callback = args[0];
    return true;
}

static constexpr ArgumentDescriptor kConstructorArgs[] = {
    {"id", SchemaString()},
    {"label", SchemaString()}};
static constexpr ArgumentDescriptor kAnchorArgs[] = {
    {"anchor", SchemaEnum(kAnchorLiterals)}};
static constexpr ArgumentDescriptor kValueArgs[] = {{"value", SchemaBoolean()}};
static constexpr ArgumentDescriptor kOpenChangeArgs[] = {
    {"callback", SchemaCallback("(open: boolean, cx: Context) => void")}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Popover", kConstructorArgs, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    // Not `anchor`: the runtime's element prototype defines that name itself
    // and would shadow this method. HoverCard carries the same method under
    // this name.
    {"card_anchor", kAnchorArgs,
     "Positions the popover relative to its trigger.", &RecordAnchor},
    {"default_open", kValueArgs, "Sets the initial uncontrolled open state.",
     &RecordBool<PopoverOp::DefaultOpen>},
    {"open", kValueArgs, "Controls whether the popover is open.",
     &RecordBool<PopoverOp::Open>},
    {"appearance", kValueArgs, "Controls the native popover surface styling.",
     &RecordBool<PopoverOp::Appearance>},
    {"overlay_closable", kValueArgs,
     "Controls whether pressing outside dismisses the popover.",
     &RecordBool<PopoverOp::OverlayClosable>},
    {"on_open_change", kOpenChangeArgs,
     "Runs when pointer interaction changes the open state.",
     &RecordOpenChange},
};
static constexpr ComponentDescriptor kPopover = {
    "Popover", kConstructors, kMethods,
    "A button-triggered popover with lazy content(element).", &Materialize};

} // namespace gpui::component_shell::overlays::popover

namespace gpui::component_shell {

bool RegisterOverlaysPopover(shell::ComponentRegistry* registry,
                             shell::RegistryError* error) {
    return registry->Register(&overlays::popover::kPopover, error);
}

} // namespace gpui::component_shell
