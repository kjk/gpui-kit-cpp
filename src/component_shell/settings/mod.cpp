// crates/component-shell/src/shell/settings/mod.rs
//
// Native typed settings hierarchy.
//
// Value fields and reset callbacks are deferred: native getters accept only
// `&App`, while shell callbacks currently require live `Window` + `App`
// authority.

#include "component_shell/families.h"
#include "component_shell/settings/mod.h"
#include "ui/setting.h"

#include <float.h>
#include <limits.h>
#include <math.h>

namespace gpui::component_shell::settings {

using component::SettingGroup;
using component::SettingItem;
using component::SettingPage;

struct Text {
    Str value;
};

struct TextOp {
    enum Kind : uint8_t {
        Description,
        Title,
    } kind = Description;
    Str value;
};

struct BoolOp {
    enum Kind : uint8_t {
        DefaultOpen,
        Resettable,
    } kind = DefaultOpen;
    bool value = false;
};

struct ItemOp {
    enum Kind : uint8_t {
        Description,
        Layout,
        Keywords,
        Disabled,
    } kind = Description;
    Str text;
    gpui::Axis axis = gpui::Axis::Horizontal;
    // The keywords: the recorded array's string elements.
    const ComponentArgument* keywords = nullptr;
    int keywordCount = 0;
    bool flag = false;
};

struct SettingsOp {
    enum Kind : uint8_t {
        Size,
        SidebarWidth,
        SidebarRange,
        Selected,
    } kind = Size;
    UiSize size = UiSize::Medium;
    float a = 0;
    float b = 0;
    int index = 0;
};

bool Positive(double value, const char* label, float* out, Str* error) {
    if (isfinite(value) && value > 0.0 && value <= (double)FLT_MAX) {
        *out = (float)value;
        return true;
    }
    *error = fmt("%s expects a positive finite pixel value", Str(label));
    return false;
}

// A lazy slot the native hierarchy builds when it renders: the frame's
// description and the slot in it. SettingField::render / title_suffix's
// closure over `factory.build(window, cx)`.
struct LazySlot {
    ShellRuntime* runtime = nullptr;
    const shell::SpecArena* specs = nullptr;
    ShellError* error = nullptr;
    shell::ComponentElementFactory factory = {};
    const char* failure = nullptr;
};

static LazySlot* NewLazySlot(MaterializeRequest* request,
                             shell::ComponentElementFactory factory,
                             const char* failure) {
    LazySlot* slot = ArenaNew<LazySlot>(request->cx->a);
    slot->runtime = request->runtime;
    slot->specs = request->specs;
    slot->error = request->error;
    slot->factory = factory;
    slot->failure = failure;
    return slot;
}

static El* BuildLazySlot(LazySlot* slot, Ctx* cx) {
    MaterializeRequest request;
    request.cx = cx;
    request.runtime = slot->runtime;
    request.specs = slot->specs;
    request.error = slot->error;
    if (El* element = request.BuildFactory(slot->factory)) return element;
    return Div(cx->a)->Child(TextEl(
        cx->a, StrDup(cx->a, fmt("%s: no element", Str(slot->failure)))));
}

static El* RenderField(void* user, const component::RenderOptions*, Ctx* cx) {
    return BuildLazySlot((LazySlot*)user, cx);
}

static El* RenderTitleSuffix(void* user, Ctx* cx) {
    return BuildLazySlot((LazySlot*)user, cx);
}

// ─── Materializers ─────────────────────────────────────────────────────────

static El* MaterializeItem(MaterializeRequest* request) {
    const Text* title = request->PayloadAs<Text>();
    if (!title) return request->Fail(StrL("SettingItem payload"));
    shell::ComponentElementFactory field = request->TakeSlotFactory("content");
    if (!field.IsSet())
        return request->Fail(StrL("SettingItem requires content(element)"));
    if (request->ChildrenLen() != 0)
        return request->Fail(StrL("SettingItem does not accept children"));
    Ctx* cx = request->cx;
    SettingItem* item = ArenaNew<SettingItem>(cx->a);
    item->title = title->value;
    item->field = component::SettingFieldKind::Element;
    item->fieldElement
        .user = NewLazySlot(request, field, "Failed to render setting field");
    item->fieldElement.renderField = &RenderField;
    // sf.style().refine(..): the refinement goes to the field, and an
    // element field's renderer (fields/element.rs) ignores the style it is
    // handed, so it changes nothing that renders.
    request->TakeStyle();
    EachMethod<ItemOp>(request, [&](const ItemOp& op) {
        switch (op.kind) {
            case ItemOp::Description:
                item->description = op.text;
                break;
            case ItemOp::Layout:
                item->layout = op.axis;
                break;
            case ItemOp::Keywords:
                item->keywords.Truncate(0);
                for (int i = 0; i < op.keywordCount; i++)
                    item->keywords.Append(cx->a, op.keywords[i].string);
                break;
            case ItemOp::Disabled:
                item->disabled = op.flag;
                break;
        }
    });
    return CarrierOf(cx, item);
}

static El* MaterializeGroup(MaterializeRequest* request) {
    if (!shell::PayloadIs<Empty>(request->payload))
        return request->Fail(StrL("SettingGroup payload"));
    Ctx* cx = request->cx;
    SettingGroup* group = ArenaNew<SettingGroup>(cx->a);
    EachMethod<TextOp>(request, [&](const TextOp& op) {
        if (op.kind == TextOp::Title)
            group->title = op.value;
        else
            group->description = op.value;
    });
    group->refiner = request->TakeStyle();
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    static constexpr const char* kAllowed[] = {"SettingItem"};
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "SettingItem", children[i].componentName,
                          kAllowed))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        SettingItem* item =
            TakeCarriedAs<SettingItem>(request, element, "SettingItem");
        if (!item) return nullptr;
        group->items.Append(cx->a, *item);
    }
    return CarrierOf(cx, group);
}

