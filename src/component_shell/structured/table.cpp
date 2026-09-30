// crates/component-shell/src/shell/structured/table.rs

#include "component_shell/families.h"
#include "component_shell/structured/mod.h"
#include "component_shell/typed_compound/mod.h"
#include "ui/table.h"

namespace gpui::component_shell::structured::table {

using typed_compound::TakeElement;
using typed_compound::TypedChildElement;

struct TableOp {
    enum Kind : uint8_t {
        AccessibilityLabel,
        Size,
    } kind = AccessibilityLabel;
    Str label;
    UiSize size = UiSize::Medium;
};

struct CellOp {
    enum Kind : uint8_t {
        ColSpan,
        Center,
        Right,
    } kind = ColSpan;
    int span = 1;
};

static El* Incompatible(MaterializeRequest* request, const char* name) {
    return request->Fail(fmt("%s received an incompatible payload", Str(name)));
}

// container_materializer!: TableHeader, TableBody and TableFooter, each
// taking TableRow children only.
template <class Tag>
static El* MaterializeGroup(MaterializeRequest* request, const char* parent,
                            component::TableGroup* (*make)(Ctx*)) {
    if (!shell::PayloadIs<Empty>(request->payload))
        return Incompatible(request, parent);
    Ctx* cx = request->cx;
    component::TableGroup* group = make(cx);
    group->refiner = request->TakeStyle();
    // take_typed: each child is checked, materialized and taken in turn.
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    static constexpr const char* kAllowed[] = {"TableRow"};
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, parent, children[i].componentName, kAllowed))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        component::TableRow* row = (component::TableRow*)TakeElement(
            request, element, shell::PayloadTag<component::TableRow>(),
            "TableRow");
        if (!row) return nullptr;
        group->Child(row);
    }
    return TypedChildElement(cx, shell::PayloadTag<Tag>(), group,
                             group->IntoEl());
}

static El* MaterializeHeader(MaterializeRequest* request) {
    return MaterializeGroup<TableHeaderPart>(request, "TableHeader",
                                             &component::TableHeader::New);
}
static El* MaterializeBody(MaterializeRequest* request) {
    return MaterializeGroup<TableBodyPart>(request, "TableBody",
                                           &component::TableBody::New);
}
static El* MaterializeFooter(MaterializeRequest* request) {
    return MaterializeGroup<TableFooterPart>(request, "TableFooter",
                                             &component::TableFooter::New);
}

static El* MaterializeRow(MaterializeRequest* request) {
    if (!shell::PayloadIs<Empty>(request->payload))
        return Incompatible(request, "TableRow");
    Ctx* cx = request->cx;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    component::TableRow* row = component::TableRow::New(cx);
    row->refiner = request->TakeStyle();
    static constexpr const char* kAllowed[] = {"TableHead", "TableCell"};
    for (int i = 0; i < count; i++) {
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        Str name = Str(children[i].componentName);
        const void* tag = nullptr;
        if (StrEq(name, StrL("TableHead")))
            tag = shell::PayloadTag<TableHeadPart>();
        else if (StrEq(name, StrL("TableCell")))
            tag = shell::PayloadTag<TableCellPart>();
        else {
            RequireChild(request, "TableRow", children[i].componentName,
                         kAllowed);
            return nullptr;
        }
        component::TableCellEl* cell = (component::TableCellEl*)TakeElement(
            request, element, tag, children[i].componentName);
        if (!cell) return nullptr;
        row->Child(cell);
    }
    return TypedChildElement(cx, shell::PayloadTag<component::TableRow>(), row,
                             row->IntoEl());
}

// leaf_materializer!: TableHead and TableCell.
template <class Tag>
static El* MaterializeLeaf(MaterializeRequest* request, const char* label,
                           component::TableCellEl* (*make)(Ctx*)) {
    if (!shell::PayloadIs<Empty>(request->payload))
        return Incompatible(request, label);
    Ctx* cx = request->cx;
    component::TableCellEl* cell = make(cx);
    EachMethod<CellOp>(request, [&](const CellOp& op) {
        switch (op.kind) {
            case CellOp::ColSpan:
                cell->ColSpan(op.span);
                break;
            case CellOp::Center:
                cell->TextCenter();
                break;
            case CellOp::Right:
                cell->TextRight();
                break;
        }
    });
    cell->refiner = request->TakeStyle();
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (children[i]) cell->Child(children[i]);
    }
    return TypedChildElement(cx, shell::PayloadTag<Tag>(), cell,
                             cell->IntoEl());
}

