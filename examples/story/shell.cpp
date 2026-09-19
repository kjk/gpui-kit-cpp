#include "Story.h"

// crates/story/src/stories/shell_story.rs and js/quotes. The market is an
// entity shared by the native board and a real ScriptView. The script reads
// plain records through its private `market` host module.
struct ShellQuote {
    const char* symbol = nullptr;
    const char* name = nullptr;
    float open = 0;
    float last = 0;
    uint64_t volume = 0;
    bool watched = false;
};

static const ShellQuote kOpeningQuotes[] = {
    {"AAPL.US", "Apple", 214.29f, 214.29f},
    {"NVDA.US", "NVIDIA", 118.11f, 118.11f},
    {"MSFT.US", "Microsoft", 421.53f, 421.53f},
    {"TSLA.US", "Tesla", 249.83f, 249.83f},
    {"AMZN.US", "Amazon", 186.34f, 186.34f},
    {"GOOGL.US", "Alphabet", 165.27f, 165.27f},
    {"META.US", "Meta", 502.18f, 502.18f},
    {"700.HK", "Tencent", 372.40f, 372.40f},
    {"9988.HK", "Alibaba", 78.15f, 78.15f},
    {"0005.HK", "HSBC", 62.05f, 62.05f},
};
static const int kShellQuoteCount = (int)(sizeof(kOpeningQuotes) /
                                          sizeof(kOpeningQuotes[0]));

struct ShellMarket {
    ShellQuote quotes[kShellQuoteCount] = {};
    uint64_t ticks = 0;
    uint64_t seed = 0x2545f4914f6cdd1dULL;

    void Tick() {
        ticks++;
        for (ShellQuote& quote : quotes) {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            float drift = (float)(seed % 2001) / 1000.f - 1.f;
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            quote.last = std::max(0.01f, quote.last * (1.f + drift * 0.0012f));
            quote.volume += seed % 4000;
        }
    }
    int Watched() const {
        int n = 0;
        for (const ShellQuote& quote : quotes) n += quote.watched;
        return n;
    }
};

struct ShellStory;
struct ShellMarketBridge {
    Entity<ShellStory> owner = {};
};

struct ShellStory {
    Entity<ShellMarket> market = {};
    Entity<ScriptView> script = {};
    Entity<ScriptView> motion = {};
    ShellRuntime* runtime = nullptr;
    Policy* policy = nullptr;
    ShellMarketBridge* bridge = nullptr;
    Window* window = nullptr;
    int feedTimer = 0;
    int sampleTimer = 0;
    int feed = 1; // Off, Quotes 50ms, Quotes 16ms, Repaint 16ms.
    bool rustPaused = false;
    bool scriptPaused = false;
    bool seeded = false;
    ShellQuote frozen[kShellQuoteCount] = {};
    uint64_t frozenTicks = 0;
    RuntimeMetrics sampled = {};
    RuntimeMetrics rate = {};
    char scriptError[512] = {};
    char motionError[512] = {};

    ~ShellStory() {
        if (window && feedTimer) WindowCancelTimer(window, feedTimer);
        if (window && sampleTimer) WindowCancelTimer(window, sampleTimer);
        App* app = window ? window->app : nullptr;
        if (app && script.id.IsValid()) EntityDrop(app, script.id);
        if (app && motion.id.IsValid()) EntityDrop(app, motion.id);
        if (app && market.id.IsValid()) EntityDrop(app, market.id);
        if (runtime) runtime->Release();
        PolicyRelease(policy);
        delete bridge;
    }

    static El* Render(ShellStory* self, Ctx* cx);
    static void FeedTick(ShellStory* self, Ctx* cx, const TickEvent*);
    static void SampleTick(ShellStory* self, Ctx* cx, const TickEvent*);
    static void Watch(ShellStory* self, Ctx* cx, const ClickEvent*, intptr_t ix);
    static void WatchAll(ShellStory* self, Ctx* cx, const ClickEvent*, intptr_t on);
    static void FeedSelect(ShellStory* self, Ctx* cx, const ClickEvent*, intptr_t ix);
    static void Pause(ShellStory* self, Ctx* cx, const ClickEvent*, intptr_t side);
    static void Reload(ShellStory* self, Ctx* cx, const ClickEvent*, intptr_t side);
    void Changed(Ctx* cx);
    void SetFeed(Ctx* cx, int value);
    void Load(Ctx* cx, bool isMotion);
};

