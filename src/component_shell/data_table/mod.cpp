// crates/component-shell/src/shell/data_table/mod.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/table.h"

namespace gpui::component_shell::data_table {

// mod.rs test_probe, widened into a seam: see families.h.
static DataTableProbe gProbe = nullptr;

// The retained DataTableState: the native TableState and the column keys the
// Delegate was made with. Rust keeps the columns on the state's delegate; the
// C++ TableDelegate is handed to the DataTable every frame, so the keys live
// here and the frame's delegate reads them.
struct State {
    App* app = nullptr;
    Entity<TableState> entity = {};
    Str* keys = nullptr;
    int count = 0;

    ~State() {
        for (int i = 0; i < count; i++) StrFree(keys[i]);
        free(keys);
        if (app && entity.IsValid()) EntityDrop(app, entity.id);
    }
};

// Delegate: the columns, the frame's rows snapshot and the cell renderer.
// Rust replaces `rows` and `render_cell` on the retained delegate each render;
// here the frame's copy lives in the frame arena.
struct Delegate {
    ShellRuntime* runtime = nullptr;
    const State* state = nullptr;
    const shell::ComponentDataValue* rows = nullptr;
    int count = 0;
    shell::ComponentCallback renderCell = {};
};

static int ColumnsCount(Ctx*, void* data) {
    return ((Delegate*)data)->state->count;
}
static int RowsCount(Ctx*, void* data) {
    return ((Delegate*)data)->count;
}
// Column::new(key, key).
static component::TableColumn Column(Ctx*, void* data, int col) {
    Str key = ((Delegate*)data)->state->keys[col];
    return component::TableColumn::New(key, key);
}

static El* CellFailure(Ctx* cx, Str error) {
    if (gProbe) gProbe(false);
    Arena* a = cx->a;
    return Div(a)->Child(TextEl(
        a, StrDup(a, fmt("Failed to render DataTable cell: %s", error))));
}

// render_td: the script's renderer with (row, column key).
static El* RenderTd(Ctx* cx, void* data, int row, int col) {
    Delegate* delegate = (Delegate*)data;
    Arena* a = cx->a;
    if (!delegate->renderCell.IsSet())
        return CellFailure(cx, StrL("DataTable cell renderer is unavailable"));
    if (row < 0 || row >= delegate->count) {
        return CellFailure(
            cx, StrDup(a, fmt("delegate row index %d is out of bounds for %d "
                              "rows",
                              row, delegate->count)));
    }
    if (col < 0 || col >= delegate->state->count) {
        return CellFailure(
            cx,
            StrDup(a, fmt("DataTable column index %d is out of bounds", col)));
    }
    shell::ComponentDataValue args[2] = {
        delegate->rows[row],
        shell::ComponentDataValue::String(delegate->state->keys[col])};
    Str error;
    El* element = delegate->renderCell
                      .BuildWith(delegate->runtime, args, 2, cx, &error);
    if (element) {
        if (gProbe) gProbe(true);
        return element;
    }
    if (len(error)) return CellFailure(cx, error);
    return Div(a);
}

// cell_text: an object row's field under the column's key, as text.
static Str CellText(Ctx* cx, void* data, int row, int col) {
    Delegate* delegate = (Delegate*)data;
    if (col < 0 || col >= delegate->state->count) return {};
    if (row < 0 || row >= delegate->count) return {};
    const shell::ComponentDataValue& value = delegate->rows[row];
    if (value.kind != shell::DataKind::Object) return {};
    const shell::ComponentDataValue* field =
        value.Get(delegate->state->keys[col]);
    if (!field) return {};
    switch (field->kind) {
        case shell::DataKind::String:
            return field->string;
        case shell::DataKind::Number:
            return StrDup(cx->a, F64DisplayTemp(field->number));
        case shell::DataKind::Boolean:
            return field->boolean ? StrL("true") : StrL("false");
        default:
            return {};
    }
}

struct Payload {
    ComponentArgument state = {};
    ComponentArgument rows = {};
    ComponentArgument cell = {};
};

struct Op {
    enum Kind : uint8_t {
        Stripe,
        Bordered,
        Scrollbars,
        RowSelectable,
        ColSelectable,
        CellSelectable,
        RowHeader,
        Sortable,
        ColResizable,
        ColMovable,
    } kind = Stripe;
    bool value = false;
    bool horizontal = false;
};

static El* Failure(Ctx* cx, Str message) {
    if (gProbe) gProbe(false);
    return Div(cx->a)->Child(TextEl(cx->a, message));
}

// DataTableHost::render.
static El* Render(MaterializeRequest* request, State* state,
                  shell::ComponentCallback rows, shell::ComponentCallback cell,
                  ElRefiner style) {
    Ctx* cx = request->cx;
    Arena* a = cx->a;
    Delegate* delegate = ArenaNew<Delegate>(a);
    delegate->runtime = request->runtime;
    delegate->state = state;
    delegate->renderCell = cell;
    Str error;
    if (!rows.SnapshotRowsWith(request->runtime, nullptr, 0, cx,
                               &delegate->rows, &delegate->count, a, &error)) {
        return Failure(
            cx, StrDup(a, fmt("DataTable rows callback must return an array "
                              "of rows: %s",
                              error)));
    }
    if (TableState* native = state->entity.Get(cx)) {
        EachMethod<Op>(request, [&](const Op& op) {
            switch (op.kind) {
                case Op::RowSelectable:
                    native->rowSelectable = op.value;
                    break;
                case Op::ColSelectable:
                    native->colSelectable = op.value;
                    break;
                case Op::CellSelectable:
                    native->cellSelectable = op.value;
                    break;
                case Op::RowHeader:
                    native->rowHeader = op.value;
                    break;
                case Op::Sortable:
                    native->sortable = op.value;
                    break;
                case Op::ColResizable:
                    native->colResizable = op.value;
                    break;
                case Op::ColMovable:
                    native->colMovable = op.value;
                    break;
                default:
                    break;
            }
        });
        // state.refresh(cx): the column groups are taken from the delegate
        // again, every render, as Rust does.
        TableRefreshCols(native);
    }
    component::TableDelegate native;
    native.data = delegate;
    native.columnsCount = &ColumnsCount;
    native.rowsCount = &RowsCount;
    native.column = &Column;
    native.renderTd = &RenderTd;
    native.cellText = &CellText;
    component::DataTable* table =
        component::DataTable::New(cx, request->elementId, state->entity)
            ->Delegate(native);
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::Stripe:
                table->Stripe(op.value);
                break;
            case Op::Bordered:
                table->Bordered(op.value);
                break;
            case Op::Scrollbars:
                table->ScrollbarVisible(op.value, op.horizontal);
                break;
            default:
                break;
        }
    });
    // `div().size_full().child(table)`, refined by the script's style.
    // The port's table virtualizes its rows only when it is told the body's
    // height as a number, where GPUI's takes it from layout: the body the
    // host was laid out around last frame — the host's content box less the
    // head row and the table's border (UseLaidOutHeight) — and on the first
    // frame a definite height in the script's style, less the same.
    El* host = Div(a)->W(kFill)->H(kFill);
    // The table fills the host, its own border inside it.
    float chrome = table->rowHeight + (table->bordered ? 2.f : 0.f);
    float body = -1;
    if (style.IsSet()) {
        style.Apply(host);
        float h = host->style.height;
        if (h > 0) body = h - chrome;
    }
    LaidOutHeight* laid = UseLaidOutHeight(cx, request->elementId, body);
    if (laid) {
        laid->contentBox = true;
        laid->inset = chrome;
        body = laid->built;
    }
    if (body > 0) table->H(body);
    host->Child(table->IntoEl());
    TrackLaidOutHeight(cx, host, laid);
    return host;
}

