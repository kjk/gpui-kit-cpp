/* Ported from crates/ui/src/list/list.rs and list/cache.rs.
 *
 * Rust binds up, down, enter, secondary-enter and escape in the "List" key
 * context and hangs an on_action off each; this walks a chord in through the
 * keymap and pins what the list makes of it. The moves themselves
 * are rows_cache.next / .prev, which wrap at both ends and start from the
 * first or the last row when nothing is selected. The row walk here is over a
 * flat count rather than an IndexPath through sections, so the section
 * headers a Rust cache steps over have nothing to step over here. */

#include "Test.h"

// The chord, resolved in the list's context, read as what the list does.
static ListKeyAction ForChord(const char* spec) {
    ListInitKeys();
    KeyChord c = {};
    utassert(KeyChordParse(Str(spec), &c));
    uint32_t ctx = KeyContextOf(ListContext());
    KeyMatch m = KeymapMatch(c, &ctx, 1);
    return ListActionOf(m.action, m.arg);
}

static void TheKeyTable() {
    utassert(ForChord("up").action == ListAction::SelectPrev);
    utassert(ForChord("down").action == ListAction::SelectNext);
    utassert(ForChord("enter").action == ListAction::Confirm);
    utassert(ForChord("escape").action == ListAction::Cancel);
    utassert(ForChord("space").action == ListAction::None);
    utassert(ForChord("tab").action == ListAction::None);

    // `Confirm { secondary }` is bound twice: to enter and to
    // secondary-enter, and the two differ only in what the action carries.
    // The flag is the binding's `arg`, which the matcher hands back.
    utassert(!ForChord("enter").secondary);
    ListKeyAction sec = ForChord("secondary-enter");
    utassert(sec.action == ListAction::Confirm && sec.secondary);
    utassert(!ForChord("down").secondary);
}

static void NextAndPrevWrap() {
    ListState s;
    s.count = 3;

    // next(None) is the first row, prev(None) the last.
    utassert(ListNextIndex(&s) == 0);
    utassert(ListPrevIndex(&s) == 2);

    s.selected = 0;
    utassert(ListNextIndex(&s) == 1);
    utassert(ListPrevIndex(&s) == 2);

    s.selected = 2;
    utassert(ListNextIndex(&s) == 0);
    utassert(ListPrevIndex(&s) == 1);
}

static void AnEmptyListHasNowhereToGo() {
    ListState s;
    s.count = 0;
    utassert(ListNextIndex(&s) == -1);
    utassert(ListPrevIndex(&s) == -1);
}

static void TheFlattenedRowsAreHeaderItemsFooter() {
    ListState s;
    const int counts[] = {2, 3};
    ListSetSections(&s, counts, 2, true, true);
    // Two sections of two and three items, each with a header and a footer.
    utassert(s.count == 5);
    utassert(ListRowCount(&s) == 9);

    ListRow r = ListRowAt(&s, 0);
    utassert(r.kind == ListRowKind::SectionHeader && r.section == 0);
    r = ListRowAt(&s, 1);
    utassert(r.kind == ListRowKind::Entry && r.section == 0 && r.row == 0 &&
             r.entry == 0);
    r = ListRowAt(&s, 3);
    utassert(r.kind == ListRowKind::SectionFooter && r.section == 0);
    r = ListRowAt(&s, 4);
    utassert(r.kind == ListRowKind::SectionHeader && r.section == 1);
    r = ListRowAt(&s, 5);
    // The second section's first item carries on the item numbering, which is
    // what the selection is kept as.
    utassert(r.kind == ListRowKind::Entry && r.section == 1 && r.row == 0 &&
             r.entry == 2);
    r = ListRowAt(&s, 8);
    utassert(r.kind == ListRowKind::SectionFooter && r.section == 1);

    // And back the other way.
    utassert(ListRowOfEntry(&s, 0) == 1);
    utassert(ListRowOfEntry(&s, 2) == 5);
    utassert(ListRowOfEntry(&s, 4) == 7);
    utassert(ListRowOfEntry(&s, 5) == -1);
}

