// crates/component-shell/src/shell/basic/dropdown_button.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "shell/view.h"
#include "ui/button.h"
#include "ui/menu.h"

namespace gpui::component_shell::basic::dropdown_button {

struct DropdownPayload {
    Str id;
    Str label;
};

enum class Variant : uint8_t {
    Primary,
    Secondary,
    Danger,
    Ghost,
};

struct DropdownOp {
    enum Kind : uint8_t {
        Outline,
        Size,
        Variant,
        Anchor,
        Item,
    } kind = Outline;
    UiSize size = UiSize::Medium;
    dropdown_button::Variant variant = dropdown_button::Variant::Primary;
    gpui::Anchor anchor = gpui::Anchor::TopRight;
    // Item: the row's label and its callback.
    Str label;
    shell::ComponentArgument callback = {};
};

static bool IsBlank(Str text) {
    for (int i = 0; i < len(text); i++) {
        char c = text.s[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f' &&
            c != '\v')
            return false;
    }
    return true;
}

static bool Construct(PayloadBuild* build, const ComponentArgument* args, int) {
    // `id.trim().is_empty()`; Rust trims Unicode whitespace, of which an id
    // is in practice only ever the ASCII kind.
    if (IsBlank(args[0].string))
        return build->Fail(StrL("DropdownButton id must not be empty"));
    DropdownPayload* payload = build->New<DropdownPayload>();
    payload->id = args[0].string;
    payload->label = args[1].string;
    return true;
}

static bool RecordOutline(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<DropdownOp>()->kind = DropdownOp::Outline;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    DropdownOp* op = build->New<DropdownOp>();
    op->kind = DropdownOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static bool RecordVariant(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    Str value = args[0].string;
    DropdownOp* op = build->New<DropdownOp>();
    op->kind = DropdownOp::Variant;
    op->variant = StrEq(value, "secondary") ? Variant::Secondary
                  : StrEq(value, "danger")  ? Variant::Danger
                  : StrEq(value, "ghost")   ? Variant::Ghost
                                            : Variant::Primary;
    return true;
}

static bool RecordAnchor(PayloadBuild* build, const ComponentArgument* args,
                         int) {
    Str value = args[0].string;
    DropdownOp* op = build->New<DropdownOp>();
    op->kind = DropdownOp::Anchor;
    op->anchor = StrEq(value, "bottom_right")  ? gpui::Anchor::BottomRight
                 : StrEq(value, "bottom_left") ? gpui::Anchor::BottomLeft
                 : StrEq(value, "top_left")    ? gpui::Anchor::TopLeft
                                               : gpui::Anchor::TopRight;
    return true;
}

// string_callback_item.
static bool RecordItem(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    DropdownOp* op = build->New<DropdownOp>();
    op->kind = DropdownOp::Item;
    op->label = args[0].string;
    op->callback = args[1];
    return true;
}

// The menu row's closure: `invoke_and_report_with(context, &[])`.
static void RunItem(const shell::ComponentEventBinding* binding,
                    ScriptView* view, Ctx* cx, const void*) {
    binding->callback.InvokeAndReport(view->runtime,
                                      "DropdownButton.menu_item callback "
                                      "failed",
                                      nullptr, 0, cx->win, cx->app);
}

static El* Materialize(MaterializeRequest* request) {
    const DropdownPayload* payload = request->PayloadAs<DropdownPayload>();
    if (!payload)
        return request
            ->Fail(StrL("DropdownButton received an incompatible payload"));
    if (request->ChildrenLen() != 0)
        return request->Fail(StrL("DropdownButton does not accept children"));
    Ctx* cx = request->cx;
    component::Button* action = component::Button::New(cx, StrL("action"))
                                    ->Label(payload->label);
    if (request->onClick)
        action->OnClick(
            Listen(cx, &ScriptView::OnClick, (intptr_t)request->onClick));
    component::DropdownButton* dropdown =
        component::DropdownButton::New(cx, payload->id)
            ->Button_(action)
            ->Disabled(request->disabled)
            ->Selected(request->selected);
    gpui::Anchor anchor = gpui::Anchor::TopRight;
    int items = 0;
    EachMethod<DropdownOp>(request, [&](const DropdownOp& op) {
        switch (op.kind) {
            case DropdownOp::Outline:
                dropdown->Outline();
                break;
            case DropdownOp::Size:
                dropdown->WithSize(op.size);
                break;
            case DropdownOp::Variant:
                switch (op.variant) {
                    case Variant::Primary:
                        dropdown->Primary();
                        break;
                    case Variant::Secondary:
                        dropdown->Secondary();
                        break;
                    case Variant::Danger:
                        dropdown->Danger();
                        break;
                    case Variant::Ghost:
                        dropdown->Ghost();
                        break;
                }
                break;
            case DropdownOp::Anchor:
                anchor = op.anchor;
                break;
            case DropdownOp::Item:
                items++;
                break;
        }
    });
    if (items > 0) {
        // dropdown_menu_with_anchor(anchor, ..): the rows in call order, each
        // running its own callback. The menu is named inside the dropdown so
        // two dropdowns keep two menus.
        component::PopupMenu* menu = nullptr;
        {
            IdScope scope(cx, payload->id);
            menu = component::PopupMenu::New(cx, StrL("menu"));
        }
        EachMethod<DropdownOp>(request, [&](const DropdownOp& op) {
            if (op.kind != DropdownOp::Item) return;
            menu->Menu(op.label)->OnClick(shell::ComponentListener(
                cx, &RunItem, request->ResolveCallback(op.callback)));
        });
        dropdown->Menu(menu);
        // DropdownMenu hangs its menu under the trigger and only chooses the
        // edge it lines up with, as DropdownMenuPopover::Anchor does: the
        // top/bottom half of the anchor is not honored.
        dropdown->anchorRight = anchor == gpui::Anchor::TopRight ||
                                anchor == gpui::Anchor::BottomRight;
    }
    return request->ApplyStyle(dropdown->IntoEl());
}

static constexpr const char* kVariantLiterals[] = {"primary", "secondary",
                                                   "danger", "ghost"};
static constexpr const char* kAnchorLiterals[] = {"top_right", "bottom_right",
                                                  "bottom_left", "top_left"};

static constexpr ArgumentDescriptor kConstructorArgs[] = {
    {"id", SchemaString()},
    {"label", SchemaString()}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kSelectedArgs[] = {
    {"selected", SchemaBoolean()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ArgumentDescriptor kVariantArgs[] = {
    {"variant", SchemaEnum(kVariantLiterals)}};
static constexpr ArgumentDescriptor kAnchorArgs[] = {
    {"anchor", SchemaEnum(kAnchorLiterals)}};
static constexpr ArgumentDescriptor kItemArgs[] = {
    {"label", SchemaString()},
    {"callback", SchemaCallback("(cx: Context) => void")}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"DropdownButton", kConstructorArgs, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"outline", {}, "Uses the outlined button treatment.", &RecordOutline},
    // common_boolean and common_on_click: the shell reads these, and the
    // descriptor carries its own sentences for them.
    {"disabled", kDisabledArgs, "Records shell-owned common control behavior.",
     &RecordCommonBehavior},
    {"selected", kSelectedArgs, "Records shell-owned common control behavior.",
     &RecordCommonBehavior},
    {"on_click", kOnClickArguments,
     "Invokes the callback when the labeled action half is activated.",
     &RecordCommonBehavior},
    {"size", kSizeArgs, "Sets the size of both halves.", &RecordSize},
    {"variant", kVariantArgs, "Sets the semantic variant of both halves.",
     &RecordVariant},
    {"menu_anchor", kAnchorArgs, "Sets the popup menu anchor.", &RecordAnchor},
    {"menu_item", kItemArgs,
     "Appends a clickable popup-menu item in call order.", &RecordItem},
};
static constexpr ComponentDescriptor kDropdownButton = {
    "DropdownButton", kConstructors, kMethods,
    "A real split DropdownButton with an adapter-owned labeled action half "
    "and optional callback menu items. It accepts no children.",
    &Materialize};

} // namespace gpui::component_shell::basic::dropdown_button

namespace gpui::component_shell {

bool RegisterBasicDropdownButton(shell::ComponentRegistry* registry,
                                 shell::RegistryError* error) {
    return registry->Register(&basic::dropdown_button::kDropdownButton, error);
}

} // namespace gpui::component_shell