static void ShellHostString(HostValue* row, Str name, Str value) {
    HostValue field;
    field.SetString(value);
    row->SetField(name, field);
    field.Free();
}
static void ShellHostNumber(HostValue* row, Str name, double value) {
    HostValue field;
    field.SetNumber(value);
    row->SetField(name, field);
    field.Free();
}
static void ShellHostBool(HostValue* row, Str name, bool value) {
    HostValue field;
    field.SetBool(value);
    row->SetField(name, field);
    field.Free();
}

static void ShellThousands(uint64_t value, char out[32]) {
    char reversed[32];
    int n = 0;
    int digits = 0;
    do {
        if (digits && digits % 3 == 0) reversed[n++] = ',';
        reversed[n++] = (char)('0' + value % 10);
        value /= 10;
        digits++;
    } while (value);
    for (int i = 0; i < n; i++) out[i] = reversed[n - i - 1];
    out[n] = 0;
}

static void ShellHostQuotes(ShellMarketBridge* bridge, HostCall* call) {
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    ShellStory* story = host.IsSet() ? bridge->owner.Get(host.GetApp()) : nullptr;
    ShellMarket* market = story ? story->market.Get(host.GetApp()) : nullptr;
    if (!market) { call->Fail(StrL("the market is no longer mounted")); return; }
    for (const ShellQuote& quote : market->quotes) {
        HostValue row;
        ShellHostString(&row, StrL("symbol"), Str(quote.symbol));
        ShellHostString(&row, StrL("name"), Str(quote.name));
        ShellHostString(&row, StrL("last"), fmt("%.2f", quote.last));
        ShellHostString(&row, StrL("change"), fmt("%+.2f", quote.last - quote.open));
        ShellHostString(&row, StrL("percent"),
                        fmt("%+.2f%%", (quote.last - quote.open) / quote.open * 100.f));
        char volume[32];
        ShellThousands(quote.volume, volume);
        ShellHostString(&row, StrL("volume"), Str(volume));
        float change = quote.last - quote.open;
        ShellHostNumber(&row, StrL("direction"), change > 0.0005f ? 1 : change < -0.0005f ? -1 : 0);
        ShellHostBool(&row, StrL("watched"), quote.watched);
        call->result.Append(row);
        row.Free();
    }
}

static void ShellHostTicks(ShellMarketBridge* bridge, HostCall* call) {
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    ShellStory* story = host.IsSet() ? bridge->owner.Get(host.GetApp()) : nullptr;
    ShellMarket* market = story ? story->market.Get(host.GetApp()) : nullptr;
    if (!market) { call->Fail(StrL("the market is no longer mounted")); return; }
    call->result.SetNumber((double)market->ticks);
}

static void ShellHostWatch(ShellMarketBridge* bridge, HostCall* call) {
    Str symbol;
    if (!call->arguments || !call->arguments->String(0, &symbol, &call->error)) return;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    ShellStory* story = host.IsSet() ? bridge->owner.Get(host.GetApp()) : nullptr;
    ShellMarket* market = story ? story->market.Get(host.GetApp()) : nullptr;
    if (!market) { call->Fail(StrL("the market is no longer mounted")); return; }
    for (ShellQuote& quote : market->quotes) {
        if (!base::StrEq(symbol, Str(quote.symbol))) continue;
        quote.watched = !quote.watched;
        call->result.SetBool(quote.watched);
        Ctx cx = {host.GetApp(), host.GetWindow(), nullptr, bridge->owner.id};
        story->Changed(&cx);
        return;
    }
    call->Fail(StrL("no quote for that symbol"));
}

static void ShellHostWatchAll(ShellMarketBridge* bridge, HostCall* call) {
    bool on = false;
    if (!call->arguments || !call->arguments->Boolean(0, &on, &call->error)) return;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    ShellStory* story = host.IsSet() ? bridge->owner.Get(host.GetApp()) : nullptr;
    ShellMarket* market = story ? story->market.Get(host.GetApp()) : nullptr;
    if (!market) { call->Fail(StrL("the market is no longer mounted")); return; }
    int changed = 0;
    for (ShellQuote& quote : market->quotes) {
        if (quote.watched != on) { quote.watched = on; changed++; }
    }
    call->result.SetNumber(changed);
    if (changed) {
        Ctx cx = {host.GetApp(), host.GetWindow(), nullptr, bridge->owner.id};
        story->Changed(&cx);
    }
}

