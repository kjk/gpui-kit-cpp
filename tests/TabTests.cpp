/* Ported from crates/ui/src/tab/tab.rs and tab_bar.rs.
 *
 * A variant is its numbers: how tall the tab is, how tall the box inside it
 * is, what it pads by, what the bar puts between two of them and how round
 * either is. Rust writes each of those as a match on the variant and the size;
 * this is the same table read back. */

#include "Test.h"

using namespace gpui::component;

static void UnderlineIsTallerThanEveryOtherVariant() {
    // The four sizes, and the one variant that stands apart from the rest.
    utassertnear(TabHeight(TabVariant::Tab, UiSize::XSmall), 20.f);
    utassertnear(TabHeight(TabVariant::Underline, UiSize::XSmall), 26.f);
    utassertnear(TabHeight(TabVariant::Pill, UiSize::Small), 24.f);
    utassertnear(TabHeight(TabVariant::Underline, UiSize::Small), 30.f);
    utassertnear(TabHeight(TabVariant::Segmented, UiSize::Medium), 32.f);
    utassertnear(TabHeight(TabVariant::Underline, UiSize::Medium), 36.f);
    utassertnear(TabHeight(TabVariant::Outline, UiSize::Large), 36.f);
    utassertnear(TabHeight(TabVariant::Underline, UiSize::Large), 44.f);
}

static void TheInnerBoxIsShorterThanTheTab() {
    // Medium is the one row where all five differ.
    utassertnear(TabInnerHeight(TabVariant::Tab, UiSize::Medium), 30.f);
    utassertnear(TabInnerHeight(TabVariant::Outline, UiSize::Medium), 26.f);
    utassertnear(TabInnerHeight(TabVariant::Pill, UiSize::Medium), 26.f);
    utassertnear(TabInnerHeight(TabVariant::Segmented, UiSize::Medium), 24.f);
    utassertnear(TabInnerHeight(TabVariant::Underline, UiSize::Medium), 26.f);
    // Segmented is the short one at every size; the boxed three share theirs.
    utassertnear(TabInnerHeight(TabVariant::Segmented, UiSize::XSmall), 16.f);
    utassertnear(TabInnerHeight(TabVariant::Tab, UiSize::XSmall), 18.f);
    utassertnear(TabInnerHeight(TabVariant::Underline, UiSize::XSmall), 20.f);
    utassertnear(TabInnerHeight(TabVariant::Pill, UiSize::Large), 36.f);
    utassertnear(TabInnerHeight(TabVariant::Segmented, UiSize::Large), 28.f);
}

static void UnderlinePadsFromTheBarInstead() {
    // Every other variant pads its own label...
    utassertnear(TabPadX(TabVariant::Tab, UiSize::Medium), 12.f);
    utassertnear(TabPadX(TabVariant::Pill, UiSize::XSmall), 8.f);
    utassertnear(TabPadX(TabVariant::Outline, UiSize::Large), 16.f);
    // ...but the underline has none, and the bar's gap does that job with the
    // same numbers.
    utassertnear(TabPadX(TabVariant::Underline, UiSize::Medium), 0.f);
    utassertnear(TabBarGap(TabVariant::Underline, UiSize::Medium), 16.f);
    utassertnear(TabBarGap(TabVariant::Underline, UiSize::XSmall), 10.f);
    // And it is the only variant with a margin above and below its box.
    utassertnear(TabMarginTop(TabVariant::Underline, UiSize::Medium), 3.f);
    utassertnear(TabMarginBottom(TabVariant::Underline, UiSize::Medium), 4.f);
    utassertnear(TabMarginTop(TabVariant::Tab, UiSize::Medium), 0.f);
}