static void AnEmptySectionTakesItsHeaderWithIt() {
    ListState s;
    const int counts[] = {2, 0, 1};
    ListSetSections(&s, counts, 3, true, true);
    // The middle section contributes nothing at all — not even its header and
    // footer, which is what Rust's cache skips.
    utassert(s.count == 3);
    utassert(ListRowCount(&s) == 4 + 3);
    ListRow r = ListRowAt(&s, 4);
    utassert(r.kind == ListRowKind::SectionHeader && r.section == 2);
    utassert(ListRowOfEntry(&s, 2) == 5);
}

static void AListWithNoSectionsIsOneSection() {
    ListState s;
    ListSetCount(&s, 4);
    utassert(s.count == 4);
    // No header, no footer: a row is an item and nothing else.
    utassert(ListRowCount(&s) == 4);
    ListRow r = ListRowAt(&s, 2);
    utassert(r.kind == ListRowKind::Entry && r.entry == 2);
    utassert(ListRowOfEntry(&s, 3) == 3);
    // A row past the end is not an item.
    utassert(ListRowAt(&s, 9).entry == -1);
}

static void LoadMoreAsksNearTheEnd() {
    ListState s;
    ListSetCount(&s, 100);
    s.loadMoreThreshold = 20;
    // Nothing to load: the delegate said there is no more.
    utassert(!ListShouldLoadMore(&s, 95));
    s.hasMore = true;
    utassert(!ListShouldLoadMore(&s, 40));
    utassert(ListShouldLoadMore(&s, 80));
    utassert(ListShouldLoadMore(&s, 100));
    // A list already loading does not ask twice.
    s.loading = true;
    utassert(!ListShouldLoadMore(&s, 100));
}

// RowsCache::prepare_if_needed: a size per flattened row, taken from the
// three that were measured. A header is not an item.s height and neither is
// a footer, which is why `v_virtual_list` takes a vector rather than one
// number.
static void EachRowKindKeepsItsOwnHeight() {
    ListState s;
    int counts[2] = {2, 1};
    ListSetSections(&s, counts, 2, true, true);
    // header, two items, footer, then header, one item, footer.
    utassert(ListRowCount(&s) == 7);
    ListPrepareRowHeights(&s, 36, 24, 20);
    const float* h = ListRowHeights(&s);
    utassert(h != nullptr);
    utassertnear(h[0], 24.f);
    utassertnear(h[1], 36.f);
    utassertnear(h[2], 36.f);
    utassertnear(h[3], 20.f);
    utassertnear(h[4], 24.f);
    utassertnear(h[5], 36.f);
    utassertnear(h[6], 20.f);
    // The whole list is what the virtual list scrolls against.
    utassertnear(VirtualListContentSize(h, 7),
                 24 + 36 + 36 + 20 + 24 + 36 + 20);
}

// need_update: the same sections measured the same way rebuild nothing, and a
// measure that moved rebuilds all of it.
static void TheHeightsAreRebuiltOnlyWhenSomethingMoved() {
    ListState s;
    ListSetCount(&s, 3);
    ListPrepareRowHeights(&s, 32, 0, 0);
    const float* first = ListRowHeights(&s);
    utassert(first != nullptr);
    utassertnear(first[0], 32.f);
    ListPrepareRowHeights(&s, 32, 0, 0);
    utassertnear(ListRowHeights(&s)[0], 32.f);
    // A row that measured taller takes every row with it.
    ListPrepareRowHeights(&s, 40, 0, 0);
    utassertnear(ListRowHeights(&s)[2], 40.f);
    utassertnear(s.rowH, 40.f);
}

