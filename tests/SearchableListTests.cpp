/* Ported from crates/ui/src/searchable_list.
 *
 * A click on a row is worked out into atomic changes by the mode — Single
 * replaces the selection, Multi toggles the row — and the delegate's default
 * `on_will_change` then applies them: a Select whose value is already
 * selected changes nothing, and a Deselect takes out whatever carries that
 * value, falling back to the index when nothing does. */

#include "Test.h"

using namespace gpui::component;

static const SearchableItem kItems[] = {
    {StrL("Apple"), StrL("apple"), 0, false},
    {StrL("Banana"), StrL("banana"), 0, false},
    {StrL("Cherry"), StrL("cherry"), 0, true},
    // Two rows for the same value, which is what makes "by value" and "by
    // index" different answers.
    {StrL("Apple (again)"), StrL("apple"), 1, false},
};
static const int kN = 4;

static void Apply(SearchableListState* s, int index) {
    Vec<SearchableListChange> changes;
    SearchableListChangesFor(s, kItems, kN, index, &changes);
    SearchableListApply(s, kItems, kN, changes.els, len(changes));
    VecReset(changes);
}

static void TheQueryIsACaseInsensitiveSubstringOfTheTitle() {
    utassert(SearchableItemMatches(&kItems[0], StrL("app")));
    utassert(SearchableItemMatches(&kItems[0], StrL("APPLE")));
    utassert(SearchableItemMatches(&kItems[0], StrL("ple")));
    utassert(!SearchableItemMatches(&kItems[0], StrL("banana")));
    // An empty query matches everything, as `contains("")` does.
    utassert(SearchableItemMatches(&kItems[1], StrL("")));
}

static void SearchLeavesTheMatchesInOrder() {
    SearchableListState s;
    SearchableListSearch(&s, kItems, kN, StrL("an"));
    // Banana, and nothing else.
    utassert(s.matches.len == 1);
    utassert(s.matches[0] == 1);
    // The list is told how many rows it has, since that is what its keys walk.
    utassert(s.list.count == 1);

    SearchableListSearch(&s, kItems, kN, StrL(""));
    utassert(s.matches.len == kN);
    utassert(s.matches[3] == 3);
}

static void SingleReplacesTheSelection() {
    SearchableListState s;
    Apply(&s, 0);
    utassert(s.selected.len == 1 && s.selected[0] == 0);
    // Picking another one deselects what was there first.
    Apply(&s, 1);
    utassert(s.selected.len == 1 && s.selected[0] == 1);
    // Picking the same one again leaves it selected: the Deselect takes it
    // out and the Select puts it straight back.
    Apply(&s, 1);
    utassert(s.selected.len == 1 && s.selected[0] == 1);
}

static void MultiTogglesOnlyTheRowThatWasClicked() {
    SearchableListState s;
    s.mode = SearchableListMode::Multi;
    Apply(&s, 0);
    Apply(&s, 1);
    utassert(s.selected.len == 2 && s.selected[0] == 0 && s.selected[1] == 1);
    // The second click on the first row takes only that one out.
    Apply(&s, 0);
    utassert(s.selected.len == 1 && s.selected[0] == 1);
}

static void ASelectionIsComparedByValue() {
    SearchableListState s;
    s.mode = SearchableListMode::Multi;
    Apply(&s, 0);
    // Row 3 carries the same value as row 0, so it is already checked...
    utassert(SearchableListIsChecked(&s, kItems, kN, 3));
    // ...and selecting it adds nothing.
    Apply(&s, 3);
    utassert(s.selected.len == 0);
    // Which is to say the toggle deselected it: a value already in the
    // selection is what the click was toggling.
    utassert(!SearchableListIsChecked(&s, kItems, kN, 0));
}

