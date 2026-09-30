// crates/component-shell/src/shell/delegate_combobox/mod.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/combobox.h"

#include <float.h>
#include <math.h>

namespace gpui::component_shell::delegate_combobox {

using component::ComboboxEvent;
using component::ComboboxEventKind;
using component::ComboboxState;
using component::SearchableListItem;

struct Payload {
    Str id;
    ComponentArgument rows = {};
    ComponentArgument onChange = {};
    ComponentArgument onConfirm = {};
};

// Op. `disabled` is the shell's common behavior here, which the request
// carries as `disabled` rather than as a recorded op.
struct Op {
    enum Kind : uint8_t {
        Placeholder,
        SearchPlaceholder,
        Searchable,
        MenuWidth,
    } kind = Placeholder;
    Str text;
    bool flag = false;
    float number = 0;
};

// mod.rs test_probe, widened into a seam: see families.h.
static ComboboxEventProbe gProbe = nullptr;

// Item / Delegate: a row is a SearchableListItem whose value is the row's id
// and whose title is its label. The delegate's `all` is the snapshot the host
// keeps; the query filtering Rust's perform_search does is the list's own
// (SearchableItemMatches, the same case-insensitive title match).
static bool SameItems(const SearchableListItem* a, int aCount,
                      const SearchableListItem* b, int bCount) {
    if (aCount != bCount) return false;
    for (int i = 0; i < aCount; i++) {
        if (!StrEq(a[i].value, b[i].value) || !StrEq(a[i].title, b[i].title) ||
            a[i].disabled != b[i].disabled)
            return false;
    }
    return true;
}

// snapshot(): the rows, each requiring a string `id` and `label`.
static bool Snapshot(MaterializeRequest* request,
                     shell::ComponentCallback callback,
                     SearchableListItem** items, int* count, Str* error) {
    Ctx* cx = request->cx;
    const shell::ComponentDataValue* rows = nullptr;
    int n = 0;
    if (!callback.SnapshotRowsWith(request->runtime, nullptr, 0, cx, &rows, &n,
                                   cx->a, error))
        return false;
    SearchableListItem* out =
        n ? (SearchableListItem*)Alloc(cx->a,
                                       (int)sizeof(SearchableListItem) * n)
          : nullptr;
    for (int i = 0; i < n; i++) {
        const shell::ComponentDataValue* id = rows[i].Get(StrL("id"));
        if (!id || id->kind != shell::DataKind::String) {
            *error =
                StrDup(cx->a, fmt("Combobox row %d requires a string `id`", i));
            return false;
        }
        const shell::ComponentDataValue* label = rows[i].Get(StrL("label"));
        if (!label || label->kind != shell::DataKind::String) {
            *error = StrDup(
                cx->a, fmt("Combobox row %d requires a string `label`", i));
            return false;
        }
        const shell::ComponentDataValue* disabled = rows[i]
                                                        .Get(StrL("disabled"));
        out[i] = SearchableListItem{};
        out[i].value = id->string;
        out[i].title = label->string;
        out[i].disabled = disabled &&
                          disabled->kind == shell::DataKind::Boolean &&
                          disabled->boolean;
    }
    *items = out;
    *count = n;
    return true;
}

// Host: the retained ComboboxState, the rows it was last given, and the
// callbacks the latest render resolved (Rust's Rc<RefCell<Callbacks>>).
struct Host {
    App* app = nullptr;
    Entity<Host> self = {};
    Entity<ComboboxState> state = {};
    // The rows the state holds, with their strings in `arena`.
    Arena* arena = nullptr;
    Vec<SearchableListItem> items;
    ShellRuntime* runtime = nullptr;
    shell::ComponentCallback change = {};
    shell::ComponentCallback confirm = {};

    ~Host() {
        if (app && state.IsValid()) EntityDrop(app, state.id);
        VecReset(items);
        if (arena) ArenaDelete(arena);
    }

    // Takes a copy of `next` as the rows, replacing the previous ones.
    void SetRows(const SearchableListItem* next, int count) {
        Arena* fresh = ArenaNew();
        VecClear(items);
        for (int i = 0; i < count; i++) {
            SearchableListItem item = next[i];
            item.value = StrDup(fresh, next[i].value);
            item.title = StrDup(fresh, next[i].title);
            VecAppend(items, item);
        }
        if (arena) ArenaDelete(arena);
        arena = fresh;
    }

