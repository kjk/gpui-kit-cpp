#ifndef GPUI_BASE_TREE_H_
#define GPUI_BASE_TREE_H_
/* Unstyled tree — crates/base/src/tree.rs */

#include "gpui/gpui.h"
#include "base/virtual_list.h"

namespace gpui {

// What a keystroke asks a tree to do. Rust binds up, down, left and right in
// the tree's key context; left and right are the folder pair, and each only
// acts in one direction. Enter is Confirm, which toggles a folder.
enum class TreeAction : uint8_t {
    None,
    SelectPrev,
    SelectNext,
    Collapse,
    Expand,
    Confirm
};

// tree.rs::init: up, down, left and right in the "Tree" key context, plus
// the enter its on_action_confirm answers to.
void TreeInitKeys();
Str TreeContext();
TreeAction TreeActionOf(uint32_t id);

namespace tree {
// Source-named module initializer. The global-prefixed spelling remains for
// callers that use this tree's conventional C++ surface.
void init();
} // namespace tree

// Where Up and Down move the selection. Both wrap, and both treat an unset
// selection as 0 before stepping — which is why Up from nothing lands on the
// last entry while Down from nothing lands on the second. Rust's
// `checked_sub(1).unwrap_or(len - 1)` against its `if ix + 1 < len`.
// `selected` is -1 for unset.
int TreeSelectPrev(int selected, int count);
int TreeSelectNext(int selected, int count);

// Left collapses an expanded folder and does nothing else; Right expands a
// collapsed one. Neither touches a leaf, and neither is a toggle.
bool TreeCollapses(bool isFolder, bool isExpanded);
bool TreeExpands(bool isFolder, bool isExpanded);

// TreeItem. Rust's holds its children and shares its expanded flag through an
// Rc; the items here live in one array on the state and name their parent,
// which is the same tree without the reference counting.
struct TreeItem {
    // One StrDup2 block: id.s is the allocation, label.s is interior.
    // StrFree(id) frees both; do not StrFree(label).
    Str id = {};
    Str label = {};
    int parent = -1;
    int depth = 0;
    // Whether anything named this item as its parent. Rust asks the children
    // vector, which is the same question.
    bool folder = false;
    bool expanded = false;
    bool disabled = false;
};

// A read-only view of one visible flattened entry. Rust stores a cloned
// TreeItem plus its depth; the state here owns items in one Vec, so the view
// carries the corresponding stable index and pointer for the duration of the
// call that produced it.
struct TreeEntry {
    const TreeItem* item = nullptr;
    int itemIx = -1;
    int depth = 0;

    bool IsRoot() const { return depth == 0; }
    bool IsFolder() const { return item && item->folder; }
    bool IsExpanded() const { return item && item->expanded; }
    bool IsDisabled() const { return item && item->disabled; }
};

// The interaction state supplied alongside a visible entry, exactly as the
// source render-item callback receives it.
struct TreeEntryState {
    bool selected = false;
    bool rightClicked = false;

    bool IsSelected() const { return selected; }
    bool IsRightClicked() const { return rightClicked; }
};

enum class TreeEventKind : uint8_t {
    Expanded,
    Collapsed
};

struct TreeEvent {
    TreeEventKind kind = TreeEventKind::Expanded;
    // The item the event is about: its id, as Rust's variants carry, and
    // where it is in the flattened list.
    Str id = {};
    int ix = 0;
};

// TreeState. The items, the flattened list of the ones on screen, and what a
// click or a key does to them.
struct TreeState {
    // As many items as the caller adds. Rust's tree is a graph of Rc'd nodes;
    // these live in one array and name their parent, which is the same tree
    // without the reference counting.
    Vec<TreeItem> items;
    // `entries`: item indices, in the order they are shown. A collapsed
    // folder's descendants are not in it at all, which is what makes the row
    // count the tree's own.
    Vec<int> entries;
    int selected = -1;
    int rightClicked = -1;
    // uniform_list: every row is the same height, so the offset of a row is
    // an index times this.
    // A row is a ListItem: py_1 over text_base, which is 34.
    float rowH = 34;
    float scrollY = 0;
    // The last height the list was laid out at, which is what scroll_to_item
    // measures against.
    float viewportH = 0;
    Listener onEvent;
    // cx.emit needs to know who is emitting, and Rust's Context<Self> does.
    // The element stamps this as it builds, so a state can send an event to
    // its subscribers without the caller carrying its handle around.
    Entity<TreeState> self = {};

