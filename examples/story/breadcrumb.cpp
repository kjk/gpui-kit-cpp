#include "Story.h"

// The names the interactive trail is built from. Each Rust closure captures
// its own string; here the listener carries the index into this table.
static const char* kCrumbs[] = {"Home", "Documents", "Projects", "Current"};

struct BreadcrumbStory {
    int clickedItem = -1;
    static El* Render(BreadcrumbStory* self, Ctx* cx);
};

static void OnCrumb(BreadcrumbStory* self, Ctx* cx, const ClickEvent*,
                    int64_t i) {
    self->clickedItem = (int)i;
    Notify(cx);
}

El* BreadcrumbStory::Render(BreadcrumbStory* self, Ctx* cx) {
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);

    El* def = StorySection(cx, "Default",
                           "Shows the current location in a hierarchy.");
    StorySectionBody(def)->W(480);
    StorySectionAdd(def, component::Breadcrumb::New(cx)
                             ->Child(StrL("Home"))
                             ->Child(StrL("Documents"))
                             ->Child(StrL("Projects"))
                             ->IntoEl());
    page->Child(def);

    El* inter = StorySection(
        cx, "Interactive", "Earlier levels can respond to navigation clicks.");
    StorySectionBody(inter)->W(480);
    El* col = Div(a)->FlexCol()->Gap(16)->ItemsCenter();
    // "Home" is a plain level: it names itself and takes no click.
    component::Breadcrumb* trail = component::Breadcrumb::New(cx)
                                       ->Child(Str(kCrumbs[0]));
    for (int i = 1; i < 4; i++) {
        trail->Child(component::BreadcrumbItem::New(cx, Str(kCrumbs[i]))
                         ->OnClick(Listen(cx, &OnCrumb, i)));
    }
    col->Child(trail->IntoEl());
    if (self->clickedItem >= 0) {
        // format!("Selected: {}") as a plain child: it takes the page's own
        // text size and color.
        col->Child(TextEl(
            a, StoryFmt(cx, "Selected: %s", kCrumbs[self->clickedItem])));
    }
    StorySectionAdd(inter, col);
    page->Child(inter);
    return page;
}

STORY_PAGE(StoryBreadcrumb, BreadcrumbStory);