    // report_values: the first value, if any.
    static void Report(Host* self, Ctx* cx, shell::ComponentCallback callback,
                       const char* phase, const ComboboxEvent* event) {
        if (!callback.IsSet() || event->nValues == 0) return;
        shell::ComponentDataValue value =
            shell::ComponentDataValue::String(event->values[0]);
        callback
            .InvokeAndReport(self->runtime, phase, &value, 1, cx->win, cx->app);
    }

    static void OnEvent(Host* self, Ctx* cx, const ComboboxEvent* event) {
        if (!self || !event) return;
        bool confirm = event->kind == ComboboxEventKind::Confirm;
        if (gProbe) gProbe(confirm, event->values, event->nValues);
        if (confirm)
            Report(self, cx, self->confirm, "Combobox.on_confirm", event);
        else
            Report(self, cx, self->change, "Combobox.on_change", event);
    }
};

// Bound::render.
static El* Render(MaterializeRequest* request, const Payload* payload,
                  shell::ComponentCallback rows,
                  shell::ComponentCallback change,
                  shell::ComponentCallback confirm, ElRefiner style) {
    Ctx* cx = request->cx;
    SearchableListItem* next = nullptr;
    int count = 0;
    Str error;
    if (!Snapshot(request, rows, &next, &count, &error)) {
        return Div(cx->a)->Child(TextEl(
            cx->a, StrDup(cx->a, fmt("Invalid Combobox rows: %s", error))));
    }
    bool searchable = true;
    EachMethod<Op>(request, [&](const Op& op) {
        if (op.kind == Op::Searchable) searchable = op.flag;
    });
    Str key = StrDup(cx->a, fmt("shell-combobox:%s:%s", payload->id,
                                Str(searchable ? "true" : "false")));
    Entity<Host> handle = UseKeyedState<Host>(cx, key, StrL("shell-combobox"));
    Host* host = handle.Get(cx);
    if (!host) return Div(cx->a);
    if (!host->state.IsValid()) {
        host->app = cx->app;
        host->self = handle;
        host->state = ComboboxState::New(cx->app);
        if (ComboboxState* state = host->state.Get(cx)) {
            state->Multiple(false)->Searchable(searchable);
        }
        host->SetRows(next, count);
        // The two window.subscribe calls: one handler hears both events and
        // routes each to its callback.
        SubscribeTo(cx->app, host->state, handle, &Host::OnEvent);
    }
    ComboboxState* state = host->state.Get(cx);
    if (!state) return Div(cx->a);
    host->runtime = request->runtime;
    host->change = change;
    host->confirm = confirm;
    if (!SameItems(host->items.els, len(host->items), next, count)) {
        Str selected = StrDup(cx->a, state->SelectedValue());
        bool hasSelected = state->state.selected.len > 0;
        host->SetRows(next, count);
        state->SetItems(host->items.els, len(host->items));
        if (hasSelected) state->SetSelectedValues(&selected, 1, nullptr);
    }
    component::Combobox* combobox =
        component::Combobox::New(cx, payload->id, host->state)
            ->Items(host->items.els, len(host->items));
    // Combobox::New makes the list searchable, which is its default; the
    // retained state's own setting is what this host was keyed by.
    state->Searchable(searchable);
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::Placeholder:
                combobox->Placeholder(op.text);
                break;
            case Op::SearchPlaceholder:
                combobox->SearchPlaceholder(op.text);
                break;
            case Op::Searchable:
                break;
            case Op::MenuWidth:
                combobox->MenuWidth(op.number);
                break;
        }
    });
    combobox->Disabled(request->disabled);
    combobox->Refiner(style);
    return combobox->IntoEl();
}

