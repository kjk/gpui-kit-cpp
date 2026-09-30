// crates/component-shell/src/shell/overlays/dropdown_menu.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "shell/view.h"
#include "ui/button.h"
#include "ui/menu.h"

namespace gpui::component_shell::overlays::dropdown_menu {

struct DropdownMenuPayload {
    Str id;
    Str label;
};

struct MenuItemOp {
    Str label;
    ComponentArgument callback = {};
};

// require_item_only_children.
static bool RequireItemOnlyChildren(MaterializeRequest* request, int count) {
    if (count == 0) return true;
    request
        ->Fail(StrL("DropdownMenu accepts item(label, callback) methods "
                    "only; ordinary and typed children are unsupported"));
    return false;
}

static void RunItem(const shell::ComponentEventBinding* binding,
                    ScriptView* view, Ctx* cx, const void*) {
    binding->callback
        .InvokeAndReport(view->runtime, "DropdownMenu.item callback failed",
                         nullptr, 0, cx->win, cx->app);
}

static El* Materialize(MaterializeRequest* request) {
    if (!RequireItemOnlyChildren(request, request->ChildrenLen()))
        return nullptr;
    const DropdownMenuPayload* payload = request
                                             ->PayloadAs<DropdownMenuPayload>();
    if (!payload)
        return request
            ->Fail(StrL("DropdownMenu received an incompatible payload"));
    Ctx* cx = request->cx;
    // button.style().refine(take_style): the style is the Button's.
    El* button = request->ApplyStyle(component::Button::New(cx, payload->id)
                                         ->Label(payload->label)
                                         ->IntoEl());
    // dropdown_menu(|menu| items.fold(..)): the rows in script order, each
    // running its own callback. The menu is named inside the dropdown so two
    // dropdowns keep two menus.
    component::PopupMenu* menu = nullptr;
    {
        IdScope scope(cx, payload->id);
        menu = component::PopupMenu::New(cx, StrL("menu"));
    }
    EachMethod<MenuItemOp>(request, [&](const MenuItemOp& op) {
        menu->Menu(op.label)->OnClick(shell::ComponentListener(
            cx, &RunItem, request->ResolveCallback(op.callback)));
    });
    return component::DropdownMenu::New(cx, payload->id)
        ->Trigger(button)
        ->Menu(menu)
        ->IntoEl();
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::String)
        return build->Fail(StrL("DropdownMenu(id, label) expects two strings"));
    if (len(StrTrimAscii(args[0].string)) == 0 ||
        len(StrTrimAscii(args[1].string)) == 0)
        return build->Fail(StrL("DropdownMenu id and label must not be empty"));
    DropdownMenuPayload* payload = build->New<DropdownMenuPayload>();
    payload->id = args[0].string;
    payload->label = args[1].string;
    return true;
}

static bool RecordItem(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::Callback)
        return build
            ->Fail(StrL("DropdownMenu.item(label, callback) expects a "
                        "string and callback"));
    if (len(StrTrimAscii(args[0].string)) == 0)
        return build->Fail(StrL("DropdownMenu.item label must not be empty"));
    MenuItemOp* op = build->New<MenuItemOp>();
    op->label = args[0].string;
    op->callback = args[1];
    return true;
}

static constexpr ArgumentDescriptor kConstructorArgs[] = {
    {"id", SchemaString()},
    {"label", SchemaString()}};
static constexpr ArgumentDescriptor kItemArgs[] = {
    {"label", SchemaString()},
    {"callback", SchemaCallback("(cx: Context) => void")}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"DropdownMenu", kConstructorArgs, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"item", kItemArgs, "Appends a command item in script order.", &RecordItem},
};
static constexpr ComponentDescriptor kDropdownMenu = {
    "DropdownMenu", kConstructors, kMethods,
    "A button-triggered native popup menu containing closed command items.",
    &Materialize};

} // namespace gpui::component_shell::overlays::dropdown_menu

namespace gpui::component_shell {

bool RegisterOverlaysDropdownMenu(shell::ComponentRegistry* registry,
                                  shell::RegistryError* error) {
    return registry->Register(&overlays::dropdown_menu::kDropdownMenu, error);
}

} // namespace gpui::component_shell
