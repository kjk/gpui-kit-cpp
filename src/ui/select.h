#ifndef GPUI_SRC_UI_SELECT_H_
#define GPUI_SRC_UI_SELECT_H_
/* Themed select — crates/ui/src/select.rs

   Rust's Select is a trigger over a SearchableList: the list holds the items,
   the query and the selection, and the Select is what shows the selection and
   opens the list. So is this one. */

#include "ui/searchable_list.h"

namespace gpui {

namespace component {

// Pinned backward-compatible re-exports. They are exact aliases to the
// searchable-list source types, as in Rust's public `pub use` block.
using SelectGroup = SearchableGroup;
using SelectDelegate = SearchableListDelegate;
using SelectItem = SearchableListItem;
using SelectListItem = SearchableListItemElement;

// The source Select owns a small render-once caret instead of drawing the
// chevron inline. Keeping it as a value also preserves the source size rule:
// xsmall and small stay themselves, every larger trigger uses medium.
struct Caret {
    UiSize size = UiSize::Medium;
    Rgba color = {};
    bool hasColor = false;

    static Caret New(UiSize size);
    Caret TextColor(Rgba color) const;
    float IconSize() const;
    El* IntoEl(Arena* a) const;
};

// SelectEvent::Confirm(Option<Value>). C++ represents the sole enum variant
// as a tagged payload: hasValue=false is Confirm(None).
struct SelectEvent {
    bool hasValue = false;
    IndexPath index = {};
    Str value = {};
};

// Rust keeps SelectState outside SearchableListState: the latter is the list
// delegate and cursor, while this entity is the committed select and the
// event emitter. `state` deliberately comes first so the same generational
// entity can be rebound to the inner state for the list listeners; the
// entity remains owned and destroyed as SelectState.
struct SelectState {
    SearchableListState state;
    InputState queryInput;
    InputState* activeQuery = nullptr;
    bool searchable = false;
    IconName icon = IconName::None;
    Str titlePrefix = {};
    bool focusRingEnabled = true;
    Entity<SelectState> self = {};

    static Entity<SelectState> New(App* app);
    SearchableListState* List() { return &state; }
    const SearchableListState* List() const { return &state; }
    void Searchable(bool value);
    void SetItems(const SearchableItem* items, int nItems);
    void SetSelectedIndex(const IndexPath* selected, Ctx* cx);
    void SetSelectedIndex(int flatIndex, Ctx* cx);
    void SetSelectedValue(Str value, Ctx* cx);
    bool SelectedIndex(IndexPath* out) const;
    Str SelectedValue() const;
    void Focus(Window* win) const;
    void SetOpen(bool open, Ctx* cx);
    void ToggleMenu(Ctx* cx);
    void ClearQueryAndRestore(Ctx* cx);
    void Clean(Ctx* cx);

    static void OnListClose(SelectState* self, Ctx* cx, const TickEvent* event);
    static void OnListChange(SelectState* self, Ctx* cx,
                             const ListEvent* event);
    static void OnMouseDownOut(SelectState* self, Ctx* cx,
                               const MouseDownEvent* event);
};

// Typed rebind of the first-member list state. This is the C++ counterpart
// of `SelectState.state.list`: one entity lifetime, two typed views.
Entity<SearchableListState> SelectListEntity(Entity<SelectState> state);

struct Select {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<SearchableListState> state = {};
    Entity<SelectState> selectState = {};
    // The items, which are the caller's: they have to outlive the frame, so a
    // static array rather than one built on the frame arena.
    const SearchableItem* items = nullptr;
    int nItems = 0;
    const Str* sections = nullptr;
    int nSections = 0;
    Str placeholder = {};
    // The accessible name. The placeholder and the selected value are not
    // used as one, because they describe the current value rather than the
    // control itself.
    Str accessibilityLabel = {};
    Str titlePrefix = {};
    Str empty = {};
    El* emptyEl = nullptr;
    float width = kFill;
    float menuWidth = 0; // menu_width(px(..)): wider than the trigger
    float menuMaxH = 0;
    UiSize size = UiSize::Medium;
    IconName icon = IconName::None; // replaces the caret when set
    // Combobox::check_icon, which the Select forwards to its list.
    IconName checkIcon = IconName::Check;
    bool disabled = false;
    bool cleanable = false;
    bool appearance = true;
    bool focusRing = true;
    // searchable(true): a query field at the top of the list, which is what
    // makes a Select a ComboBox in all but name.
    InputState* query = nullptr;
    Listener onQueryFocus = {};
    // render_trigger: what the trigger box holds instead of the title and the
    // caret. Rust hands the closure the selection and lets it build the lot,
    // including its own Caret; a caller here builds the same element and the
    // trigger box only supplies the border, the size and the click.
    El* trigger = nullptr;
    // Combobox::footer: an action under the option list.
    El* footer = nullptr;
    SearchableListDelegate delegate = {};
    bool hasDelegate = false;
    Listener onToggle;
    Listener onClear;
    Listener onMouseDownOut;
    Bounds* triggerBoundsOut = nullptr;
    Style triggerStyle = {};
    uint32_t triggerStyleSet = 0;

    static Select* New(Ctx* cx, Str id, Entity<SearchableListState> state);
    static Select* New(Ctx* cx, Str id, Entity<SelectState> state);
    Select* Items(const SearchableItem* items, int n);
    Select* Sections(const Str* titles, int n);
    Select* Placeholder(Str s);
    // Set the name a screen reader announces for the select.
    Select* AccessibilityLabel(Str s);
    Select* TitlePrefix(Str s);
    Select* Empty(Str s);
    Select* Empty(El* element);
    Select* W(float v);
    Select* MenuWidth(float v);
    Select* MenuMaxH(float v);
    Select* WithSize(UiSize s);
    Select* Icon(IconName n);
    Select* CheckIcon(IconName n);
    Select* Disabled(bool v);
    Select* Cleanable(bool v = true);
    Select* Appearance(bool v);
    // FocusableExt::focus_ring: no focus appearance on this control.
    Select* FocusRing(bool v);
    Select* Searchable(InputState* query, Listener onFocus);
    Select* Trigger(El* e);
    Select* Footer(El* e);
    Select* Delegate(const SearchableListDelegate& value);
    // Multiple: the list toggles rather than replaces, and the trigger says
    // how many are picked.
    Select* Multiple(bool v = true);
    Select* OnToggle(Listener fn);
    Select* OnClear(Listener fn);
    Select* OnMouseDownOut(Listener fn);
    Select* TriggerBoundsOut(Bounds* bounds);
    Select* TriggerRefine(const Style& style, uint32_t fields);
    El* IntoEl();
};

// What the trigger shows: the one selected title, "N selected" for several,
// or the placeholder for none. Rust builds the same three cases.
Str SelectTriggerTitle(const SearchableListState* s, Str placeholder,
                       Str titlePrefix, Arena* a);

// SelectState::toggle / clear, as handlers a trigger can bind straight to.
void SelectToggleOpen(SearchableListState* s, Ctx* cx);
void SelectToggleOpen(SelectState* s, Ctx* cx);

// Declare the "Select" key context on `root` and hang the five handlers off
// it. The themed Select does this for itself; a caller that builds its own
// select out of the same parts can too.
void SelectBindKeys(Ctx* cx, El* root, Entity<SearchableListState> state);
void SelectClear(SearchableListState* s, Ctx* cx);
void SelectClear(SelectState* s, Ctx* cx);

} // namespace component

template <>
struct EventEmitter<component::SelectState, component::SelectEvent> {};

} // namespace gpui
#endif // GPUI_SRC_UI_SELECT_H_
