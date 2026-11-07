// crates/component-shell/src/shell/grid_layout.rs
//
// The script owns which items exist and each item's limits. The user owns
// where they sit. A GridItem's placement seeds the state the first time
// that item appears; after that the state keeps the user's place.

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "shell/style.h"
#include "ui/grid_layout.h"

#include <math.h>

namespace gpui::component_shell::grid_layout {

using component::grid_layout;
using shell::ComponentArgument;
using shell::ComponentArgumentKind;
using shell::ComponentDataValue;
using shell::SpecOp;
using shell::SpecOpKind;

const char* kKind = "GridLayoutState";

enum class Kind : uint8_t {
    Root,
    Item,
    Handle
};

struct Payload {
    Kind kind = Kind::Root;
    Str id = {};
    ComponentArgument state = {};
};

enum class OpKind : uint8_t {
    AccessibilityLabel,
    Cols,
    Compaction,
    RowHeight,
    PreventCollision,
    Draggable,
    Resizable,
    Reflow,
    OnChange,
    OnSelect,
    Col,
    Row,
    ColSpan,
    RowSpan,
    MinSpan,
    MaxSpan,
    Locked,
    DragHandleOnly,
};

struct Op {
    OpKind kind = OpKind::AccessibilityLabel;
    Str text = {};
    bool flag = false;
    bool hasCompaction = false;
    Axis axis = Axis::Vertical;
    int count = 0;
    int count2 = 0;
    float length = 0;
    ComponentArgument callback = {};
};

// What a GridItem hands its parent: the item, and the area that seeds the
// state when the item first appears.
struct DeclaredItem {
    GridItem* item = nullptr;
    GridArea seed = {};
};

// The rules a script set on the root. A field stays unset until a method
// names it, and the last call wins.
struct Rules {
    bool hasCols = false;
    int cols = 0;
    bool hasCompaction = false;
    bool compactionOn = false;
    Axis axis = Axis::Vertical;
    bool hasPrevent = false;
    bool prevent = false;
    bool hasDraggable = false;
    bool draggable = false;
    bool hasResizable = false;
    bool resizable = false;
};

struct Callbacks {
    ShellRuntime* runtime = nullptr;
    shell::ComponentCallback onChange = {};
    shell::ComponentCallback onSelect = {};
};

struct EventHost {
    Callbacks callbacks = {};
    bool subscribed = false;