static El* MaterializeHead(MaterializeRequest* request) {
    return MaterializeLeaf<TableHeadPart>(request, "TableHead",
                                          &component::TableHead::New);
}
static El* MaterializeCell(MaterializeRequest* request) {
    return MaterializeLeaf<TableCellPart>(request, "TableCell",
                                          &component::TableCell::New);
}

static El* MaterializeCaption(MaterializeRequest* request) {
    if (!shell::PayloadIs<Empty>(request->payload))
        return Incompatible(request, "TableCaption");
    Ctx* cx = request->cx;
    component::TableCaption* caption = component::TableCaption::New(cx);
    caption->refiner = request->TakeStyle();
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (children[i]) caption->Child(children[i]);
    }
    return TypedChildElement(cx, shell::PayloadTag<component::TableCaption>(),
                             caption, caption->IntoEl());
}

static El* MaterializeTable(MaterializeRequest* request) {
    if (!shell::PayloadIs<Empty>(request->payload))
        return Incompatible(request, "Table");
    Ctx* cx = request->cx;
    // Table::new() names itself by its place among its siblings; the port's
    // Table takes the id every part under it is scoped by, which is the
    // node's own.
    component::Table* table = component::Table::New(cx, request->elementId);
    EachMethod<TableOp>(request, [&](const TableOp& op) {
        if (op.kind == TableOp::AccessibilityLabel)
            table->AccessibilityLabel(op.label);
        else
            table->WithSize(op.size);
    });
    ElRefiner style = request->TakeStyle();
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    static constexpr const char* kAllowed[] = {"TableHeader", "TableBody",
                                               "TableFooter", "TableCaption"};
    for (int i = 0; i < count; i++) {
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        Str name = Str(children[i].componentName);
        const void* tag = nullptr;
        if (StrEq(name, StrL("TableHeader")))
            tag = shell::PayloadTag<TableHeaderPart>();
        else if (StrEq(name, StrL("TableBody")))
            tag = shell::PayloadTag<TableBodyPart>();
        else if (StrEq(name, StrL("TableFooter")))
            tag = shell::PayloadTag<TableFooterPart>();
        else if (StrEq(name, StrL("TableCaption")))
            tag = shell::PayloadTag<component::TableCaption>();
        else {
            RequireChild(request, "Table", children[i].componentName, kAllowed);
            return nullptr;
        }
        void* part =
            TakeElement(request, element, tag, children[i].componentName);
        if (!part) return nullptr;
        if (tag == shell::PayloadTag<component::TableCaption>())
            table->Child((component::TableCaption*)part);
        else
            table->Child((component::TableGroup*)part);
    }
    El* root = table->IntoEl();
    style.Apply(root);
    return root;
}

static bool RecordCenter(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<CellOp>()->kind = CellOp::Center;
    return true;
}
static bool RecordRight(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<CellOp>()->kind = CellOp::Right;
    return true;
}

