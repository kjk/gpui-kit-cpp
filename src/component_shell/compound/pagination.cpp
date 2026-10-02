// crates/component-shell/src/shell/compound/pagination.rs

#include "component_shell/families.h"
#include "component_shell/compound/common.h"
#include "shell/view.h"
#include "ui/pagination.h"

namespace gpui::component_shell::compound::pagination {

struct PaginationPayload {
    Str id;
};

struct PaginationOp {
    enum Kind : uint8_t {
        OnChange,
        Current,
        Total,
        Visible,
        Compact,
        Size,
    } kind = OnChange;
    ComponentArgument change = {};
    uint64_t value = 0;
    UiSize size = UiSize::Medium;
};

static bool Construct(PayloadBuild* build, const ComponentArgument* args, int) {
    Str error;
    if (!common::NonemptyId(args[0].string, "Pagination", &error))
        return build->Fail(error);
    build->New<PaginationPayload>()->id = args[0].string;
    return true;
}

// positive: a nonnegative usize that is not zero, under the method's own
// name.
static bool Positive(const ComponentArgument& argument, const char* label,
                     uint64_t* out, Str* error) {
    TempStr callable = fmt("Pagination.%s(%s)", Str(label), Str(label));
    if (argument.kind != shell::ComponentArgumentKind::Number) {
        *error = fmt("%s expects a positive integer", callable);
        return false;
    }
    if (!common::NonnegativeUsize(argument.number, callable, out, error))
        return false;
    if (*out == 0) {
        *error = fmt("%s expects a positive integer", callable);
        return false;
    }
    return true;
}

template <PaginationOp::Kind K>
static bool RecordNumeric(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    const char* label = K == PaginationOp::Current ? "current_page"
                        : K == PaginationOp::Total ? "total_pages"
                                                   : "visible_pages";
    uint64_t value = 0;
    Str error;
    if (!Positive(args[0], label, &value, &error)) return build->Fail(error);
    PaginationOp* op = build->New<PaginationOp>();
    op->kind = K;
    op->value = value;
    return true;
}

static bool RecordChange(PayloadBuild* build, const ComponentArgument* args,
                         int) {
    PaginationOp* op = build->New<PaginationOp>();
    op->kind = PaginationOp::OnChange;
    op->change = args[0];
    return true;
}

static bool RecordCompact(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<PaginationOp>()->kind = PaginationOp::Compact;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    PaginationOp* op = build->New<PaginationOp>();
    op->kind = PaginationOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

// The on_change closure: the page the reader asked for, which the
// component fills in.
static void RunChange(const shell::ComponentEventBinding* binding,
                      ScriptView* view, Ctx* cx, const void*) {
    shell::ComponentDataValue page =
        shell::ComponentDataValue::Number((double)binding->value);
    binding->callback
        .InvokeAndReport(view->runtime, "Pagination.on_change callback failed",
                         &page, 1, cx->win, cx->app);
}

static El* Materialize(MaterializeRequest* request) {
    if (request->ChildrenLen() != 0)
        return request->Fail(StrL("Pagination does not accept children"));
    const PaginationPayload* payload = request->PayloadAs<PaginationPayload>();
    if (!payload)
        return request
            ->Fail(StrL("Pagination received an incompatible payload"));
    // The last on_change wins.
    const ComponentArgument* change = nullptr;
    EachMethod<PaginationOp>(request, [&](const PaginationOp& op) {
        if (op.kind == PaginationOp::OnChange) change = &op.change;
    });
    component::Pagination* pagination =
        component::Pagination::New(request->cx, 1, 1)
            ->Id(payload->id)
            ->Disabled(request->disabled);
    EachMethod<PaginationOp>(request, [&](const PaginationOp& op) {
        switch (op.kind) {
            case PaginationOp::Current:
                pagination->CurrentPage(common::UsizeInt64(op.value));
                break;
            case PaginationOp::Total:
                pagination->TotalPages(common::UsizeInt64(op.value));
                break;
            case PaginationOp::Visible:
                pagination->VisiblePages(common::UsizeInt64(op.value));
                break;
            case PaginationOp::Compact:
                pagination->Compact();
                break;
            case PaginationOp::Size:
                pagination->WithSize(op.size);
                break;
            case PaginationOp::OnChange:
                break;
        }
    });
    if (change) {
        pagination->OnChange(shell::ComponentValueListener(
            request->cx, request->elementId, &RunChange,
            request->ResolveCallback(*change)));
    }
    El* wrapper = Div(request->cx->a)->Child(pagination->IntoEl());
    return request->ApplyStyle(wrapper);
}

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kCurrentArgs[] = {
    {"current_page", SchemaNumber()}};
static constexpr ArgumentDescriptor kTotalArgs[] = {
    {"total_pages", SchemaNumber()}};
static constexpr ArgumentDescriptor kVisibleArgs[] = {
    {"visible_pages", SchemaNumber()}};
static constexpr ArgumentDescriptor kChangeArgs[] = {
    {"on_change", SchemaCallback("(page: number, cx: Context) => void")}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Pagination", kIdArgs, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    kDisabledMethod,
    {"current_page", kCurrentArgs, "Sets the current 1-based page.",
     &RecordNumeric<PaginationOp::Current>},
    {"total_pages", kTotalArgs, "Sets the positive page count.",
     &RecordNumeric<PaginationOp::Total>},
    {"visible_pages", kVisibleArgs, "Sets the maximum visible page buttons.",
     &RecordNumeric<PaginationOp::Visible>},
    {"on_change", kChangeArgs,
     "Reports the page the reader asked for, so the script can drive "
     "`currentPage`.",
     &RecordChange},
    {"compact",
     {},
     "Shows only previous and next icon buttons.",
     &RecordCompact},
    {"size", kSizeArgs, "Sets semantic size.", &RecordSize},
};
static constexpr ComponentDescriptor kPagination = {
    "Pagination", kConstructors, kMethods,
    "Controlled page navigation; disabled common behavior is supported.",
    &Materialize};

} // namespace gpui::component_shell::compound::pagination

namespace gpui::component_shell {

bool RegisterCompoundPagination(shell::ComponentRegistry* registry,
                                shell::RegistryError* error) {
    return registry->Register(&compound::pagination::kPagination, error);
}

} // namespace gpui::component_shell