static void OnlyTheStripsThatNeedGapsHaveThem() {
    // Folder tabs sit against each other; the segmented ones are two apart
    // and the pills four; outline takes the default gap for its size.
    utassertnear(TabBarGap(TabVariant::Tab, UiSize::Medium), 0.f);
    utassertnear(TabBarGap(TabVariant::Segmented, UiSize::Medium), 2.f);
    utassertnear(TabBarGap(TabVariant::Pill, UiSize::Medium), 4.f);
    utassertnear(TabBarGap(TabVariant::Outline, UiSize::Medium), 12.f);
    utassertnear(TabBarGap(TabVariant::Outline, UiSize::Small), 8.f);
    utassertnear(TabBarGap(TabVariant::Outline, UiSize::Large), 16.f);
    // Only the segmented bar pads itself, because only it has a background
    // the tabs sit inside.
    utassertnear(TabBarPadX(TabVariant::Segmented, UiSize::XSmall), 2.f);
    utassertnear(TabBarPadX(TabVariant::Segmented, UiSize::Small), 3.f);
    utassertnear(TabBarPadX(TabVariant::Segmented, UiSize::Large), 4.f);
    utassertnear(TabBarPadX(TabVariant::Tab, UiSize::Medium), 0.f);
}

static void OnlySegmentedRoundsItsBar() {
    const float r = 6, rlg = 8;
    utassertnear(TabBarRadius(TabVariant::Tab, UiSize::Medium, r, rlg), 0.f);
    utassertnear(TabBarRadius(TabVariant::Segmented, UiSize::Small, r, rlg), r);
    utassertnear(TabBarRadius(TabVariant::Segmented, UiSize::Medium, r, rlg),
                 rlg);
    // A pill and an outline tab are fully round whatever the theme says.
    utassertnear(TabRadius(TabVariant::Pill, UiSize::Medium, r, rlg), 99.f);
    utassertnear(TabRadius(TabVariant::Outline, UiSize::Medium, r, rlg), 99.f);
    utassertnear(TabRadius(TabVariant::Tab, UiSize::Medium, r, rlg), 0.f);
    // The chip inside a segmented bar is rounder by the padding around it —
    // two, or three at the large size.
    utassertnear(TabInnerRadius(TabVariant::Segmented, UiSize::Medium, r, rlg),
                 rlg - 2);
    utassertnear(TabInnerRadius(TabVariant::Segmented, UiSize::Large, r, rlg),
                 rlg - 3);
    utassertnear(TabInnerRadius(TabVariant::Pill, UiSize::Medium, r, rlg), 0.f);
}

static El* FindNamedTab(El* root, const char* name) {
    if (!root) {
        return nullptr;
    }
    if (root->id.s && base::StrEqI(root->id, name)) {
        return root;
    }
    for (El* c = root->first; c; c = c->next) {
        if (El* hit = FindNamedTab(c, name)) {
            return hit;
        }
    }
    return nullptr;
}

// The overflow menu is `more`, `more-btn` and `menu` in every bar, and the
// bar's own name over them is what keeps two strips' menus apart -- both the
// element and the PopupMenuState behind it, which is why the name is pushed
// on the id stack and not only onto the tree.
static void TwoBarsHaveTwoOverflowMenus() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    El* page = Div(a);
    El* left = component::Tabs::New(&cx, StrL("left"))
                   ->Menu()
                   ->Tab(StrL("One"))
                   ->Tab(StrL("Two"))
                   ->IntoEl();
    uint32_t leftPath = cx.path;
    El* right = component::Tabs::New(&cx, StrL("right"))
                    ->Menu()
                    ->Tab(StrL("One"))
                    ->Tab(StrL("Two"))
                    ->IntoEl();
    // The scope is off the stack again once a bar is built.
    utassert(leftPath == 0 && cx.path == 0);
    page->Child(left)->Child(right);
    IdsCollect(page);

    // The dropdown takes the trigger as it is, so the name the bar asked
    // for is the one in the tree.
    El* btnL = FindNamedTab(left, "more-btn");
    El* btnR = FindNamedTab(right, "more-btn");
    utassert(btnL && btnR);
    if (btnL && btnR) {
        utassert(btnL->clickId != 0 && btnR->clickId != 0);
        utassert(btnL->clickId != btnR->clickId);
    }

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

namespace {
struct TabCallbackRecorder {
    int child = 0;
    int group = 0;
    int scroll = 0;

