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
    // The icon's size in rems, at the rem size of the window `cx` builds for.
    float IconSize(const Ctx* cx) const;
    El* IntoEl(const Ctx* cx, Arena* a) const;
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
    // SearchableListState::new's three cx.on_blur subscriptions — the list,
    // its query field and the trigger — and the window they are on. Rust
    // makes them with the state; this state is made without a window, so the
    // first render (or WatchBlur) makes them instead.
    Window* blurWin = nullptr;
    int blurQueryFocus = 0;

    static Entity<SelectState> New(App* app);
    SearchableListState* List() { return &state; }
    const SearchableListState* List() const { return &state; }
    void Searchable(bool value);
    void SetItems(const SearchableItem* items, int nItems);
    void SetSelectedIndex(const IndexPath* selected, Ctx* cx);
    void SetSelectedIndex(int flatIndex, Ctx* cx);
    void SetSelectedValue(Str value, Ctx* cx);
    // The highlighted row (list.selected_index), not the committed value.
    bool SelectedIndex(IndexPath* out) const;
    Str SelectedValue() const;
    void Focus(Window* win) const;
    void SetOpen(bool open, Ctx* cx);
    void ToggleMenu(Ctx* cx);
    void ClearQueryAndRestore(Ctx* cx);
    void Clean(Ctx* cx);
    void WatchBlur(Window* win);

    // Select's own trigger click: what a Select bound to this state toggles
    // with when its caller gave it no OnToggle.
    static void OnToggle(SelectState* self, Ctx* cx, const ClickEvent* event);
    static void OnListClose(SelectState* self, Ctx* cx, const TickEvent* event);
    static void OnListChange(SelectState* self, Ctx* cx,
                             const ListEvent* event);
    static void OnMouseDownOut(SelectState* self, Ctx* cx,
                               const MouseDownEvent* event);
    static void OnBlur(SelectState* self, Ctx* cx, const FocusHandle* lost);
};

// Typed rebind of the first-member list state. This is the C++ counterpart
// of `SelectState.state.list`: one entity lifetime, two typed views.
Entity<SearchableListState> SelectListEntity(Entity<SelectState> state);

// What a Select and the Combobox that is one both hold: the Combobox hands
// its set to the Select underneath whole.
struct SelectFields : SearchableListContent {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<SearchableListState> state = {};
    Str placeholder = {};
    Str empty = {};
    // `w_full()`: the trigger fills what holds it.
    float width = kFill;
    float menuWidth = 0; // menu_width(px(..)): wider than the trigger
    float menuMaxH = 0;
    IconName icon = IconName::None; // replaces the caret when set
    bool disabled = false;
    bool cleanable = false;
    bool appearance = true;
    bool focusRing = true;
    // render_trigger: what the trigger box holds instead of the title and the
    // caret. Rust hands the closure the selection and lets it build the lot,
    // including its own Caret; a caller here builds the same element and the
    // trigger box only supplies the border, the size and the click.
    El* trigger = nullptr;
    Listener onToggle;
    Listener onClear;
};

// The setters both builders take, each returning its own builder.
template <typename Self>
struct SelectBuilder : SearchableContentBuilder<Self, SelectFields> {
    Self* Placeholder(Str s) {
        this->placeholder = s;
        return this->This();
    }
    Self* Empty(Str s) {
        this->empty = s;
        return this->This();
    }
    Self* W(float v) {
        this->width = v;
        return this->This();
    }
    Self* MenuWidth(float v) {
        this->menuWidth = v;
        return this->This();
    }
    Self* MenuMaxH(float v) {
        this->menuMaxH = v;
        return this->This();
    }
    Self* Icon(IconName n) {
        this->icon = n;
        return this->This();
    }
    Self* Disabled(bool v) {
        this->disabled = v;
        return this->This();
    }
    Self* Cleanable(bool v = true) {
        this->cleanable = v;
        return this->This();
    }
    Self* Appearance(bool v) {
        this->appearance = v;
        return this->This();
    }
    // FocusableExt::focus_ring: no focus appearance on this control.
    Self* FocusRing(bool v) {
        this->focusRing = v;
        return this->This();
    }
    Self* Trigger(El* e) {
        this->trigger = e;
        return this->This();
    }
    Self* OnToggle(Listener fn) {
        this->onToggle = fn;
        return this->This();
    }
    Self* OnClear(Listener fn) {
        this->onClear = fn;
        return this->This();
    }
};

struct Select : SelectBuilder<Select> {
    using SelectBuilder<Select>::Empty;
    Entity<SelectState> selectState = {};
    // The accessible name. The placeholder and the selected value are not
    // used as one, because they describe the current value rather than the
    // control itself.
    Str accessibilityLabel = {};
    Str titlePrefix = {};
    El* emptyEl = nullptr;
    // None defers to the platform. 0 off, 1 on.
    int scrollBounce = -1;
    Listener onMouseDownOut;
    Bounds* triggerBoundsOut = nullptr;
    Style triggerStyle = {};
    uint32_t triggerStyleSet = 0;
    // `impl Styled for Select`: a style refinement replayed onto the trigger
    // box after its appearance, where select.rs calls `refine_style`. What
    // the shell hands a registered Select, whose style is an El replay.
    ElRefiner triggerRefiner = {};

    static Select* New(Ctx* cx, Str id, Entity<SearchableListState> state);
    static Select* New(Ctx* cx, Str id, Entity<SelectState> state);
    // Set the name a screen reader announces for the select.
    Select* AccessibilityLabel(Str s);
    Select* TitlePrefix(Str s);
    Select* Empty(El* element);
    Select* Searchable(InputState* query, Listener onFocus);
    // Bounce the dropdown list at its vertical boundaries. On by default on
    // iOS, Android and macOS, off elsewhere.
    Select* ScrollBounce(bool v);
    // Multiple: the list toggles rather than replaces, and the trigger says
    // how many are picked.
    Select* Multiple(bool v = true);
    Select* OnMouseDownOut(Listener fn);
    Select* TriggerBoundsOut(Bounds* bounds);
    Select* TriggerRefine(const Style& style, uint32_t fields);
    Select* TriggerRefiner(ElRefiner value);
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
// Emitted once whenever an open menu closes, including after a selection is
// confirmed (SelectEvent first, then this).
template <>
struct EventEmitter<component::SelectState, DismissEvent> {};

} // namespace gpui
#endif // GPUI_SRC_UI_SELECT_H_
