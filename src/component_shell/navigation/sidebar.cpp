// crates/component-shell/src/shell/navigation/sidebar.rs

#include "component_shell/families.h"
#include "component_shell/navigation/mod.h"
#include "shell/view.h"
#include "ui/sidebar.h"

namespace gpui::component_shell::navigation {

bool RequireRegisteredChild(const char* parent, const char* expected,
                            const char* actual, Str* error) {
    if (actual && strcmp(actual, expected) == 0) return true;
    *error = fmt("%s accepts only %s children; received %s", Str(parent),
                 Str(expected), Str(actual ? actual : "an ordinary element"));
    return false;
}

bool RequireDefaultItemStyle(bool styled, Str* error) {
    if (!styled) return true;
    *error = StrL("SidebarMenuItem does not support shell style operations");
    return false;
}

} // namespace gpui::component_shell::navigation

namespace gpui::component_shell::navigation::sidebar {

struct Id {
    Str value;
};

struct ToggleOp {
    enum Kind : uint8_t {
        Side,
        Collapsed,
    } kind = Side;
    gpui::Side side = gpui::Side::Left;
    bool value = false;
};

struct ItemOp {
    enum Kind : uint8_t {
        DefaultOpen,
        ClickToOpen,
        ClickToToggle,
        Icon,
    } kind = DefaultOpen;
    bool value = false;
    IconName icon = IconName::None;
};

struct SidebarOp {
    enum Kind : uint8_t {
        Side,
        Collapsible,
        Collapsed,
    } kind = Side;
    gpui::Side side = gpui::Side::Left;
    component::SidebarCollapsible collapsible =
        component::SidebarCollapsible::Icon;
    bool value = false;
};

static bool Require(MaterializeRequest* request, const char* parent,
                    const char* expected, const char* actual) {
    Str error;
    if (RequireRegisteredChild(parent, expected, actual, &error)) return true;
    request->Fail(error);
    return false;
}

static El* MaterializeMenuItem(MaterializeRequest* request) {
    const Id* label = request->PayloadAs<Id>();
    if (!label)
        return request
            ->Fail(StrL("SidebarMenuItem received an incompatible payload"));
    Ctx* cx = request->cx;
    component::SidebarMenuItem* item =
        component::SidebarMenuItem::New(cx, label->value);
    EachMethod<ItemOp>(request, [&](const ItemOp& op) {
        switch (op.kind) {
            case ItemOp::DefaultOpen:
                item->DefaultOpen(op.value);
                break;
            case ItemOp::ClickToOpen:
                item->ClickToOpen(op.value);
                break;
            case ItemOp::ClickToToggle:
                item->ClickToToggle(op.value);
                break;
            case ItemOp::Icon:
                item->Icon(op.icon);
                break;
        }
    });
    item->Active(request->selected)->Disabled(request->disabled);
    if (request->onClick)
        item->OnClick(
            Listen(cx, &ScriptView::OnClick, (int64_t)request->onClick));
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!Require(request, "SidebarMenuItem", "SidebarMenuItem",
                     children[i].componentName))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        component::SidebarMenuItem* nested =
            TakeCarriedAs<component::SidebarMenuItem>(request, element,
                                                      "SidebarMenuItem");
        if (!nested) return nullptr;
        item->Child(nested);
    }
    Str error;
    bool styled = request->HasStyle();
    request->TakeStyle();
    if (!RequireDefaultItemStyle(styled, &error)) return request->Fail(error);
    return CarrierOf(cx, item);
}

static El* MaterializeMenu(MaterializeRequest* request) {
    if (!shell::PayloadIs<Empty>(request->payload))
        return request
            ->Fail(StrL("SidebarMenu received an incompatible payload"));
    Ctx* cx = request->cx;
    component::SidebarMenu* menu = component::SidebarMenu::New(cx);
    menu->refiner = request->TakeStyle();
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!Require(request, "SidebarMenu", "SidebarMenuItem",
                     children[i].componentName))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        component::SidebarMenuItem* item =
            TakeCarriedAs<component::SidebarMenuItem>(request, element,
                                                      "SidebarMenuItem");
        if (!item) return nullptr;
        menu->Child(item);
    }
    return CarrierOf(cx, menu);
}

// sidebar_edge_materializer!: SidebarHeader and SidebarFooter.
template <class Edge>
static El* MaterializeEdge(MaterializeRequest* request, const char* label) {
    if (!shell::PayloadIs<Empty>(request->payload))
        return request
            ->Fail(fmt("%s received an incompatible payload", Str(label)));
    Edge* edge = Edge::New(request->cx);
    // apply_edge_selected: the common selected state.
    edge->Selected(request->selected);
    edge->refiner = request->TakeStyle();
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) edge->Child(children[i]);
    return edge->IntoEl();
}

