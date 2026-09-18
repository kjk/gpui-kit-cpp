#include "base/list.h"
#include "base/actions.h"
#include "gpui/keymap.h"

namespace gpui {

Str ListContext() {
    return StrL("List");
}

void ListInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    const char* ctx = "List";
    KeyBinding bindings[] = {
        {"escape", action::Cancel(), ctx},
        {"enter", action::Confirm(), ctx},
        // Rust binds the same action twice, the second carrying
        // `secondary: true`; the payload is the binding's here.
        {"secondary-enter", action::Confirm(), ctx, action::kConfirmSecondary},
        {"up", action::SelectUp(), ctx},
        {"down", action::SelectDown(), ctx},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

ListKeyAction ListActionOf(uint32_t id, intptr_t arg) {
    ListKeyAction out;
    if (id == action::SelectUp()) {
        out.action = ListAction::SelectPrev;
    } else if (id == action::SelectDown()) {
        out.action = ListAction::SelectNext;
    } else if (id == action::Confirm()) {
        out.action = ListAction::Confirm;
        out.secondary = arg == action::kConfirmSecondary;
    } else if (id == action::Cancel()) {
        out.action = ListAction::Cancel;
    }
    return out;
}

int ListNextIndex(const ListState* s) {
    if (s->count <= 0) {
        return -1;
    }
    if (s->selected < 0) {
        return 0;
    }
    int next = s->selected + 1;
    return next < s->count ? next : 0;
}

int ListPrevIndex(const ListState* s) {
    if (s->count <= 0) {
        return -1;
    }
    if (s->selected < 0) {
        return s->count - 1;
    }
    int prev = s->selected - 1;
    return prev >= 0 ? prev : s->count - 1;
}

void ListSetSections(ListState* s, const int* counts, int n, bool headers,
                     bool footers) {
    VecClear(s->sectionCounts);
    s->count = 0;
    for (int i = 0; i < n; i++) {
        int c = counts[i] < 0 ? 0 : counts[i];
        VecAppend(s->sectionCounts, c);
        s->count += c;
    }
    s->sectionHeaders = headers;
    s->sectionFooters = footers;
}

void ListSetCount(ListState* s, int count) {
    int one = count < 0 ? 0 : count;
    ListSetSections(s, &one, 1, false, false);
}

// How many flattened rows a section contributes. An empty one contributes
// nothing — Rust skips its header and footer with it.
static int SectionRows(const ListState* s, int section) {
    int items = s->sectionCounts[section];
    if (items <= 0) {
        return 0;
    }
    return items + (s->sectionHeaders ? 1 : 0) + (s->sectionFooters ? 1 : 0);
}

int ListRowCount(const ListState* s) {
    int total = 0;
    for (int i = 0; i < s->sectionCounts.len; i++) {
        total += SectionRows(s, i);
    }
    return total;
}

ListRow ListRowAt(const ListState* s, int rowIx) {
    ListRow r;
    if (rowIx < 0) {
        return r;
    }
    int at = 0;
    int entry = 0;
    for (int i = 0; i < s->sectionCounts.len; i++) {
        int rows = SectionRows(s, i);
        if (rowIx >= at + rows) {
            at += rows;
            entry += s->sectionCounts[i];
            continue;
        }
        int within = rowIx - at;
        r.section = i;
        if (s->sectionHeaders) {
            if (within == 0) {
                r.kind = ListRowKind::SectionHeader;
                return r;
            }
            within--;
        }
        if (within < s->sectionCounts[i]) {
            r.kind = ListRowKind::Entry;
            r.row = within;
            r.entry = entry + within;
            return r;
        }
        r.kind = ListRowKind::SectionFooter;
        return r;
    }
    return r;
}

void ListPrepareRowHeights(ListState* s, float itemH, float headerH,
                           float footerH) {
    if (!s) {
        return;
    }
    int total = ListRowCount(s);
    // need_update: the sections have not moved and nothing measured
    // differently, so the vector already standing is the answer.
    bool same = s->rowHeights.len == total && s->rowH == itemH &&
                s->headerH == headerH && s->footerH == footerH;
    s->rowH = itemH;
    s->headerH = headerH;
    s->footerH = footerH;
    if (same) {
        return;
    }
    s->rowHeights.len = 0;
    for (int r = 0; r < total; r++) {
        ListRow row = ListRowAt(s, r);
        float h = row.kind == ListRowKind::SectionHeader   ? headerH
                  : row.kind == ListRowKind::SectionFooter ? footerH
                                                           : itemH;
        VecAppend(s->rowHeights, h);
    }
}

const float* ListRowHeights(const ListState* s) {
    if (!s || s->rowHeights.len == 0) {
        return nullptr;
    }
    return s->rowHeights.els;
}

IndexPath ListIndexPathOf(const ListState* s, int entry) {
    IndexPath p;
    p.row = -1;
    if (!s || entry < 0) {
        return p;
    }
    int seen = 0;
    for (int i = 0; i < s->sectionCounts.len; i++) {
        int n = s->sectionCounts[i];
        if (entry < seen + n) {
            p.section = i;
            p.row = entry - seen;
            return p;
        }
        seen += n;
    }
    return p;
}

int ListEntryOf(const ListState* s, IndexPath path) {
    if (!s || path.section < 0 || path.row < 0 ||
        path.section >= s->sectionCounts.len ||
        path.row >= s->sectionCounts[path.section]) {
        return -1;
    }
    int seen = 0;
    for (int i = 0; i < path.section; i++) {
        seen += s->sectionCounts[i];
    }
    return seen + path.row;
}

int ListRowOfEntry(const ListState* s, int entry) {
    if (entry < 0) {
        return -1;
    }
    int at = 0;
    int seen = 0;
    for (int i = 0; i < s->sectionCounts.len; i++) {
        int items = s->sectionCounts[i];
        if (items <= 0) {
            continue;
        }
        if (entry < seen + items) {
            return at + (s->sectionHeaders ? 1 : 0) + (entry - seen);
        }
        at += SectionRows(s, i);
        seen += items;
    }
    return -1;
}

void ListScrollToItem(ListState* s, int entry, ScrollStrategy strategy) {
    if (s->viewportH <= 0) {
        return;
    }
    int row = ListRowOfEntry(s, entry);
    if (row < 0) {
        return;
    }
    // A list whose header and footer are not an item's height scrolls against
    // the per-row sizes, the way `v_virtual_list` places them. Before the
    // first measure there are none, and every row is `rowH`.
    const float* sizes = ListRowHeights(s);
    int total = ListRowCount(s);
    s->scrollY = sizes ? VirtualListScrollToItem(sizes, total, row, s->scrollY,
                                                 s->viewportH, strategy)
                       : VirtualListScrollToRow(total, s->rowH, row, s->scrollY,
                                                s->viewportH, strategy);
}

bool ListShouldLoadMore(const ListState* s, int lastVisibleRow) {
    if (!s->hasMore || s->loading) {
        return false;
    }
    return ListRowCount(s) - lastVisibleRow <= s->loadMoreThreshold;
}

static void CallSelection(ListState* s, Ctx* cx, Listener listener, int entry) {
    if (!listener.IsValid()) {
        return;
    }
    ListSelectionChange ev;
    ev.hasIndex = entry >= 0;
    ev.index = ListIndexPathOf(s, entry);
    ListenerCall(cx->app, cx->win, listener, &ev);
}

void ListSetSelectedIndex(ListState* s, Ctx* cx, int entry, bool scroll,
                          ScrollStrategy strategy) {
    if (!s || !cx) {
        return;
    }
    if (entry < 0 || entry >= s->count) {
        entry = -1;
    }
    s->selected = entry;
    CallSelection(s, cx, s->onSetSelectedIndex, entry);
    if (scroll && entry >= 0) {
        ListScrollToItem(s, entry, strategy);
    }
    Notify(cx);
}

void ListSetRightClickedIndex(ListState* s, Ctx* cx, int entry) {
    if (!s || !cx) {
        return;
    }
    if (entry < 0 || entry >= s->count) {
        entry = -1;
    }
    s->rightClicked = entry;
    CallSelection(s, cx, s->onSetRightClickedIndex, entry);
    Notify(cx);
}

bool ListSelectedIndex(const ListState* s, IndexPath* out) {
    if (!s || s->selected < 0 || s->selected >= s->count) {
        return false;
    }
    if (out) {
        *out = ListIndexPathOf(s, s->selected);
    }
    return true;
}

bool ListRightClickedIndex(const ListState* s, IndexPath* out) {
    if (!s || s->rightClicked < 0 || s->rightClicked >= s->count) {
        return false;
    }
    if (out) {
        *out = ListIndexPathOf(s, s->rightClicked);
    }
    return true;
}

void ListSetItemToMeasureIndex(ListState* s, Ctx* cx, IndexPath path) {
    if (!s) {
        return;
    }
    s->itemToMeasure = path;
    if (cx) {
        Notify(cx);
    }
}

static bool ListSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static Str TrimQuery(Str query) {
    while (len(query) > 0 && ListSpace(query.s[0])) {
        query.s++;
        query.len--;
    }
    while (len(query) > 0 && ListSpace(query.s[len(query) - 1])) {
        query.len--;
    }
    return query;
}

static void StartSearch(ListState* s, Ctx* cx, Str query, bool dedupe) {
    query = TrimQuery(query);
    if (dedupe && base::StrEq(query, s->lastQuery)) {
        return;
    }
    if (s->onPerformSearch.IsValid()) {
        ListSearchRequest ev = {query};
        ListenerCall(cx->app, cx->win, s->onPerformSearch, &ev);
    }
    // The Rust task may complete later; this port's standing async exclusion
    // makes perform_search synchronous. Selection and scroll are nevertheless
    // advanced at the same semantic point.
    ListSetSelectedIndex(s, cx, s->count > 0 ? 0 : -1, false);
    s->scrollY = 0;
    StrFree(s->lastQuery);
    s->lastQuery = len(query) > 0 ? StrDup(query) : Str{};
    Notify(cx);
}

void ListSetQuery(ListState* s, Ctx* cx, Str query) {
    if (!s || !cx) {
        return;
    }
    if (s->queryInput) {
        InputSetValue(s->queryInput, query);
    }
    StartSearch(s, cx, query, false);
}

void ListState::OnQueryInput(ListState* self, Ctx* cx, const InputEvent* ev) {
    if (!self || !self->queryInput || !ev ||
        ev->kind != InputEventKind::Change) {
        return;
    }
    StartSearch(self, cx, InputValue(self->queryInput), true);
}

void ListRequestLoadMore(ListState* s, Ctx* cx) {
    if (!s || !cx || !s->onLoadMore.IsValid()) {
        return;
    }
    ListenerCall(cx->app, cx->win, s->onLoadMore, nullptr);
}

// cx.emit(ListEvent::..). Rust has only the subscriber list; the state's own
// onEvent is the shorthand this port already had for the single-subscriber
// case, so an event goes to it and to everything that has subscribed.
static void ListEmit(ListState* s, Ctx* cx, ListEventKind kind, int index,
                     bool secondary) {
    ListEvent ev = {kind, index, secondary};
    if (s->onEvent.IsValid()) {
        ListenerCall(cx->app, cx->win, s->onEvent, &ev);
    }
    EntityEmit(cx->app, cx->win, s->self, &ev);
}

// select_item: a list that is not selectable moves nothing.
static void ListSelect(ListState* s, Ctx* cx, int ix) {
    if (!s->selectable) {
        return;
    }
    ListSetSelectedIndex(s, cx, ix, false);
    ListEmit(s, cx, ListEventKind::Select, ix, false);
}

void ListPerform(ListState* s, Ctx* cx, ListAction act, bool secondary) {
    switch (act) {
        case ListAction::SelectPrev:
            if (s->count > 0) {
                ListSelect(s, cx, ListPrevIndex(s));
                // scroll_to_selected_item: a selection that moved off the
                // bottom of the viewport brings the viewport with it.
                ListScrollToItem(s, s->selected, ScrollStrategy::Top);
            }
            break;
        case ListAction::SelectNext:
            if (s->count > 0) {
                ListSelect(s, cx, ListNextIndex(s));
                ListScrollToItem(s, s->selected, ScrollStrategy::Top);
            }
            break;
        case ListAction::Confirm:
            // on_action_confirm: nothing to confirm with no rows and no
            // selection.
            if (s->count > 0 && s->selected >= 0) {
                CallSelection(s, cx, s->onSetSelectedIndex, s->selected);
                if (s->onConfirm.IsValid()) {
                    ListConfirmRequest ev = {secondary};
                    ListenerCall(cx->app, cx->win, s->onConfirm, &ev);
                }
                Notify(cx);
                ListEmit(s, cx, ListEventKind::Confirm, s->selected, secondary);
            }
            break;
        case ListAction::Cancel:
            if (s->resetOnCancel) {
                ListSetSelectedIndex(s, cx, -1, false);
            }
            if (s->onCancel.IsValid()) {
                ListenerCall(cx->app, cx->win, s->onCancel, nullptr);
            }
            Notify(cx);
            ListEmit(s, cx, ListEventKind::Cancel, s->selected, false);
            break;
        default:
            break;
    }
}

void ListClickRow(ListState* s, Ctx* cx, int ix, bool secondary) {
    if (!s->selectable) {
        return;
    }
    ListSetRightClickedIndex(s, cx, -1);
    s->selected = ix;
    // on_action_confirm deliberately synchronizes selected_index once more
    // before confirm, which is part of ListDelegate's contract.
    CallSelection(s, cx, s->onSetSelectedIndex, s->selected);
    if (s->onConfirm.IsValid()) {
        ListConfirmRequest confirm = {secondary};
        ListenerCall(cx->app, cx->win, s->onConfirm, &confirm);
    }
    Notify(cx);
    ListEmit(s, cx, ListEventKind::Confirm, ix, secondary);
}

void ListRightClickRow(ListState* s, Ctx* cx, int ix) {
    if (!s->selectable) {
        return;
    }
    ListSetRightClickedIndex(s, cx, ix);
}

void ListState::OnMouseDownOut(ListState* self, Ctx* cx,
                               const MouseDownEvent*) {
    if (self && self->rightClicked >= 0) {
        ListSetRightClickedIndex(self, cx, -1);
    }
}

void ListState::OnRowClick(ListState* self, Ctx* cx, const ClickEvent* ev,
                           intptr_t ix) {
    ListClickRow(self, cx, (int)ix, ev->modifiers.Secondary());
}

void ListState::OnScroll(ListState* self, Ctx* cx, const ScrollEvent* ev) {
    self->scrollY = ev->offsetY;
    Notify(cx);
}

void ListState::OnRowMouseDown(ListState* self, Ctx* cx,
                               const MouseDownEvent* ev, intptr_t ix) {
    // on_mouse_down(MouseButton::Right): the row under a secondary press is
    // marked, and the left button is left to the click path above.
    if (ev->button == MouseButton::Right) {
        ListRightClickRow(self, cx, (int)ix);
    }
}

void ListOnAction(ListState* self, Ctx* cx, const ActionEvent* ev) {
    if (!self) {
        return;
    }
    ListKeyAction act = ListActionOf(ev->action, ev->arg);
    if (act.action == ListAction::None) {
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    ListPerform(self, cx, act.action, act.secondary);
}

void ListBindKeys(Ctx* cx, El* root, Entity<ListState> state) {
    if (!cx || !root || !state.IsValid()) {
        return;
    }
    ListInitKeys();
    Listener onAction = ListenTo(state, &ListOnAction);
    root->KeyContext(ListContext())
        ->OnAction(action::Cancel(), onAction)
        ->OnAction(action::Confirm(), onAction)
        ->OnAction(action::SelectUp(), onAction)
        ->OnAction(action::SelectDown(), onAction);
}

} // namespace gpui