static bool RecordAccessibilityLabel(PayloadBuild* build,
                                     const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build
            ->Fail(StrL("Table.accessibility_label(label) expects a string"));
    TableOp* op = build->New<TableOp>();
    op->kind = TableOp::AccessibilityLabel;
    op->label = args[0].string;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(StrL("Table.size(size) expects a size literal"));
    bool known = false;
    for (const char* literal : kSizeLiterals)
        known = known || StrEq(args[0].string, Str(literal));
    if (!known)
        return build->Fail(fmt("unsupported Table size `%s`", args[0].string));
    TableOp* op = build->New<TableOp>();
    op->kind = TableOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static constexpr const char* kPartDoc =
    "A typed structural child in a simple Table.";

// cell_methods().
static constexpr ArgumentDescriptor kSpanArgs[] = {{"span", SchemaNumber()}};
static constexpr MethodDescriptor kCellMethods[] = {
    {"col_span", kSpanArgs, "Sets the number of columns occupied by the cell.",
     &RecordCellSpan},
    {"text_center", {}, "Centers the cell content.", &RecordCenter},
    {"text_right", {}, "Right-aligns the cell content.", &RecordRight},
};

// empty_descriptor(name, ..): a no-argument constructor recording Empty.
static constexpr ConstructorDescriptor kHeaderConstructors[] = {
    {"TableHeader", {}, &RecordEmpty}};
static constexpr ConstructorDescriptor kBodyConstructors[] = {
    {"TableBody", {}, &RecordEmpty}};
static constexpr ConstructorDescriptor kFooterConstructors[] = {
    {"TableFooter", {}, &RecordEmpty}};
static constexpr ConstructorDescriptor kRowConstructors[] = {
    {"TableRow", {}, &RecordEmpty}};
static constexpr ConstructorDescriptor kHeadConstructors[] = {
    {"TableHead", {}, &RecordEmpty}};
static constexpr ConstructorDescriptor kCellConstructors[] = {
    {"TableCell", {}, &RecordEmpty}};
static constexpr ConstructorDescriptor kCaptionConstructors[] = {
    {"TableCaption", {}, &RecordEmpty}};
static constexpr ConstructorDescriptor kTableConstructors[] = {
    {"Table", {}, &RecordEmpty}};

static constexpr ComponentDescriptor kHeader = {"TableHeader",
                                                kHeaderConstructors,
                                                {},
                                                kPartDoc,
                                                &MaterializeHeader};
static constexpr ComponentDescriptor kBody = {"TableBody",
                                              kBodyConstructors,
                                              {},
                                              kPartDoc,
                                              &MaterializeBody};
static constexpr ComponentDescriptor kFooter = {"TableFooter",
                                                kFooterConstructors,
                                                {},
                                                kPartDoc,
                                                &MaterializeFooter};
static constexpr ComponentDescriptor kRow = {"TableRow",
                                             kRowConstructors,
                                             {},
                                             kPartDoc,
                                             &MaterializeRow};
static constexpr ComponentDescriptor kHead = {
    "TableHead", kHeadConstructors, kCellMethods, kPartDoc, &MaterializeHead};
static constexpr ComponentDescriptor kCell = {
    "TableCell", kCellConstructors, kCellMethods, kPartDoc, &MaterializeCell};
static constexpr ComponentDescriptor kCaption = {"TableCaption",
                                                 kCaptionConstructors,
                                                 {},
                                                 kPartDoc,
                                                 &MaterializeCaption};

static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr MethodDescriptor kTableMethods[] = {
    {"accessibility_label", kLabelArgs,
     "Sets the table's screen-reader accessible name.",
     &RecordAccessibilityLabel},
    {"size", kSizeArgs,
     "Sets the table density and propagates it to typed descendants.",
     &RecordSize},
};
static constexpr ComponentDescriptor kTable = {
    "Table", kTableConstructors, kTableMethods,
    "A simple stateless table composed from typed table-part children.",
    &MaterializeTable};

} // namespace gpui::component_shell::structured::table

namespace gpui::component_shell::structured {

// cell_span.
bool RecordCellSpan(PayloadBuild* build, const ComponentArgument* args,
                    int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(StrL("col_span(span) expects a positive integer"));
    uint64_t span = 0;
    Str error;
    if (!PositiveUsize(args[0].number, StrL("col_span"), &span, &error))
        return build->Fail(error);
    table::CellOp* op = build->New<table::CellOp>();
    op->kind = table::CellOp::ColSpan;
    // A span wider than any row has columns is the widest one there is.
    op->span = span > 0x7fffffff ? 0x7fffffff : (int)span;
    return true;
}

} // namespace gpui::component_shell::structured

namespace gpui::component_shell {

bool RegisterStructuredTable(shell::ComponentRegistry* registry,
                             shell::RegistryError* error) {
    using namespace structured::table;
    return registry->Register(&kHeader, error) &&
           registry->Register(&kBody, error) &&
           registry->Register(&kFooter, error) &&
           registry->Register(&kRow, error) &&
           registry->Register(&kHead, error) &&
           registry->Register(&kCell, error) &&
           registry->Register(&kCaption, error) &&
           registry->Register(&kTable, error);
}

} // namespace gpui::component_shell
