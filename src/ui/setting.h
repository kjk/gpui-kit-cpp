#ifndef GPUI_SRC_UI_SETTING_H_
#define GPUI_SRC_UI_SETTING_H_
/* Themed settings — crates/ui/src/setting

   Rust's Settings is a sidebar of pages, each page a list of groups, each
   group a list of items; an item is a title, a description and a field, and
   the search box at the top of the sidebar filters the whole tree by what an
   item says. The field is built from the type of the value behind the
   setting, and so is this one — see SettingFieldKind below. */

#include "ui/sizing.h"
#include "ui/searchable_list.h"
#include "ui/group_box.h"

namespace gpui {

namespace component {

// SettingFieldType, crates/ui/src/setting/fields. Rust builds the control out
// of the type of the value behind the setting — a bool is a Switch or a
// Checkbox, an f64 a NumberInput, a String an Input or a Dropdown — reaching
// it through a getter/setter pair the field closes over. There are no closures
// here, so the pair is the caller's own value: the field is handed the address
// of the bool, the InputState or the SearchableList behind the setting, and
// does the reading, the writing, the dirty test and the reset itself. Element
// is Rust's `SettingField::element`, the escape hatch where the caller builds
// the control.
enum class SettingFieldType : uint8_t {
    Element,
    Switch,
    Checkbox,
    Input,
    NumberInput,
    Dropdown
};

// Compatibility spelling from the first port. New code uses the source name.
using SettingFieldKind = SettingFieldType;

// NumberFieldOptions: what a NumberInput field's two steppers obey. Rust's
// defaults are the whole f64 range and a step of one.
struct NumberFieldOptions {
    double min = -1e300;
    double max = 1e300;
    double step = 1;
};

// The immutable context passed to every field renderer. Rust narrows copies
// as it descends page -> group -> item; the POD port does the same explicitly.
struct RenderOptions {
    int pageIx = 0;
    int groupIx = 0;
    int itemIx = 0;
    UiSize size = UiSize::Medium;
    GroupBoxVariant groupVariant = GroupBoxVariant::Normal;
    Axis layout = Axis::Horizontal;
    bool disabled = false;

    static RenderOptions New();
    RenderOptions WithPageIx(int value) const;
    RenderOptions WithGroupIx(int value) const;
    RenderOptions WithItemIx(int value) const;
    RenderOptions WithSize(UiSize value) const;
    RenderOptions WithGroupVariant(GroupBoxVariant value) const;
    RenderOptions WithLayout(Axis value) const;
    RenderOptions WithDisabled(bool value) const;
};

// Rust expresses this as a trait. The C++ port's no-owning-callback seam is a
// renderer plus its caller-owned payload; it has the same RenderOptions input.
using SettingFieldElementFn = El* (*)(void* user, const RenderOptions* options,
                                      Ctx* cx);

struct SettingFieldElement {
    void* user = nullptr;
    SettingFieldElementFn renderField = nullptr;

    bool IsValid() const { return renderField != nullptr; }
    El* Render(const RenderOptions* options, Ctx* cx) const {
        return renderField ? renderField(user, options, cx) : nullptr;
    }
};

template <typename T>
using SettingValueFn = T (*)(void* user, const App* app);
template <typename T>
using SettingSetValueFn = void (*)(void* user, T value, App* app);
using SettingDirtyFn = bool (*)(void* user, const App* app);
using SettingResetFn = void (*)(void* user, Ctx* cx);

template <typename T>
inline bool SettingValueSame(const T& a, const T& b) {
    return a == b;
}

template <>
inline bool SettingValueSame<Str>(const Str& a, const Str& b) {
    return base::StrEq(a, b);
}

// A source-shaped typed field. Rust retains Rc closures; this tree retains
// plain function pointers and a caller-owned payload. This contract is useful
// independently of the Settings frame builder and is type-erased below.
template <typename T>
struct SettingField {
    SettingFieldType fieldType = SettingFieldType::Element;
    void* user = nullptr;
    SettingValueFn<T> value = nullptr;
    SettingSetValueFn<T> setValue = nullptr;
    T defaultValue = {};
    bool hasDefault = false;
    SettingDirtyFn dirty = nullptr;
    SettingResetFn reset = nullptr;
    NumberFieldOptions number = {};
    const SearchableItem* dropdownOptions = nullptr;
    int dropdownOptionsLen = 0;
    bool dropdownScrollable = false;
    SettingFieldElement element = {};

