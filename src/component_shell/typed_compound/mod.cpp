// crates/component-shell/src/shell/typed_compound/mod.rs: typed compound
// containers (Accordion, RadioGroup, TabBar, Stepper) and the parts only they
// accept, plus the typed part element the compound families share.

#include "component_shell/typed_compound/mod.h"
#include "component_shell/families.h"
#include "shell/view.h"
#include "ui/accordion.h"
#include "ui/radio.h"
#include "ui/stepper.h"
#include "ui/tab.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

namespace gpui::component_shell::typed_compound {

// ─── TypedChildElement / take_element ─────────────────────────────────────

// What a typed part element carries beside its rendering. The magic tells it
// from an element whose customUser means something else, and from a
// support.h Carrier.
struct TypedChildBox {
    uint32_t magic = 0;
    const void* tag = nullptr;
    void* value = nullptr;
    bool taken = false;
};

static const uint32_t kTypedChildMagic = 0x54797065; // "Type"

static TypedChildBox* BoxOf(El* element) {
    if (!element || element->customPaint || element->prePaint) return nullptr;
    TypedChildBox* box = (TypedChildBox*)element->customUser;
    return box && box->magic == kTypedChildMagic ? box : nullptr;
}

El* TypedChildElement(Ctx* cx, const void* tag, void* value, El* rendered) {
    El* host = rendered;
    // Rust's element lays out as the value it wraps. The box rides on the
    // rendering itself when the rendering has no custom paint of its own —
    // every part here — and on a plain div around it otherwise.
    if (!host || host->customUser || host->customPaint || host->prePaint) {
        host = Div(cx->a);
        if (rendered) host->Child(rendered);
    }
    TypedChildBox* box = ArenaNew<TypedChildBox>(cx->a);
    box->magic = kTypedChildMagic;
    box->tag = tag;
    box->value = value;
    host->customUser = box;
    return host;
}

bool IsTypedElement(El* element, const void* tag) {
    TypedChildBox* box = BoxOf(element);
    return box && box->tag == tag && !box->taken;
}

void* TakeElement(MaterializeRequest* request, El* element, const void* tag,
                  const char* name) {
    TypedChildBox* box = BoxOf(element);
    if (!box || box->tag != tag) {
        request->Fail(fmt("registered %s materialized an incompatible element",
                          Str(name)));
        return nullptr;
    }
    if (box->taken) {
        request
            ->Fail(fmt("registered %s child was already consumed", Str(name)));
        return nullptr;
    }
    box->taken = true;
    return box->value;
}

// ─── Payload helpers ───────────────────────────────────────────────────────

// index_payload's check: a finite, nonnegative whole number no larger than
// usize::MAX (inclusive: 2^64 passes and saturates, as `as usize` does).
static bool IsIndex(double value) {
    return isfinite(value) && value >= 0 && value == floor(value) &&
           value <= 18446744073709551615.0;
}

static bool IndexOr(PayloadBuild* build, double value, const char* callable) {
    if (IsIndex(value)) return true;
    return build->Fail(fmt("%s expects a nonnegative integer", Str(callable)));
}

// A usize index as the int the components count in.
static int IndexInt(double value) {
    return value >= (double)INT_MAX ? INT_MAX : (int)value;
}

// id_constructor: `id.trim().is_empty()` refuses. Rust trims Unicode
// whitespace; an id's is in practice only ever the ASCII kind.
static bool NonemptyId(PayloadBuild* build, Str id, const char* callable) {
    if (len(StrTrimAscii(id)) > 0) return true;
    return build
        ->Fail(fmt("%s(id) expects a nonempty string id", Str(callable)));
}

// A payload that is just the component's string id.
struct IdPayload {
    Str id;
};

// Reports a zero-based index to the script: index_callback_method's closure.
// `user` is the callback's failure context.
static void RunIndex(const shell::ComponentEventBinding* binding,
                     ScriptView* view, Ctx* cx, const void*) {
    shell::ComponentDataValue value =
        shell::ComponentDataValue::Number((double)binding->value);
    binding->callback.InvokeAndReport(view->runtime, (const char*)binding->user,
                                      &value, 1, cx->win, cx->app);
}

static Listener IndexListener(MaterializeRequest* request,
                              const ComponentArgument& argument,
                              const char* context) {
    return shell::ComponentValueListener(
        request->cx, request->elementId, &RunIndex,
        request->ResolveCallback(argument), (void*)context);
}

// finish_typed: the container's own style onto the element Rust refines,
// which for each container here is its root.
static El* FinishTyped(MaterializeRequest* request, El* element) {
    return request->ApplyStyle(element);
}

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kIndexArgs[] = {{"index", SchemaNumber()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ArgumentDescriptor kIndexCallbackArgs[] = {
    // The runtime appends the call context to every component callback, so
    // the declared signature has to name it — otherwise a script that wants
    // to `cx.notify()` has no typed way to.
    {"callback", SchemaCallback("(index: number, cx: Context) => void")}};
static constexpr const char* kIndexCallbackDoc =
    "Reports the newly selected zero-based index.";
static constexpr const char* kSizeDoc = "Sets the semantic component size.";

// One recorded operation of any of the containers or parts below: which one
// is the payload's type, which is the op struct it is recorded as.
template <class Kind>
struct Op {
    Kind kind = {};
    bool flag = false;
    double index = 0;
    UiSize size = UiSize::Medium;
    Str text;
    ComponentArgument argument = {};
};

// ─── AccordionItem ─────────────────────────────────────────────────────────

struct AccordionItemPayload {};

enum class AccordionItemKind : uint8_t {
    Title,
    Open,
    Disabled,
};
using AccordionItemOp = Op<AccordionItemKind>;

static bool ConstructAccordionItem(PayloadBuild* build,
                                   const ComponentArgument*, int) {
    return build->Mark<AccordionItemPayload>();
}

static bool RecordAccordionItemTitle(PayloadBuild* build,
                                     const ComponentArgument* args, int) {
    AccordionItemOp* op = build->New<AccordionItemOp>();
    op->kind = AccordionItemKind::Title;
    op->argument = args[0];
    return true;
}

template <AccordionItemKind K>
static bool RecordAccordionItemBool(PayloadBuild* build,
                                    const ComponentArgument* args, int) {
    AccordionItemOp* op = build->New<AccordionItemOp>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

static El* MaterializeAccordionItem(MaterializeRequest* request) {
    if (!shell::PayloadIs<AccordionItemPayload>(request->payload))
        return request
            ->Fail(StrL("AccordionItem received an incompatible payload"));
    component::AccordionItem* item = component::AccordionItem::New(request->cx)
                                         ->Disabled(request->disabled);
    bool failed = false;
    EachMethod<AccordionItemOp>(request, [&](const AccordionItemOp& op) {
        if (failed) return;
        switch (op.kind) {
            case AccordionItemKind::Title: {
                El* title = request->ResolveElement(op.argument);
                if (!title) {
                    failed = true;
                    return;
                }
                item->Title(title);
                break;
            }
            case AccordionItemKind::Open:
                item->Open(op.flag);
                break;
            case AccordionItemKind::Disabled:
                item->Disabled(op.flag);
                break;
        }
    });
    if (failed) return nullptr;
    return FinishPart(request, item);
}

static constexpr ArgumentDescriptor kTitleArgs[] = {{"title", SchemaElement()}};
static constexpr ArgumentDescriptor kOpenArgs[] = {{"open", SchemaBoolean()}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};

static constexpr ConstructorDescriptor kAccordionItemConstructors[] = {
    {"AccordionItem", {}, &ConstructAccordionItem}};
static constexpr MethodDescriptor kAccordionItemMethods[] = {
    {"title", kTitleArgs, "Sets the interactive title row element.",
     &RecordAccordionItemTitle},
    {"open", kOpenArgs, "Controls expanded state.",
     &RecordAccordionItemBool<AccordionItemKind::Open>},
    {"disabled", kDisabledArgs, "Disables this item.",
     &RecordAccordionItemBool<AccordionItemKind::Disabled>},
};
static constexpr ComponentDescriptor kAccordionItem = {
    "AccordionItem", kAccordionItemConstructors, kAccordionItemMethods,
    "An accordion part accepted only as a direct Accordion child.",
    &MaterializeAccordionItem};

// ─── Accordion ─────────────────────────────────────────────────────────────

enum class AccordionKind : uint8_t {
    Multiple,
    Bordered,
    Size,
    OnToggle,
};
using AccordionOp = Op<AccordionKind>;

static bool ConstructAccordion(PayloadBuild* build,
                               const ComponentArgument* args, int) {
    if (!NonemptyId(build, args[0].string, "Accordion")) return false;
    build->New<IdPayload>()->id = args[0].string;
    return true;
}

template <AccordionKind K>
static bool RecordAccordionBool(PayloadBuild* build,
                                const ComponentArgument* args, int) {
    AccordionOp* op = build->New<AccordionOp>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

static bool RecordAccordionSize(PayloadBuild* build,
                                const ComponentArgument* args, int) {
    AccordionOp* op = build->New<AccordionOp>();
    op->kind = AccordionKind::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static bool RecordAccordionToggle(PayloadBuild* build,
                                  const ComponentArgument* args, int) {
    AccordionOp* op = build->New<AccordionOp>();
    op->kind = AccordionKind::OnToggle;
    op->argument = args[0];
    return true;
}

// What Rust's Accordion keeps in its `open_indices` cell: which items were
// open when it rendered, and whether several may be.
struct AccordionToggle {
    const bool* open = nullptr;
    int count = 0;
    bool multiple = false;
};

// Accordion::render's root on_click after the clicked item's on_change: the
// clicked item flips, a single-open accordion closing the rest when it
// opens, and the script hears the open indices. Rust's are a HashSet in
// iteration order; these ascend.
static void RunToggle(const shell::ComponentEventBinding* binding,
                      ScriptView* view, Ctx* cx, const void*) {
    const AccordionToggle* toggle = (const AccordionToggle*)binding->user;
    int clicked = (int)binding->value;
    Arena* a = cx->a;
    shell::ComponentDataValue* indices =
        toggle->count
            ? (shell::ComponentDataValue*)Alloc(
                  a, (int)sizeof(shell::ComponentDataValue) * toggle->count)
            : nullptr;
    bool opening =
        clicked >= 0 && clicked < toggle->count && !toggle->open[clicked];
    int n = 0;
    for (int i = 0; i < toggle->count; i++) {
        bool open = toggle->open[i];
        if (i == clicked) {
            open = opening;
        } else if (opening && !toggle->multiple) {
            open = false;
        }
        if (open) indices[n++] = shell::ComponentDataValue::Number((double)i);
    }
    shell::ComponentDataValue value =
        shell::ComponentDataValue::Array(indices, n);
    binding->callback
        .InvokeAndReport(view->runtime, "Accordion.on_toggle callback failed",
                         &value, 1, cx->win, cx->app);
}

static const char* const kAccordionItemName[] = {"AccordionItem"};

static El* MaterializeAccordion(MaterializeRequest* request) {
    const IdPayload* payload = request->PayloadAs<IdPayload>();
    if (!payload)
        return request
            ->Fail(StrL("Accordion received an incompatible payload"));
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "Accordion", children[i].componentName,
                          kAccordionItemName))
            return nullptr;
    }
    Ctx* cx = request->cx;
    component::Accordion* accordion = component::Accordion::New(cx, payload->id)
                                          ->Disabled(request->disabled);
    AccordionToggle* toggle = nullptr;
    EachMethod<AccordionOp>(request, [&](const AccordionOp& op) {
        switch (op.kind) {
            case AccordionKind::Multiple:
                accordion->Multiple(op.flag);
                break;
            case AccordionKind::Bordered:
                accordion->Bordered(op.flag);
                break;
            case AccordionKind::Size:
                accordion->WithSize(op.size);
                break;
            case AccordionKind::OnToggle:
                if (!toggle) toggle = ArenaNew<AccordionToggle>(cx->a);
                accordion->OnToggle(shell::ComponentValueListener(
                    cx, request->elementId, &RunToggle,
                    request->ResolveCallback(op.argument), toggle));
                break;
        }
    });
    for (int i = 0; i < count; i++) {
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        component::AccordionItem* item =
            TakeElementAs<component::AccordionItem>(request, element,
                                                    "AccordionItem");
        if (!item) return nullptr;
        accordion->Item(item);
    }
    // Rust's `.filter(|_| !self.disabled)`: a disabled accordion reports
    // nothing.
    if (toggle && accordion->disabled) accordion->OnToggle({});
    if (toggle) {
        bool* open =
            count ? (bool*)Alloc(cx->a, (int)sizeof(bool) * count) : nullptr;
        for (int i = 0; i < count; i++) open[i] = accordion->items[i]->open;
        toggle->open = open;
        toggle->count = count;
        toggle->multiple = accordion->multiple;
    }
    return FinishTyped(request, accordion->IntoEl());
}

static constexpr ArgumentDescriptor kMultipleArgs[] = {
    {"multiple", SchemaBoolean()}};
static constexpr ArgumentDescriptor kBorderedArgs[] = {
    {"bordered", SchemaBoolean()}};
static constexpr ArgumentDescriptor kToggleArgs[] = {
    {"on_toggle",
     SchemaCallback("(openIndices: number[], cx: Context) => void")}};

static constexpr ConstructorDescriptor kAccordionConstructors[] = {
    {"Accordion", kIdArgs, &ConstructAccordion}};
static constexpr MethodDescriptor kAccordionMethods[] = {
    {"multiple", kMultipleArgs, "Allows multiple items to remain open.",
     &RecordAccordionBool<AccordionKind::Multiple>},
    {"bordered", kBorderedArgs, "Controls the joined outer border.",
     &RecordAccordionBool<AccordionKind::Bordered>},
    {"size", kSizeArgs, kSizeDoc, &RecordAccordionSize},
    {"on_toggle", kToggleArgs,
     "Reports which sections are open after a click, so the script can drive "
     "`AccordionItem.open`.",
     &RecordAccordionToggle},
};
static constexpr ComponentDescriptor kAccordion = {
    "Accordion", kAccordionConstructors, kAccordionMethods,
    "A typed accordion container accepting only AccordionItem children. "
    "Toggle callbacks are not exposed: the real component requires a Send + "
    "Sync handler, while shell callbacks are runtime-local.",
    &MaterializeAccordion};

// ─── RadioGroup ────────────────────────────────────────────────────────────

struct RadioGroupPayload {
    Str id;
    Axis axis = Axis::Vertical;
};

enum class RadioGroupKind : uint8_t {
    Selected,
    Disabled,
    OnChange,
};
using RadioGroupOp = Op<RadioGroupKind>;

template <Axis A>
static bool ConstructRadioGroup(PayloadBuild* build,
                                const ComponentArgument* args, int) {
    const char* callable =
        A == Axis::Horizontal ? "HorizontalRadioGroup" : "RadioGroup";
    if (!NonemptyId(build, args[0].string, callable)) return false;
    RadioGroupPayload* payload = build->New<RadioGroupPayload>();
    payload->id = args[0].string;
    payload->axis = A;
    return true;
}

static bool RecordRadioGroupSelected(PayloadBuild* build,
                                     const ComponentArgument* args, int) {
    if (!IndexOr(build, args[0].number, "RadioGroup.selected_index(index)"))
        return false;
    RadioGroupOp* op = build->New<RadioGroupOp>();
    op->kind = RadioGroupKind::Selected;
    op->index = args[0].number;
    return true;
}

static bool RecordRadioGroupDisabled(PayloadBuild* build,
                                     const ComponentArgument* args, int) {
    RadioGroupOp* op = build->New<RadioGroupOp>();
    op->kind = RadioGroupKind::Disabled;
    op->flag = args[0].boolean;
    return true;
}

bool IndexCallbackPayload(PayloadBuild* build, const ComponentArgument* args,
                          int count, const char* component) {
    const ComponentArgument* value = count == 1 ? args[0].Some() : nullptr;
    if (!value || value->kind != shell::ComponentArgumentKind::Callback) {
        return build->Fail(
            fmt("%s.on_change(callback) expects a callback", Str(component)));
    }
    RadioGroupOp* op = build->New<RadioGroupOp>();
    op->kind = RadioGroupKind::OnChange;
    op->argument = *value;
    return true;
}

static bool RecordRadioGroupChange(PayloadBuild* build,
                                   const ComponentArgument* args, int count) {
    return IndexCallbackPayload(build, args, count, "RadioGroup");
}

static const char* const kRadioName[] = {"Radio"};

static El* MaterializeRadioGroup(MaterializeRequest* request) {
    const RadioGroupPayload* payload = request->PayloadAs<RadioGroupPayload>();
    if (!payload)
        return request
            ->Fail(StrL("RadioGroup received an incompatible payload"));
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "RadioGroup", children[i].componentName,
                          kRadioName))
            return nullptr;
    }
    Ctx* cx = request->cx;
    component::RadioGroup* group =
        (payload->axis == Axis::Horizontal
             ? component::RadioGroup::Horizontal(cx, payload->id)
             : component::RadioGroup::Vertical(cx, payload->id))
            ->Disabled(request->disabled);
    EachMethod<RadioGroupOp>(request, [&](const RadioGroupOp& op) {
        switch (op.kind) {
            case RadioGroupKind::Selected:
                group->Selected(IndexInt(op.index));
                break;
            case RadioGroupKind::Disabled:
                group->Disabled(op.flag);
                break;
            case RadioGroupKind::OnChange:
                group->OnChange(
                    IndexListener(request, op.argument,
                                  "RadioGroup.on_change callback failed"));
                break;
        }
    });
    for (int i = 0; i < count; i++) {
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        component::Radio* radio =
            TakeElementAs<component::Radio>(request, element, "Radio");
        if (!radio) return nullptr;
        group->Child(radio);
    }
    return FinishTyped(request, group->IntoEl());
}

