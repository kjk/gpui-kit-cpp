#include "Story.h"

// The Rust story fills its list with random companies; ours keeps a fixed
// set with the same shape, grouped by industry.
struct ListQuote {
    const char* name;
    const char* price;
    const char* change;
    bool up;
};

struct QuoteSection {
    const char* industry;
    int section; // the index the Rust delegate prints in the header
    int count;   // items_count for it, which the footer prints
    ListQuote rows[4];
};

static const QuoteSection kSections[] = {
    {"Airlines / Aviation",
     1,
     4,
     {{"Daugherty and Sons", "422.23", "54.63%", true},
      {"Jaskolski and Rowe Inc", "958.26", "-14.56%", false},
      {"Windler and Sons", "329.63", "-12.71%", false},
      {"Jaskolski and Rowe Inc", "331.79", "21.49%", true}}},
    {"Automotive",
     4,
     4,
     {{"Walter Group", "137.70", "10.01%", true},
      {"Hilpert Group", "962.30", "-15.44%", false},
      {"Windler and Sons", "613.71", "15.03%", true},
      {"Hilpert Group", "48.07", "42.82%", true}}},
    {"Think Tanks",
     5,
     4,
     {{"Windler and Sons", "352.49", "131.67%", true},
      {"Hills LLC", "768.95", "-726.01%", false},
      {"Muller and Rippin Inc", "512.03", "-805.82%", false},
      {"Hills LLC", "477.77", "-26.30%", false}}},
    {"Internet",
     6,
     2,
     {{"Upton and Auer Sons", "691.74", "12.81%", true},
      {"Hartmann Group", "375.69", "12.45%", true}}},
    {"Machinery",
     7,
     3,
     {{"Hartmann Group", "806.45", "22.73%", true},
      {"Dare Group", "261.88", "-12.83%", false},
      {"Kilback Group", "170.31", "-50.65%", false}}},
};
static const int kSectionCount =
    (int)(sizeof(kSections) / sizeof(kSections[0]));

enum {
    ListMenuGoTo = 1,
    ListMenuOptions
};

enum {
    ListActGoTop = 100,
    ListActGoSelected,
    ListActGoRow,
    ListActGoBottom,
    ListActSelectable,
    ListActSearchable,
    ListActLoading,
    ListActLazyLoad,
    ListActDraggable
};

struct ListStory {
    // ListState is an entity in Rust too, which is what the row closures and
    // cx.subscribe capture.
    Entity<ListState> list = {};
    int confirmedRow = -1;
    int openMenu = 0;
    bool selectable = true;
    bool searchable = true;
    bool loading = false;
    bool lazyLoad = false;
    bool draggable = false;
    InputState search;
    bool seeded = false;

    static El* Render(ListStory* self, Ctx* cx);
};

// cx.subscribe(&list, ..): a Confirm is what the story acts on, and a Cancel
// clears what it was showing.
static void OnListEvent(ListStory* self, Ctx* cx, const ListEvent* ev) {
    if (ev->kind == ListEventKind::Confirm) {
        self->confirmedRow = ev->index;
    } else if (ev->kind == ListEventKind::Cancel) {
        self->confirmedRow = -1;
    }
    Notify(cx);
}

static void ListMenuOpen(ListStory* self, Ctx* cx, const ClickEvent*,
                         intptr_t which) {
    self->openMenu = self->openMenu == (int)which ? 0 : (int)which;
    Notify(cx);
}
static void ListMenuAct(ListStory* self, Ctx* cx, const ClickEvent*,
                        intptr_t act) {
    switch (act) {
        case ListActSelectable:
            self->selectable = !self->selectable;
            break;
        case ListActSearchable:
            self->searchable = !self->searchable;
            break;
        case ListActLoading:
            self->loading = !self->loading;
            break;
        case ListActLazyLoad:
            self->lazyLoad = !self->lazyLoad;
            break;
        case ListActDraggable:
            self->draggable = !self->draggable;
            break;
        default:
            break;
    }
    self->openMenu = 0;
    Notify(cx);
}
static void FocusSearch(ListStory* self, Ctx* cx, const ClickEvent*) {
    self->search.focused = true;
    Notify(cx);
}

// render_section_header / render_section_footer / render_item: the three
// halves of the delegate, over the story's own data.
static El* SectionHeader(Ctx* cx, void*, int section) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const QuoteSection& s = kSections[section];
    // `h_flex().pb_1().px_2().gap_2()`: the padding is below the header, not
    // above it. It only started to show once the list measured a header
    // rather than forcing it to an item's height.
    El* head = Div(a)->FlexRow()->PadX(8)->PadB(4)->Gap(8)->ItemsCenter();
    head->Child(IconEl(a, IconName::Folder, 16)->Fg(th.mutedFg));
    head->Child(StoryTxt(cx, Str(s.industry), 14, th.mutedFg));
    head->Child(
        StoryTxt(cx, StoryFmt(cx, "(section: %d)", s.section), 14, th.mutedFg));
    return head;
}

