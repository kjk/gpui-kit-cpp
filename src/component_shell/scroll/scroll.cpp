// crates/component-shell/src/shell/scroll/scroll.rs

#include "component_shell/families.h"
#include "component_shell/scroll/mod.h"

namespace gpui::component_shell::scroll::scroll {

struct Payload {
    Str id;
    bool hasId = false;
    ComponentArgument state = {};
};

void ScrollHandleState::OnScroll(ScrollHandleState* self, Ctx* cx,
                                 const ScrollEvent* event) {
    if (!self || !event) return;
    self->offsetX = event->offsetX;
    self->offsetY = event->offsetY;
    Notify(cx);
}

void ResolvedOps::Fold(const Op& op) {
    switch (op.kind) {
        case Op::Axis:
            hasAxis = true;
            axis = op.axis;
            break;
        case Op::Mode:
            hasMode = true;
            mode = op.mode;
            break;
        case Op::ViewportFromLayout:
            viewportFromLayout = op.flag;
            break;
    }
}

static ResolvedOps ResolveOps(const MaterializeRequest* request) {
    ResolvedOps resolved;
    EachMethod<Op>(request, [&](const Op& op) { resolved.Fold(op); });
    return resolved;
}

bool RequireLeaf(int children, bool styled, Str* error) {
    if (children != 0) {
        *error = StrL("Scrollbar does not accept children");
        return false;
    }
    if (styled) {
        *error = StrL(
            "Scrollbar is a low-level Element and does not support shell "
            "style");
        return false;
    }
    return true;
}

using HandleEntity = EntityState<ScrollHandleState>;

static El* MaterializeScroll(MaterializeRequest* request) {
    const Payload* payload = request->PayloadAs<Payload>();
    if (!payload)
        return request->Fail(StrL("Scroll received an incompatible payload"));
    HandleEntity* held =
        request->StateAs<HandleEntity>(payload->state, "ScrollbarHandle");
    if (!held) return nullptr;
    ScrollHandleState* handle = held->entity.Get(request->cx->app);
    if (!handle) return request->Fail(StrL("ScrollbarHandle is not live"));
    ResolvedOps ops = ResolveOps(request);
    ScrollbarAxis axis = ops.hasAxis ? ops.axis : ScrollbarAxis::Vertical;
    const ComponentArgument* state = payload->state.Some();
    if (!state || state->kind != shell::ComponentArgumentKind::Entity)
        return request->Fail(StrL("Scroll expects a ScrollbarHandle entity"));
    Ctx* cx = request->cx;
    Str id = StrDup(
        cx->a, fmt("shell-scroll-%llu", (unsigned long long)state->handle));
    // div().id(("shell-scroll", handle)).flex().track_scroll(&handle)
    //     .lock_scroll_axis(), then the axis's overflow.
    El* area =
        Div(cx->a)
            ->Id(id)
            ->Flex()
            ->ScrollId(HashClickId(id))
            ->OnScroll(ListenTo(held->entity, &ScrollHandleState::OnScroll));
    bool scrollsX = axis != ScrollbarAxis::Vertical;
    bool scrollsY = axis != ScrollbarAxis::Horizontal;
    if (axis == ScrollbarAxis::Vertical) area->FlexCol();
    if (axis == ScrollbarAxis::Horizontal) area->FlexRow();
    if (scrollsX) area->ScrollX(handle->offsetX)->ScrollMask(Axis::Horizontal);
    if (scrollsY) area->ScrollY(handle->offsetY)->ScrollMask(Axis::Vertical);
    // The bar a Scrollbar sharing this handle asked for in this render cycle
    // or the one before: a Scrollbar written after the viewport is read on
    // the viewport's next render. A plain overflow box shows none.
    int now = ++handle->renders;
    bool bar = handle->barAt >= now - 1;
    ScrollbarAxis barAxis =
        handle->hasBarAxis ? handle->barAxis : ScrollbarAxis::Both;
    if (scrollsX && (!bar || barAxis == ScrollbarAxis::Vertical))
        area->HideScrollbarX();
    if (scrollsY && (!bar || barAxis == ScrollbarAxis::Horizontal))
        area->HideScrollbarY();
    if (bar && handle->hasBarMode) area->ScrollMode(handle->barMode);
    if (!request->AppendChildren(area)) return nullptr;
    return request->ApplyStyle(area);
}

static El* MaterializeScrollbar(MaterializeRequest* request) {
    const Payload* payload = request->PayloadAs<Payload>();
    if (!payload)
        return request
            ->Fail(StrL("Scrollbar received an incompatible payload"));
    if (!payload->hasId)
        return request->Fail(StrL("Scrollbar requires a stable id"));
    HandleEntity* held =
        request->StateAs<HandleEntity>(payload->state, "ScrollbarHandle");
    if (!held) return nullptr;
    ScrollHandleState* handle = held->entity.Get(request->cx->app);
    if (!handle) return request->Fail(StrL("ScrollbarHandle is not live"));
    ResolvedOps ops = ResolveOps(request);
    bool styled = request->HasStyle();
    request->TakeStyle();
    Str error;
    if (!RequireLeaf(request->ChildrenLen(), styled, &error))
        return request->Fail(error);
    // Scrollbar::new(&handle): both axes and the theme's mode unless told
    // otherwise. The bar itself is painted by the viewport that shares the
    // handle, so `viewport_from_layout` — the bar measuring its own box as
    // the viewport — has nothing to change here: the viewport is the box.
    handle->barAt = handle->renders;
    handle->hasBarAxis = ops.hasAxis;
    handle->barAxis = ops.axis;
    handle->hasBarMode = ops.hasMode;
    handle->barMode = ops.mode;
    // Rust's element is an absolutely placed overlay taking no room in its
    // parent's flow; so is this one, and it has nothing of its own to draw.
    return Div(request->cx->a)->Id(payload->id)->Absolute();
}

// ─── State and recorders ───────────────────────────────────────────────────

static bool NewHandle(shell::StateBuild* build, const ComponentArgument*, int) {
    HandleEntity* held = build->New<HandleEntity>();
    held->app = build->app;
    held->entity = EntityNewState<ScrollHandleState>(build->app);
    return true;
}

static bool ConstructScroll(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Entity)
        return build->Fail(StrL("Scroll expects one ScrollbarHandle entity"));
    build->New<Payload>()->state = args[0];
    return true;
}

