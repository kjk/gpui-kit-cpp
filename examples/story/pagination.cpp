#include "Story.h"

struct PaginationStory {
    int page = 5;
    int pageMany = 1;
    int pageCompact = 3;
    StoryToolbarState toolbar;

    static El* Render(PaginationStory* self, Ctx* cx);
};

static void SetPage(PaginationStory* self, Ctx* cx, const ClickEvent*,
                    int64_t p) {
    self->page = (int)p;
    Notify(cx);
}
static void SetPageMany(PaginationStory* self, Ctx* cx, const ClickEvent*,
                        int64_t p) {
    self->pageMany = (int)p;
    Notify(cx);
}
static void SetPageCompact(PaginationStory* self, Ctx* cx, const ClickEvent*,
                           int64_t p) {
    self->pageCompact = (int)p;
    Notify(cx);
}

El* PaginationStory::Render(PaginationStory* self, Ctx* cx) {
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    El* def = StorySection(cx, "Default", nullptr);
    StorySectionAdd(def, component::Pagination::New(cx, self->page, 10)
                             ->Id(StrL("basic-pagination"))
                             ->WithSize(self->toolbar.size)
                             ->OnChange(Listen(cx, &SetPage))
                             ->IntoEl());
    page->Child(def);

    El* many = StorySection(
        cx, "Visible Pages",
        "Control how many page links remain visible in a larger result set.");
    StorySectionAdd(many, component::Pagination::New(cx, self->pageMany, 50)
                              ->Id(StrL("many-pages-pagination"))
                              ->VisiblePages(10)
                              ->WithSize(self->toolbar.size)
                              ->OnChange(Listen(cx, &SetPageMany))
                              ->IntoEl());
    page->Child(many);

    El* compact = StorySection(cx, "Compact Style", nullptr);
    StorySectionAdd(compact,
                    component::Pagination::New(cx, self->pageCompact, 10)
                        ->Id(StrL("compact-pagination"))
                        ->Compact()
                        ->WithSize(self->toolbar.size)
                        ->OnChange(Listen(cx, &SetPageCompact))
                        ->IntoEl());
    page->Child(compact);

    El* dis = StorySection(cx, "Disabled", nullptr);
    StorySectionAdd(dis, component::Pagination::New(cx, 4, 10)
                             ->Id(StrL("disabled-pagination"))
                             ->WithSize(self->toolbar.size)
                             ->Disabled(true)
                             ->IntoEl());
    page->Child(dis);
    return page;
}

STORY_PAGE(StoryPagination, PaginationStory);