// Before anything has been measured there are no per-row heights at all, and
// the list falls back to the one it starts at.
static void AListThatHasNotMeasuredHasNoHeights() {
    ListState s;
    ListSetCount(&s, 4);
    utassert(ListRowHeights(&s) == nullptr);
    utassertnear(s.rowH, 32.f);
}

struct ListDelegateSink {
    int searches = 0;
    Str query = {};
    int selections = 0;
    bool hasSelection = false;
    IndexPath selected = {};
    int rightClicks = 0;
    bool hasRightClick = false;
    int confirms = 0;
    bool secondary = false;
    int cancels = 0;
    int loads = 0;

    ~ListDelegateSink() { StrFree(query); }

    static El* Render(ListDelegateSink*, Ctx* cx) { return Div(cx->a); }
    static void OnSearch(ListDelegateSink* self, Ctx*,
                         const ListSearchRequest* ev) {
        self->searches++;
        StrFree(self->query);
        self->query = StrDup(ev->query);
    }
    static void OnSelection(ListDelegateSink* self, Ctx*,
                            const ListSelectionChange* ev) {
        self->selections++;
        self->hasSelection = ev->hasIndex;
        self->selected = ev->index;
    }
    static void OnRightClick(ListDelegateSink* self, Ctx*,
                             const ListSelectionChange* ev) {
        self->rightClicks++;
        self->hasRightClick = ev->hasIndex;
    }
    static void OnConfirm(ListDelegateSink* self, Ctx*,
                          const ListConfirmRequest* ev) {
        self->confirms++;
        self->secondary = ev->secondary;
    }
    static void OnCancel(ListDelegateSink* self, Ctx*, const void*) {
        self->cancels++;
    }
    static void OnLoadMore(ListDelegateSink* self, Ctx*, const void*) {
        self->loads++;
    }
};

struct ListRenderProbe {
    int sectionsCalls = 0;
    int itemCountCalls = 0;
    int renderCalls = 0;
};

static int DelegateSections(Ctx*, void* data) {
    ((ListRenderProbe*)data)->sectionsCalls++;
    return 2;
}

static int DelegateItems(Ctx*, void* data, int section) {
    ((ListRenderProbe*)data)->itemCountCalls++;
    return section == 0 ? 2 : 1;
}

static component::ListItem* DelegateItem(Ctx* cx, void* data, int section,
                                         int row, int entry) {
    ListRenderProbe* probe = (ListRenderProbe*)data;
    probe->renderCalls++;
    utassert(entry == (section == 0 ? row : 2 + row));
    return component::ListItem::New(cx, Div(cx->a)->H(24));
}

// list.rs measurement_tests: a delegate whose section counts a test can
// change between frames, with a 36px first row and 48px rows after it, so the
// measured height says which row was measured.
struct MeasureProbe {
    int counts[2] = {0, 2};
};

static int MeasureSections(Ctx*, void*) {
    return 2;
}

static int MeasureItems(Ctx*, void* data, int section) {
    return ((MeasureProbe*)data)->counts[section];
}

static component::ListItem* MeasureItem(Ctx* cx, void* data, int section,
                                        int row, int) {
    MeasureProbe* probe = (MeasureProbe*)data;
    if (section < 0 || section > 1 || row >= probe->counts[section]) {
        return nullptr;
    }
    return component::ListItem::New(cx, Div(cx->a)->H(row == 0 ? 36.f : 48.f));
}