static bool ConstructScrollbar(PayloadBuild* build,
                               const ComponentArgument* args, int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::Entity ||
        len(StrTrim(args[0].string)) == 0) {
        return build
            ->Fail(StrL("Scrollbar expects a non-empty window-unique id and "
                        "ScrollbarHandle"));
    }
    Payload* payload = build->New<Payload>();
    payload->id = args[0].string;
    payload->hasId = true;
    payload->state = args[1];
    return true;
}

static bool RecordAxis(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(StrL("axis expects one enum string"));
    Str value = args[0].string;
    ScrollbarAxis axis;
    if (StrEq(value, "vertical"))
        axis = ScrollbarAxis::Vertical;
    else if (StrEq(value, "horizontal"))
        axis = ScrollbarAxis::Horizontal;
    else if (StrEq(value, "both"))
        axis = ScrollbarAxis::Both;
    else
        return build->Fail(StrL("axis expects vertical, horizontal, or both"));
    Op* op = build->New<Op>();
    op->kind = Op::Axis;
    op->axis = axis;
    return true;
}

static bool RecordMode(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(StrL("Scrollbar.mode expects one enum string"));
    Str value = args[0].string;
    ScrollbarMode mode;
    if (StrEq(value, "scrolling"))
        mode = ScrollbarMode::Scrolling;
    else if (StrEq(value, "hover"))
        mode = ScrollbarMode::Hover;
    else if (StrEq(value, "always"))
        mode = ScrollbarMode::Always;
    else
        return build
            ->Fail(StrL("Scrollbar.mode expects scrolling, hover, or always"));
    Op* op = build->New<Op>();
    op->kind = Op::Mode;
    op->mode = mode;
    return true;
}

