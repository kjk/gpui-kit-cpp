#ifndef GPUI_BASE_LIST_H_
#define GPUI_BASE_LIST_H_
/* Unstyled list — crates/ui/src/list/list.rs */

#include "gpui/gpui.h"
#include "base/index_path.h"
#include "base/virtual_list.h"

namespace gpui {

// ListEvent, what the list tells whoever is listening. Rust emits these
// through `cx.emit` and a caller subscribes; here the state carries one
// listener, the way InputState and SliderState do.
enum class ListEventKind : uint8_t {
    // The selection moved, by a key or by a click.
    Select,
    // A row was taken: Enter on the selected row, or a click on any row.
    Confirm,
    // Escape.
    Cancel
};

struct ListEvent {
    ListEventKind kind = ListEventKind::Select;
    // The row it is about; -1 for a Cancel that cleared the selection.
    int index = -1;
    // Confirm { secondary }: the modifier that means "the other way" —
    // Command on macOS, Control elsewhere.
    bool secondary = false;
};

// The synchronous C++ projection of the non-rendering calls on UI's
// ListDelegate trait. A ListDelegate installs these retained Listeners on its
// ListState, so calls made later by a key or pointer event still resolve the
// delegate's owning entity generationally rather than retaining a frame-local
// pointer.
struct ListSelectionChange {
    bool hasIndex = false;
    IndexPath index = {};
};

struct ListSearchRequest {
    // Borrowed for the duration of the listener call. The state separately
    // owns `lastQuery`, just as Rust's ListState owns its last_query String.
    Str query = {};
};

struct ListConfirmRequest {
    bool secondary = false;
};

// What a keystroke asks a list to do. Rust binds up, down, enter, escape and
// secondary-enter in the "List" key context; this is that table, read as an
// answer rather than routed as an action.
enum class ListAction : uint8_t {
    None,
    SelectPrev,
    SelectNext,
    Confirm,
    Cancel
};

// Rust's Confirm carries a field — `Confirm { secondary }`, bound twice, once
// to enter and once to secondary-enter — so the answer here is the pair. The
// modifier is the platform's shortcut key, which is `ctrl` on a KeyEvent.
struct ListKeyAction {
    ListAction action = ListAction::None;
    bool secondary = false;
};

// list.rs::init: escape, enter, secondary-enter, up and down in the "List"
// key context.
void ListInitKeys();
Str ListContext();

// The action, read as what the list does about it. Rust's Confirm carries a
// field — `Confirm { secondary }`, bound twice — so the two bindings are two
// names here and the flag comes back beside the answer.
// `arg` is the action's payload — `ActionEvent::arg`, which for Confirm is
// `action::kConfirmSecondary` or 0.
ListKeyAction ListActionOf(uint32_t id, int64_t arg = 0);

// RowEntry, from crates/ui/src/list/cache.rs: what one row of the flattened
// list is. A section contributes a header, its items and a footer, and a
// section with no items contributes nothing at all.
enum class ListRowKind : uint8_t {
    Entry,
    SectionHeader,
    SectionFooter
};

struct ListRow {
    ListRowKind kind = ListRowKind::Entry;
    int section = 0;
    // Which row of its section this is, and which item of the whole list —
    // Rust's IndexPath and the position the selection is kept as.
    int row = 0;
    int entry = -1;