    static void OnEvent(EventHost* self, Ctx* cx, const GridLayoutEvent* ev) {
        if (!self || !ev || !cx) return;
        if (ev->kind == GridLayoutEventKind::Change && self->callbacks.onChange
                                                           .IsSet()) {
            ComponentDataValue layout =
                GridLayoutLayoutArgument(cx->a, ev->areas, ev->nAreas);
            self->callbacks.onChange.InvokeAndReport(
                self->callbacks.runtime, "GridLayout.on_change callback failed",
                &layout, 1, cx->win, cx->app);
        } else if (ev->kind == GridLayoutEventKind::Select && ev->hasId &&
                   self->callbacks.onSelect.IsSet()) {
            ComponentDataValue id = ComponentDataValue::String(ev->id);
            self->callbacks.onSelect.InvokeAndReport(
                self->callbacks.runtime, "GridLayout.on_select callback failed",
                &id, 1, cx->win, cx->app);
        }
    }
};

static bool Nonnegative(const ComponentArgument& argument, double* out) {
    if (argument.kind != ComponentArgumentKind::Number ||
        !IsUsizeNumber(argument.number))
        return false;
    *out = argument.number;
    return true;
}

static bool Positive(const ComponentArgument& argument, double* out) {
    double value = 0;
    if (!Nonnegative(argument, &value) || value == 0) return false;
    *out = value;
    return true;
}

static bool PositiveLength(const ComponentArgument& argument, float* out) {
    if (argument.kind != ComponentArgumentKind::Number) return false;
    double value = argument.number;
    if (!isfinite(value) || value <= 0) return false;
    *out = (float)value;
    return true;
}

static bool Nonempty(const ComponentArgument& argument) {
    return argument.kind == ComponentArgumentKind::String &&
           len(StrTrim(argument.string)) != 0;
}

static bool IsEntity(const ComponentArgument& argument) {
    return argument.kind == ComponentArgumentKind::Entity;
}

// Some(axis), Some(none), or unknown.
enum class Compaction : uint8_t {
    Unknown,
    None,
    Vertical,
    Horizontal
};

static Compaction CompactionOf(Str value) {
    if (StrEq(value, StrL("vertical"))) return Compaction::Vertical;
    if (StrEq(value, StrL("horizontal"))) return Compaction::Horizontal;
    if (StrEq(value, StrL("none"))) return Compaction::None;
    return Compaction::Unknown;
}

static bool SameScript(const GridArea& a, const GridArea& b) {
    return StrEq(a.id, b.id) && a.minColSpan == b.minColSpan &&
           a.minRowSpan == b.minRowSpan && a.maxColSpan == b.maxColSpan &&
           a.maxRowSpan == b.maxRowSpan && a.locked == b.locked;
}

// reconcile. False when nothing the script owns changed.
static bool Reconcile(const GridArea* current, int nCurrent,
                      const GridArea* seeds, int nSeeds, Vec<GridArea>* next) {
    if (!next) return false;
    Vec<GridArea> built;
    for (int i = 0; i < nSeeds; i++) {
        GridArea kept = seeds[i];
        for (int j = 0; j < nCurrent; j++) {
            if (!StrEq(current[j].id, seeds[i].id)) continue;
            kept.col = current[j].col;
            kept.row = current[j].row;
            kept.colSpan = current[j].colSpan;
            kept.rowSpan = current[j].rowSpan;
            break;
        }
        VecAppend(built, kept);
    }
    bool unchanged = len(built) == nCurrent;
    if (unchanged) {
        for (int i = 0; i < len(built); i++) {
            bool found = false;
            for (int j = 0; j < nCurrent; j++) {
                if (SameScript(current[j], built[i])) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                unchanged = false;
                break;
            }
        }
    }
    if (unchanged) {
        VecReset(built);
        return false;
    }
    VecReset(*next);
    *next = built;
    return true;
}

static ComponentDataValue LayoutArgument(Arena* a, const GridArea* areas,
                                         int n) {
    ComponentDataValue* rows = nullptr;
    if (n > 0)
        rows =
            (ComponentDataValue*)Alloc(a, n * (int)sizeof(ComponentDataValue));
    for (int i = 0; i < n; i++) {
        ComponentDataValue* cells =
            (ComponentDataValue*)Alloc(a, 5 * (int)sizeof(ComponentDataValue));
        cells[0] = ComponentDataValue::String(areas[i].id);
        cells[1] = ComponentDataValue::Number((double)areas[i].col);
        cells[2] = ComponentDataValue::Number((double)areas[i].row);
        cells[3] = ComponentDataValue::Number((double)areas[i].colSpan);
        cells[4] = ComponentDataValue::Number((double)areas[i].rowSpan);
        rows[i] = ComponentDataValue::Array(cells, 5);
    }
    return ComponentDataValue::Array(rows, n);
}

static Entity<GridLayoutState> StateOf(MaterializeRequest* request,
                                       const ComponentArgument& argument) {
    EntityState<GridLayoutState>* state =
        request->StateAs<EntityState<GridLayoutState>>(argument, kKind);
    return state ? state->entity : Entity<GridLayoutState>{};
}

static void NamedGaps(const MaterializeRequest* request, bool* x, bool* y) {
    *x = false;
    *y = false;
    if (!request->node) return;
    for (const SpecOp& op : request->node->ops) {
        if (op.kind != SpecOpKind::NullaryStyle &&
            op.kind != SpecOpKind::ParamStyle)
            continue;
        if (StrStartsWith(op.name, StrL("gap_x")))
            *x = true;
        else if (StrStartsWith(op.name, StrL("gap_y")))
            *y = true;
        else if (StrStartsWith(op.name, StrL("gap"))) {
            *x = true;
            *y = true;
        }
    }
}

// A gap named as a fraction has nothing to be a fraction of, so it is none.
static void ApplyScriptGap(GridLayout* grid, El* probe, bool x, bool y) {
    if (!grid || !probe) return;
    if (x) {
        float v = (probe->style.relLengths & (uint16_t)kRelGapX) != 0
                      ? 0
                      : probe->style.gapX;
        grid->GapX(v);
    }
    if (y) {
        float v = (probe->style.relLengths & (uint16_t)kRelGapY) != 0
                      ? 0
                      : probe->style.gapY;
        grid->GapY(v);
    }
}

static void ApplyItemStyle(GridItem* item, MaterializeRequest* request) {
    uint32_t fields = 0;
    if (request->node) {
        for (const SpecOp& op : request->node->ops) {
            if (op.kind != SpecOpKind::NullaryStyle &&
                op.kind != SpecOpKind::ParamStyle)
                continue;
            fields |= shell::StyleFieldsOf(op.name);
        }
    }
    ElRefiner refiner = request->TakeStyle();
    if (fields == 0) return;
    El* probe = Div(request->cx->a);
    refiner.Apply(probe);
    item->style.style = probe->style;
    item->style.set = fields;
}

static bool CompactionSame(const GridLayoutState* state, bool on, Axis axis) {
    if (!on) return !state->HasCompaction();
    return state->HasCompaction() && state->CompactionAxis() == axis;
}

static El* MaterializeRoot(MaterializeRequest* request, const Payload* payload,
                           Entity<GridLayoutState> state) {
    Ctx* cx = request->cx;
    Rules rules;
    const ComponentArgument* onChange = nullptr;
    const ComponentArgument* onSelect = nullptr;
    Str label = {};
    bool hasLabel = false;
    bool hasRow = false;
    float rowHeight = 0;
    bool hasReflow = false;
    int reflow = 0;
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case OpKind::AccessibilityLabel:
                label = op.text;
                hasLabel = true;
                break;
            case OpKind::RowHeight:
                hasRow = true;
                rowHeight = op.length;
                break;
            case OpKind::Reflow:
                hasReflow = true;
                reflow = op.count;
                break;
            case OpKind::Cols:
                rules.hasCols = true;
                rules.cols = op.count;
                break;
            case OpKind::Compaction:
                rules.hasCompaction = true;
                rules.compactionOn = op.hasCompaction;
                rules.axis = op.axis;
                break;
            case OpKind::PreventCollision:
                rules.hasPrevent = true;
                rules.prevent = op.flag;
                break;
            case OpKind::Draggable:
                rules.hasDraggable = true;
                rules.draggable = op.flag;
                break;
            case OpKind::Resizable:
                rules.hasResizable = true;
                rules.resizable = op.flag;
                break;
            case OpKind::OnChange:
                onChange = &op.callback;
                break;
            case OpKind::OnSelect:
                onSelect = &op.callback;
                break;
            default:
                break;
        }
    });
    shell::ComponentCallback change = {};
    shell::ComponentCallback select = {};
    if (onChange) {
        change = request->ResolveCallback(*onChange);
        if (!change.IsSet())
            return request->Fail(StrL("component argument is not a callback"));
    }
    if (onSelect) {
        select = request->ResolveCallback(*onSelect);
        if (!select.IsSet())
            return request->Fail(StrL("component argument is not a callback"));
    }

    GridLayout* grid = grid_layout(cx, payload->id, state);
    if (hasLabel) grid->Label(label);
    if (hasRow) grid->RowHeight(rowHeight);
    if (hasReflow) grid->Reflow(reflow);

    GridLayoutState* held = state.Get(cx);
    if (held) {
        Ctx stateCx = *cx;
        stateCx.self = state.id;
        if (rules.hasCols && held->ColumnCount() != rules.cols)
            held->SetCols(&stateCx, rules.cols);
        if (rules.hasCompaction &&
            !CompactionSame(held, rules.compactionOn, rules.axis))
            held->SetCompaction(&stateCx, rules.compactionOn, rules.axis);
        if (rules.hasPrevent && held->IsCollisionPrevented() != rules.prevent)
            held->SetPreventCollision(&stateCx, rules.prevent);
        if (rules.hasDraggable && held->IsDraggable() != rules.draggable)
            held->SetDraggable(&stateCx, rules.draggable);
        if (rules.hasResizable && held->IsResizable() != rules.resizable)
            held->SetResizable(&stateCx, rules.resizable);
    }

    static constexpr const char* kAllowed[] = {"GridItem"};
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "GridLayout", children[i].componentName,
                          kAllowed))
            return nullptr;
    }
    Vec<GridArea> seeds;
    for (int i = 0; i < count; i++) {
        El* child = request->MaterializeChild(&children[i]);
        if (!child) {
            VecReset(seeds);
            return nullptr;
        }
        DeclaredItem* declared =
            TakeCarriedAs<DeclaredItem>(request, child, "GridItem");
        if (!declared) {
            VecReset(seeds);
            return nullptr;
        }
        for (int j = 0; j < len(seeds); j++) {
            if (StrEq(seeds[j].id, declared->seed.id)) {
                VecReset(seeds);
                return request
                    ->Fail(fmt("GridLayout has two items with the id `%s`",
                               declared->seed.id));
            }
        }
        VecAppend(seeds, declared->seed);
        grid->Item(declared->item);
    }
    if (held) {
        int nCurrent = 0;
        const GridArea* current = held->Areas(&nCurrent);
        Vec<GridArea> next;
        if (Reconcile(current, nCurrent, seeds.els, len(seeds), &next)) {
            Ctx stateCx = *cx;
            stateCx.self = state.id;
            held->SetAreas(&stateCx, next.els, len(next));
            VecReset(next);
        }
    }
    VecReset(seeds);

    bool gapX = false, gapY = false;
    NamedGaps(request, &gapX, &gapY);
    ElRefiner refiner = request->TakeStyle();
    if (gapX || gapY) {
        El* probe = Div(cx->a);
        refiner.Apply(probe);
        ApplyScriptGap(grid, probe, gapX, gapY);
    }
    El* surface = grid->IntoEl();
    refiner.Apply(surface);

    Str key = StrDup(cx->a, fmt("shell-grid-layout:%s:%d:%u", payload->id,
                                state.id.index, state.id.gen));
    Entity<EventHost> host =
        UseKeyedState<EventHost>(cx, key, StrL("shell-grid-layout"));
    if (EventHost* events = host.Get(cx)) {
        if (!events->subscribed) {
            events->subscribed = true;
            SubscribeTo(cx->app, state, host, &EventHost::OnEvent);
        }
        events->callbacks.runtime = request->runtime;
        events->callbacks.onChange = change;
        events->callbacks.onSelect = select;
    }
    return surface;
}

