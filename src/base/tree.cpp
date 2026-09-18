#include "base/tree.h"
#include "base/actions.h"
#include "gpui/keymap.h"

namespace gpui {

Str TreeContext() {
    return StrL("Tree");
}

void TreeInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    const char* ctx = "Tree";
    KeyBinding bindings[] = {
        {"up", action::SelectUp(), ctx},
        {"down", action::SelectDown(), ctx},
        {"left", action::SelectLeft(), ctx},
        {"right", action::SelectRight(), ctx},
        {"enter", action::Confirm(), ctx},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

void tree::init() {
    TreeInitKeys();
}

TreeAction TreeActionOf(uint32_t id) {
    if (id == action::SelectUp()) {
        return TreeAction::SelectPrev;
    }
    if (id == action::SelectDown()) {
        return TreeAction::SelectNext;
    }
    if (id == action::SelectLeft()) {
        return TreeAction::Collapse;
    }
    if (id == action::SelectRight()) {
        return TreeAction::Expand;
    }
    if (id == action::Confirm()) {
        return TreeAction::Confirm;
    }
    return TreeAction::None;
}

int TreeSelectPrev(int selected, int count) {
    if (count <= 0) {
        return -1;
    }
    int ix = selected < 0 ? 0 : selected;
    return ix == 0 ? count - 1 : ix - 1;
}

int TreeSelectNext(int selected, int count) {
    if (count <= 0) {
        return -1;
    }
    int ix = selected < 0 ? 0 : selected;
    return ix + 1 < count ? ix + 1 : 0;
}

bool TreeCollapses(bool isFolder, bool isExpanded) {
    return isFolder && isExpanded;
}

bool TreeExpands(bool isFolder, bool isExpanded) {
    return isFolder && !isExpanded;
}

int TreeAddItem(TreeState* s, Str id, Str label, int parent) {
    TreeItem it;
    // Copies, owned by the state and freed with it — the convention
    // InputSetPlaceholder set. The strings used to be stored as handed in,
    // which left the caller's heap copies to leak: nothing freed them, and
    // nothing could have without also freeing a caller's literal. One
    // allocation: id and label are always a pair.
    StrDup2(id, label, it.id, it.label);
    it.parent = parent;
    int ix = s->items.len;
    if (parent >= 0 && parent < ix) {
        it.depth = s->items[parent].depth + 1;
        // A folder is an item something else calls its parent.
        s->items[parent].folder = true;
    }
    VecAppend(s->items, it);
    return ix;
}

// add_entry: the item, then the children of an expanded one.
static void TreeAddEntry(TreeState* s, int item) {
    VecAppend(s->entries, item);
    if (!s->items[item].expanded) {
        return;
    }
    for (int i = item + 1; i < s->items.len; i++) {
        if (s->items[i].parent == item) {
            TreeAddEntry(s, i);
        }
    }
}

void TreeRebuild(TreeState* s) {
    s->entries.len = 0;
    for (int i = 0; i < s->items.len; i++) {
        if (s->items[i].parent < 0) {
            TreeAddEntry(s, i);
        }
    }
    if (s->selected >= s->entries.len) {
        s->selected = s->entries.len > 0 ? s->entries.len - 1 : -1;
    }
    if (s->rightClicked >= s->entries.len) {
        s->rightClicked = -1;
    }
}

const TreeItem* TreeEntryItem(const TreeState* s, int entryIx) {
    if (entryIx < 0 || entryIx >= s->entries.len) {
        return nullptr;
    }
    return &s->items[s->entries[entryIx]];
}

TreeEntry TreeEntryAt(const TreeState* s, int entryIx) {
    TreeEntry entry;
    if (!s || entryIx < 0 || entryIx >= s->entries.len) {
        return entry;
    }
    entry.itemIx = s->entries[entryIx];
    entry.item = &s->items[entry.itemIx];
    entry.depth = entry.item->depth;
    return entry;
}

void TreeSetItems(TreeState* s, Ctx* cx, const TreeItem* items, int count) {
    if (!s) {
        return;
    }
    // The state owns every item's strings (see TreeAddItem): the old ones go
    // with the old items, and the new ones come in as copies.
    for (int i = 0; i < s->items.len; i++) {
        StrFree(s->items[i].id);
    }
    s->items.len = 0;
    if (items && count > 0) {
        for (int i = 0; i < count; i++) {
            TreeItem it = items[i];
            StrDup2(items[i].id, items[i].label, it.id, it.label);
            VecAppend(s->items, it);
        }
    }
    s->selected = -1;
    s->rightClicked = -1;
    TreeRebuild(s);
    if (cx) {
        Notify(cx);
    }
}

int TreeIndexOf(const TreeState* s, Str id) {
    for (int i = 0; i < s->entries.len; i++) {
        if (base::StrEq(s->items[s->entries[i]].id, id)) {
            return i;
        }
    }
    return -1;
}

// cx.emit(TreeEvent::..) — see the note on ListEmit.
static void TreeEmit(TreeState* s, Ctx* cx, TreeEventKind kind, Str id,
                     int ix) {
    TreeEvent ev = {kind, id, ix};
    if (s->onEvent.IsValid()) {
        ListenerCall(cx->app, cx->win, s->onEvent, &ev);
    }
    EntityEmit(cx->app, cx->win, s->self, &ev);
}

bool TreeToggleExpandAt(TreeState* s, int entryIx, bool* expandedOut) {
    if (entryIx < 0 || entryIx >= s->entries.len) {
        return false;
    }
    TreeItem& it = s->items[s->entries[entryIx]];
    if (!it.folder) {
        return false;
    }
    it.expanded = !it.expanded;
    if (expandedOut) {
        *expandedOut = it.expanded;
    }
    // A right-click marks a row, and the rows move when a folder opens.
    s->rightClicked = -1;
    TreeRebuild(s);
    return true;
}

void TreeToggleExpand(TreeState* s, Ctx* cx, int entryIx) {
    if (entryIx < 0 || entryIx >= s->entries.len) {
        return;
    }
    Str id = s->items[s->entries[entryIx]].id;
    bool expanded = false;
    if (!TreeToggleExpandAt(s, entryIx, &expanded)) {
        return;
    }
    TreeEmit(s, cx,
             expanded ? TreeEventKind::Expanded : TreeEventKind::Collapsed, id,
             entryIx);
    Notify(cx);
}

int TreeRevealItem(TreeState* s, Str id) {
    int item = -1;
    for (int i = 0; i < s->items.len; i++) {
        if (base::StrEq(s->items[i].id, id)) {
            item = i;
            break;
        }
    }
    if (item < 0) {
        return -1;
    }
    for (int p = s->items[item].parent; p >= 0; p = s->items[p].parent) {
        s->items[p].expanded = true;
    }
    TreeRebuild(s);
    return TreeIndexOf(s, id);
}

static void TreeExpandAncestors(TreeState* s, Ctx* cx, int item) {
    // ancestors() returns nearest parent to root and expand_ancestors walks
    // it in reverse, so observers see the root before its descendant.
    Vec<int> ancestors;
    for (int p = s->items[item].parent; p >= 0; p = s->items[p].parent) {
        VecAppend(ancestors, p);
    }
    for (int i = ancestors.len - 1; i >= 0; i--) {
        TreeItem& ancestor = s->items[ancestors[i]];
        if (!ancestor.expanded) {
            ancestor.expanded = true;
            if (cx) {
                TreeEmit(s, cx, TreeEventKind::Expanded, ancestor.id, -1);
            }
        }
    }
    VecReset(ancestors);
}

int TreeRevealItem(TreeState* s, Ctx* cx, Str id, ScrollStrategy strategy) {
    if (!s) {
        return -1;
    }
    int item = -1;
    for (int i = 0; i < s->items.len; i++) {
        if (base::StrEq(s->items[i].id, id)) {
            item = i;
            break;
        }
    }
    if (item < 0) {
        return -1;
    }

    TreeExpandAncestors(s, cx, item);
    TreeRebuild(s);
    int ix = TreeIndexOf(s, id);
    TreeScrollToItem(s, ix, strategy);
    if (cx) {
        Notify(cx);
    }
    return ix;
}

void TreeScrollToItem(TreeState* s, int entryIx, ScrollStrategy strategy) {
    if (s->viewportH <= 0) {
        return;
    }
    s->scrollY = VirtualListScrollToRow(s->entries.len, s->rowH, entryIx,
                                        s->scrollY, s->viewportH, strategy);
}

void TreeSetSelected(TreeState* s, Ctx* cx, int entryIx) {
    s->selected = entryIx;
    Notify(cx);
}

void TreeSetSelectedItem(TreeState* s, Ctx* cx, Str id) {
    if (!s) {
        return;
    }
    if (!id.s) {
        s->selected = -1;
    } else {
        int ix = TreeIndexOf(s, id);
        if (ix < 0) {
            int item = -1;
            for (int i = 0; i < s->items.len; i++) {
                if (base::StrEq(s->items[i].id, id)) {
                    item = i;
                    break;
                }
            }
            if (item >= 0) {
                TreeExpandAncestors(s, cx, item);
                TreeRebuild(s);
                ix = TreeIndexOf(s, id);
            }
        }
        s->selected = ix;
    }
    if (cx) {
        Notify(cx);
    }
}

void TreeClickEntry(TreeState* s, Ctx* cx, int entryIx) {
    const TreeItem* it = TreeEntryItem(s, entryIx);
    if (!it || it->disabled) {
        return;
    }
    s->selected = entryIx;
    TreeToggleExpand(s, cx, entryIx);
    Notify(cx);
}

void TreePerform(TreeState* s, Ctx* cx, TreeAction act) {
    const TreeItem* sel = TreeEntryItem(s, s->selected);
    switch (act) {
        case TreeAction::SelectPrev:
            s->selected = TreeSelectPrev(s->selected, s->entries.len);
            // Rust scrolls to the top for Up and the bottom for Down; both
            // strategies come down to the same "move as little as you can" in
            // the list, which is what keeps the row that arrived on screen.
            TreeScrollToItem(s, s->selected, ScrollStrategy::Top);
            break;
        case TreeAction::SelectNext:
            s->selected = TreeSelectNext(s->selected, s->entries.len);
            TreeScrollToItem(s, s->selected, ScrollStrategy::Bottom);
            break;
        case TreeAction::Collapse:
            if (sel && TreeCollapses(sel->folder, sel->expanded)) {
                TreeToggleExpand(s, cx, s->selected);
            }
            return;
        case TreeAction::Expand:
            if (sel && TreeExpands(sel->folder, sel->expanded)) {
                TreeToggleExpand(s, cx, s->selected);
            }
            return;
        case TreeAction::Confirm:
            if (sel && sel->folder) {
                TreeToggleExpand(s, cx, s->selected);
            }
            return;
        default:
            return;
    }
    Notify(cx);
}

void TreeState::OnRowClick(TreeState* self, Ctx* cx, const ClickEvent*,
                           intptr_t entryIx) {
    TreeClickEntry(self, cx, (int)entryIx);
}

void TreeState::OnRowMouseDown(TreeState* self, Ctx* cx,
                               const MouseDownEvent* ev, intptr_t entryIx) {
    if (ev->button != MouseButton::Right) {
        return;
    }
    const TreeItem* it = TreeEntryItem(self, (int)entryIx);
    if (!it || it->disabled) {
        return;
    }
    self->rightClicked = (int)entryIx;
    Notify(cx);
}

void TreeState::OnScroll(TreeState* self, Ctx* cx, const ScrollEvent* ev) {
    self->scrollY = ev->offsetY;
    Notify(cx);
}

El* Tree::New(Ctx* cx) {
    Arena* a = cx->a;
    return Div(a);
}

El* TreeList::New(Ctx* cx, Str id, Entity<TreeState> state, float h,
                  TreeRowFn row, void* user) {
    Arena* a = cx->a;
    TreeState* s = state.Get(cx);
    if (!s || !row) {
        return Div(a)->H(h);
    }
    s->self = state;
    // The height the list was laid out at is what scroll_to_item measures
    // against, and the caller is the one that knows it.
    s->viewportH = h;

    // uniform_list: only the rows the viewport can show are built, and the
    // two spacers stand in for the rest so the scrollbar spans the whole
    // tree.
    VirtualRange range =
        VirtualListVisibleRows(s->entries.len, s->rowH, s->scrollY, h);
    El* list = Div(a)->FlexCol()->W(kFill);
    if (range.first > 0) {
        list->Child(Div(a)->W(kFill)->H((float)range.first * s->rowH));
    }
    Listener click = ListenTo(state, &TreeState::OnRowClick, 0);
    Listener down = ListenTo(state, &TreeState::OnRowMouseDown, 0);
    for (int i = range.first; i < range.end; i++) {
        TreeEntry entry = TreeEntryAt(s, i);
        const TreeItem* it = entry.item;
        if (!it) {
            break;
        }
        El* wrap = TreeItemEl::New(cx, StrDup(a, fmt("row-%d", i)),
                                   ListenerArg(click, i))
                       ->FlexCol()
                       ->W(kFill);
        if (!it->disabled) {
            wrap->OnMouseDown(ListenerArg(down, i));
        }
        TreeEntryState entryState = {i == s->selected, i == s->rightClicked};
        if (El* built = row(user, cx, i, entry, entryState)) {
            wrap->Child(built);
        }
        list->Child(wrap);
    }
    if (range.end < s->entries.len) {
        list->Child(
            Div(a)->W(kFill)->H((float)(s->entries.len - range.end) * s->rowH));
    }

    El* box = Tree::New(cx)
                  // The tree's own name, so its rows are `("row", ix)` rather
                  // than spelling it out again in each of them.
                  ->Id(id)
                  ->FlexCol()
                  ->W(kFill)
                  ->H(h)
                  ->ClipY()
                  ->ScrollY(s->scrollY)
                  ->ScrollFromPath()
                  ->OnScroll(ListenTo(state, &TreeState::OnScroll));
    box->Child(list);
    // The tree's own context and the four arrows in it. The rows are not
    // focusable, so the box is: Rust tracks focus on the same element it
    // declares the context on.
    box->FocusId(HashClickId(id))->FocusRing(false)->FocusOnPress();
    TreeBindKeys(cx, box, state);
    return box;
}

El* TreeItemEl::New(Ctx* cx, Str id, Listener onClick) {
    Arena* a = cx->a;
    El* e = Div(a);
    if (id.s) {
        // The row takes focus on a press, which is what puts the tree's key
        // context on the focused ancestry — a tree with a row selected is one
        // the arrows can walk. Not a tab stop: Tab reaches the tree itself,
        // not each of its hundreds of rows.
        e->PathId(id)
            ->TabStop(false)
            // The selected row is drawn by the tree itself; a ring around the
            // last one pressed is not something Rust's tree shows.
            ->FocusRing(false)
            // tree.rs `focus()` on a row press.
            ->FocusOnPress();
    }
    if (onClick.IsValid()) {
        e->OnClick(onClick);
    }
    return e;
}
void TreeOnAction(TreeState* self, Ctx* cx, const ActionEvent* ev) {
    if (!self) {
        return;
    }
    TreeAction act = TreeActionOf(ev->action);
    if (act == TreeAction::None) {
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    TreePerform(self, cx, act);
}

void TreeBindKeys(Ctx* cx, El* root, Entity<TreeState> state) {
    if (!cx || !root || !state.IsValid()) {
        return;
    }
    TreeInitKeys();
    Listener onAction = ListenTo(state, &TreeOnAction);
    root->KeyContext(TreeContext())
        ->OnAction(action::SelectUp(), onAction)
        ->OnAction(action::SelectDown(), onAction)
        ->OnAction(action::SelectLeft(), onAction)
        ->OnAction(action::SelectRight(), onAction)
        ->OnAction(action::Confirm(), onAction);
}

} // namespace gpui