static constexpr ConstructorDescriptor kRadioGroupConstructors[] = {
    {"RadioGroup", kIdArgs, &ConstructRadioGroup<Axis::Vertical>},
    {"HorizontalRadioGroup", kIdArgs, &ConstructRadioGroup<Axis::Horizontal>},
};
static constexpr MethodDescriptor kRadioGroupMethods[] = {
    {"selected_index", kIndexArgs,
     "Controls the selected zero-based radio index.",
     &RecordRadioGroupSelected},
    {"disabled", kDisabledArgs, "Disables the group.",
     &RecordRadioGroupDisabled},
    {"on_change", kIndexCallbackArgs, kIndexCallbackDoc,
     &RecordRadioGroupChange},
};
static constexpr ComponentDescriptor kRadioGroup = {
    "RadioGroup", kRadioGroupConstructors, kRadioGroupMethods,
    "A controlled radio set accepting only registered Radio children.",
    &MaterializeRadioGroup};

// ─── Tab ───────────────────────────────────────────────────────────────────

struct TabPayload {};

enum class TabKind : uint8_t {
    Label,
    AriaLabel,
    Disabled,
    Selected,
};
using TabOp = Op<TabKind>;

static bool ConstructTab(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<TabPayload>();
}

template <TabKind K>
static bool RecordTabString(PayloadBuild* build, const ComponentArgument* args,
                            int) {
    TabOp* op = build->New<TabOp>();
    op->kind = K;
    op->text = args[0].string;
    return true;
}