static El* MaterializeItem(MaterializeRequest* request,
                           const Payload* payload) {
    Ctx* cx = request->cx;
    GridItem* item = GridItem::New(cx, payload->id);
    GridArea seed = GridArea::New(payload->id);
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case OpKind::AccessibilityLabel:
                item->Label(op.text);
                break;
            case OpKind::DragHandleOnly:
                item->DragHandleOnly(op.flag);
                break;
            case OpKind::Col:
                seed = seed.Col(op.count);
                break;
            case OpKind::Row:
                seed = seed.Row(op.count);
                break;
            case OpKind::ColSpan:
                seed = seed.ColSpan(op.count);
                break;
            case OpKind::RowSpan:
                seed = seed.RowSpan(op.count);
                break;
            case OpKind::MinSpan:
                seed = seed.MinSpan(op.count, op.count2);
                break;
            case OpKind::MaxSpan:
                seed = seed.MaxSpan(op.count, op.count2);
                break;
            case OpKind::Locked:
                seed = seed.Locked(op.flag);
                break;
            default:
                break;
        }
    });
    ApplyItemStyle(item, request);
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (children[i]) item->Child(children[i]);
    }
    DeclaredItem* declared = ArenaNew<DeclaredItem>(cx->a);
    declared->item = item;
    declared->seed = seed;
    return CarrierOf(cx, declared);
}

