// crates/component-shell/src/shell/structured/description_list.rs

#include "component_shell/families.h"
#include "component_shell/structured/mod.h"
#include "ui/description_list.h"

namespace gpui::component_shell::structured {

void ListConfig::Apply(const ListOp& op) {
    switch (op.kind) {
        case ListOp::Vertical:
            vertical = true;
            break;
        case ListOp::Bordered:
            bordered = op.bordered;
            break;
        case ListOp::Columns:
            columns = op.columns;
            break;
        case ListOp::Size:
            size = op.size;
            break;
    }
}

bool EnsureItemSurface(int children, bool styled, Str* error) {
    if (children != 0) {
        *error =
            StrL("DescriptionItem does not accept children; use value(string)");
        return false;
    }
    if (styled) {
        *error = StrL(
            "DescriptionItem does not accept style methods because its native "
            "value is a typed, non-element list part");
        return false;
    }
    return true;
}

} // namespace gpui::component_shell::structured

namespace gpui::component_shell::structured::description_list {

struct ItemPayload {
    Str label;
};
struct ItemOp {
    enum Kind : uint8_t {
        Value,
        Span,
    } kind = Value;
    Str value;
    int span = 1;
};

struct ListPayload {};

// An integer from a usize the list counts in as int: a span wider than any
// list has columns is the widest one there is.
static int SpanInt(uint64_t value) {
    return value > 0x7fffffff ? 0x7fffffff : (int)value;
}

static El* MaterializeItem(MaterializeRequest* request) {
    Str error;
    bool styled = request->HasStyle();
    request->TakeStyle();
    if (!EnsureItemSurface(request->ChildrenLen(), styled, &error))
        return request->Fail(error);
    const ItemPayload* payload = request->PayloadAs<ItemPayload>();
    if (!payload)
        return request
            ->Fail(StrL("DescriptionItem received an incompatible payload"));
    component::DescriptionItem* item =
        ArenaNew<component::DescriptionItem>(request->cx->a);
    *item = component::DescriptionItem::New(
        component::DescriptionText::From(payload->label));
    EachMethod<ItemOp>(request, [&](const ItemOp& op) {
        if (op.kind == ItemOp::Value)
            item->Value(component::DescriptionText::From(op.value));
        else
            item->Span(op.span);
    });
    return CarrierOf(request->cx, item);
}

static El* MaterializeList(MaterializeRequest* request) {
    if (!shell::PayloadIs<ListPayload>(request->payload))
        return request
            ->Fail(StrL("DescriptionList received an incompatible payload"));
    ListConfig config;
    EachMethod<ListOp>(request, [&](const ListOp& op) { config.Apply(op); });
    Ctx* cx = request->cx;
    component::DescriptionList* list =
        (config.vertical ? component::DescriptionList::Vertical(cx)
                         : component::DescriptionList::Horizontal(cx))
            ->Bordered(config.bordered)
            ->Columns(config.columns)
            ->WithSize(config.size);
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    static constexpr const char* kAllowed[] = {"DescriptionItem"};
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "DescriptionList", children[i].componentName,
                          kAllowed))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        component::DescriptionItem* item =
            TakeCarriedAs<component::DescriptionItem>(request, element,
                                                      "DescriptionItem");
        if (!item) return nullptr;
        list->Child(*item);
    }
    El* wrapper = Div(cx->a)->Child(list->IntoEl());
    return request->ApplyStyle(wrapper);
}

static bool ConstructItem(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(StrL("DescriptionItem(label) expects a string"));
    build->New<ItemPayload>()->label = args[0].string;
    return true;
}

static bool RecordValue(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build
            ->Fail(StrL("DescriptionItem.value(value) expects a string"));
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Value;
    op->value = args[0].string;
    return true;
}

static bool ConstructList(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<ListPayload>();
}

static bool RecordVertical(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<ListOp>()->kind = ListOp::Vertical;
    return true;
}