struct ShellSummaryWork {
    char best[20] = {};
    char worst[20] = {};
    float bestPct = 0;
    float worstPct = 0;
    float average = 0;
};
static void ShellSummaryFree(ShellSummaryWork* work) { delete work; }
static void ShellSummaryRun(ShellSummaryWork* work, HostCall* call) {
    PlatSleepMs(900);
    ShellHostString(&call->result, StrL("leader"), Str(work->best));
    ShellHostString(&call->result, StrL("leader_percent"), fmt("%+.2f%%", work->bestPct));
    ShellHostString(&call->result, StrL("laggard"), Str(work->worst));
    ShellHostString(&call->result, StrL("laggard_percent"), fmt("%+.2f%%", work->worstPct));
    ShellHostString(&call->result, StrL("average_percent"), fmt("%+.2f%%", work->average));
}
static void ShellSummaryBegin(ShellMarketBridge* bridge, HostAsyncRequest* request) {
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    ShellStory* story = host.IsSet() ? bridge->owner.Get(host.GetApp()) : nullptr;
    ShellMarket* market = story ? story->market.Get(host.GetApp()) : nullptr;
    if (!market) { request->error.Set(StrL("the market is no longer mounted")); return; }
    ShellSummaryWork* work = new ShellSummaryWork();
    float best = -1e9f, worst = 1e9f;
    for (const ShellQuote& quote : market->quotes) {
        float pct = (quote.last - quote.open) / quote.open * 100.f;
        work->average += pct / kShellQuoteCount;
        if (pct > best) { best = pct; work->bestPct = pct; StrCopyZ(work->best, 20, quote.symbol); }
        if (pct < worst) { worst = pct; work->worstPct = pct; StrCopyZ(work->worst, 20, quote.symbol); }
    }
    request->work = MkFunc1(&ShellSummaryRun, work);
    request->release = MkFunc0(&ShellSummaryFree, work);
}

void ShellStory::Changed(Ctx* cx) {
    if (!scriptPaused && script.id.IsValid()) {
        ScriptView* view = script.Get(cx);
        Ctx scriptCx = {cx->app, cx->win, cx->a, script.id};
        ScriptView::Refresh(view, &scriptCx);
    }
    Notify(cx);
}

void ShellStory::SetFeed(Ctx* cx, int value) {
    if (feedTimer) WindowCancelTimer(cx->win, feedTimer);
    feedTimer = 0;
    feed = value;
    sampled = runtime ? runtime->ReadMetrics() : RuntimeMetrics{};
    rate = {};
    int interval = value == 1 ? 50 : value == 2 || value == 3 ? 16 : 0;
    if (interval)
        feedTimer = WindowSetInterval(cx->win, interval,
                                      Listen(cx, &ShellStory::FeedTick));
    Notify(cx);
}

void ShellStory::FeedTick(ShellStory* self, Ctx* cx, const TickEvent*) {
    if (self->feed == 1 || self->feed == 2) {
        ShellMarket* market = self->market.Get(cx);
        if (market) market->Tick();
        self->Changed(cx);
    } else if (self->feed == 3 && self->script.id.IsValid()) {
        // A frame from the retained description: the script sees no change.
        Ctx scriptCx = {cx->app, cx->win, cx->a, self->script.id};
        Notify(&scriptCx);
    }
}

void ShellStory::SampleTick(ShellStory* self, Ctx* cx, const TickEvent*) {
    if (!self->runtime) return;
    RuntimeMetrics now = self->runtime->ReadMetrics();
    self->rate = now.Since(self->sampled);
    self->sampled = now;
    Notify(cx);
}

void ShellStory::Watch(ShellStory* self, Ctx* cx, const ClickEvent*, intptr_t ix) {
    ShellMarket* market = self->market.Get(cx);
    if (!market || ix < 0 || ix >= kShellQuoteCount) return;
    market->quotes[ix].watched = !market->quotes[ix].watched;
    self->Changed(cx);
}