static El* Materialize(MaterializeRequest* request) {
    const Payload* payload = request->PayloadAs<Payload>();
    if (!payload)
        return request->Fail(
            StrL("GridLayout component received an incompatible payload"));
    if (payload->kind == Kind::Item) return MaterializeItem(request, payload);
    Entity<GridLayoutState> state = StateOf(request, payload->state);
    if (!state.IsValid()) return nullptr;
    if (payload->kind == Kind::Handle)
        return request->Finish(GridDragHandle(request->cx, state, payload->id));
    return MaterializeRoot(request, payload, state);
}

// ─── Constructors ─────────────────────────────────────────────────────────

static bool RequireEntity(PayloadBuild* build,
                          const ComponentArgument& argument,
                          const char* component) {
    if (IsEntity(argument)) return true;
    return build
        ->Fail(fmt("%s expects a GridLayoutState entity", Str(component)));
}

static bool RootConstruct(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 2)
        return build
            ->Fail(StrL("GridLayout(id, state) expects a nonempty id and "
                        "GridLayoutState entity"));
    if (!Nonempty(args[0]))
        return build->Fail(StrL("GridLayout expects a nonempty string id"));
    if (!RequireEntity(build, args[1], "GridLayout")) return false;
    Payload* payload = build->New<Payload>();
    payload->kind = Kind::Root;
    payload->id = args[0].string;
    payload->state = args[1];
    return true;
}

