#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static void GoPage(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                   intptr_t page) {
    app->page = (int)page;
    Notify(cx);
}

static El* PageBtn(Ctx* cx, int p, bool on) {
    Arena* a = cx->a;
    El* b = Button::New(cx, DupFmt(cx, "page-%d", p))
                ->OnClick(Listen(cx, &GoPage, p))
                ->W(28)
                ->H(28)
                ->ItemsCenter()
                ->JustifyCenter()
                ->Border(1, ExampleRgb(0xd4d4d4));
    if (on) {
        b->Bg(ExampleRgb(0x171717))
            ->Child(TextEl(a, DupFmt(cx, "%d", p))
                        ->Font(12)
                        ->Fg(ExampleRgb(0xffffff)));
    } else {
        b->HoverBg(ExampleRgb(0xf5f5f5))
            ->Child(TextEl(a, DupFmt(cx, "%d", p))
                        ->Font(12)
                        ->Fg(ExampleRgb(0x171717)));
    }
    return b;
}

El* ShowcasePagination(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    constexpr int n = 8;
    constexpr int maxVis = 5;
    int cur = app->page;
    cur = ClampI(cur, 1, n);
    El* row = Pagination::New(cx, StrL("example-pagination"))
                  ->FlexRow()
                  ->ItemsCenter()
                  ->Gap(8);
    // Both are fixed here, so the compiler is right that this never runs;
    // keep it for the day the page count stops being a constant.
    if constexpr (n <= maxVis) {
        for (int p = 1; p <= n; p++) {
            row->Child(PageBtn(cx, p, p == cur));
        }
        return row;
    }
    int side = (maxVis - 3) / 2;
    int start =
        cur <= side + 1 ? 2 : (cur > n - side - 1 ? n - side - 1 : cur - side);
    int end =
        cur >= n - side ? n - 1 : (cur <= side + 1 ? side + 2 : cur + side);
    row->Child(PageBtn(cx, 1, cur == 1));
    if (start > 2) {
        row->Child(Div(a)->W(20)->H(28)->ItemsCenter()->JustifyCenter()->Child(
            TextEl(a, StrL("…"))->Font(12)->Fg(ExampleRgb(0x171717))));
    }
    for (int p = start; p <= end; p++) {
        row->Child(PageBtn(cx, p, p == cur));
    }
    if (end < n - 1) {
        row->Child(Div(a)->W(20)->H(28)->ItemsCenter()->JustifyCenter()->Child(
            TextEl(a, StrL("…"))->Font(12)->Fg(ExampleRgb(0x171717))));
    }
    row->Child(PageBtn(cx, n, cur == n));
    return row;
}

SHOWCASE_PAGE(CompPagination, ShowcasePagination);