static El* MaterializePage(MaterializeRequest* request) {
    const Text* title = request->PayloadAs<Text>();
    if (!title) return request->Fail(StrL("SettingPage payload"));
    Ctx* cx = request->cx;
    SettingPage* page = ArenaNew<SettingPage>(cx->a);
    page->title = title->value;
    // Rust visits every method and asks each payload type in turn; the two
    // op types here are disjoint, so their order within a type is what
    // matters.
    for (int i = 0; i < request->MethodCount(); i++) {
        shell::RecordedComponentMethod method = request->Method(i);
        if (const TextOp* op = shell::PayloadAs<TextOp>(method.payload)) {
            if (op->kind == TextOp::Description) page->description = op->value;
        }
        if (const BoolOp* op = shell::PayloadAs<BoolOp>(method.payload)) {
            if (op->kind == BoolOp::DefaultOpen)
                page->defaultOpen = op->value;
            else
                page->resettable = op->value;
        }
    }
    shell::ComponentElementFactory suffix = request->TakeSlotFactory("content");
    if (suffix.IsSet()) {
        page->titleSuffixFn = &RenderTitleSuffix;
        page->titleSuffixUser =
            NewLazySlot(request, suffix, "Failed to render title suffix");
    }
    if (!RejectStyle(request, "SettingPage")) return nullptr;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    static constexpr const char* kAllowed[] = {"SettingGroup"};
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "SettingGroup", children[i].componentName,
                          kAllowed))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        SettingGroup* group =
            TakeCarriedAs<SettingGroup>(request, element, "SettingGroup");
        if (!group) return nullptr;
        page->groups.Append(cx->a, *group);
    }
    return CarrierOf(cx, page);
}

static El* MaterializeSettings(MaterializeRequest* request) {
    const Text* id = request->PayloadAs<Text>();
    if (!id) return request->Fail(StrL("Settings payload"));
    Ctx* cx = request->cx;
    component::Settings* settings = component::Settings::New(cx, id->value);
    EachMethod<SettingsOp>(request, [&](const SettingsOp& op) {
        switch (op.kind) {
            case SettingsOp::Size:
                settings->WithSize(op.size);
                break;
            case SettingsOp::SidebarWidth:
                settings->SidebarWidth(op.a);
                break;
            case SettingsOp::SidebarRange:
                settings->SidebarSizeRange(op.a, op.b);
                break;
            case SettingsOp::Selected: {
                component::SelectIndex index;
                index.pageIx = op.index;
                index.groupIx = -1;
                settings->DefaultSelectedIndex(index);
                break;
            }
        }
    });
    if (!RejectStyle(request, "Settings")) return nullptr;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    static constexpr const char* kAllowed[] = {"SettingPage"};
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "SettingPage", children[i].componentName,
                          kAllowed))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        SettingPage* page =
            TakeCarriedAs<SettingPage>(request, element, "SettingPage");
        if (!page) return nullptr;
        settings->pages.Append(cx->a, *page);
    }
    return settings->IntoEl();
}

// ─── Recorders ─────────────────────────────────────────────────────────────

// The constructors taking one non-empty string: SettingItem's and
// SettingPage's title, Settings' id.
template <int Which>
static bool ConstructText(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        len(StrTrimAscii(args[0].string)) == 0) {
        return build
            ->Fail(Which == 0   ? StrL("SettingItem expects non-empty "
                                       "title")
                   : Which == 1 ? StrL("SettingPage expects non-empty "
                                       "title")
                                : StrL("Settings expects non-empty id"));
    }
    build->New<Text>()->value = args[0].string;
    return true;
}