template <TabKind K>
static bool RecordTabBool(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    TabOp* op = build->New<TabOp>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

static El* MaterializeTab(MaterializeRequest* request) {
    if (!shell::PayloadIs<TabPayload>(request->payload))
        return request->Fail(StrL("Tab received an incompatible payload"));
    component::Tab* tab = component::Tab::New(request->cx)
                              ->Disabled(request->disabled)
                              ->Selected(request->selected);
    EachMethod<TabOp>(request, [&](const TabOp& op) {
        switch (op.kind) {
            case TabKind::Label:
                tab->Label(op.text);
                break;
            case TabKind::AriaLabel:
                tab->AriaLabel(op.text);
                break;
            case TabKind::Disabled:
                tab->Disabled(op.flag);
                break;
            case TabKind::Selected:
                tab->Selected(op.flag);
                break;
        }
    });
    return FinishPart(request, tab);
}

static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kSelectedArgs[] = {
    {"selected", SchemaBoolean()}};

static constexpr ConstructorDescriptor kTabConstructors[] = {
    {"Tab", {}, &ConstructTab}};
static constexpr MethodDescriptor kTabMethods[] = {
    {"label", kLabelArgs, "Sets the visible tab label.",
     &RecordTabString<TabKind::Label>},
    {"aria_label", kLabelArgs, "Sets the accessible tab label.",
     &RecordTabString<TabKind::AriaLabel>},
    {"disabled", kDisabledArgs, "Disables the tab.",
     &RecordTabBool<TabKind::Disabled>},
    {"selected", kSelectedArgs, "Controls selected state.",
     &RecordTabBool<TabKind::Selected>},
};
static constexpr ComponentDescriptor kTab = {
    "Tab", kTabConstructors, kTabMethods,
    "A tab accepted only as a direct TabBar child.", &MaterializeTab};

// ─── TabBar ────────────────────────────────────────────────────────────────

enum class TabBarKind : uint8_t {
    Selected,
    Variant,
    Menu,
    Size,
    OnChange,
};

struct TabBarOp {
    TabBarKind kind = TabBarKind::Selected;
    double index = 0;
    component::TabVariant variant = component::TabVariant::Tab;
    bool flag = false;
    UiSize size = UiSize::Medium;
    ComponentArgument argument = {};
};

template <int Constructor>
static bool ConstructTabBar(PayloadBuild* build, const ComponentArgument* args,
                            int) {
    if (!NonemptyId(build, args[0].string,
                    Constructor == 0 ? "TabBar" : "Tabs"))
        return false;
    build->New<IdPayload>()->id = args[0].string;
    return true;
}

static bool RecordTabBarSelected(PayloadBuild* build,
                                 const ComponentArgument* args, int) {
    if (!IndexOr(build, args[0].number, "TabBar.selected_index(index)"))
        return false;
    TabBarOp* op = build->New<TabBarOp>();
    op->kind = TabBarKind::Selected;
    op->index = args[0].number;
    return true;
}

static bool RecordTabBarVariant(PayloadBuild* build,
                                const ComponentArgument* args, int) {
    Str value = args[0].string;
    component::TabVariant variant;
    if (StrEq(value, "tab")) {
        variant = component::TabVariant::Tab;
    } else if (StrEq(value, "outline")) {
        variant = component::TabVariant::Outline;
    } else if (StrEq(value, "pill")) {
        variant = component::TabVariant::Pill;
    } else if (StrEq(value, "segmented")) {
        variant = component::TabVariant::Segmented;
    } else if (StrEq(value, "underline")) {
        variant = component::TabVariant::Underline;
    } else {
        return build->Fail(fmt("unsupported TabBar variant `%s`", value));
    }
    TabBarOp* op = build->New<TabBarOp>();
    op->kind = TabBarKind::Variant;
    op->variant = variant;
    return true;
}

static bool RecordTabBarMenu(PayloadBuild* build, const ComponentArgument* args,
                             int) {
    TabBarOp* op = build->New<TabBarOp>();
    op->kind = TabBarKind::Menu;
    op->flag = args[0].boolean;
    return true;
}

static bool RecordTabBarSize(PayloadBuild* build, const ComponentArgument* args,
                             int) {
    TabBarOp* op = build->New<TabBarOp>();
    op->kind = TabBarKind::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static bool RecordTabBarChange(PayloadBuild* build,
                               const ComponentArgument* args, int count) {
    const ComponentArgument* value = count == 1 ? args[0].Some() : nullptr;
    if (!value || value->kind != shell::ComponentArgumentKind::Callback)
        return build
            ->Fail(StrL("TabBar.on_change(callback) expects a callback"));
    TabBarOp* op = build->New<TabBarOp>();
    op->kind = TabBarKind::OnChange;
    op->argument = *value;
    return true;
}

static const char* const kTabName[] = {"Tab"};

static El* MaterializeTabBar(MaterializeRequest* request) {
    const IdPayload* payload = request->PayloadAs<IdPayload>();
    if (!payload)
        return request->Fail(StrL("TabBar received an incompatible payload"));
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "TabBar", children[i].componentName,
                          kTabName))
            return nullptr;
    }
    component::TabBar* bar = component::TabBar::New(request->cx, payload->id);
    EachMethod<TabBarOp>(request, [&](const TabBarOp& op) {
        switch (op.kind) {
            case TabBarKind::Selected:
                bar->Selected(IndexInt(op.index));
                break;
            case TabBarKind::Variant:
                bar->WithVariant(op.variant);
                break;
            case TabBarKind::Menu:
                bar->Menu(op.flag);
                break;
            case TabBarKind::Size:
                bar->WithSize(op.size);
                break;
            case TabBarKind::OnChange:
                bar->OnChange(IndexListener(
                    request, op.argument, "TabBar.on_change callback failed"));
                break;
        }
    });
    for (int i = 0; i < count; i++) {
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        component::Tab* tab =
            TakeElementAs<component::Tab>(request, element, "Tab");
        if (!tab) return nullptr;
        bar->Child(tab);
    }
    return FinishTyped(request, bar->IntoEl());
}