    // The same place under the name Rust addresses it by. A header or a
    // footer has a section but no row, which is what its `row` of 0 means.
    IndexPath Path() const { return IndexPath{section, row, 0}; }
};

// What a list is between frames. Rust splits this across ListState and the
// delegate; the rows themselves stay with the caller either way, so this is
// the part that answers keys and clicks.
struct ListState {
    // How many rows there are, which the caller sets every frame.
    int count = 0;
    // selected_index: none is -1.
    int selected = -1;
    // right_clicked_index: the row under a secondary press, which paints as
    // secondary_selected until the next click.
    int rightClicked = -1;
    bool selectable = true;
    // reset_on_cancel: whether Escape clears the selection or only reports.
    bool resetOnCancel = true;
    // The sections, as the number of items in each. RowsCache keeps the same
    // list and flattens it; a section with no items is skipped, header and
    // footer with it.
    Vec<int> sectionCounts;
    bool sectionHeaders = false;
    bool sectionFooters = false;
    // The list is virtualized through `v_virtual_list`, which takes a size
    // per row rather than one for all of them: a section header is not an
    // item's height, and neither is a footer. `rowH` is the item height and
    // is *measured* — `prepare_items_if_needed` lays out the row at
    // `itemToMeasure` on its own and takes what it came out as — with 32 the
    // value it starts at, for the frame before anything has been built.
    // `viewportH` is what the last frame was laid out at, which is what
    // scroll_to_item measures against.
    float rowH = 32;
    float headerH = 0;
    float footerH = 0;
    // item_to_measure_index: which row stands for the rest. Rust defaults to
    // the first and lets a delegate name another, for a list whose first row
    // is not typical of it.
    IndexPath itemToMeasure = {};
    // entries_sizes: the height of every flattened row, which is what the
    // virtual list scrolls and places against. Rebuilt whenever the sections
    // or the three measured heights change.
    Vec<float> rowHeights;
    float scrollY = 0;
    float viewportH = 0;
    // delegate.loading() / has_more() / load_more_threshold(): the rows are
    // replaced by a loading view, and coming within `loadMoreThreshold` rows
    // of the end asks for more.
    bool loading = false;
    bool hasMore = false;
    int loadMoreThreshold = 20;
    Listener onEvent = {};
    // ListDelegate::perform_search / set_selected_index /
    // set_right_clicked_index / confirm / cancel / load_more. The required
    // Rust set_selected_index method may be empty in C++ because ListState
    // also retains the selection itself; the other five have empty defaults
    // upstream too.
    Listener onPerformSearch = {};
    Listener onSetSelectedIndex = {};
    Listener onSetRightClickedIndex = {};
    Listener onConfirm = {};
    Listener onCancel = {};
    Listener onLoadMore = {};
    // query_input and last_query. UI supplies the InputState when searchable;
    // the owned copy makes repeated Change events with the same trimmed value
    // no-ops, matching on_query_input_event.
    InputState* queryInput = nullptr;
    Str lastQuery = {};
    // cx.emit needs to know who is emitting, and Rust's Context<Self> does.
    // The element stamps this as it builds, so a state can send an event to
    // its subscribers without the caller carrying its handle around.
    Entity<ListState> self = {};
    // The list's own focus handle — `self.focus_handle(cx)`, which list.rs
    // tracks on the element it declares the key context on. Asked for once and
    // kept, because the state is what outlives the frame; the port derived it
    // from the caller's id instead.
    FocusHandle focus = {};

    ~ListState() {
        StrFree(lastQuery);
        VecReset(sectionCounts);
        VecReset(rowHeights);
    }