    static void Child(TabCallbackRecorder* self, Ctx*, const ClickEvent*) {
        self->child++;
    }
    static void Group(TabCallbackRecorder* self, Ctx*, const ClickEvent*,
                      intptr_t ix) {
        self->group = (int)ix + 1;
    }
    static void Scroll(TabCallbackRecorder* self, Ctx*, const ScrollEvent*) {
        self->scroll++;
    }
};
} // namespace

static void SourceNamedTabAndTabBarKeepContentAndCallbackRules() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Entity<TabCallbackRecorder> recorder =
        EntityNewState<TabCallbackRecorder>(&app);
    Ctx cx = {&app, win, a, recorder.id};

    El* prefix = Div(a)->Id(StrL("tab-prefix"));
    El* content = Div(a)->Id(StrL("tab-content"));
    El* suffix = Div(a)->Id(StrL("tab-suffix"));
    component::Tab* item =
        component::Tab::New(&cx)
            ->Label(StrL("First"))
            ->AriaLabel(StrL("First accessible"))
            ->Prefix(prefix)
            ->Child(content)
            ->Suffix(suffix)
            ->OnClick(Listen(&cx, &TabCallbackRecorder::Child));
    TabBar* bar = TabBar::New(&cx, StrL("source-tabs"))
                      ->Child(item)
                      ->OnClick(Listen(&cx, &TabCallbackRecorder::Group));
    El* root = bar->IntoEl();
    El* tab = FindNamedTab(root, "0");
    utassert(tab != nullptr);
    utassert(tab &&
             base::StrEq(tab->accessibility.label, StrL("First accessible")));
    El* prefixWrap = tab ? tab->first : nullptr;
    El* inner = prefixWrap ? prefixWrap->next : nullptr;
    El* suffixWrap = inner ? inner->next : nullptr;
    utassert(prefixWrap && prefixWrap->first == prefix);
    utassert(inner && inner->last == content);
    utassert(suffixWrap && suffixWrap->first == suffix);
    utassert(!suffixWrap || suffixWrap->next == nullptr);

    ClickEvent click = {};
    ListenerCall(&app, win, tab->listener, &click);
    TabCallbackRecorder* seen = recorder.Get(&app);
    utassert(seen && seen->child == 0 && seen->group == 1);

    TabBar* childOnly = TabBar::New(&cx, StrL("child-only"))->Child(item);
    El* childTab = FindNamedTab(childOnly->IntoEl(), "0");
    ListenerCall(&app, win, childTab->listener, &click);
    utassert(seen && seen->child == 1 && seen->group == 1);

    item->Disabled();
    El* disabled =
        FindNamedTab(TabBar::New(&cx, StrL("disabled"))
                         ->Child(item)
                         ->OnClick(Listen(&cx, &TabCallbackRecorder::Group))
                         ->IntoEl(),
                     "0");
    utassert(disabled && !disabled->listener.IsValid());

    WindowMotionFree(win);
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