static El* MaterializeHeader(MaterializeRequest* request) {
    return MaterializeEdge<component::SidebarHeader>(request, "SidebarHeader");
}
static El* MaterializeFooter(MaterializeRequest* request) {
    return MaterializeEdge<component::SidebarFooter>(request, "SidebarFooter");
}

static El* MaterializeSidebar(MaterializeRequest* request) {
    const Id* id = request->PayloadAs<Id>();
    if (!id)
        return request->Fail(StrL("Sidebar received an incompatible payload"));
    Ctx* cx = request->cx;
    component::Sidebar* sidebar = component::Sidebar::New(cx, id->value);
    EachMethod<SidebarOp>(request, [&](const SidebarOp& op) {
        switch (op.kind) {
            case SidebarOp::Side:
                sidebar->WithSide(op.side);
                break;
            case SidebarOp::Collapsible:
                sidebar->Collapsible(op.collapsible);
                break;
            case SidebarOp::Collapsed:
                sidebar->Collapsed(op.value);
                break;
        }
    });
    if (El* header = request->TakeSlot("header")) sidebar->Header(header);
    if (El* footer = request->TakeSlot("footer")) sidebar->Footer(footer);
    ElRefiner style = request->TakeStyle();
    if (style.IsSet()) {
        // sidebar_expanded_width reads a pixel width out of the style; the
        // port's Sidebar takes it through W, and the rest of the refinement
        // as a whole.
        El* probe = Div(cx->a);
        style.Apply(probe);
        if (probe->style.width > 0) sidebar->W(probe->style.width);
        sidebar->refiner = style;
    }
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!Require(request, "Sidebar", "SidebarMenu",
                     children[i].componentName))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        component::SidebarMenu* menu = TakeCarriedAs<component::SidebarMenu>(
            request, element, "SidebarMenu");
        if (!menu) return nullptr;
        sidebar->Child(menu);
    }
    return sidebar->IntoEl();
}

static El* MaterializeToggle(MaterializeRequest* request) {
    if (!shell::PayloadIs<Empty>(request->payload))
        return request->Fail(
            StrL("SidebarToggleButton received an incompatible payload"));
    Ctx* cx = request->cx;
    component::SidebarToggleButton* toggle =
        component::SidebarToggleButton::New(cx);
    EachMethod<ToggleOp>(request, [&](const ToggleOp& op) {
        if (op.kind == ToggleOp::Side)
            toggle->WithSide(op.side);
        else
            toggle->Collapsed(op.value);
    });
    if (request->onClick)
        toggle->OnClick(
            Listen(cx, &ScriptView::OnClick, (int64_t)request->onClick));
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    if (count != 0)
        return request
            ->Fail(StrL("SidebarToggleButton does not accept children"));
    El* wrapper = Div(cx->a)->Child(toggle->IntoEl());
    return request->ApplyStyle(wrapper);
}

// id_constructor(name, argument).
template <int Which>
static bool ConstructId(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    const char* name = Which == 0 ? "SidebarMenuItem" : "Sidebar";
    const char* argument = Which == 0 ? "label" : "id";
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build
            ->Fail(fmt("%s expects one string %s", Str(name), Str(argument)));
    if (len(args[0].string) == 0)
        return build
            ->Fail(fmt("%s %s must not be empty", Str(name), Str(argument)));
    build->New<Id>()->value = args[0].string;
    return true;
}

// bool_method(owner, name, ..) for the item's own flags.
template <ItemOp::Kind K>
static bool RecordItemBool(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean) {
        const char* name = K == ItemOp::DefaultOpen   ? "default_open"
                           : K == ItemOp::ClickToOpen ? "click_to_open"
                                                      : "click_to_toggle";
        return build
            ->Fail(fmt("SidebarMenuItem.%s expects one boolean", Str(name)));
    }
    ItemOp* op = build->New<ItemOp>();
    op->kind = K;
    op->value = args[0].boolean;
    return true;
}

static bool RecordItemIcon(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(StrL("SidebarMenuItem.icon expects one icon name"));
    Str value = args[0].string;
    IconName icon;
    if (StrEq(value, StrL("home")))
        icon = IconName::SquareTerminal;
    else if (StrEq(value, StrL("components")))
        icon = IconName::LayoutDashboard;
    else if (StrEq(value, StrL("settings")))
        icon = IconName::Settings2;
    else if (StrEq(value, StrL("archive")))
        icon = IconName::BookOpen;
    else if (StrEq(value, StrL("account")))
        icon = IconName::User;
    else
        return build->Fail(fmt("unsupported SidebarMenuItem icon `%s`", value));
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Icon;
    op->icon = icon;
    return true;
}