static constexpr const char* kTabVariantLiterals[] = {"tab", "outline", "pill",
                                                      "segmented", "underline"};
static constexpr ArgumentDescriptor kVariantArgs[] = {
    {"variant", SchemaEnum(kTabVariantLiterals)}};
static constexpr ArgumentDescriptor kMenuArgs[] = {{"menu", SchemaBoolean()}};

static constexpr ConstructorDescriptor kTabBarConstructors[] = {
    {"TabBar", kIdArgs, &ConstructTabBar<0>},
    {"Tabs", kIdArgs, &ConstructTabBar<1>, "TabBar",
     "Use TabBar; Tabs is retained as a compatibility alias."},
};
static constexpr MethodDescriptor kTabBarMethods[] = {
    {"selected_index", kIndexArgs,
     "Controls the selected zero-based tab index.", &RecordTabBarSelected},
    {"variant", kVariantArgs, "Sets one of the component's five tab variants.",
     &RecordTabBarVariant},
    {"menu", kMenuArgs, "Enables the overflow menu.", &RecordTabBarMenu},
    {"size", kSizeArgs, kSizeDoc, &RecordTabBarSize},
    {"on_change", kIndexCallbackArgs, kIndexCallbackDoc, &RecordTabBarChange},
};
static constexpr ComponentDescriptor kTabBar = {
    "TabBar", kTabBarConstructors, kTabBarMethods,
    "A typed tab list accepting only Tab children; Tabs is a deprecated alias.",
    &MaterializeTabBar};