static void SourceDelegateQueriesFlatAndGroupedItems() {
    SearchableListDelegate flat = SearchableListDelegate::Items(kItems, kN);
    utassert(flat.SectionsCount() == 1);
    utassert(flat.ItemsCount(0) == kN);
    utassert(flat.Item(IndexPathNew(1)) == &kItems[1]);
    IndexPath path;
    utassert(flat.Position(StrL("banana"), &path));
    utassert(path == IndexPathNew(1));

    SearchableGroup* fruit = SearchableGroup::New(StrL("Fruit"))
                                 ->Items(kItems, 2);
    SearchableGroup* more = SearchableGroup::New(StrL("More"))->Item(kItems[3]);
    SearchableGroup* groups[] = {fruit, more};
    SearchableListDelegate grouped = SearchableListDelegate::Groups(groups, 2);
    utassert(grouped.SectionsCount() == 2);
    utassert(grouped.ItemsCount(1) == 1);
    utassert(grouped.Item(IndexPathNew(0).Section(1)) == &more->items[0]);
    utassert(grouped.Position(StrL("apple"), &path));
    utassert(path == IndexPathNew(0).Section(0));
    utassert(fruit->Matches(StrL("FRU")));
    utassert(fruit->Matches(StrL("nan")));
    utassert(!fruit->Matches(StrL("stone")));
    delete fruit;
    delete more;
}

static void SearchableVecRebuildsItsMatchedView() {
    SearchableVec* values = SearchableVec::New(kItems, 3);
    utassert(values->ItemsCount() == 3);
    values->PerformSearch(StrL("app"));
    utassert(values->ItemsCount() == 1);
    utassert(base::StrEq(values->Item(IndexPathNew(0))->value, StrL("apple")));
    IndexPath path;
    utassert(!values->Position(StrL("banana"), &path));

    // Pinned SearchableVec::push appends to both master and current views;
    // it does not re-run the last query.
    SearchableListItem blueberry = {StrL("Blueberry"), StrL("blue")};
    values->Push(blueberry);
    utassert(values->ItemsCount() == 2);
    utassert(values->Position(StrL("blue"), &path));
    utassert(path == IndexPathNew(1));
    values->PerformSearch(Str{});
    utassert(values->ItemsCount() == 4);
    delete values;
}

static void ItemElementReservesItsCheckAndUsesListSizing() {
    App app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    El* row = SearchableListItemElement::New(&cx, 7)
                  ->WithSize(UiSize::Small)
                  ->Child(TextEl(a, StrL("Seven")))
                  ->IntoEl();
    utassertnear(row->style.pad.left, 8.f);
    utassertnear(row->style.pad.top, 2.f);
    utassertnear(row->style.fontSize, 14.f);
    El* inner = row->first;
    utassert(inner != nullptr && inner->next == nullptr);
    El* invisibleCheck = inner->first->next;
    utassert(invisibleCheck != nullptr && invisibleCheck->next == nullptr);
    utassertnear(invisibleCheck->style.width, 12.f);
    utassertnear(invisibleCheck->style.opacity, 0.f);

    El* checked = SearchableListItemElement::New(&cx, 8)
                      ->WithSize(UiSize::Large)
                      ->Checked(true)
                      ->Selected(true)
                      ->CheckIcon(IconName::CircleCheck)
                      ->IntoEl();
    utassertnear(checked->style.pad.left, 12.f);
    utassertnear(checked->style.pad.top, 8.f);
    utassertnear(checked->style.fontSize, 16.f);
    El* visibleCheck = checked->first->first->next;
    utassertnear(visibleCheck->style.opacity, 1.f);
    utassert(base::StrEq(visibleCheck->iconPath,
                         IconNamePath(IconName::CircleCheck)));

    ArenaDelete(a);
}

static void StateAccessorsUseGroupedIndexPaths() {
    SearchableListState s;
    SearchableListSearch(&s, kItems, kN, Str{});
    utassert(s.AddSelectedIndex(IndexPathNew(0).Section(1)));
    utassert(!s.AddSelectedIndex(IndexPathNew(0).Section(1)));
    Vec<Str> values;
    s.SelectedValues(&values);
    utassert(len(values) == 1 && base::StrEq(values[0], StrL("apple")));
    utassert(s.RemoveSelectedIndex(IndexPathNew(0).Section(1)));
    utassert(!s.RemoveSelectedIndex(IndexPathNew(0).Section(1)));

    IndexPath selection[] = {IndexPathNew(1), IndexPathNew(0).Section(1)};
    s.SetSelectedIndices(selection, 2);
    utassert(s.Selection().len == 2);
    s.SelectedValues(&values);
    utassert(len(values) == 2);
    utassert(base::StrEq(values[0], StrL("banana")));
    utassert(base::StrEq(values[1], StrL("apple")));
    utassert(!s.IsOpen());
    utassert(s.Focus() == &s.triggerFocus);
    VecReset(values);
}