    static void OnRowClick(TreeState* self, Ctx* cx, const ClickEvent* ev,
                           intptr_t entryIx);
    static void OnRowMouseDown(TreeState* self, Ctx* cx,
                               const MouseDownEvent* ev, intptr_t entryIx);
    static void OnScroll(TreeState* self, Ctx* cx, const ScrollEvent* ev);

    ~TreeState() {
        for (int i = 0; i < items.len; i++) {
            StrFree(items[i].id);
        }
        VecReset(items);
        VecReset(entries);
    }
};

// Build the tree. `parent` is an item index, or -1 for a root; the children of
// an item must be added after it. Answers the new item's index. `id` and
// `label` are copied — the state owns its strings and frees them with itself,
// the way InputSetPlaceholder does — so a literal, a stack buffer or a frame
// arena string are all fine to pass.
int TreeAddItem(TreeState* s, Str id, Str label, int parent);
// replace_items + add_entry: walk the roots depth-first and take in the
// children of every expanded folder. Called for you by everything that
// changes an expansion; call it yourself after building the tree.
void TreeRebuild(TreeState* s);
// Where the item with this id is in the flattened list, or -1 — Rust's
// `index_of`, which also only sees what is on screen.
int TreeIndexOf(const TreeState* s, Str id);
// The item behind a row, or null.
const TreeItem* TreeEntryItem(const TreeState* s, int entryIx);
TreeEntry TreeEntryAt(const TreeState* s, int entryIx);

// set_items: replace the complete item array in one operation, rebuild the
// visible entries, clear both interaction indices and notify. `id` and
// `label` are copied the way TreeAddItem copies them; the pointers in
// `items` need not outlive the call.
void TreeSetItems(TreeState* s, Ctx* cx, const TreeItem* items, int count);

// toggle_expand, without a window to notify. Answers false for a leaf or a
// row that is not there; `expandedOut` says which way it went.
bool TreeToggleExpandAt(TreeState* s, int entryIx, bool* expandedOut);
void TreeToggleExpand(TreeState* s, Ctx* cx, int entryIx);
// expand_ancestors: open everything above the item so it has a row, and
// answer where that row is.
int TreeRevealItem(TreeState* s, Str id);
// reveal_item: the source-semantic form also emits Expanded for each ancestor
// it opens (root first), rebuilds, scrolls and notifies.
int TreeRevealItem(TreeState* s, Ctx* cx, Str id, ScrollStrategy strategy);
// scroll_to_item, against the last height the list was laid out at.
void TreeScrollToItem(TreeState* s, int entryIx, ScrollStrategy strategy);
void TreeSetSelected(TreeState* s, Ctx* cx, int entryIx);
void TreeSetSelectedItem(TreeState* s, Ctx* cx, Str id);
// on_entry_click: select the row, then toggle it — so a press on a folder
// opens it and a press on a leaf only selects.
void TreeClickEntry(TreeState* s, Ctx* cx, int entryIx);
void TreePerform(TreeState* s, Ctx* cx, TreeAction act);

void TreeOnAction(TreeState* self, Ctx* cx, const ActionEvent* ev);
void TreeBindKeys(Ctx* cx, El* root, Entity<TreeState> state);

struct Tree {
    static El* New(Ctx* cx);
};

// tree.rs's `Tree`: the unstyled, virtualized element. Only the rows the
// viewport can show are built, and two spacers stand in for the rest, so the
// scrollbar spans the whole tree; the offset, the selection and the keyboard
// belong to the state. What a row *looks* like is the caller's, which is
// `Tree::item(..)` in Rust and a function with its user pointer here, since
// an element in this tree holds no closures.
//
// The wrapper around each row carries the press handlers and nothing else —
// the caller's element is what has the height, the padding and the
// background, the way Rust's item closure builds its own div.
using TreeRowFn = El* (*)(void* user, Ctx* cx, int entryIx,
                          const TreeEntry& entry, TreeEntryState state);

struct TreeList {
    // `h` is the height the list is laid out at, which is also what
    // scroll_to_item measures against.
    static El* New(Ctx* cx, Str id, Entity<TreeState> state, float h,
                   TreeRowFn row, void* user);
};
// on_entry_click selects the entry and toggles it, so a press on a folder's
// row opens it and a press on a leaf just selects.
struct TreeItemEl {
    static El* New(Ctx* cx, Str id = {}, Listener onClick = {});
};

template <>
struct EventEmitter<TreeState, TreeEvent> {};
} // namespace gpui
#endif // GPUI_BASE_TREE_H_
