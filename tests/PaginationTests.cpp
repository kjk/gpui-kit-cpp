/* Ported from crates/base/src/pagination.rs, mod tests.
 *
 * Two of the four Rust cases are pure logic and come over as they are. The
 * other two need TestAppContext: one drives `request_page` through a window to
 * watch the handler fire, and the other reads an accessibility node. The
 * guards the first one is really about are `PaginationCanRequest`, which is
 * checked here without the window. */

#include "Test.h"

static void ClampsControlledValuesAndNavigationBoundaries() {
    PaginationState first = PaginationStateNew(0, 0);
    utassert(first.currentPage == 1);
    utassert(first.totalPages == 1);
    utassert(PaginationPrevPage(&first) == 0);
    utassert(PaginationNextPage(&first) == 0);

    PaginationState last = PaginationStateNew(20, 10);
    utassert(last.currentPage == 10);
    utassert(PaginationPrevPage(&last) == 9);
    utassert(PaginationNextPage(&last) == 0);

    PaginationState off = last;
    off.disabled = true;
    utassert(PaginationPrevPage(&off) == 0);
}

static void CreatesPagesAndNavigableEllipsisRanges() {
    PaginationState s = PaginationStateNew(5, 10);
    s.visiblePages = 7;
    PaginationItem items[16];
    int n = PaginationItems(&s, items, 16);
    utassert(n == 9);
    utassert(items[0].page == 1);
    // Rust's Ellipsis(2..3) is the half-open range over page 2 alone.
    utassert(items[1].page == 0);
    utassert(items[1].from == 2 && items[1].to == 2);
    utassert(items[2].page == 3);
    utassert(items[3].page == 4);
    utassert(items[4].page == 5);
    utassert(items[5].page == 6);
    utassert(items[6].page == 7);
    // Ellipsis(8..10) covers pages 8 and 9.
    utassert(items[7].page == 0);
    utassert(items[7].from == 8 && items[7].to == 9);
    utassert(items[8].page == 10);
}

static void ASinglePageHasNothingToNavigate() {
    PaginationState s = PaginationStateNew(1, 1);
    PaginationItem items[16];
    utassert(PaginationItems(&s, items, 16) == 0);
}

static void EveryPageChangeRequestIsValidated() {
    PaginationState s = PaginationStateNew(3, 5);
    utassert(!PaginationCanRequest(&s, 3));
    utassert(!PaginationCanRequest(&s, 0));
    utassert(!PaginationCanRequest(&s, 6));
    utassert(PaginationCanRequest(&s, 4));

    PaginationState off = s;
    off.disabled = true;
    utassert(!PaginationCanRequest(&off, 2));
}

static El* FindNamedPg(El* root, const char* name) {
    if (!root) {
        return nullptr;
    }
    if (root->id.s && base::StrEqI(root->id, name)) {
        return root;
    }
    for (El* c = root->first; c; c = c->next) {
        if (El* hit = FindNamedPg(c, name)) {
            return hit;
        }
    }
    return nullptr;
}

// The first element whose name starts with `prefix`, which is how the two
// ellipses are found without the test knowing where in the row they landed.
static El* FindPrefixed(El* root, const char* prefix) {
    if (!root) {
        return nullptr;
    }
    if (root->id.s && base::StrStartsWithI(root->id, prefix)) {
        return root;
    }
    for (El* c = root->first; c; c = c->next) {
        if (El* hit = FindPrefixed(c, prefix)) {
            return hit;
        }
    }
    return nullptr;
}

// window.element_id_stack: GPUI has the ids of everything a widget is being
// built inside already pushed by the time that widget's render runs, so a
// `use_keyed_state` asked for under a local name is scoped for free. The port
// builds its tree before anything is folded, so a widget that owns a name
// says so, and IdScope is what a `.id()` on the way down amounts to.
static void AScopeIsWhatMakesALocalNameItsOwn() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;

    uint32_t bare = KeyedName(&cx, StrL("ellipsis-1"));
    uint32_t left = 0;
    {
        IdScope scope(&cx, StrL("left"));
        left = KeyedName(&cx, StrL("ellipsis-1"));
        utassert(left != bare);
    }
    // And it is off the stack again once the widget is built.
    utassert(cx.path == 0);
    {
        IdScope scope(&cx, StrL("right"));
        utassert(KeyedName(&cx, StrL("ellipsis-1")) != left);
    }

    WindowKeyedFree(win);
    delete win;
    EntityDropAll(&app);
}

