/* Nested scroll: a 300px DataTable (own vertical scrollbar) inside a
   scrollable page. Wheel over the table scrolls rows until an edge, then
   the page; wheel outside the table always scrolls the page.
   Rust: examples/table_in_scrollable */

#include "gpui.h"

using namespace gpui;

static const int kRows = 30;
static const float kRowH = 28;
static const float kHeadH = 28;
static const float kTableH = 300;
static const float kAboveH = 400;
static const float kBelowH = 800;
static const float kPagePad = 16;
static const float kPageGap = 16;

struct TableApp {
    static El* Render(TableApp* self, Ctx* cx);
    float pageScroll = 0;
    float tableScroll = 0;
    float viewH = 700;
    float tableTopPage = kPagePad + kAboveH + kPageGap;
};

static float PageContentH() {
    return kPagePad + kAboveH + kPageGap + kTableH + kPageGap + kBelowH +
           kPagePad;
}

static float BodyH() {
    return kTableH - kHeadH;
}

static float RowsH() {
    return kRows * kRowH;
}

static float MaxTableScroll() {
    float m = RowsH() - BodyH();
    return m > 0 ? m : 0;
}

static float MaxPageScroll(float viewH) {
    float m = PageContentH() - viewH;
    return m > 0 ? m : 0;
}

static El* Thumb(Arena* a, float top, float h, Rgba c) {
    return Div(a)->Absolute()->Right(2)->Top(top)->W(6)->H(h)->Radius(3)->Bg(c);
}

static void OnWheel(TableApp* app, Ctx* cx, const ScrollWheelEvent* ev) {
    (void)cx;
    float x = ev->x;
    float y = ev->y;
    float delta = ev->deltaY;
    (void)x;
    float tableTop = app->tableTopPage - app->pageScroll;
    float tableBot = tableTop + kTableH;
    bool overTable = y >= tableTop && y <= tableBot;
    float maxT = MaxTableScroll();
    float maxP = MaxPageScroll(app->viewH);
    if (overTable) {
        float next = app->tableScroll - delta;
        if (next < 0) {
            app->tableScroll = 0;
            if (delta > 0) {
                app->pageScroll -= delta;
            }
        } else if (next > maxT) {
            app->tableScroll = maxT;
            if (delta < 0) {
                app->pageScroll -= delta;
            }
        } else {
            app->tableScroll = next;
        }
    } else {
        app->pageScroll -= delta;
    }
    app->pageScroll = ClampF(app->pageScroll, 0, maxP);
}

static El* Filler(Arena* a, Str label, float h, const Theme& th) {
    return Div(a)
        ->H(h)
        ->W(kFill)
        ->ItemsCenter()
        ->JustifyCenter()
        ->Border(1, th.border)
        ->Dashed()
        ->Child(TextEl(a, label)->Font(14)->Fg(th.mutedFg));
}

El* TableApp::Render(TableApp* app, Ctx* cx) {
    Arena* frame = cx->a;

    WinSize size = WindowSize(cx->win);
    const Theme& th = ThemeNow(cx->app);
    app->viewH = size.dipH;
    app->tableTopPage = kPagePad + kAboveH + kPageGap;

    float maxP = MaxPageScroll(app->viewH);
    if (app->pageScroll > maxP) {
        app->pageScroll = maxP;
    }
    float maxT = MaxTableScroll();
    if (app->tableScroll > maxT) {
        app->tableScroll = maxT;
    }

    const char* heads[] = {"ID", "Name", "Email", "Role", "Status"};
    const float widths[] = {50, 150, 250, 150, 100};

    El* table = Div(frame)
                    ->FlexCol()
                    ->H(kTableH)
                    ->W(kFill)
                    ->Border(1, th.border)
                    ->Radius(th.radius)
                    ->Bg(th.tokens.tableBg);
    El* head = Div(frame)
                   ->FlexRow()
                   ->H(kHeadH)
                   ->Shrink0()
                   ->Bg(th.tokens.muted)
                   ->BorderB(1, th.tableRowBorder);
    for (int i = 0; i < 5; i++) {
        head->Child(
            Div(frame)->W(widths[i])->H(kHeadH)->PadX(8)->ItemsCenter()->Child(
                TextEl(frame, Str(heads[i]))->Font(12)->Fg(th.tableHeadFg)));
    }
    table->Child(head);

    El* body = Div(frame)->FlexCol()->Flex1()->W(kFill)->ClipY()->ScrollY(
        app->tableScroll);
    for (int r = 0; r < kRows; r++) {
        El* row = Div(frame)->FlexRow()->H(kRowH)->Shrink0();
        if (r % 2) {
            row->Bg(th.tokens.tableEven);
        }
        TempStr id = fmt("%d", r);
        TempStr name = fmt("User %d", r);
        TempStr email = fmt("user-%d@mail.com", r);
        const char* cells[] = {id.s, name.s, email.s, "User", "Active"};
        for (int c = 0; c < 5; c++) {
            row->Child(Div(frame)
                           ->W(widths[c])
                           ->H(kRowH)
                           ->PadX(8)
                           ->ItemsCenter()
                           ->Child(TextEl(frame, Str(cells[c]))
                                       ->Font(13)
                                       ->Fg(th.foreground)));
        }
        body->Child(row);
    }
    table->Child(body);

    float bodyH = BodyH();
    float rowsH = RowsH();
    float tThumbH = rowsH > 0 ? bodyH * bodyH / rowsH : bodyH;
    tThumbH = ClampF(tThumbH, 24, bodyH);
    float tThumbY =
        maxT > 0 ? (app->tableScroll / maxT) * (bodyH - tThumbH) : 0;
    table->Child(Thumb(frame, kHeadH + tThumbY, tThumbH,
                       RgbaOpacity(th.mutedFg, 0.55f)));

    El* page = Div(frame)->FlexCol()->Pad(kPagePad)->Gap(kPageGap);
    page->Child(Filler(frame, StrL("Content above the table"), kAboveH, th));
    page->Child(table);
    page->Child(Filler(frame, StrL("Content below the table"), kBelowH, th));

    float viewH = size.dipH;
    float contentH = PageContentH();
    float pThumbH = contentH > 0 ? viewH * viewH / contentH : viewH;
    pThumbH = ClampF(pThumbH, 32, viewH);
    float pThumbY = maxP > 0 ? (app->pageScroll / maxP) * (viewH - pThumbH) : 0;

    El* root = Div(frame)
                   ->SizeFull()
                   ->ClipY()
                   ->ScrollY(app->pageScroll)
                   ->Bg(th.tokens.background);
    root->Child(page);
    root->Child(Thumb(frame, pThumbY, pThumbH, RgbaOpacity(th.mutedFg, 0.45f)));
    return root;
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    Entity<TableApp> view = EntityNew<TableApp>(app);
    TableApp* self = view.Get(app);
    (void)self;
    ThemeSet(app, ThemeMode::Light);
    WinOpts opts = {};
    Window* win = KitOpenWindow(app, StrL("Table in Scrollable C++"), 700, 700,
                                view.id, opts);
    WindowOnScrollWheel(win, ListenTo(view, &OnWheel));
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