static void TabBarRetainsStyleSpacingScrollAndUnboundedChildren() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Entity<TabCallbackRecorder> recorder =
        EntityNewState<TabCallbackRecorder>(&app);
    Ctx cx = {&app, win, a, recorder.id};

    component::Tab* item =
        component::Tab::New(&cx, StrL("Many"))->MaxWidth(90)->Selected();
    for (int i = 0; i < 40; i++) {
        item->Child(Div(a));
    }
    Style refinement = {};
    refinement.opacity = 0.5f;
    El* empty = Div(a)->Id(StrL("last-empty"))->W(19);
    TabBar* bar =
        TabBar::New(&cx, StrL("complete-tabs"))
            ->Child(item)
            ->Pill()
            ->Menu()
            ->LastEmptySpace(empty)
            ->TrackScroll(77, 15, Listen(&cx, &TabCallbackRecorder::Scroll))
            ->Refine(refinement, StyleFieldOpacity);
    El* root = bar->IntoEl();
    utassert(root->StyleStates()->refineSet == StyleFieldOpacity);
    El* strip = FindNamedTab(root, "tabs-inner");
    utassert(strip != nullptr);
    utassert(strip && strip->style.overflowX == Overflow::Scroll);
    utassertnear(strip ? strip->scrollX : 0, 15.f);
    utassert(strip && strip->scrollId == 77);
    utassert(strip && strip->onScroll.IsValid());
    utassert(strip && strip->last == empty);

    El* rendered = FindNamedTab(root, "0");
    utassert(rendered && rendered->accessibility.selected);
    El* inner = rendered ? rendered->first : nullptr;
    int innerChildren = 0;
    for (El* child = inner ? inner->first : nullptr; child;
         child = child->next) {
        innerChildren++;
    }
    // Visible label followed by every arbitrary child.
    utassert(innerChildren == 41);

    component::Tab* standalone =
        component::Tab::New(&cx, StrL("Standalone"))->Outline()->Selected();
    El* standaloneEl = standalone->IntoEl();
    utassert(standaloneEl && base::StrEq(standaloneEl->id, StrL("0")));
    utassert(standaloneEl && standaloneEl->accessibility.selected);

    WindowMotionFree(win);
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

static void SegmentedShadowFitsInsideExpandedClips() {
    App app;
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};

    El* root = TabBar::New(&cx, StrL("segmented-shadow"))
                   ->Segmented()
                   ->Selected(0)
                   ->Tab(StrL("One"))
                   ->IntoEl();
    El* viewport = FindNamedTab(root, "tabs");
    El* strip = FindNamedTab(root, "tabs-inner");
    utassert(viewport && strip);
    utassertnear(viewport ? viewport->style.margin.left : 0, -4.f);
    utassertnear(viewport ? viewport->style.pad.left : 0, 4.f);
    utassertnear(strip ? strip->style.margin.left : 0, -4.f);
    utassertnear(strip ? strip->style.pad.left : 0, 4.f);

    El* tab = FindNamedTab(root, "0");
    El* inner = tab ? tab->first : nullptr;
    utassert(inner && inner->style.shadowCount == 2);
    if (inner && inner->style.shadowCount == 2) {
        utassertnear(inner->style.shadows[0].blur, 1.5f);
        utassertnear(inner->style.shadows[1].blur, 1.f);
        utassertnear(inner->style.shadows[1].spread, -1.f);
    }

    WindowMotionFree(win);
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

static void FlexTabsGrowForEveryVariant() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    const TabVariant variants[] = {
        TabVariant::Tab,  TabVariant::Outline,   TabVariant::Segmented,
        TabVariant::Pill, TabVariant::Underline,
    };
    for (const TabVariant variant : variants) {
        TabBar* bar = TabBar::New(&cx, StrL("flex-tabs"))->Variant(variant);
        bar->Tab(StrL("A"))->Flex1();
        bar->Tab(StrL("B"))->Flex1();
        El* root = bar->IntoEl();
        El* first = FindNamedTab(root, "0");
        El* second = FindNamedTab(root, "1");
        utassert(first && second);
        utassert(first && first->style.flexGrow == 1.f &&
                 first->style.flexShrink == 1.f &&
                 first->style.flexBasis == 0.f);
        utassert(second && second->style.flexGrow == 1.f &&
                 second->style.flexShrink == 1.f &&
                 second->style.flexBasis == 0.f);
    }

    WindowMotionFree(win);
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

void TestTab() {
    TestSuite("tab");
    UnderlineIsTallerThanEveryOtherVariant();
    TheInnerBoxIsShorterThanTheTab();
    UnderlinePadsFromTheBarInstead();
    OnlyTheStripsThatNeedGapsHaveThem();
    OnlySegmentedRoundsItsBar();
    TwoBarsHaveTwoOverflowMenus();
    SourceNamedTabAndTabBarKeepContentAndCallbackRules();
    TabBarRetainsStyleSpacingScrollAndUnboundedChildren();
    SegmentedShadowFitsInsideExpandedClips();
    FlexTabsGrowForEveryVariant();
}
