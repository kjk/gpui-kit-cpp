// crates/component-shell/src/shell/delegate_select/mod.rs

#include "component_shell/families.h"
#include "component_shell/searchable_rows.h"
#include "component_shell/support.h"
#include "ui/select.h"

#include <float.h>
#include <math.h>

namespace gpui::component_shell::delegate_select {

using component::SearchableListItem;
using component::SelectEvent;
using component::SelectState;

struct Payload {
    Str id;
    ComponentArgument rows = {};
    ComponentArgument renderRow = {};
    ComponentArgument onSelect = {};
};

// Op. `disabled` is the shell's common behavior here, which the request
// carries as `disabled` rather than as a recorded op.
struct Op {
    enum Kind : uint8_t {
        Placeholder,
        MenuWidth,
    } kind = Placeholder;
    Str text;
    float number = 0;
};

// mod.rs test_probe, widened into a seam: see families.h.
static SelectProbe gProbe = nullptr;

// One frame's rows: Rust's Item keeps its row and the renderer beside the
// id, title and disabled flag. The SearchableListItem is what the retained
// state holds; the row data and the renderer are the frame's, and the row an
// item stands for is its index, since the delegate has one section.
struct Rows {
    ShellRuntime* runtime = nullptr;
    shell::ComponentCallback renderer = {};
    const shell::ComponentDataValue* rows = nullptr;
    int count = 0;
};

// Item::render.
static El* RenderContent(void* user, Ctx* cx, IndexPath path,
                         const SearchableListItem* item) {
    Rows* rows = (Rows*)user;
    Arena* a = cx->a;
    if (path.section != 0 || path.row < 0 || path.row >= rows->count)
        return nullptr;
    Str error;
    El* element = rows->renderer.BuildWith(rows->runtime, &rows->rows[path.row],
                                           1, cx, &error);
    if (element) return element;
    if (len(error))
        return Div(a)->Child(TextEl(
            a, StrDup(a, fmt("Failed to render Select row: %s", error))));
    return Div(a)->Child(TextEl(a, item->title));
}

// Host: the retained SelectState, the items it holds, and the callback the
// latest render resolved (Rust's Rc<RefCell<..>>).
struct Host {
    App* app = nullptr;
    Entity<SelectState> state = {};
    RetainedSearchableItems rows;
    ShellRuntime* runtime = nullptr;
    shell::ComponentCallback callback = {};

    ~Host() {
        if (app && state.IsValid()) EntityDrop(app, state.id);
        rows.Free();
    }

