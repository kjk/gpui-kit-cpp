#include "Story.h"

struct SeparatorStory {
    static El* Render(SeparatorStory* self, Ctx* cx);
};

El* SeparatorStory::Render(SeparatorStory*, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);

    El* h = StorySection(
        cx, "Horizontal",
        "Separates stacked content, with optional labels and dashed rules.");
    StorySectionBody(h)->W(520);
    El* col = Div(a)->FlexCol()->Gap(16)->W(kFill)->PadT(16);
    col->Child(component::Separator::Horizontal(cx)->IntoEl());
    col->Child(component::Separator::Horizontal(cx)
                   ->Label(StrL("With Label"))
                   ->IntoEl());
    col->Child(component::Separator::Horizontal(cx)->Dashed()->IntoEl());
    col->Child(component::Separator::Horizontal(cx)
                   ->Dashed()
                   ->Label(StrL("Dashed With Label"))
                   ->IntoEl());
    StorySectionAdd(h, col);
    page->Child(h);

    El* v = StorySection(cx, "Vertical",
                         "Separates actions or values arranged in a row.");
    StorySectionBody(v)->W(520);
    El* row = Div(a)->FlexRow()->Gap(16)->H(100)->ItemsCenter();
    row->Child(component::Separator::Vertical(cx)->IntoEl());
    row->Child(
        component::Separator::Vertical(cx)->Label(StrL("Solid"))->IntoEl());
    row->Child(component::Separator::Vertical(cx)->Dashed()->IntoEl());
    row->Child(component::Separator::Vertical(cx)
                   ->Dashed()
                   ->Label(StrL("Dashed"))
                   ->IntoEl());
    StorySectionAdd(v, row);
    page->Child(v);

    El* ctx = StorySection(
        cx, "In Context",
        "Horizontal and vertical rules can structure compact content.");
    StorySectionBody(ctx)->W(520);
    El* box = Div(a)->FlexCol()->GapY(16)->W(kFill);
    El* head = Div(a)->FlexCol()->GapY(8)->W(kFill);
    head->Child(StoryTxt(cx, StrL("Hello GPUI Component"), 16, th.foreground));
    head->Child(
        StoryTxt(cx,
                 StrL("GPUI Component is a Rust GUI components for building "
                      "fantastic cross-platform desktop application by using "
                      "GPUI."),
                 14, th.mutedFg)
            ->Wrap()
            ->W(kFill));
    box->Child(head);
    box->Child(component::Separator::Horizontal(cx)->IntoEl());
    El* links = Div(a)->FlexRow()->GapX(16)->ItemsCenter();
    links->Child(StoryTxt(cx, StrL("Docs"), 14, th.foreground));
    links->Child(component::Separator::Vertical(cx)->Dashed()->IntoEl());
    links->Child(StoryTxt(cx, StrL("GitHub"), 14, th.foreground));
    links->Child(component::Separator::Vertical(cx)->Dashed()->IntoEl());
    links->Child(StoryTxt(cx, StrL("Source"), 14, th.foreground));
    box->Child(links);
    StorySectionAdd(ctx, box);
    page->Child(ctx);
    return page;
}

STORY_PAGE(StorySeparator, SeparatorStory);