static bool ItemConstruct(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1)
        return build->Fail(StrL("GridItem(id) expects a nonempty id"));
    if (!Nonempty(args[0]))
        return build->Fail(StrL("GridItem expects a nonempty string id"));
    Payload* payload = build->New<Payload>();
    payload->kind = Kind::Item;
    payload->id = args[0].string;
    return true;
}

static bool HandleConstruct(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 2)
        return build
            ->Fail(StrL("GridDragHandle(state, item) expects a GridLayoutState "
                        "entity and an item id"));
    if (!RequireEntity(build, args[0], "GridDragHandle")) return false;
    if (!Nonempty(args[1]))
        return build->Fail(StrL("GridDragHandle expects a nonempty string id"));
    Payload* payload = build->New<Payload>();
    payload->kind = Kind::Handle;
    payload->state = args[0];
    payload->id = args[1].string;
    return true;
}

// ─── Methods ──────────────────────────────────────────────────────────────

static bool RecordLabelFor(PayloadBuild* build, const ComponentArgument* args,
                           int count, const char* component) {
    if (count != 1 || args[0].kind != ComponentArgumentKind::String)
        return build->Fail(
            fmt("%s.accessibility_label expects one string", Str(component)));
    Op* op = build->New<Op>();
    op->kind = OpKind::AccessibilityLabel;
    op->text = args[0].string;
    return true;
}

static bool RecordGridLabel(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    return RecordLabelFor(build, args, count, "GridLayout");
}

static bool RecordItemLabel(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    return RecordLabelFor(build, args, count, "GridItem");
}

template <OpKind K>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    const char* component = "GridLayout";
    const char* name = "prevent_collision";
    if constexpr (K == OpKind::Draggable) name = "draggable";
    if constexpr (K == OpKind::Resizable) name = "resizable";
    if constexpr (K == OpKind::Locked) {
        component = "GridItem";
        name = "locked";
    }
    if constexpr (K == OpKind::DragHandleOnly) {
        component = "GridItem";
        name = "drag_handle_only";
    }
    if (count != 1 || args[0].kind != ComponentArgumentKind::Boolean)
        return build
            ->Fail(fmt("%s.%s expects one boolean", Str(component), Str(name)));
    Op* op = build->New<Op>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

// positive or nonnegative integer, named "{component}.{name}(value)".
template <OpKind K, bool kPositive>
static bool RecordCount(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    const char* component = "GridLayout";
    const char* name = "cols";
    if constexpr (K == OpKind::RowHeight) name = "row_height";
    if constexpr (K == OpKind::Reflow) name = "reflow";
    if constexpr (K == OpKind::Col) {
        component = "GridItem";
        name = "col";
    }
    if constexpr (K == OpKind::Row) {
        component = "GridItem";
        name = "row";
    }
    if constexpr (K == OpKind::ColSpan) {
        component = "GridItem";
        name = "col_span";
    }
    if constexpr (K == OpKind::RowSpan) {
        component = "GridItem";
        name = "row_span";
    }
    if (count != 1)
        return build->Fail(
            fmt("%s.%s(value) expects one number", Str(component), Str(name)));
    if constexpr (K == OpKind::RowHeight) {
        float length = 0;
        if (!PositiveLength(args[0], &length))
            return build->Fail(fmt("%s.%s(value) expects a positive number",
                                   Str(component), Str(name)));
        Op* op = build->New<Op>();
        op->kind = K;
        op->length = length;
        return true;
    } else {
        double value = 0;
        bool ok = false;
        if constexpr (kPositive)
            ok = Positive(args[0], &value);
        else
            ok = Nonnegative(args[0], &value);
        if (!ok) {
            const char* bound = "nonnegative";
            if constexpr (kPositive) bound = "positive";
            return build->Fail(fmt("%s.%s(value) expects a %s integer",
                                   Str(component), Str(name), Str(bound)));
        }
        Op* op = build->New<Op>();
        op->kind = K;
        op->count = SaturatingInt(value);
        return true;
    }
}