static bool RecordItemDescription(PayloadBuild* build,
                                  const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(StrL("description expects text"));
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Description;
    op->text = args[0].string;
    return true;
}

static bool RecordItemLayout(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    bool isEnum =
        count == 1 && args[0].kind == shell::ComponentArgumentKind::Enum;
    if (!isEnum || (!StrEq(args[0].string, "horizontal") &&
                    !StrEq(args[0].string, "vertical")))
        return build->Fail(StrL("layout expects horizontal or vertical"));
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Layout;
    op->axis = StrEq(args[0].string, "vertical") ? gpui::Axis::Vertical
                                                 : gpui::Axis::Horizontal;
    return true;
}

static bool RecordItemKeywords(PayloadBuild* build,
                               const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Array)
        return build->Fail(StrL("keywords expects string array"));
    for (int i = 0; i < args[0].count; i++) {
        if (args[0].items[i].kind != shell::ComponentArgumentKind::String)
            return build->Fail(StrL("keywords expects strings"));
    }
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Keywords;
    op->keywords = args[0].items;
    op->keywordCount = args[0].count;
    return true;
}

static bool RecordItemDisabled(PayloadBuild* build,
                               const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("disabled expects boolean"));
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Disabled;
    op->flag = args[0].boolean;
    return true;
}

// text_method(name, ..).
template <TextOp::Kind K>
static bool RecordText(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        len(StrTrimAscii(args[0].string)) == 0) {
        return build
            ->Fail(fmt("%s expects non-empty text",
                       Str(K == TextOp::Title ? "title" : "description")));
    }
    TextOp* op = build->New<TextOp>();
    op->kind = K;
    op->value = args[0].string;
    return true;
}

// bool_method("Setting", name, ..).
template <BoolOp::Kind K>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean) {
        return build->Fail(
            fmt("Setting.%s expects one boolean",
                Str(K == BoolOp::DefaultOpen ? "default_open" : "resettable")));
    }
    BoolOp* op = build->New<BoolOp>();
    op->kind = K;
    op->value = args[0].boolean;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(StrL("size expects semantic size"));
    bool known = false;
    for (const char* literal : kSizeLiterals)
        known = known || StrEq(args[0].string, literal);
    if (!known) return build->Fail(StrL("unsupported size"));
    SettingsOp* op = build->New<SettingsOp>();
    op->kind = SettingsOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static bool RecordSidebarWidth(PayloadBuild* build,
                               const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(StrL("sidebar_width expects number"));
    float width = 0;
    Str error;
    if (!Positive(args[0].number, "sidebar_width", &width, &error))
        return build->Fail(error);
    SettingsOp* op = build->New<SettingsOp>();
    op->kind = SettingsOp::SidebarWidth;
    op->a = width;
    return true;
}

static bool RecordSidebarRange(PayloadBuild* build,
                               const ComponentArgument* args, int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::Number ||
        args[1].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(StrL("sidebar_size_range expects two numbers"));
    float min = 0, max = 0;
    Str error;
    if (!Positive(args[0].number, "minimum", &min, &error) ||
        !Positive(args[1].number, "maximum", &max, &error))
        return build->Fail(error);
    if (min > max) return build->Fail(StrL("minimum must not exceed maximum"));
    SettingsOp* op = build->New<SettingsOp>();
    op->kind = SettingsOp::SidebarRange;
    op->a = min;
    op->b = max;
    return true;
}

static bool RecordSelectedPage(PayloadBuild* build,
                               const ComponentArgument* args, int count) {
    // `*value <= usize::MAX as f64`: 2^64.
    double value = count == 1 ? args[0].number : -1.0;
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number ||
        !isfinite(value) || value < 0.0 || value != floor(value) ||
        value > 18446744073709551616.0) {
        return build
            ->Fail(StrL("default_selected_page expects a nonnegative integer"));
    }
    SettingsOp* op = build->New<SettingsOp>();
    op->kind = SettingsOp::Selected;
    // Pages are int indexed here; any index past the last page selects none,
    // so a larger one says the same thing as INT_MAX.
    op->index = value >= (double)INT_MAX ? INT_MAX : (int)value;
    return true;
}

// ─── Descriptors ───────────────────────────────────────────────────────────

static constexpr const char* kDescriptionDoc =
    "Sets the supporting description shown under the title.";