// ─── StepperItem ───────────────────────────────────────────────────────────

struct StepperItemPayload {};

struct StepperItemOp {
    bool disabled = false;
};

static bool ConstructStepperItem(PayloadBuild* build, const ComponentArgument*,
                                 int) {
    return build->Mark<StepperItemPayload>();
}

static bool RecordStepperItemDisabled(PayloadBuild* build,
                                      const ComponentArgument* args, int) {
    build->New<StepperItemOp>()->disabled = args[0].boolean;
    return true;
}

static El* MaterializeStepperItem(MaterializeRequest* request) {
    if (!shell::PayloadIs<StepperItemPayload>(request->payload))
        return request
            ->Fail(StrL("StepperItem received an incompatible payload"));
    component::StepperItem* item = component::StepperItem::New(request->cx)
                                       ->Disabled(request->disabled);
    EachMethod<StepperItemOp>(
        request, [&](const StepperItemOp& op) { item->Disabled(op.disabled); });
    return FinishPart(request, item);
}

static constexpr ConstructorDescriptor kStepperItemConstructors[] = {
    {"StepperItem", {}, &ConstructStepperItem}};
static constexpr MethodDescriptor kStepperItemMethods[] = {
    {"disabled", kDisabledArgs,
     "Disables this step independently of its parent.",
     &RecordStepperItemDisabled},
};
static constexpr ComponentDescriptor kStepperItem = {
    "StepperItem", kStepperItemConstructors, kStepperItemMethods,
    "A step part accepted only as a direct Stepper child.",
    &MaterializeStepperItem};