static bool ParseSide(PayloadBuild* build, const ComponentArgument* args,
                      int count, const char* owner, gpui::Side* out) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build
            ->Fail(fmt("%s.side expects `left` or `right`", Str(owner)));
    if (StrEq(args[0].string, StrL("left")))
        *out = gpui::Side::Left;
    else if (StrEq(args[0].string, StrL("right")))
        *out = gpui::Side::Right;
    else
        return build
            ->Fail(fmt("unsupported %s side `%s`", Str(owner), args[0].string));
    return true;
}

// side_method("Sidebar", SidebarOp::Side).
static bool RecordSidebarSide(PayloadBuild* build,
                              const ComponentArgument* args, int count) {
    gpui::Side side;
    if (!ParseSide(build, args, count, "Sidebar", &side)) return false;
    SidebarOp* op = build->New<SidebarOp>();
    op->kind = SidebarOp::Side;
    op->side = side;
    return true;
}

static bool RecordCollapsible(PayloadBuild* build,
                              const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(
            StrL("Sidebar.collapsible expects `icon`, `offcanvas`, or `none`"));
    Str value = args[0].string;
    component::SidebarCollapsible mode;
    if (StrEq(value, StrL("icon")))
        mode = component::SidebarCollapsible::Icon;
    else if (StrEq(value, StrL("offcanvas")))
        mode = component::SidebarCollapsible::Offcanvas;
    else if (StrEq(value, StrL("none")))
        mode = component::SidebarCollapsible::None;
    else
        return build
            ->Fail(fmt("unsupported Sidebar collapsible mode `%s`", value));
    SidebarOp* op = build->New<SidebarOp>();
    op->kind = SidebarOp::Collapsible;
    op->collapsible = mode;
    return true;
}

static bool RecordSidebarCollapsed(PayloadBuild* build,
                                   const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("Sidebar.collapsed expects one boolean"));
    SidebarOp* op = build->New<SidebarOp>();
    op->kind = SidebarOp::Collapsed;
    op->value = args[0].boolean;
    return true;
}

// side_method("SidebarToggleButton", ToggleOp::Side).
static bool RecordToggleSide(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    gpui::Side side;
    if (!ParseSide(build, args, count, "SidebarToggleButton", &side))
        return false;
    ToggleOp* op = build->New<ToggleOp>();
    op->kind = ToggleOp::Side;
    op->side = side;
    return true;
}

static bool RecordToggleCollapsed(PayloadBuild* build,
                                  const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build
            ->Fail(StrL("SidebarToggleButton.collapsed expects one boolean"));
    ToggleOp* op = build->New<ToggleOp>();
    op->kind = ToggleOp::Collapsed;
    op->value = args[0].boolean;
    return true;
}

// on_click_method(owner): the common behavior, with Rust's own sentence.
static constexpr MethodDescriptor kOnClick = {
    "on_click", kOnClickArguments,
    "Invokes the callback when the control is activated.",
    &RecordCommonBehavior};

static constexpr const char* kSideLiterals[] = {"left", "right"};
static constexpr ArgumentDescriptor kSideArgs[] = {
    {"side", SchemaEnum(kSideLiterals)}};
static constexpr const char* kSideDoc =
    "Sets the physical side occupied by the sidebar control.";

static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kSelectedArgs[] = {
    {"selected", SchemaBoolean()}};
static constexpr ArgumentDescriptor kDefaultOpenArgs[] = {
    {"default_open", SchemaBoolean()}};
static constexpr ArgumentDescriptor kClickToOpenArgs[] = {
    {"click_to_open", SchemaBoolean()}};