void ShellStory::WatchAll(ShellStory* self, Ctx* cx, const ClickEvent*, intptr_t on) {
    ShellMarket* market = self->market.Get(cx);
    if (!market) return;
    for (ShellQuote& quote : market->quotes) quote.watched = on != 0;
    self->Changed(cx);
}

void ShellStory::FeedSelect(ShellStory* self, Ctx* cx, const ClickEvent*, intptr_t ix) {
    if (ix >= 0 && ix <= 3) self->SetFeed(cx, (int)ix);
}

void ShellStory::Pause(ShellStory* self, Ctx* cx, const ClickEvent*, intptr_t side) {
    if (side == 0) {
        self->rustPaused = !self->rustPaused;
        if (self->rustPaused) {
            ShellMarket* market = self->market.Get(cx);
            if (market) {
                memcpy(self->frozen, market->quotes, sizeof(self->frozen));
                self->frozenTicks = market->ticks;
            }
        }
    } else {
        self->scriptPaused = !self->scriptPaused;
        if (!self->scriptPaused) self->Changed(cx);
    }
    Notify(cx);
}

void ShellStory::Load(Ctx* cx, bool isMotion) {
    const Str path = isMotion ? StrL("assets/story/motion")
                              : StrL("assets/story/quotes");
    Entity<ScriptView>* target = isMotion ? &motion : &script;
    char* errorText = isMotion ? motionError : scriptError;
    ShellError error = {};
    if (target->id.IsValid()) {
        ScriptView* view = target->Get(cx);
        Ctx viewCx = {cx->app, cx->win, cx->a, target->id};
        if (ScriptView::Reload(view, &viewCx, path, StrL("main.js"), &error)) {
            errorText[0] = 0;
        } else {
            StrCopyZ(errorText, 512, error.message.s ? error.message.s : "reload failed");
        }
    } else {
        ViewType* type = runtime->LoadApp(path, StrL("main.js"), policy, &error);
        if (type) {
            *target = ScriptView::New(cx->app, runtime, type, policy);
            ViewTypeRelease(type);
            errorText[0] = 0;
        } else {
            StrCopyZ(errorText, 512, error.message.s ? error.message.s : "load failed");
        }
    }
    ShellErrorClear(&error);
    Notify(cx);
}

void ShellStory::Reload(ShellStory* self, Ctx* cx, const ClickEvent*, intptr_t side) {
    if (self->runtime) self->Load(cx, side != 0);
}

static void ShellSeed(ShellStory* self, Ctx* cx) {
    self->seeded = true;
    self->window = cx->win;
    self->market = EntityNewState<ShellMarket>(cx->app);
    ShellMarket* market = self->market.Get(cx);
    if (market) memcpy(market->quotes, kOpeningQuotes, sizeof(kOpeningQuotes));
    self->bridge = new ShellMarketBridge();
    self->bridge->owner = Entity<ShellStory>{cx->self};
    self->policy = PolicyDefault();
    HostModule* module = HostModule::New(StrL("market"))
        ->Function(StrL("quotes"), MkFunc1(&ShellHostQuotes, self->bridge))
        ->Function(StrL("ticks"), MkFunc1(&ShellHostTicks, self->bridge))
        ->Function(StrL("watch"), MkFunc1(&ShellHostWatch, self->bridge))
        ->Function(StrL("watch_all"), MkFunc1(&ShellHostWatchAll, self->bridge))
        ->AsyncFunction(StrL("summary"), MkFunc1(&ShellSummaryBegin, self->bridge))
        ->Declarations(StrL(
            "export function quotes(): unknown[];\n"
            "export function ticks(): number;\n"
            "export function watch(symbol: string): boolean;\n"
            "export function watch_all(watched: boolean): number;\n"
            "export function summary(): Promise<unknown>;\n"));
    HostError hostError = {};
    if (!PolicyAddHostModule(self->policy, module, &hostError)) {
        StrCopyZ(self->scriptError, 512,
                 hostError.message.s ? hostError.message.s : "market module failed");
    }
    hostError.Clear();
    module->Release();
    ShellError error = {};
    self->runtime = ShellRuntime::New(cx->app, &error);
    if (!self->runtime) {
        StrCopyZ(self->scriptError, 512,
                 error.message.s ? error.message.s : "shell runtime failed");
    } else {
        self->Load(cx, false);
        self->Load(cx, true);
        self->sampled = self->runtime->ReadMetrics();
        self->sampleTimer = WindowSetInterval(cx->win, 1000,
                                               Listen(cx, &ShellStory::SampleTick));
        self->SetFeed(cx, 1);
    }
    ShellErrorClear(&error);
}