template <OpKind K>
static bool RecordSpan(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    const char* name = "max_span";
    if constexpr (K == OpKind::MinSpan) name = "min_span";
    if (count != 2)
        return build
            ->Fail(fmt("GridItem.%s(col_span, row_span) expects two "
                       "numbers",
                       Str(name)));
    TempStr callable = fmt("GridItem.%s(col_span, row_span)", Str(name));
    double cols = 0, rows = 0;
    if (!Positive(args[0], &cols) || !Positive(args[1], &rows))
        return build->Fail(fmt("%s expects a positive integer", callable));
    Op* op = build->New<Op>();
    op->kind = K;
    op->count = SaturatingInt(cols);
    op->count2 = SaturatingInt(rows);
    return true;
}

static bool RecordCompaction(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 1 || args[0].kind != ComponentArgumentKind::Enum)
        return build->Fail(
            StrL("GridLayout.compaction(compaction) expects one direction"));
    Compaction which = CompactionOf(args[0].string);
    if (which == Compaction::Unknown)
        return build->Fail(
            StrL("GridLayout.compaction expects vertical, horizontal, none"));
    Op* op = build->New<Op>();
    op->kind = OpKind::Compaction;
    op->hasCompaction = which != Compaction::None;
    op->axis =
        which == Compaction::Horizontal ? Axis::Horizontal : Axis::Vertical;
    return true;
}

template <OpKind K>
static bool RecordCallback(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    const char* name = "on_select";
    if constexpr (K == OpKind::OnChange) name = "on_change";
    if (count != 1 || args[0].kind != ComponentArgumentKind::Callback)
        return build->Fail(
            fmt("GridLayout.%s(callback) expects one callback", Str(name)));
    Op* op = build->New<Op>();
    op->kind = K;
    op->callback = args[0];
    return true;
}

// ─── State ────────────────────────────────────────────────────────────────

static bool NewState(shell::StateBuild* build, const ComponentArgument* args,
                     int count) {
    if (count != 2)
        return build->Fail(StrL("GridLayoutState expects cols and compaction"));
    int cols = 12;
    if (args[0].kind != ComponentArgumentKind::Optional)
        return build->Fail(StrL("GridLayoutState cols must be optional"));
    if (const ComponentArgument* some = args[0].Some()) {
        double value = 0;
        if (!Positive(*some, &value))
            return build->Fail(
                StrL("GridLayoutState(cols) expects a positive integer"));
        cols = SaturatingInt(value);
    }
    if (args[1].kind != ComponentArgumentKind::Optional)
        return build->Fail(StrL("GridLayoutState compaction must be optional"));
    bool compactionOn = true;
    Axis axis = Axis::Vertical;
    if (const ComponentArgument* some = args[1].Some()) {
        if (some->kind != ComponentArgumentKind::Enum)
            return build->Fail(StrL(
                "GridLayoutState compaction expects vertical, horizontal or "
                "none"));
        Compaction which = CompactionOf(some->string);
        if (which == Compaction::Unknown)
            return build->Fail(StrL(
                "GridLayoutState compaction expects vertical, horizontal or "
                "none"));
        compactionOn = which != Compaction::None;
        axis =
            which == Compaction::Horizontal ? Axis::Horizontal : Axis::Vertical;
    }
    EntityState<GridLayoutState>* held =
        build->New<EntityState<GridLayoutState>>();
    held->app = build->app;
    held->entity = EntityNewState<GridLayoutState>(build->app);
    if (GridLayoutState* native = held->entity.Get(build->app))
        native->Cols(cols)->Compaction(compactionOn, axis);
    return true;
}

// ─── Tables ───────────────────────────────────────────────────────────────

static constexpr ArgumentSchema kNumber = SchemaNumber();
static constexpr const char* kCompactionLiterals[] = {"vertical", "horizontal",
                                                      "none"};