static El* Materialize(MaterializeRequest* request) {
    const Payload* payload = request->PayloadAs<Payload>();
    if (!payload)
        return request
            ->Fail(StrL("DataTable received an incompatible payload"));
    if (request->ChildrenLen() != 0)
        return request->Fail(StrL("DataTable does not accept children"));
    State* state = request->StateAs<State>(payload->state, "DataTableState");
    if (!state) return nullptr;
    shell::ComponentCallback rows = request->ResolveCallback(payload->rows);
    shell::ComponentCallback cell = request->ResolveCallback(payload->cell);
    if (!rows.IsSet() || !cell.IsSet())
        return request->Fail(StrL("component argument is not a callback"));
    return Render(request, state, rows, cell, request->TakeStyle());
}

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 3 || args[0].kind != shell::ComponentArgumentKind::Entity ||
        args[1].kind != shell::ComponentArgumentKind::Callback ||
        args[2].kind != shell::ComponentArgumentKind::Callback) {
        return build->Fail(
            StrL("DataTable expects DataTableState, rows callback and cell "
                 "renderer"));
    }
    Payload* payload = build->New<Payload>();
    payload->state = args[0];
    payload->rows = args[1];
    payload->cell = args[2];
    return true;
}

static const char* OpName(Op::Kind kind) {
    switch (kind) {
        case Op::Stripe:
            return "stripe";
        case Op::Bordered:
            return "bordered";
        case Op::RowSelectable:
            return "row_selectable";
        case Op::ColSelectable:
            return "column_selectable";
        case Op::CellSelectable:
            return "cell_selectable";
        case Op::RowHeader:
            return "row_header";
        case Op::Sortable:
            return "sortable";
        case Op::ColResizable:
            return "column_resizable";
        case Op::ColMovable:
            return "column_movable";
        default:
            return "scrollbar_visible";
    }
}