    // Rust's ListState is an Entity, which is what lets the item closures
    // capture it; here the row elements name these handlers instead, so a
    // page holds an Entity<ListState> and the list binds to it.
    static void OnRowClick(ListState* self, Ctx* cx, const ClickEvent* ev,
                           int64_t ix);
    static void OnRowMouseDown(ListState* self, Ctx* cx,
                               const MouseDownEvent* ev, int64_t ix);
    static void OnScroll(ListState* self, Ctx* cx, const ScrollEvent* ev);
    static void OnQueryInput(ListState* self, Ctx* cx, const InputEvent* ev);
    static void OnMouseDownOut(ListState* self, Ctx* cx,
                               const MouseDownEvent* ev);
};

// The sections and their item counts, which is what RowsCache is built from.
// `count` follows: the items of every section added up.
void ListSetSections(ListState* s, const int* counts, int n, bool headers,
                     bool footers);
// One section holding every item, which is what a list with no sections of
// its own is.
void ListSetCount(ListState* s, int count);
// The flattened rows: every section's header, its items and its footer.
int ListRowCount(const ListState* s);
// What the row at this position is. Rust keeps the answers in a vector; there
// are at most sixteen sections to walk here, so they are worked out instead.
ListRow ListRowAt(const ListState* s, int rowIx);
// Where an item sits among the flattened rows, or -1.
int ListRowOfEntry(const ListState* s, int entry);
// RowsCache::prepare_if_needed: the per-row heights, rebuilt from the three
// measured ones. Rust rebuilds when the sections or the measured sizes have
// changed; the check here is the same, since the walk is the cost and the
// list is rebuilt every frame. A header or a footer that measured 0 — because
// the list has none — still takes a row of its own only when the state says
// the section has one, which is what SectionRows already counts.
void ListPrepareRowHeights(ListState* s, float itemH, float headerH,
                           float footerH);
// The heights the virtual list scrolls against, or null before anything has
// been measured — in which case every row is `rowH`.
const float* ListRowHeights(const ListState* s);
// The two directions between the flat entry index this tree keys on and the
// IndexPath Rust keys on. An entry outside the list is section 0, row -1;
// a path outside it is -1.
IndexPath ListIndexPathOf(const ListState* s, int entry);
int ListEntryOf(const ListState* s, IndexPath path);
// scroll_to_item, against the height the list was last laid out at.
void ListScrollToItem(ListState* s, int entry, ScrollStrategy strategy);
// load_more: true when the last row built is within the threshold of the end
// and there is more to come. Rust asks this while it renders the visible
// range, and so does the list here.
bool ListShouldLoadMore(const ListState* s, int lastVisibleRow);

// The state-facing methods whose Rust counterparts live on ListState<D>.
// Selection setters synchronize the delegate but do not emit ListEvent;
// movement/click helpers below emit at the same points as upstream.
void ListSetSelectedIndex(ListState* s, Ctx* cx, int entry, bool scroll = false,
                          ScrollStrategy strategy = ScrollStrategy::Top);
void ListSetRightClickedIndex(ListState* s, Ctx* cx, int entry);
bool ListSelectedIndex(const ListState* s, IndexPath* out);
bool ListRightClickedIndex(const ListState* s, IndexPath* out);
void ListSetItemToMeasureIndex(ListState* s, Ctx* cx, IndexPath path);
// set_query writes the query input silently and explicitly starts the search,
// as Rust does because InputState::set_value does not emit Change.
void ListSetQuery(ListState* s, Ctx* cx, Str query);
void ListRequestLoadMore(ListState* s, Ctx* cx);

// rows_cache.next / .prev. Both wrap: past the last row is the first, and
// before the first is the last. With nothing selected, next takes the first
// row and prev the last.
int ListNextIndex(const ListState* s);
int ListPrevIndex(const ListState* s);

// The action, applied to the state and reported through `onEvent`.
void ListPerform(ListState* s, Ctx* cx, ListAction act, bool secondary);

// The five bindings, arriving as actions on the element that declares the
// context. Rust hangs one on_action per action off the list; there is one id
// to switch on here.
void ListOnAction(ListState* self, Ctx* cx, const ActionEvent* ev);

// Declare the "List" key context on `root` and hang the handlers off it.
void ListBindKeys(Ctx* cx, El* root, Entity<ListState> state);

// A click on a row. Rust's on_click clears the right-clicked row, selects
// this one and confirms it in one go — a click is a Select and a Confirm.
void ListClickRow(ListState* s, Ctx* cx, int ix, bool secondary);

// A secondary press on a row, which only marks it.
void ListRightClickRow(ListState* s, Ctx* cx, int ix);

template <>
struct EventEmitter<ListState, ListEvent> {};

} // namespace gpui
#endif // GPUI_BASE_LIST_H_