static constexpr ArgumentSchema kCompactionSchema =
    SchemaEnum(kCompactionLiterals);
static constexpr ArgumentDescriptor kStateArgs[] = {
    {"cols", SchemaOptional(&kNumber)},
    {"compaction", SchemaOptional(&kCompactionSchema)},
};
static constexpr shell::StateDescriptor kState = {
    "GridLayoutState",
    "GridLayoutState",
    kStateArgs,
    "Retained GridLayout areas, rules, selection, and drag state. Items join "
    "it with the placement their GridItem declares and keep the place the "
    "user gives them.",
    &NewState,
    {}};

static constexpr ArgumentDescriptor kIdStateArgs[] = {
    {"id", SchemaString()},
    {"state", SchemaEntity("GridLayoutState")}};
static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kHandleArgs[] = {
    {"state", SchemaEntity("GridLayoutState")},
    {"item", SchemaString()}};
static constexpr ArgumentDescriptor kLabelArgs[] = {
    {"accessibility_label", SchemaString()}};
static constexpr ArgumentDescriptor kValueArgs[] = {{"value", SchemaNumber()}};
static constexpr ArgumentDescriptor kPreventArgs[] = {
    {"prevent_collision", SchemaBoolean()}};
static constexpr ArgumentDescriptor kDraggableArgs[] = {
    {"draggable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kResizableArgs[] = {
    {"resizable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kLockedArgs[] = {
    {"locked", SchemaBoolean()}};
static constexpr ArgumentDescriptor kHandleOnlyArgs[] = {
    {"drag_handle_only", SchemaBoolean()}};
static constexpr ArgumentDescriptor kCompactionArgs[] = {
    {"compaction", kCompactionSchema}};
static constexpr ArgumentDescriptor kSpanArgs[] = {
    {"col_span", SchemaNumber()},
    {"row_span", SchemaNumber()}};
static constexpr ArgumentDescriptor kChangeArgs[] = {
    {"callback",
     SchemaCallback("(layout: [id: string, col: number, row: number, col_span: "
                    "number, row_span: number][], cx: Context) => void")}};
static constexpr ArgumentDescriptor kSelectArgs[] = {
    {"callback", SchemaCallback("(id: string, cx: Context) => void")}};

static constexpr const char* kLabelDoc =
    "Sets the name announced by accessibility clients.";

static constexpr MethodDescriptor kGridMethods[] = {
    {"accessibility_label", kLabelArgs, kLabelDoc, &RecordGridLabel},
    {"cols", kValueArgs,
     "Sets the number of equal columns the width is divided into.",
     &RecordCount<OpKind::Cols, true>},
    {"compaction", kCompactionArgs,
     "Sets the direction items float toward when space opens up, or none to "
     "leave them where they are put.",
     &RecordCompaction},
    {"row_height", kValueArgs,
     "Sets the height of one row in rems, so rows follow the application's "
     "zoom.",
     &RecordCount<OpKind::RowHeight, true>},
    {"prevent_collision", kPreventArgs,
     "Refuses a move or resize that would overlap another item instead of "
     "pushing it.",
     &RecordBool<OpKind::PreventCollision>},
    {"draggable", kDraggableArgs, "Controls whether items can be moved.",
     &RecordBool<OpKind::Draggable>},
    {"resizable", kResizableArgs, "Controls whether items can be resized.",
     &RecordBool<OpKind::Resizable>},
    {"reflow", kValueArgs,
     "Shows the items over this many columns without changing the stored "
     "layout \u2014 a one-column stack for a narrow window. The grid is "
     "read-only while reflowed.",
     &RecordCount<OpKind::Reflow, true>},
    {"on_change", kChangeArgs,
     "Reports the whole layout, one [id, col, row, col_span, row_span] entry "
     "per item, after the user moves or resizes an item.",
     &RecordCallback<OpKind::OnChange>},
    {"on_select", kSelectArgs,
     "Reports the item the user selects, by pressing it or with Tab, once per "
     "change.",
     &RecordCallback<OpKind::OnSelect>},
};
static constexpr MethodDescriptor kItemMethods[] = {
    {"accessibility_label", kLabelArgs, kLabelDoc, &RecordItemLabel},
    {"col", kValueArgs,
     "Sets the leading column, from zero, the item takes when it first joins "
     "the grid.",
     &RecordCount<OpKind::Col, false>},
    {"row", kValueArgs,
     "Sets the top row, from zero, the item takes when it first joins the "
     "grid.",
     &RecordCount<OpKind::Row, false>},
    {"col_span", kValueArgs,
     "Sets how many columns the item covers when it first joins the grid.",
     &RecordCount<OpKind::ColSpan, true>},
    {"row_span", kValueArgs,
     "Sets how many rows the item covers when it first joins the grid.",
     &RecordCount<OpKind::RowSpan, true>},
    {"min_span", kSpanArgs,
     "Sets the fewest columns and rows a resize may leave the item.",
     &RecordSpan<OpKind::MinSpan>},
    {"max_span", kSpanArgs,
     "Sets the most columns and rows a resize may give the item.",
     &RecordSpan<OpKind::MaxSpan>},
    {"locked", kLockedArgs,
     "Locks the item in place; other items flow around it.",
     &RecordBool<OpKind::Locked>},
    {"drag_handle_only", kHandleOnlyArgs,
     "Moves the item only from a GridDragHandle inside it.",
     &RecordBool<OpKind::DragHandleOnly>},
};

static constexpr ConstructorDescriptor kGridCtors[] = {
    {"GridLayout", kIdStateArgs, &RootConstruct}};
static constexpr ConstructorDescriptor kItemCtors[] = {
    {"GridItem", kIdArgs, &ItemConstruct}};
static constexpr ConstructorDescriptor kHandleCtors[] = {
    {"GridDragHandle", kHandleArgs, &HandleConstruct}};

static constexpr ComponentDescriptor kDescriptors[] = {
    {"GridLayout", kGridCtors, kGridMethods,
     "A grid whose items the user drags and resizes; accepts only GridItem "
     "children. Style its gaps with gap, gap_x and gap_y.",
     &Materialize},
    {"GridItem", kItemCtors, kItemMethods,
     "One GridLayout item that accepts ordinary content children.",
     &Materialize},
    {"GridDragHandle",
     kHandleCtors,
     {},
     "The part of a GridItem the pointer moves it by, for an item set to "
     "drag_handle_only.",
     &Materialize},
};

} // namespace gpui::component_shell::grid_layout

namespace gpui::component_shell {

bool GridLayoutNonnegativeUsize(const shell::ComponentArgument& argument,
                                double* out) {
    double value = 0;
    if (!grid_layout::Nonnegative(argument, &value)) return false;
    if (out) *out = value;
    return true;
}
bool GridLayoutPositiveUsize(const shell::ComponentArgument& argument,
                             double* out) {
    double value = 0;
    if (!grid_layout::Positive(argument, &value)) return false;
    if (out) *out = value;
    return true;
}
bool GridLayoutPositiveLength(const shell::ComponentArgument& argument,
                              float* out) {
    float value = 0;
    if (!grid_layout::PositiveLength(argument, &value)) return false;
    if (out) *out = value;
    return true;
}
bool GridLayoutNonemptyId(const shell::ComponentArgument& argument) {
    return grid_layout::Nonempty(argument);
}
int GridLayoutCompactionCode(Str value) {
    switch (grid_layout::CompactionOf(value)) {
        case grid_layout::Compaction::None:
            return 1;
        case grid_layout::Compaction::Vertical:
            return 2;
        case grid_layout::Compaction::Horizontal:
            return 3;
        default:
            return 0;
    }
}
bool GridLayoutReconcile(const GridArea* current, int nCurrent,
                         const GridArea* seeds, int nSeeds,
                         Vec<GridArea>* next) {
    return grid_layout::Reconcile(current, nCurrent, seeds, nSeeds, next);
}
shell::ComponentDataValue GridLayoutLayoutArgument(Arena* a,
                                                   const GridArea* areas,
                                                   int n) {
    return grid_layout::LayoutArgument(a, areas, n);
}

bool RegisterGridLayout(shell::ComponentRegistry* registry,
                        shell::RegistryError* error) {
    if (!registry->RegisterState(&grid_layout::kState, error)) return false;
    for (const shell::ComponentDescriptor& descriptor :
         grid_layout::kDescriptors) {
        if (!registry->Register(&descriptor, error)) return false;
    }
    return true;
}

} // namespace gpui::component_shell