// ─── Stepper ───────────────────────────────────────────────────────────────

enum class StepperKind : uint8_t {
    Selected,
    Vertical,
    TextCenter,
    Disabled,
    Size,
    OnChange,
};
using StepperOp = Op<StepperKind>;

static bool ConstructStepper(PayloadBuild* build, const ComponentArgument* args,
                             int) {
    if (!NonemptyId(build, args[0].string, "Stepper")) return false;
    build->New<IdPayload>()->id = args[0].string;
    return true;
}

static bool RecordStepperSelected(PayloadBuild* build,
                                  const ComponentArgument* args, int) {
    if (!IndexOr(build, args[0].number, "Stepper.selected_index(index)"))
        return false;
    StepperOp* op = build->New<StepperOp>();
    op->kind = StepperKind::Selected;
    op->index = args[0].number;
    return true;
}

template <StepperKind K>
static bool RecordStepperBool(PayloadBuild* build,
                              const ComponentArgument* args, int) {
    StepperOp* op = build->New<StepperOp>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

static bool RecordStepperSize(PayloadBuild* build,
                              const ComponentArgument* args, int) {
    StepperOp* op = build->New<StepperOp>();
    op->kind = StepperKind::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static bool RecordStepperChange(PayloadBuild* build,
                                const ComponentArgument* args, int count) {
    const ComponentArgument* value = count == 1 ? args[0].Some() : nullptr;
    if (!value || value->kind != shell::ComponentArgumentKind::Callback)
        return build
            ->Fail(StrL("Stepper.on_change(callback) expects a callback"));
    StepperOp* op = build->New<StepperOp>();
    op->kind = StepperKind::OnChange;
    op->argument = *value;
    return true;
}

static const char* const kStepperItemName[] = {"StepperItem"};

static El* MaterializeStepper(MaterializeRequest* request) {
    const IdPayload* payload = request->PayloadAs<IdPayload>();
    if (!payload)
        return request->Fail(StrL("Stepper received an incompatible payload"));
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "Stepper", children[i].componentName,
                          kStepperItemName))
            return nullptr;
    }
    component::Stepper* stepper =
        component::Stepper::New(request->cx, payload->id)
            ->Disabled(request->disabled);
    EachMethod<StepperOp>(request, [&](const StepperOp& op) {
        switch (op.kind) {
            case StepperKind::Selected:
                stepper->SelectedIndex(IndexInt(op.index));
                break;
            case StepperKind::Vertical:
                stepper->Layout(op.flag ? Axis::Vertical : Axis::Horizontal);
                break;
            case StepperKind::TextCenter:
                stepper->TextCenter(op.flag);
                break;
            case StepperKind::Disabled:
                stepper->Disabled(op.flag);
                break;
            case StepperKind::Size:
                stepper->WithSize(op.size);
                break;
            case StepperKind::OnChange:
                stepper->OnClick(IndexListener(
                    request, op.argument, "Stepper.on_change callback failed"));
                break;
        }
    });
    for (int i = 0; i < count; i++) {
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        component::StepperItem* item = TakeElementAs<component::StepperItem>(
            request, element, "StepperItem");
        if (!item) return nullptr;
        stepper->Item(item);
    }
    return FinishTyped(request, stepper->IntoEl());
}