// bool_method("DataTable", name, "Sets native DataTable behavior.", ..).
template <Op::Kind K>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build
            ->Fail(fmt("DataTable.%s expects one boolean", Str(OpName(K))));
    Op* op = build->New<Op>();
    op->kind = K;
    op->value = args[0].boolean;
    return true;
}

static bool RecordScrollbars(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::Boolean ||
        args[1].kind != shell::ComponentArgumentKind::Boolean)
        return build
            ->Fail(StrL("DataTable.scrollbar_visible expects two booleans"));
    Op* op = build->New<Op>();
    op->kind = Op::Scrollbars;
    op->value = args[0].boolean;
    op->horizontal = args[1].boolean;
    return true;
}

static bool NewState(shell::StateBuild* build, const ComponentArgument* args,
                     int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Array)
        return build->Fail(StrL("DataTableState expects a string array"));
    const ComponentArgument& columns = args[0];
    for (int i = 0; i < columns.count; i++) {
        const ComponentArgument& column = columns.items[i];
        if (column.kind != shell::ComponentArgumentKind::String ||
            len(StrTrimAscii(column.string)) == 0)
            return build->Fail(
                StrL("DataTableState columns must be non-empty strings"));
    }
    if (columns.count == 0)
        return build->Fail(StrL("DataTableState requires at least one column"));
    for (int i = 0; i < columns.count; i++) {
        for (int j = 0; j < i; j++) {
            if (StrEq(columns.items[i].string, columns.items[j].string))
                return build
                    ->Fail(StrL("DataTableState column keys must be unique"));
        }
    }
    State* state = build->New<State>();
    state->app = build->app;
    state->keys = (Str*)calloc((size_t)(uint32_t)columns.count, sizeof(Str));
    state->count = columns.count;
    for (int i = 0; i < columns.count; i++)
        state->keys[i] = StrDup(columns.items[i].string);
    state->entity = EntityNewState<TableState>(build->app);
    return true;
}