    static void OnSelect(Host* self, Ctx* cx, const SelectEvent* event) {
        if (!self || !event || !event->hasValue) return;
        if (gProbe) gProbe(event->value);
        if (!self->callback.IsSet()) return;
        shell::ComponentDataValue value =
            shell::ComponentDataValue::String(event->value);
        self->callback.InvokeAndReport(self->runtime, "Select.on_select",
                                       &value, 1, cx->win, cx->app);
    }
};

static El* Failure(Ctx* cx, const char* what, Str error) {
    return Div(cx->a)
        ->Child(TextEl(cx->a, StrDup(cx->a, fmt("%s: %s", Str(what), error))));
}

// BoundSelect::render.
static El* Render(MaterializeRequest* request, const Payload* payload,
                  shell::ComponentCallback rowsCallback,
                  shell::ComponentCallback renderer,
                  shell::ComponentCallback onSelect, ElRefiner style) {
    Ctx* cx = request->cx;
    Rows* rows = ArenaNew<Rows>(cx->a);
    rows->runtime = request->runtime;
    rows->renderer = renderer;
    Str error;
    if (!rowsCallback
             .SnapshotRowsWith(request->runtime, nullptr, 0, cx, &rows->rows,
                               &rows->count, cx->a, &error))
        return Failure(cx, "Failed to snapshot Select rows", error);
    SearchableListItem* next = nullptr;
    if (!SearchableItemsFromRows(cx, "Select", rows->rows, rows->count, &next,
                                 &error))
        return Failure(cx, "Invalid Select rows", error);

    Str key = StrDup(cx->a, fmt("shell-select:%s", payload->id));
    Entity<Host> handle = UseKeyedState<Host>(cx, key, StrL("shell-select"));
    Host* host = handle.Get(cx);
    if (!host) return Div(cx->a);
    if (!host->state.IsValid()) {
        host->app = cx->app;
        host->state = SelectState::New(cx->app);
        host->rows.Set(next, rows->count);
        SubscribeTo(cx->app, host->state, handle, &Host::OnSelect);
    }
    SelectState* state = host->state.Get(cx);
    if (!state) return Div(cx->a);
    host->runtime = request->runtime;
    host->callback = onSelect;
    // set_items then set_selected_value on every render: the selection is
    // kept by value across a fresh snapshot.
    // The selected value is read from the host's own copy of the rows: while
    // the list is open the state's item pointer is the previous frame's copy
    // of them, in the same order.
    Str selected = {};
    bool hasSelected = false;
    if (state->state.selected.len > 0) {
        int ix = state->state.selected[0];
        if (ix >= 0 && ix < len(host->rows.items)) {
            selected = StrDup(cx->a, host->rows.items[ix].value);
            hasSelected = true;
        }
    }
    if (!host->rows.Same(next, rows->count)) host->rows.Set(next, rows->count);
    state->SetItems(host->rows.items.els, len(host->rows.items));
    if (hasSelected) state->SetSelectedValue(selected, nullptr);

    component::SearchableListDelegate delegate =
        component::SearchableListDelegate::Items(host->rows.items.els,
                                                 len(host->rows.items));
    delegate.user = rows;
    delegate.renderItemContent = &RenderContent;
    component::Select* select =
        component::Select::New(cx, payload->id, host->state)
            ->Items(host->rows.items.els, len(host->rows.items))
            ->Delegate(delegate);
    EachMethod<Op>(request, [&](const Op& op) {
        if (op.kind == Op::Placeholder)
            select->Placeholder(op.text);
        else
            select->MenuWidth(op.number);
    });
    select->Disabled(request->disabled);
    select->TriggerRefiner(style);
    return select->IntoEl();
}

static El* Materialize(MaterializeRequest* request) {
    const Payload* payload = request->PayloadAs<Payload>();
    if (!payload) return request->Fail(StrL("Select incompatible payload"));
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    if (count != 0)
        return request->Fail(StrL("Select does not accept children"));
    shell::ComponentCallback rows = request->ResolveCallback(payload->rows);
    shell::ComponentCallback renderer =
        request->ResolveCallback(payload->renderRow);
    shell::ComponentCallback onSelect =
        request->ResolveCallback(payload->onSelect);
    if (!rows.IsSet() || !renderer.IsSet() || !onSelect.IsSet())
        return request->Fail(StrL("component argument is not a callback"));
    return Render(request, payload, rows, renderer, onSelect,
                  request->TakeStyle());
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (!IsIdAndThreeCallbacks(args, count)) {
        return build
            ->Fail(StrL("Select expects id, rows callback, row renderer, and "
                        "selection callback"));
    }
    Payload* payload = build->New<Payload>();
    payload->id = args[0].string;
    payload->rows = args[1];
    payload->renderRow = args[2];
    payload->onSelect = args[3];
    return true;
}

static bool RecordPlaceholder(PayloadBuild* build,
                              const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build
            ->Fail(StrL("Select.placeholder received an invalid value"));
    Op* op = build->New<Op>();
    op->kind = Op::Placeholder;
    op->text = args[0].string;
    return true;
}

static bool RecordMenuWidth(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    float width = 0;
    if (!MenuWidthArgument(build, "Select", args, count, &width)) return false;
    Op* op = build->New<Op>();
    op->kind = Op::MenuWidth;
    op->number = width;
    return true;
}

static constexpr ArgumentDescriptor kArguments[] = {
    {"id", SchemaString()},
    {"rows",
     SchemaCallback(
         "() => readonly { id: string; label: string; disabled?: boolean }[]")},
    {"render_row", SchemaCallback("(row: unknown) => Element | null")},
    {"on_select", SchemaCallback("(value: string, cx: Context) => void")},
};
static constexpr ArgumentDescriptor kPlaceholderArgs[] = {
    {"placeholder", SchemaString()}};
static constexpr ArgumentDescriptor kMenuWidthArgs[] = {
    {"menu_width", SchemaNumber()}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Select", kArguments, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"placeholder", kPlaceholderArgs,
     "Sets the text shown while nothing is selected.", &RecordPlaceholder},
    {"menu_width", kMenuWidthArgs, "Sets the popup menu width in pixels.",
     &RecordMenuWidth},
    {"disabled", kDisabledArgs, "Disables the select.", &RecordCommonBehavior},
};
static constexpr ComponentDescriptor kSelect = {
    "Select", kConstructors, kMethods,
    "Native retained single-value Select backed by immutable "
    "`{id,label,disabled?}` snapshots and a lazy row renderer.",
    &Materialize};

} // namespace gpui::component_shell::delegate_select

namespace gpui::component_shell {

void SetSelectProbe(SelectProbe probe) {
    delegate_select::gProbe = probe;
}

bool RegisterDelegateSelect(shell::ComponentRegistry* registry,
                            shell::RegistryError* error) {
    return registry->Register(&delegate_select::kSelect, error);
}

} // namespace gpui::component_shell