static El* SectionFooter(Ctx* cx, void*, int section) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    // `div().pt_1().pb_5().px_2()`: the twenty below the footer is the gap
    // between one section and the next, and it belongs to the footer rather
    // than to the header under it.
    return Div(a)->PadX(8)->PadT(4)->PadB(20)->Child(StoryTxt(
        cx,
        StoryFmt(cx, "Total %d items in section.", kSections[section].count),
        12, th.mutedFg));
}

static component::ListItem* RenderQuote(Ctx* cx, void* data, int section,
                                        int row, int entry) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    ListStory* self = (ListStory*)data;
    const ListQuote& r = kSections[section].rows[row];
    El* line =
        Div(a)->FlexRow()->W(kFill)->Gap(8)->ItemsCenter()->JustifyBetween();
    line->Child(StoryTxt(cx, Str(r.name), 16, th.foreground));
    El* right = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->JustifyEnd();
    right->Child(StoryTxt(cx, Str(r.price), 16, th.foreground)->W(65));
    right->Child(Div(a)->FlexRow()->W(65)->JustifyEnd()->Child(
        StoryTxt(cx, Str(r.change), 12, r.up ? th.green : th.red)->PadX(4)));
    line->Child(right);
    // list_story.rs refines the row with `.px_2().py_1().border_1()
    // .rounded(radius)` — the padding is `ListItem`'s own here, and the
    // border is transparent until the selection colours it. It is two of the
    // pixels the list measures the row at, which is what makes it the 36 of
    // upstream's rather than 34.
    StateStyle rowStyle;
    rowStyle.Border(1, Rgba8(0, 0, 0, 0));
    return component::ListItem::New(cx, line)
        ->Style(rowStyle)
        ->Confirmed(self->confirmedRow == entry);
}

El* ListStory::Render(ListStory* self, Ctx* cx) {
    Arena* a = cx->a;
    if (!self->seeded) {
        self->seeded = true;
        InputSetPlaceholder(&self->search, StrL("Search..."));
        self->list = EntityNewState<ListState>(cx->app);
    }
    ListState* st = self->list.Get(cx);
    if (st) {
        st->selectable = self->selectable;
        st->onEvent = Listen(cx, &OnListEvent);
    }
    if (self->search.focused) {
        cx->win->input = &self->search;
    }
    Listener openMenu = Listen(cx, &ListMenuOpen);
    Listener act = Listen(cx, &ListMenuAct);

    // size_full().gap_4(): the list under the toolbar takes the rest.
    El* page = Div(a)->FlexCol()->Gap(16)->W(kFill)->H(kFill);

    // story_toolbar_group() with two dropdowns: Go To and Options.
    El* toolbarRow = Div(a)->FlexRow()->W(kFill)->JustifyEnd()->ItemsStart();
    El* group = StoryToolbarGroup(cx);
    StoryToolbarOpt goTo[4] = {
        {"Top", false, ListActGoTop, true},
        {"Selected", false, ListActGoSelected, true},
        {"Section 5, Row 1", false, ListActGoRow, true},
        {"Bottom", false, ListActGoBottom, true},
    };
    group->Child(StoryToolbarDropdown(
        cx, StrL("list-go-to"), StrL("Go To"), self->openMenu == ListMenuGoTo,
        ListenerArg(openMenu, ListMenuGoTo), goTo, 4, act));
    group->Child(StoryToolbarDivider(cx));
    // The five rows list_story's Options menu has. Active Highlight is not
    // one of them — that setting lives in the gallery's Appearance menu.
    StoryToolbarOpt options[5] = {
        {"Selectable", self->selectable, ListActSelectable},
        {"Searchable", self->searchable, ListActSearchable},
        {"Loading", self->loading, ListActLoading},
        {"Lazy Load", self->lazyLoad, ListActLazyLoad},
        {"Draggable", self->draggable, ListActDraggable},
    };
    group->Child(StoryToolbarDropdown(cx, StrL("list-options"), StrL("Options"),
                                      self->openMenu == ListMenuOptions,
                                      ListenerArg(openMenu, ListMenuOptions),
                                      options, 5, act));
    toolbarRow->Child(group);
    page->Child(toolbarRow);

    // The list is the component now: it owns the rows' identity, the search
    // field, and what a click does to the selection. Only the rows the frame
    // can show are ever built, which is what the delegate is for. It is
    // `flex_1()` under the toolbar and fills what the toolbar leaves.
    if (st) {
        st->loading = self->loading;
    }
    component::List* list =
        component::List::New(cx, StrL("list-story"), self->list)
            ->Padding(8)
            ->Headers(&SectionHeader, &SectionFooter)
            ->Items(self, &RenderQuote);
    int counts[8];
    for (int i = 0; i < kSectionCount; i++) {
        counts[i] = kSections[i].count;
    }
    list->Sections(counts, kSectionCount);
    if (self->searchable) {
        list->Searchable(&self->search, Listen(cx, &FocusSearch));
    }
    El* frame = list->IntoEl()
                    ->Flex1()
                    ->W(kFill)
                    ->Border(1, ThemeNow(cx->app).border)
                    ->Radius(ThemeNow(cx->app).radius);
    page->Child(frame);
    return page;
}

STORY_PAGE(StoryList, ListStory);