static constexpr ArgumentDescriptor kTitleArgs[] = {{"title", SchemaString()}};
static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kTextArgs[] = {{"text", SchemaString()}};
static constexpr const char* kAxisLiterals[] = {"horizontal", "vertical"};
static constexpr ArgumentDescriptor kLayoutArgs[] = {
    {"axis", SchemaEnum(kAxisLiterals)}};
static constexpr ArgumentSchema kKeyword = SchemaString();
static constexpr ArgumentDescriptor kKeywordsArgs[] = {
    {"keywords", SchemaArray(&kKeyword)}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kDefaultOpenArgs[] = {
    {"default_open", SchemaBoolean()}};
static constexpr ArgumentDescriptor kResettableArgs[] = {
    {"resettable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ArgumentDescriptor kPixelsArgs[] = {
    {"pixels", SchemaNumber()}};
static constexpr ArgumentDescriptor kRangeArgs[] = {
    {"minimum", SchemaNumber()},
    {"maximum", SchemaNumber()}};
static constexpr ArgumentDescriptor kIndexArgs[] = {{"index", SchemaNumber()}};

// SettingItem
static constexpr ConstructorDescriptor kItemConstructors[] = {
    {"SettingItem", kTitleArgs, &ConstructText<0>}};
static constexpr MethodDescriptor kItemMethods[] = {
    {"description", kTextArgs, kDescriptionDoc, &RecordItemDescription},
    {"layout", kLayoutArgs,
     "Lays the item's label and field out along the given axis.",
     &RecordItemLayout},
    {"keywords", kKeywordsArgs, "Adds search keywords that match this item.",
     &RecordItemKeywords},
    {"disabled", kDisabledArgs, "Disables the item's field.",
     &RecordItemDisabled},
};
static constexpr ComponentDescriptor kItem = {
    "SettingItem", kItemConstructors, kItemMethods,
    "A typed native setting item requiring lazy content(element); style "
    "applies to the field.",
    &MaterializeItem};

// SettingGroup
static constexpr ConstructorDescriptor kGroupConstructors[] = {
    {"SettingGroup", {}, &RecordEmpty}};
static constexpr MethodDescriptor kGroupMethods[] = {
    {"title", kTextArgs, "Sets the displayed title.",
     &RecordText<TextOp::Title>},
    {"description", kTextArgs, kDescriptionDoc,
     &RecordText<TextOp::Description>},
};
static constexpr ComponentDescriptor kGroup = {
    "SettingGroup", kGroupConstructors, kGroupMethods,
    "A styled native setting group accepting only SettingItem children.",
    &MaterializeGroup};

// SettingPage
static constexpr ConstructorDescriptor kPageConstructors[] = {
    {"SettingPage", kTitleArgs, &ConstructText<1>}};
static constexpr MethodDescriptor kPageMethods[] = {
    {"description", kTextArgs, kDescriptionDoc,
     &RecordText<TextOp::Description>},
    {"default_open", kDefaultOpenArgs, "Opens the page's groups by default.",
     &RecordBool<BoolOp::DefaultOpen>},
    {"resettable", kResettableArgs,
     "Shows the control that restores the default value.",
     &RecordBool<BoolOp::Resettable>},
};
static constexpr ComponentDescriptor kPage = {
    "SettingPage", kPageConstructors, kPageMethods,
    "A typed native setting page accepting SettingGroup children and lazy "
    "content(element) as its title suffix; style is rejected.",
    &MaterializePage};

// Settings
static constexpr ConstructorDescriptor kSettingsConstructors[] = {
    {"Settings", kIdArgs, &ConstructText<2>}};
static constexpr MethodDescriptor kSettingsMethods[] = {
    {"size", kSizeArgs, "Sets the settings surface's semantic size.",
     &RecordSize},
    {"sidebar_width", kPixelsArgs, "Sets the sidebar's width in pixels.",
     &RecordSidebarWidth},
    {"sidebar_size_range", kRangeArgs,
     "Bounds how far the sidebar can be resized, in pixels.",
     &RecordSidebarRange},
    {"default_selected_page", kIndexArgs,
     "Selects the page shown when the surface first opens.",
     &RecordSelectedPage},
};
static constexpr ComponentDescriptor kSettings = {
    "Settings", kSettingsConstructors, kSettingsMethods,
    "A native keyed-state settings container accepting only SettingPage "
    "children; style is rejected.",
    &MaterializeSettings};

} // namespace gpui::component_shell::settings

namespace gpui::component_shell {

bool RegisterSettings(shell::ComponentRegistry* registry,
                      shell::RegistryError* error) {
    return registry->Register(&settings::kItem, error) &&
           registry->Register(&settings::kGroup, error) &&
           registry->Register(&settings::kPage, error) &&
           registry->Register(&settings::kSettings, error);
}

} // namespace gpui::component_shell
