#ifndef GPUI_UI_COMBOBOX_H_
#define GPUI_UI_COMBOBOX_H_
/* Themed combobox — crates/ui/src/combobox.rs

   A ComboBox is a Select whose list is always searchable: the same
   SearchableList underneath, with a query field in the dropdown. */

#include "ui/select.h"

namespace gpui {

namespace component {

using ComboboxChange = SearchableListChange;

enum class ComboboxEventKind : uint8_t {
    Change,
    Confirm
};

// Rust's enum variants both carry Vec<Value>. The event call is synchronous,
// so the C++ tagged payload borrows a temporary POD array for its duration.
struct ComboboxEvent {
    ComboboxEventKind kind = ComboboxEventKind::Change;
    const Str* values = nullptr;
    int nValues = 0;
};

struct ComboboxTriggerContext {
    const SearchableListState* state = nullptr;
    Str placeholder = {};
    bool hasPlaceholder = false;
    bool open = false;
    bool disabled = false;
    UiSize size = UiSize::Medium;

    int SelectionCount() const;
    int SelectionIndex(int at) const;
    const SearchableListItem* SelectionItem(int at) const;
    Str Placeholder() const;
    bool IsOpen() const { return open; }
    bool IsDisabled() const { return disabled; }
    UiSize Size() const { return size; }
};

// ComboboxState owns the searchable list but remains its own event-emitting
// entity. The first-member rebind is the same ownership shape as SelectState.
struct ComboboxState {
    SearchableListState state;
    InputState queryInput;
    Vec<int> selectionSnapshot;
    bool multiple = false;
    bool searchable = false;
    IconName triggerIcon = IconName::None;
    IconName checkIcon = IconName::Check;
    bool focusRingEnabled = true;
    Bounds bounds = {};
    Entity<ComboboxState> self = {};

    static Entity<ComboboxState> New(App* app);
    SearchableListState* List() { return &state; }
    const SearchableListState* List() const { return &state; }
    ComboboxState* Multiple(bool value);
    ComboboxState* Searchable(bool value);
    void SetItems(const SearchableListItem* items, int nItems);
    void SetDelegate(const SearchableListDelegate& delegate);
    void SelectedValues(Vec<Str>* out) const;
    Str SelectedValue() const;
    const Vec<int>& Selection() const { return state.selected; }
    void SetSelectedValues(const Str* values, int nValues, Ctx* cx);
    void SetSelectedIndices(const IndexPath* indices, int n, Ctx* cx);
    bool AddSelectedIndex(IndexPath index, Ctx* cx);
    bool RemoveSelectedIndex(IndexPath index, Ctx* cx);
    void ClearSelection(Ctx* cx);
    void Focus(Window* win) const;
    const FocusHandle* FocusHandleNow() const;
    Str Query() const { return InputValue(&queryInput); }
    void SetQuery(Str query, Ctx* cx);
    void SetOpen(bool open, Ctx* cx);
    void SyncSnapshot();
    void Emit(Ctx* cx, ComboboxEventKind kind);

    static void OnListChange(ComboboxState* self, Ctx* cx,
                             const ListEvent* event);
    static void OnToggle(ComboboxState* self, Ctx* cx, const ClickEvent* event);
    static void OnClear(ComboboxState* self, Ctx* cx, const ClickEvent* event);
    static void OnMouseDownOut(ComboboxState* self, Ctx* cx,
                               const MouseDownEvent* event);

    ~ComboboxState() { VecReset(selectionSnapshot); }
};

Entity<SearchableListState> ComboboxListEntity(Entity<ComboboxState> state);

struct Combobox {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<SearchableListState> state = {};
    Entity<ComboboxState> comboboxState = {};
    const SearchableItem* items = nullptr;
    int nItems = 0;
    const Str* sections = nullptr;
    int nSections = 0;
    Str placeholder = {};
    Str searchPlaceholder = {};
    Str empty = {};
    // An optional icon before the title, as the icons story shows.
    IconName icon = IconName::None;
    // Combobox::check_icon: what marks a selected row.
    IconName checkIcon = IconName::Check;
    float width = 280;
    float menuWidth = 0;
    float menuMaxH = 0;
    UiSize size = UiSize::Medium;
    bool disabled = false;
    bool cleanable = false;
    bool appearance = true;
    bool focusRing = true;
    InputState* query = nullptr;
    Listener onQueryFocus = {};
    // Combobox::render_trigger / Combobox::footer, both forwarded to the
    // Select underneath.
    El* trigger = nullptr;
    El* footer = nullptr;
    void* triggerData = nullptr;
    void* footerData = nullptr;
    void* emptyData = nullptr;
    El* (*renderTrigger)(Ctx* cx, void* data,
                         const ComboboxTriggerContext* trigger) = nullptr;
    El* (*renderFooter)(Ctx* cx, void* data) = nullptr;
    El* (*renderEmpty)(Ctx* cx, void* data) = nullptr;
    SearchableListDelegate delegate = {};
    bool hasDelegate = false;
    Style style = {};
    uint32_t styleSet = 0;
    // The same refinement as an El replay (Select::TriggerRefiner).
    ElRefiner refiner = {};
    Listener onToggle;
    Listener onClear;

    static Combobox* New(Ctx* cx, Str id, Entity<SearchableListState> state,
                         InputState* query);
    static Combobox* New(Ctx* cx, Str id, Entity<ComboboxState> state);
    Combobox* Items(const SearchableItem* items, int n);
    Combobox* Sections(const Str* titles, int n);
    Combobox* Placeholder(Str s);
    Combobox* SearchPlaceholder(Str s);
    Combobox* Empty(Str s);
    Combobox* Icon(IconName n);
    Combobox* CheckIcon(IconName n);
    Combobox* W(float v);
    Combobox* MenuWidth(float v);
    Combobox* MenuMaxH(float v);
    Combobox* WithSize(UiSize value);
    Combobox* Disabled(bool v);
    Combobox* Cleanable(bool v = true);
    Combobox* Appearance(bool v);
    // FocusableExt::focus_ring: no focus appearance on this control.
    Combobox* FocusRing(bool v);
    Combobox* Multiple(bool v = true);
    Combobox* Trigger(El* e);
    Combobox* RenderTrigger(void* data,
                            El* (*fn)(Ctx* cx, void* data,
                                      const ComboboxTriggerContext* trigger));
    Combobox* Footer(El* e);
    Combobox* RenderFooter(void* data, El* (*fn)(Ctx* cx, void* data));
    Combobox* RenderEmpty(void* data, El* (*fn)(Ctx* cx, void* data));
    Combobox* Delegate(const SearchableListDelegate& value);
    Combobox* Refine(const Style& value, uint32_t fields);
    Combobox* Refiner(ElRefiner value);
    // ComboboxState::max_selected, which the Max2 delegate's on_will_change
    // comes to here.
    Combobox* MaxSelected(int n);
    Combobox* OnQueryFocus(Listener fn);
    Combobox* OnToggle(Listener fn);
    Combobox* OnClear(Listener fn);
    El* IntoEl();
};

} // namespace component

template <>
struct EventEmitter<component::ComboboxState, component::ComboboxEvent> {};

} // namespace gpui
#endif // GPUI_UI_COMBOBOX_H_