static constexpr ArgumentSchema kColumnSchema = SchemaString();
static constexpr ArgumentDescriptor kStateArgs[] = {
    {"columns", SchemaArray(&kColumnSchema)}};
static constexpr shell::StateDescriptor kDataTableState = {
    "DataTableState",
    "DataTableState",
    kStateArgs,
    "Retained native DataTable focus, selection, scrolling, measurement and "
    "column state.",
    &NewState,
    {}};

static constexpr ArgumentDescriptor kArguments[] = {
    {"state", SchemaEntity("DataTableState")},
    {"rows", SchemaCallback("(cx: Context) => readonly unknown[]")},
    {"render_cell",
     SchemaCallback("(row: unknown, column: string, cx: Context) => Element")},
};
static constexpr ConstructorDescriptor kConstructors[] = {
    {"DataTable", kArguments, &Construct}};

static constexpr const char* kBehaviorDoc = "Sets native DataTable behavior.";
static constexpr ArgumentDescriptor kStripeArgs[] = {
    {"stripe", SchemaBoolean()}};
static constexpr ArgumentDescriptor kBorderedArgs[] = {
    {"bordered", SchemaBoolean()}};
static constexpr ArgumentDescriptor kScrollbarArgs[] = {
    {"vertical", SchemaBoolean()},
    {"horizontal", SchemaBoolean()}};
static constexpr ArgumentDescriptor kRowSelectableArgs[] = {
    {"row_selectable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kColSelectableArgs[] = {
    {"column_selectable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kCellSelectableArgs[] = {
    {"cell_selectable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kRowHeaderArgs[] = {
    {"row_header", SchemaBoolean()}};
static constexpr ArgumentDescriptor kSortableArgs[] = {
    {"sortable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kColResizableArgs[] = {
    {"column_resizable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kColMovableArgs[] = {
    {"column_movable", SchemaBoolean()}};

static constexpr MethodDescriptor kMethods[] = {
    {"stripe", kStripeArgs, kBehaviorDoc, &RecordBool<Op::Stripe>},
    {"bordered", kBorderedArgs, kBehaviorDoc, &RecordBool<Op::Bordered>},
    {"scrollbar_visible", kScrollbarArgs,
     "Chooses when the table shows its scrollbars.", &RecordScrollbars},
    {"row_selectable", kRowSelectableArgs, kBehaviorDoc,
     &RecordBool<Op::RowSelectable>},
    {"column_selectable", kColSelectableArgs, kBehaviorDoc,
     &RecordBool<Op::ColSelectable>},
    {"cell_selectable", kCellSelectableArgs, kBehaviorDoc,
     &RecordBool<Op::CellSelectable>},
    {"row_header", kRowHeaderArgs, kBehaviorDoc, &RecordBool<Op::RowHeader>},
    {"sortable", kSortableArgs, kBehaviorDoc, &RecordBool<Op::Sortable>},
    {"column_resizable", kColResizableArgs, kBehaviorDoc,
     &RecordBool<Op::ColResizable>},
    {"column_movable", kColMovableArgs, kBehaviorDoc,
     &RecordBool<Op::ColMovable>},
};

static constexpr ComponentDescriptor kDataTable = {
    "DataTable", kConstructors, kMethods,
    "A real retained native DataTable. Rows are captured as an immutable "
    "plain-data snapshot and visible cells are built lazily from (row, "
    "column). Style applies to the full-size table host.",
    &Materialize};

} // namespace gpui::component_shell::data_table

namespace gpui::component_shell {

void SetDataTableProbe(DataTableProbe probe) {
    data_table::gProbe = probe;
}

bool RegisterDataTable(shell::ComponentRegistry* registry,
                       shell::RegistryError* error) {
    return registry->RegisterState(&data_table::kDataTableState, error) &&
           registry->Register(&data_table::kDataTable, error);
}

} // namespace gpui::component_shell