    static SettingField New(SettingFieldType type, void* user,
                            SettingValueFn<T> getValue,
                            SettingSetValueFn<T> putValue) {
        SettingField out;
        out.fieldType = type;
        out.user = user;
        out.value = getValue;
        out.setValue = putValue;
        return out;
    }
    SettingField& DefaultValue(T value_) {
        defaultValue = value_;
        hasDefault = true;
        return *this;
    }
    SettingField& OnReset(SettingDirtyFn isDirty, SettingResetFn doReset) {
        dirty = isDirty;
        reset = doReset;
        return *this;
    }
    bool IsResettable(const App* app) const {
        if (dirty) {
            return dirty(user, app);
        }
        if (fieldType == SettingFieldType::Element || !hasDefault || !value) {
            return false;
        }
        return !SettingValueSame(value(user, app), defaultValue);
    }
    void Reset(Ctx* cx) const {
        if (reset) {
            reset(user, cx);
        } else if (hasDefault && setValue) {
            setValue(user, defaultValue, cx ? cx->app : nullptr);
        }
    }
};

using SettingFieldTypeId = uintptr_t;

template <typename T>
inline SettingFieldTypeId SettingFieldTypeOf() {
    static const uint8_t tag = 0;
    return (SettingFieldTypeId)&tag;
}

// The Rust trait object, represented without RTTI. It retains the concrete
// field address and trampolines only the operations Settings needs.
struct AnySettingField {
    void* field = nullptr;
    SettingFieldTypeId typeId = 0;
    SettingFieldType fieldType = SettingFieldType::Element;
    bool (*isResettable)(void* field, const App* app) = nullptr;
    void (*reset)(void* field, Ctx* cx) = nullptr;

    bool IsValid() const { return field != nullptr; }
    bool IsResettable(const App* app) const {
        return isResettable && isResettable(field, app);
    }
    void Reset(Ctx* cx) const {
        if (reset) {
            reset(field, cx);
        }
    }
};

template <typename T>
inline AnySettingField EraseSettingField(SettingField<T>* field) {
    AnySettingField out;
    out.field = field;
    out.typeId = SettingFieldTypeOf<T>();
    out.fieldType = field ? field->fieldType : SettingFieldType::Element;
    out.isResettable = [](void* p, const App* app) {
        return ((SettingField<T>*)p)->IsResettable(app);
    };
    out.reset = [](void* p, Ctx* cx) { ((SettingField<T>*)p)->Reset(cx); };
    return out;
}

// Which page and optional group should be selected initially. `-1` is Rust's
// `None`; group zero remains distinguishable from no deferred group scroll.
struct SelectIndex {
    int pageIx = 0;
    int groupIx = -1;
};

// SettingItem::Item: the title, what it is for, and the control that changes
// it. `keywords` is what the search box matches on beyond the two strings.
struct SettingItem {
    Str title = {};
    Str description = {};
    El* control = nullptr;
    SettingFieldElement fieldElement = {};
    ArenaVec<Str> keywords;
    bool disabled = false;
    // is_resettable / on_reset: an item that has been changed shows a reset
    // button beside it. A typed field works both out for itself; these are
    // what `Resettable()` fills in for an Element field.
    bool dirty = false;
    Listener onReset = {};
    // The typed field, when the item has one. `control` is the Element case.
    SettingFieldKind field = SettingFieldKind::Element;
    // Switch / Checkbox.
    bool* boolValue = nullptr;
    bool defBool = false;
    // Input / NumberInput. `value` is what the field reads on the frame it is
    // first built -- `get_value(&field, cx)`, which upstream asks the setting
    // registry for. The field itself belongs to the row it is on and is keyed
    // there, not held by the caller.
    Str value = {};
    Str defStr = {};
    NumberFieldOptions num = {};
    // Dropdown.
    Entity<SearchableListState> list = {};
    const SearchableItem* items = nullptr;
    int nItems = 0;
    int defIndex = 0;
    // The field is the caller's default unless it named one, and it is what
    // decides whether the reset button is there at all.
    bool hasDefault = false;
    // 0 takes the layout's width: w_32 beside the text, full width under it.
    float fieldW = 0;
    // layout(Axis): Horizontal puts the control beside the text, Vertical
    // under it.
    Axis layout = Axis::Horizontal;
};

struct SettingGroup {
    Str title = {};
    Str description = {};
    ArenaVec<SettingItem> items;
};

struct SettingPage {
    Str title = {};
    Str description = {};
    IconName icon = IconName::None;
    ArenaVec<SettingGroup> groups;
    // SettingPage::title_suffix: whatever the caller puts beside the title.
    // The story's is a ghost Info button that opens the docs.
    El* titleSuffix = nullptr;
    // SettingPage::resettable, default true: whether this page offers the
    // reset buttons at all — the per-item one, and the Reset All in its
    // header once anything on it has been changed.
    bool resettable = true;
};

// is_match: the query against the title, the description and the keywords,
// case-insensitively. An empty query matches everything.
bool SettingItemMatches(const SettingItem* it, Str query);
bool SettingGroupMatches(const SettingGroup* g, Str query);
bool SettingPageMatches(const SettingPage* p, Str query);

// One typed field as the frame rendered it, kept so the listeners hung off it
// can find what to change. The element tree and the Settings builder are on
// the frame arena and go with the frame; this table is rebuilt with it, in the
// order the fields painted, and an index into it is what the listeners bind —
// which is the same thing Rust's `options.item_ix()` keys its state on.
struct SettingBinding {
    SettingFieldKind kind = SettingFieldKind::Element;
    bool* boolValue = nullptr;
    bool defBool = false;
    InputState* input = nullptr;
    Str defStr = {};
    NumberFieldOptions num = {};
    Entity<SearchableListState> list = {};
    int defIndex = 0;
};

// The field behind one string or number setting. `use_keyed_state("string-
// state-{page}-{group}-{item}", .., |window, cx| InputState::new(window, cx)
// .default_value(value))`: the input belongs to the row it is drawn on, and
// the port's id stack is what folds the page, the group and the item into its
// name. `seeded` is `default_value`, which only the first frame does.
struct SettingFieldInput {
    InputState input;
    bool seeded = false;
};

// SettingsState: which page is showing, which group the sidebar last jumped
// to, the field the sidebar searches by, and the fields the last frame built —
// the values behind those are the caller's.
struct SettingsState {
    int page = 0;
    int group = -1;
    bool selectionInitialized = false;
    // `SettingsState { search_input: cx.new(|cx| InputState::new(window, cx)
    // .placeholder(t!("Settings.search_placeholder"))), .. }`: the pane's own
    // field, made with the state rather than asked of the application. Every
    // settings pane has one and they all read the same, so there is nothing
    // for a caller to decide about it.
    InputState search;
    Vec<SettingBinding> fields;