// measures_an_existing_row_when_the_requested_item_is_absent. The row
// measured is the configured one when it exists, else the first row of the
// first non-empty section, else nothing — and the configured index is left
// as the caller set it, so it is measured again once it exists.
static void MeasuresAnExistingRowWhenTheRequestedItemIsAbsent() {
    App app;
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* arena = ArenaNew();
    win->frameArena = arena;
    ThemeInstall(&app, ThemeMode::Light, ThemeLight());
    Entity<ListState> state = EntityNewState<ListState>(&app);
    Ctx cx = {&app, win, arena, {}};
    MeasureProbe probe;
    component::ListDelegate delegate;
    delegate.data = &probe;
    delegate.sectionsCount = &MeasureSections;
    delegate.itemsCount = &MeasureItems;
    delegate.renderItem = &MeasureItem;
    ListState* list = state.Get(&app);

    // Upstream sets the height on the ListItem itself; ours wraps the caller's
    // element in the item's padded row, so every measurement carries the
    // row's 4px above and below.
    const float kItemPad = 8;
    auto measure = [&]() {
        arena->Reset();
        component::List::New(&cx, StrL("measure-list"), state)
            ->WithDelegate(delegate)
            ->ScrollbarVisible(false)
            ->H(300)
            ->IntoEl();
        return list->rowH;
    };

    struct Case {
        IndexPath requested;
        float height;
    } cases[] = {
        {IndexPathNew(0), 36 + kItemPad},
        {IndexPathNew(1).Section(1), 48 + kItemPad},
        {IndexPathNew(99).Section(1), 36 + kItemPad},
        {IndexPathNew(0).Section(99), 36 + kItemPad},
    };
    for (const Case& c : cases) {
        ListSetItemToMeasureIndex(list, &cx, c.requested);
        utassertnear(measure(), c.height);
        utassert(list->itemToMeasure == c.requested);
    }

    // Filtering removes the requested row, then all rows, before restoring
    // it. With nothing to measure the height keeps what it had, and the list
    // has no rows to give it to.
    IndexPath requested = IndexPathNew(1).Section(1);
    ListSetItemToMeasureIndex(list, &cx, requested);
    struct Filtered {
        int counts[2];
        float height;
    } filtered[] = {
        {{0, 2}, 48 + kItemPad},
        {{0, 1}, 36 + kItemPad},
        {{0, 0}, 36 + kItemPad},
        {{0, 2}, 48 + kItemPad},
    };
    for (const Filtered& f : filtered) {
        probe.counts[0] = f.counts[0];
        probe.counts[1] = f.counts[1];
        utassertnear(measure(), f.height);
        if (f.counts[1] == 0) {
            utassert(list->count == 0 && ListRowCount(list) == 0);
        }
        utassert(list->itemToMeasure == requested);
    }

    WindowMotionFree(win);
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
}