static bool RecordBordered(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(
            StrL("DescriptionList.bordered(bordered) expects a boolean"));
    ListOp* op = build->New<ListOp>();
    op->kind = ListOp::Bordered;
    op->bordered = args[0].boolean;
    return true;
}

// size(args, "DescriptionList.size", ..).
static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(StrL("DescriptionList.size expects a size literal"));
    bool known = false;
    for (const char* literal : kSizeLiterals)
        known = known || StrEq(args[0].string, Str(literal));
    if (!known)
        return build->Fail(
            fmt("unsupported DescriptionList.size size `%s`", args[0].string));
    ListOp* op = build->New<ListOp>();
    op->kind = ListOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kValueArgs[] = {{"value", SchemaString()}};
static constexpr ArgumentDescriptor kSpanArgs[] = {{"span", SchemaNumber()}};
static constexpr ConstructorDescriptor kItemConstructors[] = {
    {"DescriptionItem", kLabelArgs, &ConstructItem}};
static constexpr MethodDescriptor kItemMethods[] = {
    {"value", kValueArgs, "Sets the item's textual value.", &RecordValue},
    {"span", kSpanArgs,
     "Sets how many description-list columns the item spans.", &RecordItemSpan},
};
static constexpr ComponentDescriptor kItem = {
    "DescriptionItem", kItemConstructors, kItemMethods,
    "A typed label/value child for DescriptionList. It accepts value(string) "
    "and span(number), but no children or common style methods because the "
    "native item is not an independently rendered element.",
    &MaterializeItem};

static constexpr ArgumentDescriptor kBorderedArgs[] = {
    {"bordered", SchemaBoolean()}};
static constexpr ArgumentDescriptor kColumnsArgs[] = {
    {"columns", SchemaNumber()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ConstructorDescriptor kListConstructors[] = {
    {"DescriptionList", {}, &ConstructList}};
static constexpr MethodDescriptor kListMethods[] = {
    {"vertical", {}, "Uses the vertical label/value layout.", &RecordVertical},
    {"bordered", kBorderedArgs, "Controls the horizontal-layout border.",
     &RecordBordered},
    {"columns", kColumnsArgs, "Sets the column count from 1 through 10.",
     &RecordListColumns},
    {"size", kSizeArgs, "Sets the description-list density.", &RecordSize},
};
static constexpr ComponentDescriptor kList = {
    "DescriptionList", kListConstructors, kListMethods,
    "A structured label/value list accepting DescriptionItem children.",
    &MaterializeList};

} // namespace gpui::component_shell::structured::description_list

namespace gpui::component_shell::structured {

// positive_usize_payload(args, "DescriptionItem.span", ..).
bool RecordItemSpan(PayloadBuild* build, const ComponentArgument* args,
                    int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build
            ->Fail(StrL("DescriptionItem.span expects a positive integer"));
    uint64_t span = 0;
    Str error;
    if (!PositiveUsize(args[0].number, StrL("DescriptionItem.span"), &span,
                       &error))
        return build->Fail(error);
    description_list::ItemOp* op = build->New<description_list::ItemOp>();
    op->kind = description_list::ItemOp::Span;
    op->span = description_list::SpanInt(span);
    return true;
}

// columns_payload.
bool RecordListColumns(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    static const Str kRange =
        StrL("DescriptionList.columns expects an integer from 1 through 10");
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(kRange);
    uint64_t columns = 0;
    Str error;
    if (!PositiveUsize(args[0].number, StrL("DescriptionList.columns"),
                       &columns, &error))
        return build->Fail(error);
    if (columns > 10) return build->Fail(kRange);
    ListOp* op = build->New<ListOp>();
    op->kind = ListOp::Columns;
    op->columns = (int)columns;
    return true;
}

} // namespace gpui::component_shell::structured

namespace gpui::component_shell {

bool RegisterStructuredDescriptionList(shell::ComponentRegistry* registry,
                                       shell::RegistryError* error) {
    return registry->Register(&structured::description_list::kItem, error) &&
           registry->Register(&structured::description_list::kList, error);
}

} // namespace gpui::component_shell