static bool RecordViewportFromLayout(PayloadBuild* build,
                                     const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build
            ->Fail(StrL("Scrollbar.viewport_from_layout expects boolean"));
    Op* op = build->New<Op>();
    op->kind = Op::ViewportFromLayout;
    op->flag = args[0].boolean;
    return true;
}

// ─── Descriptors ───────────────────────────────────────────────────────────

static constexpr shell::StateDescriptor kHandle = {
    "ScrollbarHandle",
    "ScrollbarHandle",
    {},
    "A retained native scroll capability owned by one Scroll viewport and "
    "shared with any Scrollbar elements that control it.",
    &NewHandle,
    {}};

static constexpr const char* kAxisLiterals[] = {"vertical", "horizontal",
                                                "both"};
static constexpr const char* kModeLiterals[] = {"scrolling", "hover", "always"};
static constexpr ArgumentDescriptor kAxisArgs[] = {
    {"axis", SchemaEnum(kAxisLiterals)}};
static constexpr ArgumentDescriptor kModeArgs[] = {
    {"mode", SchemaEnum(kModeLiterals)}};
static constexpr ArgumentDescriptor kViewportArgs[] = {
    {"enabled", SchemaBoolean()}};

// axis_method(): not `axis`, which the runtime's element prototype checks
// against `horizontal | vertical` before the call can reach a registered
// component, so `both` — which this surface does support — would be refused
// there and never arrive.
static constexpr MethodDescriptor kAxisMethod = {
    "scroll_axis", kAxisArgs, "Selects the native scroll axes.", &RecordAxis};

static constexpr ArgumentDescriptor kScrollArgs[] = {
    {"handle", SchemaEntity("ScrollbarHandle")}};
static constexpr ConstructorDescriptor kScrollConstructors[] = {
    {"Scroll", kScrollArgs, &ConstructScroll}};
static constexpr MethodDescriptor kScrollMethods[] = {kAxisMethod};
static constexpr ComponentDescriptor kScroll = {
    "Scroll", kScrollConstructors, kScrollMethods,
    "Adapter wrapper for ScrollableElement overflow behavior. It shares a "
    "retained native handle, accepts ordinary children and shell style, and "
    "defaults to vertical scrolling.",
    &MaterializeScroll};

static constexpr ArgumentDescriptor kScrollbarArgs[] = {
    {"id", SchemaString()},
    {"handle", SchemaEntity("ScrollbarHandle")}};
static constexpr ConstructorDescriptor kScrollbarConstructors[] = {
    {"Scrollbar", kScrollbarArgs, &ConstructScrollbar}};
static constexpr MethodDescriptor kScrollbarMethods[] = {
    kAxisMethod,
    {"mode", kModeArgs, "Sets the native scrollbar visibility policy.",
     &RecordMode},
    {"viewport_from_layout", kViewportArgs,
     "Uses this element's layout bounds as the native viewport.",
     &RecordViewportFromLayout},
};
static constexpr ComponentDescriptor kScrollbar = {
    "Scrollbar", kScrollbarConstructors, kScrollbarMethods,
    "The real native low-level Scrollbar sharing a retained handle. Its "
    "stable id must be window-unique; children and generic shell style are "
    "rejected.",
    &MaterializeScrollbar};

} // namespace gpui::component_shell::scroll::scroll

namespace gpui::component_shell {

bool RegisterScrollScroll(shell::ComponentRegistry* registry,
                          shell::RegistryError* error) {
    return registry->RegisterState(&scroll::scroll::kHandle, error) &&
           registry->Register(&scroll::scroll::kScroll, error) &&
           registry->Register(&scroll::scroll::kScrollbar, error);
}

} // namespace gpui::component_shell