    ~SettingsState() { VecReset(fields); }

    static void OnPageClick(SettingsState* self, Ctx* cx, const ClickEvent* ev,
                            intptr_t page);
    static void OnGroupClick(SettingsState* self, Ctx* cx, const ClickEvent* ev,
                             intptr_t packed);
    // A typed field's own handlers. `ix` is into `fields`.
    static void OnFieldClick(SettingsState* self, Ctx* cx, const ClickEvent* ev,
                             intptr_t ix);
    static void OnFieldReset(SettingsState* self, Ctx* cx, const ClickEvent* ev,
                             intptr_t ix);
    // reset_all: every field the page has built goes back to its default.
    static void OnResetPage(SettingsState* self, Ctx* cx, const ClickEvent* ev,
                            intptr_t unused);
    // A click in the search field, which is where the window's keystrokes go
    // from then on.
    static void OnSearchFocus(SettingsState* self, Ctx* cx,
                              const ClickEvent* ev);
};

struct Settings {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<SettingsState> state = {};
    // The three levels grow into the frame arena the builder is on, so a
    // page is as long as the caller makes it.
    ArenaVec<SettingPage> pages;
    float sidebarWidth = 250;
    float sidebarMinWidth = 160;
    float sidebarMaxWidth = 360;
    float h = 480;
    UiSize size = UiSize::Medium;
    SelectIndex defaultSelectedIndex = {};
    // GroupBoxVariant: whether a group is a card with a border or a plain
    // run of rows under a heading.
    bool bordered = true;

    // The state is optional, as `use_keyed_state(self.id, ..)` is upstream:
    // a pane left to itself keys its own off the id.
    static Settings* New(Ctx* cx, Str id, Entity<SettingsState> state = {});
    Settings* Page(Str title, IconName icon = IconName::None,
                   Str description = {});
    Settings* Group(Str title, Str description = {});
    Settings* Item(Str title, Str description, El* control = nullptr);
    Settings* FieldElement(SettingFieldElement element);
    // The typed fields, each filling in the control of the item last added.
    // `defValue` is Rust's `default_value`: naming one is what puts the reset
    // button behind the item, and a Str default has to outlive the frame.
    Settings* SwitchField(bool* value, bool defValue = false,
                          bool hasDefault = false);
    Settings* CheckboxField(bool* value, bool defValue = false,
                            bool hasDefault = false);
    Settings* InputField(Str value = {}, Str defValue = {});
    Settings* NumberField(Str value = {}, NumberFieldOptions opts = {},
                          Str defValue = {});
    Settings* DropdownField(Entity<SearchableListState> list,
                            const SearchableItem* items, int nItems,
                            int defIndex = -1);
    // The width of the field built above; 0 is the layout's own.
    Settings* FieldWidth(float v);
    // SettingPage::resettable, on the page last added.
    Settings* PageResettable(bool v);
    // SettingPage::title_suffix, on the page just declared.
    Settings* PageTitleSuffix(El* e);
    // The item last added: its keywords, whether it is disabled, and what a
    // reset does.
    Settings* Keywords(Str a1, Str a2 = {}, Str a3 = {});
    Settings* Keyword(Str keyword);
    Settings* Disabled(bool v = true);
    Settings* Resettable(bool dirty, Listener onReset);
    Settings* Layout(Axis axis);
    Settings* SidebarWidth(float v);
    Settings* SidebarSizeRange(float minWidth, float maxWidth);
    Settings* WithSize(UiSize value);
    Settings* DefaultSelectedIndex(SelectIndex value);
    Settings* H(float v);
    Settings* Bordered(bool v);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_SETTING_H_
