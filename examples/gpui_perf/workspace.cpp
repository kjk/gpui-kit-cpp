#include "perf.h"

namespace gpui {

void HiddenPanel::OnFeed(HiddenPanel* self, Ctx*, const EntityId*) {
    if (self) {
        self->seen++;
    }
}

static void FeedInit(Feed* feed) {
    if (!feed || feed->ready) {
        return;
    }
    for (int ix = 0; ix < kPerfSymbols; ix++) {
        float price = 20.f + (float)((ix * 13) % 80);
        feed->last[ix] = price;
        feed->volume[ix] = (uint32_t)(1000 + ix * 17);
        for (int p = 0; p < kPerfSpark; p++) {
            feed->spark[ix * kPerfSpark + p] =
                price + (float)((p * 3 + ix) % 7) - 3.f;
        }
    }
    for (int c = 0; c < 120; c++) {
        feed->candles[c] = 40.f + (float)((c * 5) % 30);
    }
    feed->ready = true;
}

void WorkspacePrepare(App* app, Workspace* w) {
    if (!app || !w || w->ready) {
        return;
    }
    w->feed = EntityNewState<Feed>(app);
    FeedInit(w->feed.Get(app));
    w->list = EntityNew<Watchlist>(app);
    w->list.Get(app)->feed = w->feed;
    w->panels = EntityNew<Panels>(app);
    w->panels.Get(app)->feed = w->feed;
    for (int i = 0; i < kPerfHidden; i++) {
        w->hidden[i] = EntityNewState<HiddenPanel>(app);
        ObserveTo(app, w->feed, w->hidden[i], &HiddenPanel::OnFeed);
    }
    w->ready = true;
}

void WorkspaceTick(App* app, Window* win, Workspace* w) {
    if (!w || !w->streaming || !w->feed.IsValid()) {
        return;
    }
    Feed* feed = w->feed.Get(app);
    if (!feed) {
        return;
    }
    FeedInit(feed);
    int n = w->quotesPerTick;
    if (n < 1) {
        n = 1;
    }
    for (int i = 0; i < n; i++) {
        int ix = (feed->tick + i) % kPerfSymbols;
        float next = feed->last[ix] + (float)(((ix * 3 + feed->tick) % 7) - 3);
        if (next < 1.f) {
            next = 1.f;
        }
        feed->last[ix] = next;
        feed->volume[ix] += (uint32_t)(10 + (ix % 20));
        float* spark = &feed->spark[ix * kPerfSpark];
        for (int p = 0; p < kPerfSpark - 1; p++) {
            spark[p] = spark[p + 1];
        }
        spark[kPerfSpark - 1] = next;
    }
    feed->tick++;
    if ((feed->tick % 2) == 0) {
        feed->panelSymbol = (feed->panelSymbol + 1) % kPerfSymbols;
    }
    if ((feed->tick % 40) == 0) {
        for (int c = 0; c < 119; c++) {
            feed->candles[c] = feed->candles[c + 1];
        }
        feed->candles[119] = feed->last[feed->panelSymbol];
    }
    NotifyEntity(app, w->feed.id, win);
}

void Watchlist::OnScroll(Watchlist* self, Ctx* cx, const ScrollEvent* ev) {
    self->handle.offset = ev->offsetY;
    Notify(cx);
}

static void PaintSpark(PaintCtx* ctx, El* e, void* user) {
    const float* pts = (const float*)user;
    if (!ctx || !e || !pts) {
        return;
    }
    float lo = pts[0];
    float hi = pts[0];
    for (int i = 1; i < kPerfSpark; i++) {
        if (pts[i] < lo) {
            lo = pts[i];
        }
        if (pts[i] > hi) {
            hi = pts[i];
        }
    }
    float span = hi - lo;
    if (span < 0.01f) {
        span = 1.f;
    }
    float left = e->x;
    float top = e->y;
    float w = e->w > 1.f ? e->w : 1.f;
    float h = e->h > 1.f ? e->h : 1.f;
    Rgba color = Rgb(0x16, 0xa3, 0x4a);
    for (int i = 1; i < kPerfSpark; i++) {
        float x0 = left + w * (float)(i - 1) / (float)(kPerfSpark - 1);
        float x1 = left + w * (float)i / (float)(kPerfSpark - 1);
        float y0 = top + h - (pts[i - 1] - lo) / span * (h - 2.f);
        float y1 = top + h - (pts[i] - lo) / span * (h - 2.f);
        CanvasLine(ctx, x0, y0, x1, y1, 1.f, color);
    }
}

static Str DupStr(Ctx* cx, TempStr text) {
    return StrDup(cx->a, text);
}

static El* WatchCell(Ctx* cx, float width, Str text, Rgba color) {
    El* cell = Div(cx->a)->H(kPerfRowH)->ItemsCenter()->PadX(4)->Shrink0();
    if (width > 0) {
        cell->W(width);
    } else {
        cell->Flex1()->MinW(64);
    }
    cell->Child(TextEl(cx->a, text)->Font(11)->Fg(color)->Truncate());
    return cell;
}

static El* WatchRow(void* user, Ctx* cx, int ix) {
    Watchlist* list = (Watchlist*)user;
    Feed* feed = list->feed.Get(cx);
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    float price = feed ? feed->last[ix] : 0;
    uint32_t volume = feed ? feed->volume[ix] : 0;
    int first = 0;
    if (list->handle.offset > 0) {
        first = (int)(list->handle.offset / kPerfRowH);
    }
    El* row =
        Div(cx->a)->FlexRow()->W(kFill)->H(kPerfRowH)->ItemsCenter()->BorderB(
            1, th.border);
    if ((ix - first) == list->hover) {
        row->Bg(Rgb(0xee, 0xf2, 0xff));
    } else if (ix % 2 == 1) {
        row->Bg(Rgb(0xf8, 0xf8, 0xf8));
    }
    row->Child(
        WatchCell(cx, 28, Str(ix % 3 == 0 ? "US" : "HK"), th.mutedForeground));
    row->Child(WatchCell(cx, 64, DupStr(cx, fmt("S%03d", ix)), th.foreground));
    row->Child(WatchCell(cx, 0, DupStr(cx, fmt("Name %d", ix)), th.foreground));
    row->Child(
        WatchCell(cx, 56, DupStr(cx, fmt("%.1f", price)), th.foreground));
    row->Child(WatchCell(cx, 48,
                         DupStr(cx, fmt("%+.1f", (float)((ix % 9) - 4))),
                         th.foreground));
    row->Child(
        WatchCell(cx, 56, DupStr(cx, fmt("%u", volume)), th.mutedForeground));
    row->Child(WatchCell(cx, 48, Str(ix % 2 ? "Buy" : "Sell"), th.foreground));
    row->Child(
        WatchCell(cx, 36, Str(ix % 5 == 0 ? "HOT" : "OK"), th.mutedForeground));
    El* range = Div(cx->a)->W(48)->H(6)->Radius(3)->Bg(th.border)->Shrink0();
    range->Child(
        Div(cx->a)->H(6)->W(12.f + (float)(ix % 30))->Bg(th.foreground));
    row->Child(range);
    El* spark = Div(cx->a)->W(64)->H(20)->Shrink0();
    spark->customPaint = &PaintSpark;
    spark->customUser = feed ? &feed->spark[ix * kPerfSpark] : nullptr;
    row->Child(spark);
    row->Child(WatchCell(cx, 40, DupStr(cx, fmt("%d", 10 + ix % 50)),
                         th.mutedForeground));
    row->Child(WatchCell(cx, 40, DupStr(cx, fmt("%d", 20 + ix % 40)),
                         th.mutedForeground));
    row->Child(WatchCell(cx, 36, StrL("m"), th.mutedForeground));
    row->Child(WatchCell(cx, 36, StrL("*"), th.mutedForeground));
    row->Child(
        WatchCell(cx, 44, DupStr(cx, fmt("%d", ix % 100)), th.mutedForeground));
    return row;
}

El* Watchlist::Render(Watchlist* self, Ctx* cx) {
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    VirtualListOpts o;
    o.count = kPerfSymbols;
    o.rowH = kPerfRowH;
    o.handle = &self->handle;
    o.scrollY = self->handle.offset;
    o.scrollId = HashClickId(StrL("perf-watch"));
    o.onScroll = Listen(cx, &Watchlist::OnScroll);
    o.axis = ScrollAxis::Vertical;
    o.row = &WatchRow;
    o.user = self;
    return Div(cx->a)
        ->Flex1()
        ->MinH(0)
        ->MinW(0)
        ->FlexCol()
        ->BorderR(1, th.border)
        ->Child(Div(cx->a)->H(28)->ItemsCenter()->PadX(8)->Child(
            TextEl(cx->a, StrL("Watchlist"))
                ->Font(12)
                ->Semibold()
                ->Fg(th.foreground)))
        ->Child(VirtualList::New(cx, StrL("perf-watch"), o)->Flex1()->MinH(0));
}

static void PaintCandles(PaintCtx* ctx, El* e, void* user) {
    Feed* feed = (Feed*)user;
    if (!ctx || !e || !feed) {
        return;
    }
    float lo = feed->candles[0];
    float hi = feed->candles[0];
    for (int i = 1; i < 120; i++) {
        if (feed->candles[i] < lo) {
            lo = feed->candles[i];
        }
        if (feed->candles[i] > hi) {
            hi = feed->candles[i];
        }
    }
    float span = hi - lo;
    if (span < 0.01f) {
        span = 1.f;
    }
    float slot = e->w / 120.f;
    for (int i = 0; i < 120; i++) {
        float t = (feed->candles[i] - lo) / span;
        float bh = t * (e->h - 8.f);
        if (bh < 1.f) {
            bh = 1.f;
        }
        CanvasFillRect(ctx, e->x + slot * (float)i + 1.f,
                       e->y + e->h - bh - 4.f, slot > 2.f ? slot - 1.f : 1.f,
                       bh, Rgb(0x16, 0xa3, 0x4a));
    }
}

static void PaintDonut(PaintCtx* ctx, El* e, void* user) {
    (void)user;
    if (!ctx || !e) {
        return;
    }
    float rx = e->w * 0.35f;
    float ry = e->h * 0.35f;
    CanvasEllipse(ctx, e->x + e->w * 0.5f, e->y + e->h * 0.5f, rx, ry, 8.f,
                  Rgb(0x25, 0x63, 0xeb));
}

El* Panels::Render(Panels* self, Ctx* cx) {
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    Feed* feed = self->feed.Get(cx);
    int sym = feed ? feed->panelSymbol : 0;
    float last = feed ? feed->last[sym] : 0;
    El* header = Div(cx->a)
                     ->FlexRow()
                     ->Gap(12)
                     ->ItemsCenter()
                     ->H(36)
                     ->Child(TextEl(cx->a, DupStr(cx, fmt("S%03d", sym)))
                                 ->Font(16)
                                 ->Semibold()
                                 ->Fg(th.foreground))
                     ->Child(TextEl(cx->a, DupStr(cx, fmt("%.2f", last)))
                                 ->Font(16)
                                 ->Fg(th.foreground))
                     ->Child(TextEl(cx->a, StrL("Open  High  Low  Vol"))
                                 ->Font(11)
                                 ->Fg(th.mutedForeground));
    El* chart = Div(cx->a)->H(150)->W(kFill)->Bg(Rgb(0xfb, 0xfb, 0xfb));
    chart->customPaint = &PaintCandles;
    chart->customUser = feed;
    El* book = Div(cx->a)->FlexCol()->Flex1()->Gap(2);
    book->Child(TextEl(cx->a, StrL("Book"))
                    ->Font(11)
                    ->Semibold()
                    ->Fg(th.mutedForeground));
    for (int i = 0; i < 10; i++) {
        float bar = 20.f + (float)((i * 17 + sym) % 80);
        book->Child(
            Div(cx->a)
                ->FlexRow()
                ->Gap(6)
                ->ItemsCenter()
                ->Child(Div(cx->a)->H(8)->W(bar)->Bg(Rgb(0xdc, 0x26, 0x26)))
                ->Child(TextEl(cx->a,
                               DupStr(cx, fmt("%.2f", last + (float)i * 0.1f)))
                            ->Font(11)
                            ->Fg(th.foreground)));
    }
    El* trades = Div(cx->a)->FlexCol()->Flex1()->Gap(2);
    trades->Child(TextEl(cx->a, StrL("Time and sales"))
                      ->Font(11)
                      ->Semibold()
                      ->Fg(th.mutedForeground));
    int shown = 80;
    for (int i = 0; i < shown; i++) {
        trades->Child(
            TextEl(cx->a,
                   DupStr(cx, fmt("%02d:%02d  %.2f  %d", (9 + i / 10) % 24,
                                  (i * 3) % 60, last + (float)(i % 5) * 0.05f,
                                  10 + (i * 13) % 200)))
                ->Font(11)
                ->Fg(th.foreground));
    }
    El* donut = Div(cx->a)->W(120)->H(120);
    donut->customPaint = &PaintDonut;
    donut->customUser = feed;
    return Div(cx->a)
        ->W(420)
        ->H(kFill)
        ->FlexCol()
        ->Gap(8)
        ->Pad(8)
        ->Child(header)
        ->Child(chart)
        ->Child(Div(cx->a)
                    ->FlexRow()
                    ->Flex1()
                    ->MinH(0)
                    ->Gap(8)
                    ->Child(book)
                    ->Child(trades)
                    ->Child(Div(cx->a)
                                ->FlexCol()
                                ->ItemsCenter()
                                ->Gap(4)
                                ->Child(donut)
                                ->Child(TextEl(cx->a, StrL("Flow"))
                                            ->Font(11)
                                            ->Fg(th.mutedForeground))));
}

El* Workspace::Render(Workspace* self, Ctx* cx) {
    El* list = nullptr;
    El* panels = nullptr;
    if (self->list.IsValid()) {
        list = EntityRender(cx->app, cx->win, cx->a, self->list.id);
    }
    if (self->panels.IsValid()) {
        panels = EntityRender(cx->app, cx->win, cx->a, self->panels.id);
    }
    El* row = Div(cx->a)->Flex1()->MinH(0)->FlexRow();
    if (list) {
        row->Child(list);
    }
    if (panels) {
        row->Child(panels);
    }
    return row;
}

} // namespace gpui
