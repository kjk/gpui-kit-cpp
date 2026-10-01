// crates/component-shell/src/shell/delegate_collections/list.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/list.h"

namespace gpui::component_shell::delegate_collections::list {

struct Payload {
    Str id;
    ComponentArgument rows = {};
    ComponentArgument renderRow = {};
};

// list.rs test_probe, widened into a seam: see families.h.
static ListRowProbe gRowProbe = nullptr;

// Delegate: what one frame's rows are. Rust keeps the snapshot and the
// renderer on the retained ListState's delegate and replaces them each
// render; the C++ ListDelegate is handed to the List every frame instead, so
// the frame's copy lives in the frame arena and the selection stays on the
// retained ListState (Rust's `selected`).
struct Delegate {
    ShellRuntime* runtime = nullptr;
    const shell::ComponentDataValue* rows = nullptr;
    int count = 0;
    shell::ComponentCallback renderRow = {};
};

// Delegate::row_id: an object row's string `id`, else `row-{index}`.
static Str RowId(Arena* a, const shell::ComponentDataValue& row, int index) {
    if (const shell::ComponentDataValue* id = row.Get(StrL("id"))) {
        if (id->kind == shell::DataKind::String) return id->string;
    }
    return StrDup(a, fmt("row-%d", index));
}

static int ItemsCount(Ctx*, void* data, int section) {
    Delegate* delegate = (Delegate*)data;
    return section == 0 ? delegate->count : 0;
}

static component::ListItem* RenderItem(Ctx* cx, void* data, int, int row, int) {
    Delegate* delegate = (Delegate*)data;
    Arena* a = cx->a;
    if (row < 0 || row >= delegate->count) {
        // ComponentDelegateSnapshot::row's error.
        return component::ListItem::New(
            cx, TextEl(a, StrDup(a, fmt("Failed to read List row: delegate "
                                        "row index %d is out of bounds for "
                                        "%d rows",
                                        row, delegate->count))));
    }
    const shell::ComponentDataValue& value = delegate->rows[row];
    Str id = RowId(a, value, row);
    Str error;
    El* child = delegate->renderRow
                    .BuildWith(delegate->runtime, &value, 1, cx, &error);
    if (child) {
        if (gRowProbe) gRowProbe(id);
    } else if (len(error)) {
        child = Div(a)->Child(
            TextEl(a, StrDup(a, fmt("Failed to render List row: %s", error))));
    } else {
        child = Div(a);
    }
    // ListItem::new(id): the port's List names each row by its index path
    // rather than by the id the row carries; the id is what the probe sees.
    return component::ListItem::New(cx, child);
}

// The retained ListState, owned by the keyed state it is kept in.
struct Host {
    App* app = nullptr;
    Entity<ListState> state = {};

    ~Host() {
        if (app && state.IsValid()) EntityDrop(app, state.id);
    }
};

static El* Failure(Ctx* cx, const char* what, Str error) {
    return Div(cx->a)
        ->Child(TextEl(cx->a, StrDup(cx->a, fmt("%s: %s", Str(what), error))));
}

// BoundList::render.
static El* Render(MaterializeRequest* request, const Payload* payload,
                  shell::ComponentCallback rows,
                  shell::ComponentCallback renderRow, ElRefiner style) {
    Ctx* cx = request->cx;
    Delegate* delegate = ArenaNew<Delegate>(cx->a);
    delegate->runtime = request->runtime;
    delegate->renderRow = renderRow;
    Str error;
    if (!rows.SnapshotRowsWith(request->runtime, nullptr, 0, cx,
                               &delegate->rows, &delegate->count, cx->a,
                               &error)) {
        return Failure(cx, "Failed to snapshot List rows", error);
    }
    Host* host = UseKeyedState<Host>(cx, payload->id, StrL("shell-list"))
                     .Get(cx);
    if (!host) return Div(cx->a);
    if (!host->state.IsValid()) {
        host->app = cx->app;
        host->state = EntityNewState<ListState>(cx->app);
    }
    component::ListDelegate native;
    native.data = delegate;
    native.itemsCount = &ItemsCount;
    native.renderItem = &RenderItem;
    component::List* list = component::List::New(cx, payload->id, host->state)
                                ->WithDelegate(native);
    // The list fills its box and works out what it needs before layout from
    // the height it was laid out at last frame, where GPUI's v_virtual_list
    // reads its bounds at prepaint. A definite height in the script's style
    // is what the first frame builds with; the root is `size_full()`
    // otherwise, and the style refines it, as in Rust.
    if (style.IsSet()) {
        El* probe = Div(cx->a);
        style.Apply(probe);
        if (probe->style.height > 0 && probe->style.height != kAuto)
            list->H(probe->style.height);
    }
    El* root = list->IntoEl();
    style.Apply(root);
    return root;
}

static El* Materialize(MaterializeRequest* request) {
    const Payload* payload = request->PayloadAs<Payload>();
    if (!payload) return request->Fail(StrL("List incompatible payload"));
    shell::ComponentCallback rows = request->ResolveCallback(payload->rows);
    shell::ComponentCallback renderRow =
        request->ResolveCallback(payload->renderRow);
    if (!rows.IsSet() || !renderRow.IsSet())
        return request->Fail(StrL("component argument is not a callback"));
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    if (count != 0)
        return request
            ->Fail(StrL("List does not accept children; rows come from its "
                        "immutable delegate snapshot"));
    return Render(request, payload, rows, renderRow, request->TakeStyle());
}

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 3 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::Callback ||
        args[2].kind != shell::ComponentArgumentKind::Callback ||
        len(StrTrimAscii(args[0].string)) == 0) {
        return build
            ->Fail(StrL("List expects a non-empty id, rows callback, and row "
                        "renderer"));
    }
    Payload* payload = build->New<Payload>();
    payload->id = args[0].string;
    payload->rows = args[1];
    payload->renderRow = args[2];
    return true;
}

static constexpr ArgumentDescriptor kArguments[] = {
    {"id", SchemaString()},
    {"rows", SchemaCallback("() => readonly unknown[]")},
    {"render_row", SchemaCallback("(row: unknown) => Element | null")},
};
static constexpr ConstructorDescriptor kConstructors[] = {
    {"List", kArguments, &Construct}};
static constexpr ComponentDescriptor kList = {
    "List",
    kConstructors,
    {},
    "Native retained List backed by an immutable rows snapshot. Each row is "
    "lazily rendered; object rows should provide a stable string `id`.",
    &Materialize};

} // namespace gpui::component_shell::delegate_collections::list

namespace gpui::component_shell {

void SetListRowProbe(ListRowProbe probe) {
    delegate_collections::list::gRowProbe = probe;
}

bool RegisterDelegateCollectionsList(shell::ComponentRegistry* registry,
                                     shell::RegistryError* error) {
    return registry->Register(&delegate_collections::list::kList, error);
}

} // namespace gpui::component_shell