static constexpr ArgumentDescriptor kClickToToggleArgs[] = {
    {"click_to_toggle", SchemaBoolean()}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr const char* kIconLiterals[] = {
    "home", "components", "settings", "archive", "account"};
static constexpr ArgumentDescriptor kIconArgs[] = {
    {"icon", SchemaEnum(kIconLiterals)}};
static constexpr const char* kModeLiterals[] = {"icon", "offcanvas", "none"};
static constexpr ArgumentDescriptor kModeArgs[] = {
    {"mode", SchemaEnum(kModeLiterals)}};
static constexpr ArgumentDescriptor kCollapsedArgs[] = {
    {"collapsed", SchemaBoolean()}};

static constexpr ConstructorDescriptor kItemConstructors[] = {
    {"SidebarMenuItem", kLabelArgs, &ConstructId<0>}};
static constexpr MethodDescriptor kItemMethods[] = {
    kOnClick,
    {"selected", kSelectedArgs, "Sets the active destination state.",
     &RecordCommonBehavior},
    {"default_open", kDefaultOpenArgs,
     "Sets the initial submenu disclosure state.",
     &RecordItemBool<ItemOp::DefaultOpen>},
    {"click_to_open", kClickToOpenArgs, "Lets a row click open its submenu.",
     &RecordItemBool<ItemOp::ClickToOpen>},
    {"click_to_toggle", kClickToToggleArgs,
     "Lets a row click toggle its submenu.",
     &RecordItemBool<ItemOp::ClickToToggle>},
    {"disabled", kDisabledArgs, "Disables pointer activation.",
     &RecordCommonBehavior},
    {"icon", kIconArgs,
     "Sets the navigation icon used in expanded and icon-collapse modes.",
     &RecordItemIcon},
};
static constexpr ComponentDescriptor kMenuItem = {
    "SidebarMenuItem", kItemConstructors, kItemMethods,
    "A typed navigation row accepted by SidebarMenu. Shell style operations "
    "are unsupported because the native SidebarMenuItem is not Styled.",
    &MaterializeMenuItem};

static constexpr ConstructorDescriptor kMenuConstructors[] = {
    {"SidebarMenu", {}, &RecordEmpty}};
static constexpr ComponentDescriptor kMenu = {
    "SidebarMenu",
    kMenuConstructors,
    {},
    "A typed Sidebar menu accepting SidebarMenuItem children.",
    &MaterializeMenu};

// bool_method("Sidebar edge", "selected", ..): the common selected state.
static constexpr MethodDescriptor kEdgeMethods[] = {
    {"selected", kSelectedArgs, "Sets the selected presentation.",
     &RecordCommonBehavior}};
static constexpr ConstructorDescriptor kHeaderConstructors[] = {
    {"SidebarHeader", {}, &RecordEmpty}};
static constexpr ComponentDescriptor kHeader = {
    "SidebarHeader", kHeaderConstructors, kEdgeMethods,
    "A styled sidebar header accepting ordinary children.", &MaterializeHeader};
static constexpr ConstructorDescriptor kFooterConstructors[] = {
    {"SidebarFooter", {}, &RecordEmpty}};
static constexpr ComponentDescriptor kFooter = {
    "SidebarFooter", kFooterConstructors, kEdgeMethods,
    "A styled sidebar footer accepting ordinary children.", &MaterializeFooter};

static constexpr ConstructorDescriptor kSidebarConstructors[] = {
    {"Sidebar", kIdArgs, &ConstructId<1>}};
static constexpr MethodDescriptor kSidebarMethods[] = {
    {"side", kSideArgs, kSideDoc, &RecordSidebarSide},
    {"collapsible", kModeArgs, "Sets the sidebar collapse behavior.",
     &RecordCollapsible},
    {"collapsed", kCollapsedArgs, "Sets the controlled collapsed state.",
     &RecordSidebarCollapsed},
};
static constexpr ComponentDescriptor kSidebar = {
    "Sidebar", kSidebarConstructors, kSidebarMethods,
    "A typed application sidebar accepting SidebarMenu children and named "
    "header/footer slots.",
    &MaterializeSidebar};

static constexpr ConstructorDescriptor kToggleConstructors[] = {
    {"SidebarToggleButton", {}, &RecordEmpty}};
static constexpr MethodDescriptor kToggleMethods[] = {
    kOnClick,
    {"side", kSideArgs, kSideDoc, &RecordToggleSide},
    {"collapsed", kCollapsedArgs,
     "Sets the icon for the current collapsed state.", &RecordToggleCollapsed},
};
static constexpr ComponentDescriptor kToggle = {
    "SidebarToggleButton", kToggleConstructors, kToggleMethods,
    "A button that reflects sidebar side and collapsed state; on_click is "
    "forwarded. Shell style is applied to an explicit wrapper, and children "
    "are rejected.",
    &MaterializeToggle};

} // namespace gpui::component_shell::navigation::sidebar

namespace gpui::component_shell {

bool RegisterNavigationSidebar(shell::ComponentRegistry* registry,
                               shell::RegistryError* error) {
    using namespace navigation::sidebar;
    return registry->Register(&kMenuItem, error) &&
           registry->Register(&kMenu, error) &&
           registry->Register(&kHeader, error) &&
           registry->Register(&kFooter, error) &&
           registry->Register(&kSidebar, error) &&
           registry->Register(&kToggle, error);
}

} // namespace gpui::component_shell