static El* Materialize(MaterializeRequest* request) {
    const Payload* payload = request->PayloadAs<Payload>();
    if (!payload) return request->Fail(StrL("Combobox incompatible payload"));
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    if (count != 0)
        return request->Fail(StrL("Combobox does not accept children"));
    shell::ComponentCallback rows = request->ResolveCallback(payload->rows);
    shell::ComponentCallback change = request
                                          ->ResolveCallback(payload->onChange);
    shell::ComponentCallback confirm =
        request->ResolveCallback(payload->onConfirm);
    if (!rows.IsSet() || !change.IsSet() || !confirm.IsSet())
        return request->Fail(StrL("component argument is not a callback"));
    return Render(request, payload, rows, change, confirm,
                  request->TakeStyle());
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 4 || args[0].kind != shell::ComponentArgumentKind::String ||
        args[1].kind != shell::ComponentArgumentKind::Callback ||
        args[2].kind != shell::ComponentArgumentKind::Callback ||
        args[3].kind != shell::ComponentArgumentKind::Callback ||
        len(StrTrimAscii(args[0].string)) == 0) {
        return build
            ->Fail(StrL("Combobox expects id, rows, on_change, and on_confirm "
                        "callbacks"));
    }
    Payload* payload = build->New<Payload>();
    payload->id = args[0].string;
    payload->rows = args[1];
    payload->onChange = args[2];
    payload->onConfirm = args[3];
    return true;
}

// method(name, ..): the argument is named after the method, and a value the
// op cannot take is "Combobox.{name} received an invalid value".
static bool Invalid(PayloadBuild* build, const char* name) {
    return build->Fail(fmt("Combobox.%s received an invalid value", Str(name)));
}

template <Op::Kind K>
static bool RecordText(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    const char* name =
        K == Op::Placeholder ? "placeholder" : "search_placeholder";
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return Invalid(build, name);
    Op* op = build->New<Op>();
    op->kind = K;
    op->text = args[0].string;
    return true;
}

static bool RecordSearchable(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return Invalid(build, "searchable");
    Op* op = build->New<Op>();
    op->kind = Op::Searchable;
    op->flag = args[0].boolean;
    return true;
}

static bool RecordMenuWidth(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number ||
        !isfinite(args[0].number) || args[0].number <= 0 ||
        args[0].number > (double)FLT_MAX)
        return Invalid(build, "menu_width");
    Op* op = build->New<Op>();
    op->kind = Op::MenuWidth;
    op->number = (float)args[0].number;
    return true;
}

static constexpr ArgumentDescriptor kArguments[] = {
    {"id", SchemaString()},
    {"rows",
     SchemaCallback(
         "() => readonly { id: string; label: string; disabled?: boolean }[]")},
    {"on_change", SchemaCallback("(value: string, cx: Context) => void")},
    {"on_confirm", SchemaCallback("(value: string, cx: Context) => void")},
};
static constexpr ArgumentDescriptor kPlaceholderArgs[] = {
    {"placeholder", SchemaString()}};
static constexpr ArgumentDescriptor kSearchPlaceholderArgs[] = {
    {"search_placeholder", SchemaString()}};
static constexpr ArgumentDescriptor kSearchableArgs[] = {
    {"searchable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kMenuWidthArgs[] = {
    {"menu_width", SchemaNumber()}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Combobox", kArguments, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"placeholder", kPlaceholderArgs,
     "Sets the text shown while nothing is selected.",
     &RecordText<Op::Placeholder>},
    {"search_placeholder", kSearchPlaceholderArgs,
     "Sets the text shown in the empty search field.",
     &RecordText<Op::SearchPlaceholder>},
    {"searchable", kSearchableArgs,
     "Shows the search field above the item list.", &RecordSearchable},
    {"disabled", kDisabledArgs, "Disables the combobox.",
     &RecordCommonBehavior},
    {"menu_width", kMenuWidthArgs, "Sets the popup menu width in pixels.",
     &RecordMenuWidth},
};
static constexpr ComponentDescriptor kCombobox = {
    "Combobox", kConstructors, kMethods,
    "Native retained single-select searchable Combobox backed by immutable "
    "`{id,label,disabled?}` snapshots.",
    &Materialize};

} // namespace gpui::component_shell::delegate_combobox

namespace gpui::component_shell {

void SetComboboxEventProbe(ComboboxEventProbe probe) {
    delegate_combobox::gProbe = probe;
}

bool RegisterDelegateCombobox(shell::ComponentRegistry* registry,
                              shell::RegistryError* error) {
    return registry->Register(&delegate_combobox::kCombobox, error);
}

} // namespace gpui::component_shell