static constexpr ArgumentDescriptor kVerticalArgs[] = {
    {"vertical", SchemaBoolean()}};
static constexpr ArgumentDescriptor kTextCenterArgs[] = {
    {"text_center", SchemaBoolean()}};

static constexpr ConstructorDescriptor kStepperConstructors[] = {
    {"Stepper", kIdArgs, &ConstructStepper}};
static constexpr MethodDescriptor kStepperMethods[] = {
    {"selected_index", kIndexArgs, "Controls the current zero-based step.",
     &RecordStepperSelected},
    {"vertical", kVerticalArgs,
     "Switches between vertical and horizontal layout.",
     &RecordStepperBool<StepperKind::Vertical>},
    {"text_center", kTextCenterArgs,
     "Centers each step's text in horizontal layouts.",
     &RecordStepperBool<StepperKind::TextCenter>},
    {"disabled", kDisabledArgs, "Disables every step.",
     &RecordStepperBool<StepperKind::Disabled>},
    {"size", kSizeArgs, kSizeDoc, &RecordStepperSize},
    {"on_change", kIndexCallbackArgs, kIndexCallbackDoc, &RecordStepperChange},
};
static constexpr ComponentDescriptor kStepper = {
    "Stepper", kStepperConstructors, kStepperMethods,
    "A typed progress stepper accepting only StepperItem children.",
    &MaterializeStepper};

bool RadioGroupOnChangeCallback(const ComponentPayload& payload,
                                shell::CallbackId* callback) {
    const RadioGroupOp* op = shell::PayloadAs<RadioGroupOp>(payload);
    if (!op || op->kind != RadioGroupKind::OnChange) return false;
    *callback = op->argument.callback;
    return true;
}

} // namespace gpui::component_shell::typed_compound

namespace gpui::component_shell {

bool RegisterTypedCompound(shell::ComponentRegistry* registry,
                           shell::RegistryError* error) {
    using namespace typed_compound;
    return registry->Register(&kAccordionItem, error) &&
           registry->Register(&kAccordion, error) &&
           registry->Register(&kRadioGroup, error) &&
           registry->Register(&kTab, error) &&
           registry->Register(&kTabBar, error) &&
           registry->Register(&kStepperItem, error) &&
           registry->Register(&kStepper, error);
}

} // namespace gpui::component_shell