struct DelegateHooks {
    int renderedItems = 0;
    int renderedHeaders = 0;
    int willChange = 0;
    int confirmed = 0;
};

static bool HookMatches(void*, const SearchableListItem* item, Str) {
    return base::StrEq(item->value, StrL("banana"));
}

static Str HookSectionTitle(void*, int) {
    return StrL("Fruit");
}

static El* HookRenderItem(void* user, Ctx* cx, IndexPath,
                          const SearchableListItem* item, bool checked) {
    DelegateHooks* hooks = (DelegateHooks*)user;
    hooks->renderedItems++;
    return Div(cx->a)->Child(TextEl(cx->a, item->title))->AriaSelected(checked);
}

static El* HookRenderHeader(void* user, Ctx* cx, int) {
    DelegateHooks* hooks = (DelegateHooks*)user;
    hooks->renderedHeaders++;
    return TextEl(cx->a, StrL("Custom header"));
}

static bool HookChecked(void*, IndexPath, const SearchableListItem*,
                        const SearchableListState*, const App*) {
    return true;
}

static void HookWillChange(void* user, SearchableListState*,
                           const SearchableListChange*, int) {
    // Leaving the selection untouched is the source delegate's veto seam.
    ((DelegateHooks*)user)->willChange++;
}

static void HookConfirm(void* user, const SearchableListState*, IndexPath,
                        bool) {
    ((DelegateHooks*)user)->confirmed++;
}

static void DelegateHooksDriveSearchRenderingAndSelection() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    DelegateHooks hooks;
    SearchableListDelegate delegate = SearchableListDelegate::Items(kItems, 2);
    delegate.user = &hooks;
    delegate.sectionTitle = &HookSectionTitle;
    delegate.matches = &HookMatches;
    delegate.renderItem = &HookRenderItem;
    delegate.renderSectionHeader = &HookRenderHeader;
    delegate.isItemChecked = &HookChecked;
    delegate.onWillChange = &HookWillChange;
    delegate.onConfirm = &HookConfirm;

    Entity<SearchableListState> entity =
        EntityNewState<SearchableListState>(&app);
    El* box = SearchableList::New(&cx, StrL("hook-list"), entity, nullptr)
                  ->Delegate(delegate)
                  ->IntoEl();
    SearchableListState* state = entity.Get(&app);
    utassert(state->hasDelegate);
    utassert(state->matches.len == 1 && state->matches[0] == 1);
    utassert(hooks.renderedHeaders == 1);
    utassert(hooks.renderedItems == 1);
    El* rows = box->first;
    utassert(rows != nullptr && rows->first != nullptr);
    utassert(base::StrEq(rows->first->text, StrL("Custom header")));
    utassert(rows->first->next != nullptr);
    utassert(rows->first->next->accessibility.selected);

    utassert(SearchableListClick(state, 1));
    utassert(hooks.willChange == 1);
    utassert(state->selected.len == 0);
    delegate.OnConfirm(state, IndexPathNew(1), false);
    utassert(hooks.confirmed == 1);

    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

void TestSearchableList() {
    TestSuite("searchable_list");
    TheQueryIsACaseInsensitiveSubstringOfTheTitle();
    SearchLeavesTheMatchesInOrder();
    SingleReplacesTheSelection();
    MultiTogglesOnlyTheRowThatWasClicked();
    ASelectionIsComparedByValue();
    SourceDelegateQueriesFlatAndGroupedItems();
    SearchableVecRebuildsItsMatchedView();
    ItemElementReservesItsCheckAndUsesListSizing();
    StateAccessorsUseGroupedIndexPaths();
    DelegateHooksDriveSearchRenderingAndSelection();
}