static El* ShellSmall(Ctx* cx, Str label, Rgba color, bool medium = false) {
    El* text = StoryTxt(cx, label, 11, color)->LineHeight(1.4f);
    if (medium) text->Medium();
    return text;
}

static El* ShellBoardHeader(Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(cx->a)->FlexRow()->W(kFill)->ItemsCenter()->Gap(8)
        ->PadX(8)->PadB(4)->BorderB(1, th.border);
    row->Child(ShellSmall(cx, StrL("Symbol"), th.mutedFg)->W(78)->Shrink0());
    row->Child(Div(cx->a)->Flex1());
    row->Child(Div(cx->a)->W(68)->Shrink0()->FlexRow()->JustifyEnd()
                   ->Child(ShellSmall(cx, StrL("Last"), th.mutedFg)));
    row->Child(Div(cx->a)->W(66)->Shrink0()->FlexRow()->JustifyEnd()
                   ->Child(ShellSmall(cx, StrL("Change"), th.mutedFg)));
    row->Child(Div(cx->a)->W(82)->Shrink0()->FlexRow()->JustifyEnd()
                   ->Child(ShellSmall(cx, StrL("Volume"), th.mutedFg)));
    row->Child(Div(cx->a)->W(6)->Shrink0());
    return row;
}

static El* ShellBoardRow(Ctx* cx, const ShellQuote& quote, int ix) {
    char volume[32];
    ShellThousands(quote.volume, volume);
    const Theme& th = ThemeNow(cx->app);
    float change = quote.last - quote.open;
    Rgba color = change > 0.0005f ? RgbaHex(0x16a34a)
                 : change < -0.0005f ? th.red : th.foreground;
    El* row = Div(cx->a)->FlexRow()->W(kFill)->ItemsCenter()->Gap(8)
        ->PadX(8)->PadY(2)->Radius(th.radius);
    row->Click(HashClickId(StoryFmt(cx, "shell-quote-%d", ix)))
        ->OnClick(Listen(cx, &ShellStory::Watch, ix))->HoverBg(th.muted);
    row->Child(ShellSmall(cx, Str(quote.symbol), th.foreground, true)
                   ->W(78)->Shrink0());
    row->Child(ShellSmall(cx, Str(quote.name), th.mutedFg)
                   ->Flex1()->MinW(0)->ClipX());
    row->Child(Div(cx->a)->W(68)->Shrink0()->FlexRow()->JustifyEnd()
                   ->Child(ShellSmall(cx, StoryFmt(cx, "%.2f", quote.last), color)));
    row->Child(Div(cx->a)->W(66)->Shrink0()->FlexRow()->JustifyEnd()
                   ->Child(ShellSmall(cx, StoryFmt(cx, "%+.2f%%",
                         (double)(change / quote.open * 100.f)), color)));
    row->Child(Div(cx->a)->W(82)->Shrink0()->FlexRow()->JustifyEnd()
                   ->Child(ShellSmall(cx, StrDup(cx->a, Str(volume)), th.mutedFg)));
    row->Child(Div(cx->a)->W(6)->H(6)->Shrink0()->Radius(3)
                   ->Bg(quote.watched ? th.primary : Rgba8(0, 0, 0, 0)));
    return row;
}

static El* ShellButton(Ctx* cx, const char* id, const char* label,
                       Listener click) {
    return component::Button::New(cx, Str(id))
        ->WithSize(UiSize::XSmall)->Outline()->Label(Str(label))
        ->OnClick(click)->IntoEl();
}

