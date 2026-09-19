#include "Story.h"

// crates/story has no searchable-list page at the pinned SHA — the delegate
// is only ever seen through a Select or a ComboBox. This page is a demo of
// its own: the machinery behind both, on its own, with a query, items in
// sections, and one or many of them picked.
static const Str kSections[] = {StrL("Fruit"), StrL("Vegetable")};

static const component::SearchableItem kItems[] = {
    {StrL("Apple"), StrL("apple"), 0, false},
    {StrL("Banana"), StrL("banana"), 0, false},
    {StrL("Blueberry"), StrL("blueberry"), 0, false},
    {StrL("Cherry"), StrL("cherry"), 0, true},
    {StrL("Grapes"), StrL("grapes"), 0, false},
    {StrL("Carrot"), StrL("carrot"), 1, false},
    {StrL("Cabbage"), StrL("cabbage"), 1, false},
    {StrL("Potato"), StrL("potato"), 1, false},
};
static const int kNItems = (int)(sizeof(kItems) / sizeof(kItems[0]));

struct SearchableListStory {
    Entity<component::SearchableListState> single = {};
    Entity<component::SearchableListState> multi = {};
    InputState singleQuery;
    InputState multiQuery;
    bool seeded = false;

    static El* Render(SearchableListStory* self, Ctx* cx);
};

// on_confirm: the list has already applied the change; this is only what the
// page does about it.
static void OnPicked(SearchableListStory*, Ctx* cx, const ListEvent*) {
    Notify(cx);
}
static void FocusSingleQuery(SearchableListStory* self, Ctx* cx,
                             const ClickEvent*) {
    self->singleQuery.focused = true;
    self->multiQuery.focused = false;
    Notify(cx);
}
static void FocusMultiQuery(SearchableListStory* self, Ctx* cx,
                            const ClickEvent*) {
    self->multiQuery.focused = true;
    self->singleQuery.focused = false;
    Notify(cx);
}

static El* SelectionLine(Ctx* cx, component::SearchableListState* s) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->FlexWrap();
    if (!s || s->selected.len == 0) {
        row->Child(StoryTxt(cx, StrL("Nothing selected."), 14, th.mutedFg));
        return row;
    }
    for (int i = 0; i < s->selected.len; i++) {
        int ix = s->selected[i];
        if (ix < 0 || ix >= kNItems) {
            continue;
        }
        row->Child(component::Tag::New(cx, kItems[ix].title)->IntoEl());
    }
    return row;
}

El* SearchableListStory::Render(SearchableListStory* self, Ctx* cx) {
    Arena* a = cx->a;
    if (!self->seeded) {
        self->seeded = true;
        InputSetPlaceholder(&self->singleQuery, StrL("Search..."));
        InputSetPlaceholder(&self->multiQuery, StrL("Search..."));
        self->single = EntityNewState<component::SearchableListState>(cx->app);
        self->multi = EntityNewState<component::SearchableListState>(cx->app);
        component::SearchableListState* m = self->multi.Get(cx);
        if (m) {
            m->mode = component::SearchableListMode::Multi;
            m->closeOnSelect = false;
        }
    }
    if (self->singleQuery.focused) {
        cx->win->input = &self->singleQuery;
    } else if (self->multiQuery.focused) {
        cx->win->input = &self->multiQuery;
    }
    component::SearchableListState* single = self->single.Get(cx);
    component::SearchableListState* multi = self->multi.Get(cx);
    if (single) {
        single->onChange = Listen(cx, &OnPicked);
    }
    if (multi) {
        multi->onChange = Listen(cx, &OnPicked);
    }

    El* page = Div(a)->FlexCol()->Gap(16)->W(kFill);

    El* one = StorySection(cx, "Single",
                           "One value at a time: picking a row replaces "
                           "whatever was selected.");
    El* col = Div(a)->FlexCol()->Gap(12);
    col->Child(component::SearchableList::New(cx, StrL("sl-single"),
                                              self->single, &self->singleQuery)
                   ->Items(kItems, kNItems)
                   ->Sections(kSections, 2)
                   ->OnQueryFocus(Listen(cx, &FocusSingleQuery))
                   ->W(280)
                   ->IntoEl());
    col->Child(SelectionLine(cx, single));
    StorySectionAdd(one, col);
    page->Child(one);

    El* many = StorySection(cx, "Multiple",
                            "Multi mode toggles the row that was clicked and "
                            "leaves the rest alone.");
    El* col2 = Div(a)->FlexCol()->Gap(12);
    col2->Child(component::SearchableList::New(cx, StrL("sl-multi"),
                                               self->multi, &self->multiQuery)
                    ->Items(kItems, kNItems)
                    ->Sections(kSections, 2)
                    ->OnQueryFocus(Listen(cx, &FocusMultiQuery))
                    ->W(280)
                    ->IntoEl());
    col2->Child(SelectionLine(cx, multi));
    StorySectionAdd(many, col2);
    page->Child(many);
    return page;
}

// The searchable list remains available as a standalone implementation; the
// pinned Rust gallery does not expose it as a separate story page.