struct PgSink {
    int page = 0;
    static void OnPage(PgSink* self, Ctx*, const ClickEvent*, intptr_t p) {
        self->page = (int)p;
    }
};

// Two paginations on one page. The ellipsis is `ellipsis-{i}` in both, and
// the row it sits in is what keeps the dropdown it opens — the element, and
// the two states behind it — from being the other row's.
static void TwoPaginationsHaveTwoEllipsisMenus() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    // An ellipsis is only a dropdown if a page change has somewhere to go, so
    // both rows need a handler before either grows one.
    Entity<PgSink> sink = EntityNewState<PgSink>(&app);
    Listener onPage = ListenTo(sink, &PgSink::OnPage);
    El* page = Div(a);
    El* left = component::Pagination::New(&cx, 5, 10)
                   ->Id(StrL("left"))
                   ->OnChange(onPage)
                   ->IntoEl();
    El* right = component::Pagination::New(&cx, 5, 10)
                    ->Id(StrL("right"))
                    ->OnChange(onPage)
                    ->IntoEl();
    page->Child(left)->Child(right);
    IdsCollect(page);

    El* boxL = FindPrefixed(left, "ellipsis-");
    El* boxR = FindPrefixed(right, "ellipsis-");
    utassert(boxL && boxR);
    if (boxL && boxR) {
        // The same local name in both rows, and two different folds.
        utassert(StrEqI(boxL->id, boxR->id));
        utassert(boxL->pathId != boxR->pathId);
        // `Button::new("ellipsis-{start}-{end}").dropdown_menu(..)`: the
        // button carries the name and the dropdown is built around it, so the
        // trigger inside keeps the name it came with rather than being given
        // one. It is still its own hit target, folded under the row.
        El* trigL = nullptr;
        El* trigR = nullptr;
        for (El* c = boxL->first; c && !trigL; c = c->next) {
            trigL = FindNamedPg(c, boxL->id.s);
        }
        for (El* c = boxR->first; c && !trigR; c = c->next) {
            trigR = FindNamedPg(c, boxR->id.s);
        }
        utassert(trigL && trigR);
        if (trigL && trigR) {
            utassert(trigL->clickId != 0 && trigR->clickId != 0);
            utassert(trigL->clickId != trigR->clickId);
        }
    }

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

static bool MenuPagesAre(int hiddenStart, int hiddenEnd, int current,
                         int wantStart, int wantEnd) {
    int start = -1;
    int end = -1;
    component::PaginationEllipsisMenuPages(hiddenStart, hiddenEnd, current,
                                           &start, &end);
    return start == wantStart && end == wantEnd;
}

// pagination.rs test_ellipsis_menu_pages.
static void EllipsisMenuPagesStayNearTheCurrentPage() {
    // Short gaps list every hidden page.
    utassert(MenuPagesAre(2, 8, 1, 2, 8));
    utassert(MenuPagesAre(2, 8, 9, 2, 8));

    // Long gaps keep the pages next to the current page.
    utassert(MenuPagesAre(4, 10000, 1, 4, 104));
    utassert(MenuPagesAre(2, 9997, 10000, 9897, 9997));
    utassert(MenuPagesAre(5003, 10000, 5000, 5003, 5103));
    utassert(MenuPagesAre(2, 4998, 5000, 4898, 4998));
}

void TestPagination() {
    TestSuite("pagination");
    ClampsControlledValuesAndNavigationBoundaries();
    CreatesPagesAndNavigableEllipsisRanges();
    ASinglePageHasNothingToNavigate();
    EveryPageChangeRequestIsValidated();
    AScopeIsWhatMakesALocalNameItsOwn();
    TwoPaginationsHaveTwoEllipsisMenus();
    EllipsisMenuPagesStayNearTheCurrentPage();
}