static El* ShellNativeBoard(ShellStory* self, Ctx* cx) {
    ShellMarket* market = self->market.Get(cx);
    if (!market) return Div(cx->a);
    const ShellQuote* quotes = self->rustPaused ? self->frozen : market->quotes;
    uint64_t ticks = self->rustPaused ? self->frozenTicks : market->ticks;
    int watched = 0;
    for (int i = 0; i < kShellQuoteCount; i++) watched += quotes[i].watched;
    const Theme& th = ThemeNow(cx->app);
    El* board = Div(cx->a)->FlexCol()->W(kFill)->Gap(12);
    El* heading = Div(cx->a)->FlexRow()->W(kFill)->JustifyBetween()->Gap(8);
    heading->Child(Div(cx->a)->FlexCol()->Gap(2)
        ->Child(StoryTxt(cx, StrL("Live quotes"), 13, th.foreground)->Semibold())
        ->Child(ShellSmall(cx,
            StrL("Drawn by shell_story.rs · prices read from Entity<Market>"),
            th.mutedFg)));
    heading->Child(Div(cx->a)->FlexCol()->Gap(2)->ItemsEnd()
        ->Child(ShellSmall(cx, StoryFmt(cx, "%d / %d watched", watched,
                                      kShellQuoteCount), th.foreground))
        ->Child(ShellSmall(cx, StoryFmt(cx, "tick %llu",
                                      (unsigned long long)ticks), th.mutedFg)));
    board->Child(heading)->Child(ShellBoardHeader(cx));
    El* rows = Div(cx->a)->FlexCol()->W(kFill)->Gap(2);
    for (int i = 0; i < kShellQuoteCount; i++)
        rows->Child(ShellBoardRow(cx, quotes[i], i));
    board->Child(rows);
    board->Child(Div(cx->a)->W(kFill)->H(1)->Bg(th.border));
    El* actions = Div(cx->a)->FlexRow()->W(kFill)->ItemsCenter()
        ->JustifyBetween()->Gap(8);
    actions->Child(ShellSmall(cx,
        watched ? StrL("") : StrL("Nothing on the watchlist"), th.mutedFg));
    El* buttons = Div(cx->a)->FlexRow()->Gap(4);
    buttons->Child(ShellButton(cx, "shell-watch-all", "Watch all",
                              Listen(cx, &ShellStory::WatchAll, 1)));
    buttons->Child(ShellButton(cx, "shell-watch-none", "Clear",
                              Listen(cx, &ShellStory::WatchAll, 0)));
    actions->Child(buttons);
    board->Child(actions);
    return board;
}

static El* ShellMetric(Ctx* cx, const char* title, Str value, Str detail) {
    const Theme& th = ThemeNow(cx->app);
    return Div(cx->a)->FlexCol()->Gap(4)->Flex1()->MinW(0)
        ->Child(StoryTxt(cx, Str(title), 12, th.mutedFg))
        ->Child(StoryTxt(cx, value, 14, th.foreground)->Semibold())
        ->Child(StoryTxt(cx, detail, 12, th.mutedFg)->Wrap());
}