static void TheDelegateTableOwnsTheWholeContract() {
    component::ListDelegate defaults;
    utassert(defaults.sectionsCount == nullptr);
    utassert(defaults.itemsCount == nullptr);
    utassert(defaults.renderItem == nullptr);
    utassert(!defaults.performSearch.IsValid());
    utassert(!defaults.setSelectedIndex.IsValid());
    utassert(defaults.loadMoreThreshold == nullptr);

    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    win->frameArena = arena;
    ThemeInstall(&app, ThemeMode::Light, ThemeLight());
    Entity<ListState> state = EntityNewState<ListState>(&app);
    Entity<ListDelegateSink> sink = EntityNew<ListDelegateSink>(&app);
    Ctx cx = {&app, win, arena, sink.id};
    InputState query;
    ListRenderProbe probe;
    component::ListDelegate delegate;
    delegate.data = &probe;
    delegate.sectionsCount = &DelegateSections;
    delegate.itemsCount = &DelegateItems;
    delegate.renderItem = &DelegateItem;
    delegate.performSearch = ListenTo(sink, &ListDelegateSink::OnSearch);
    delegate.setSelectedIndex = ListenTo(sink, &ListDelegateSink::OnSelection);
    delegate
        .setRightClickedIndex = ListenTo(sink, &ListDelegateSink::OnRightClick);
    delegate.confirm = ListenTo(sink, &ListDelegateSink::OnConfirm);
    delegate.cancel = ListenTo(sink, &ListDelegateSink::OnCancel);
    delegate.loadMore = ListenTo(sink, &ListDelegateSink::OnLoadMore);

    El* root = component::List::New(&cx, StrL("delegate-list"), state)
                   ->WithDelegate(delegate)
                   ->Searchable(&query, {})
                   ->SearchPlaceholder(StrL("Find"))
                   ->ScrollbarVisible(false)
                   ->H(120)
                   ->IntoEl();
    ListState* list = state.Get(&app);
    utassert(root && list);
    utassert(list->count == 3 && list->sectionCounts.len == 2);
    utassert(list->sectionCounts[0] == 2 && list->sectionCounts[1] == 1);
    utassert(probe.sectionsCalls == 1 && probe.itemCountCalls == 2);
    // One representative item plus every visible item.
    utassert(probe.renderCalls >= 4);
    utassert(StrEqI(query.placeholder, "Find"));
    utassert(query.onChange.IsValid());
    utassert(list->loadMoreThreshold == 20);
    utassert(!list->loading && !list->hasMore);

    InputSetValue(&query, StrL("  alpha  "));
    InputEvent changed = {InputEventKind::Change};
    ListState::OnQueryInput(list, &cx, &changed);
    ListDelegateSink* got = sink.Get(&app);
    utassert(got->searches == 1 && StrEqI(got->query, "alpha"));
    utassert(list->selected == 0 && got->hasSelection);
    IndexPath first = {0, 0, 0};
    utassert(got->selected == first);
    // The Input Change subscription suppresses a duplicate trimmed query.
    InputSetValue(&query, StrL("alpha"));
    ListState::OnQueryInput(list, &cx, &changed);
    utassert(got->searches == 1);
    // set_query explicitly searches even when set_value would emit nothing.
    ListSetQuery(list, &cx, StrL("alpha"));
    utassert(got->searches == 2);

    int selections = got->selections;
    ListPerform(list, &cx, ListAction::SelectNext, false);
    utassert(list->selected == 1 && got->selections == selections + 1);
    selections = got->selections;
    ListPerform(list, &cx, ListAction::Confirm, true);
    utassert(got->selections == selections + 1);
    utassert(got->confirms == 1 && got->secondary);

    selections = got->selections;
    ListClickRow(list, &cx, 2, false);
    utassert(list->selected == 2 && got->selections == selections + 1);
    utassert(got->confirms == 2 && !got->secondary);
    utassert(got->rightClicks == 1 && !got->hasRightClick);
    ListRightClickRow(list, &cx, 1);
    utassert(got->rightClicks == 2 && got->hasRightClick);
    IndexPath right;
    utassert(ListRightClickedIndex(list, &right));
    IndexPath second = {0, 1, 0};
    utassert(right == second);
    MouseDownEvent outside = {};
    ListState::OnMouseDownOut(list, &cx, &outside);
    utassert(!ListRightClickedIndex(list, nullptr));
    utassert(got->rightClicks == 3 && !got->hasRightClick);

    list->selectable = false;
    ListRightClickRow(list, &cx, 1);
    utassert(!ListRightClickedIndex(list, nullptr));
    utassert(got->rightClicks == 3);
    list->selectable = true;

    ListPerform(list, &cx, ListAction::Cancel, false);
    utassert(got->cancels == 1 && list->selected == -1);
    utassert(!got->hasSelection);
    ListRequestLoadMore(list, &cx);
    utassert(got->loads == 1);

    WindowMotionFree(win);
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
}

void TestList() {
    TheKeyTable();
    NextAndPrevWrap();
    AnEmptyListHasNowhereToGo();
    TheFlattenedRowsAreHeaderItemsFooter();
    AnEmptySectionTakesItsHeaderWithIt();
    AListWithNoSectionsIsOneSection();
    LoadMoreAsksNearTheEnd();
    EachRowKindKeepsItsOwnHeight();
    TheHeightsAreRebuiltOnlyWhenSomethingMoved();
    AListThatHasNotMeasuredHasNoHeights();
    TheDelegateTableOwnsTheWholeContract();
    MeasuresAnExistingRowWhenTheRequestedItemIsAbsent();
}