El* ShellStory::Render(ShellStory* self, Ctx* cx) {
    if (!self->seeded) ShellSeed(self, cx);
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(cx->a)->FlexCol()->W(kFill)->Gap(12);
    El* compare = Div(cx->a)->FlexRow()->W(kFill)->Gap(16)->ItemsStart();
    El* native = StorySection(cx, "Rust", nullptr);
    StorySectionSubTitle(native, ShellButton(cx, "pause-rust",
        self->rustPaused ? "Resume" : "Pause", Listen(cx, &ShellStory::Pause, 0)));
    StorySectionBody(native)->FlexCol()->W(kFill);
    StorySectionAdd(native, ShellNativeBoard(self, cx));
    // Rust's quote columns give each panel a 436 DIP intrinsic minimum at
    // this gallery size; the second panel extends under the viewport clip.
    compare->Child(Div(cx->a)->Flex1()->MinW(436)->Child(native));

    El* js = StorySection(cx, "JavaScript · gpui-shell", nullptr);
    El* jsControls = Div(cx->a)->FlexRow()->Gap(4)
        ->Child(ShellButton(cx, "pause-script",
            self->scriptPaused ? "Resume" : "Pause", Listen(cx, &ShellStory::Pause, 1)))
        ->Child(ShellButton(cx, "reload-script", "Reload script",
            Listen(cx, &ShellStory::Reload, 0)));
    StorySectionSubTitle(js, jsControls);
    StorySectionBody(js)->FlexCol()->W(kFill);
    if (self->scriptError[0])
        StorySectionAdd(js, StoryTxt(cx, Str(self->scriptError), 12, th.red)->Wrap());
    if (self->script.id.IsValid())
        StorySectionAdd(js, EntityRender(cx->app, cx->win, cx->a, self->script.id));
    compare->Child(Div(cx->a)->Flex1()->MinW(436)->Child(js));
    page->Child(compare);

    El* frequency = StorySection(cx, "Render frequency",
        "A script render and a GPUI frame are not the same event. Change the feed and watch the two counters come apart.");
    static const char* labels[] = {"Off", "Quotes · 50 ms", "Quotes · 16 ms",
                                   "Repaint only · 16 ms"};
    El* feed = Div(cx->a)->FlexRow()->Gap(4);
    for (int i = 0; i < 4; i++) {
        component::Button* button = component::Button::New(
            cx, StoryFmt(cx, "shell-feed-%d", i))
            ->WithSize(UiSize::XSmall)->Label(Str(labels[i]))
            ->OnClick(Listen(cx, &ShellStory::FeedSelect, i));
        if (i != self->feed) button->Outline();
        feed->Child(button->IntoEl());
    }
    StorySectionSubTitle(frequency, feed);
    RuntimeMetrics r = self->rate;
    El* readings = Div(cx->a)->FlexRow()->W(kFill)->Gap(24);
    readings->Child(ShellMetric(cx, "Script renders",
        StoryFmt(cx, "%llu/s", (unsigned long long)r.scriptRenders),
        StoryFmt(cx, "%.2f ms describing · %.2f ms in host calls",
            (double)r.MeanScriptOnlyNanos() / 1e6,
            (double)r.MeanNativeNanos() / 1e6)));
    readings->Child(ShellMetric(cx, "Frames drawn",
        StoryFmt(cx, "%llu/s", (unsigned long long)r.materializations),
        StoryFmt(cx, "%.2f ms each", (double)r.MeanMaterializeNanos() / 1e6)));
    readings->Child(ShellMetric(cx, "Feed", Str(labels[self->feed]),
        Str(self->feed == 0 ? "nothing is driving the board" :
            self->feed == 3 ? "the view is redrawn every 16 ms" :
            self->feed == 1 ? "every price moves every 50 ms" :
                              "every price moves every 16 ms")));
    StorySectionAdd(frequency, Div(cx->a)->FlexCol()->W(kFill)->Gap(8)
        ->Child(readings)
        ->Child(ShellSmall(cx, self->feed == 3
            ? StrL("Nothing the script reads changed, so frames repaint its published snapshot.")
            : StrL("The script reads prices, so every tick invalidates its snapshot."),
            th.mutedFg)));
    page->Child(frequency);

    El* motion = StorySection(cx, "Native motion · gpui-shell",
        "A separate ScriptView with pixel targets. Clicking a control retargets transition or spring tracks; GPUI owns every in-between frame.");
    StorySectionSubTitle(motion, ShellButton(cx, "reload-motion", "Reload motion",
        Listen(cx, &ShellStory::Reload, 1)));
    StorySectionBody(motion)->FlexCol()->W(kFill);
    if (self->motionError[0])
        StorySectionAdd(motion, StoryTxt(cx, Str(self->motionError), 12, th.red)->Wrap());
    if (self->motion.id.IsValid())
        StorySectionAdd(motion, EntityRender(cx->app, cx->win, cx->a, self->motion.id));
    page->Child(motion);

    El* boundary = StorySection(cx, "Where the boundary is",
        "The script holds no host object. Market data crosses the one native module this story registered; appearance comes from gpui-shell's call-scoped context.");
    El* lines = Div(cx->a)->FlexCol()->W(kFill)->Gap(4);
    const char* names[] = {"native(\"market\")", "cx.theme()", "Editing main.js"};
    const char* details[] = {
        "quotes() · ticks() · watch(symbol) · watch_all(on)",
        "read-only semantic colors, spacing, radius and mode",
        "needs no rebuild: press Reload script above"};
    for (int i = 0; i < 3; i++)
        lines->Child(Div(cx->a)->FlexRow()->W(kFill)->Gap(12)
            ->Child(ShellSmall(cx, Str(names[i]), th.foreground, true)->W(192))
            ->Child(ShellSmall(cx, Str(details[i]), th.mutedFg)));
    StorySectionAdd(boundary, lines);
    page->Child(boundary);
    return page;
}

STORY_PAGE(StoryShell, ShellStory);
