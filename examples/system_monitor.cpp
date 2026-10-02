#include "gpui.h"

using namespace gpui;

static const int kMaxHist = 120;
static const int kKeepProcs = 200;

struct MonitorApp {
    static El* Render(MonitorApp* self, Ctx* cx);
    // Dropped with the entity; this is what onShutdown used to do.
    ~MonitorApp() { SysStateFree(&sys); }

    SysState sys;
    float cpuHist[kMaxHist] = {};
    float memHist[kMaxHist] = {};
    // MetricPoint::time: the tick each point was taken at, which the chart's
    // x axis labels "{n}s".
    int timeHist[kMaxHist] = {};
    int histN = 0;
    int timeIndex = 0;
    int tab = 0;
    // Column::new("cpu", ..).sortable().sort(ColumnSort::Descending): the
    // process list starts sorted by CPU, heaviest first, and the other three
    // columns start at Default.
    ProcessSort sort = ProcessSort::Cpu;
    ColumnSort sortOrder = ColumnSort::Descending;
    float tableScroll = 0;
};

// sort_processes: `is_descending = matches!(self.sort_order,
// ColumnSort::Descending)`, so a column cycled back to Default sorts ascending
// rather than going back to whatever order the list arrived in.
static bool SortIsDesc(ColumnSort s) {
    return s == ColumnSort::Descending;
}

static void PushHist(MonitorApp* app, float cpu, float mem) {
    if (app->histN < kMaxHist) {
        app->cpuHist[app->histN] = cpu;
        app->memHist[app->histN] = mem;
        app->timeHist[app->histN] = app->timeIndex;
        app->histN++;
    } else {
        memmove(app->cpuHist, app->cpuHist + 1, sizeof(float) * (kMaxHist - 1));
        memmove(app->memHist, app->memHist + 1, sizeof(float) * (kMaxHist - 1));
        memmove(app->timeHist, app->timeHist + 1, sizeof(int) * (kMaxHist - 1));
        app->cpuHist[kMaxHist - 1] = cpu;
        app->memHist[kMaxHist - 1] = mem;
        app->timeHist[kMaxHist - 1] = app->timeIndex;
    }
    app->timeIndex++;
}

static void Collect(MonitorApp* app) {
    SysRefresh(&app->sys);
    SysSortProcesses(&app->sys, app->sort, SortIsDesc(app->sortOrder),
                     kKeepProcs);
    PushHist(app, app->sys.cpu, app->sys.mem);
}

static void OnTick(MonitorApp* app, Ctx* cx, const TickEvent*) {
    Collect(app);
    Notify(cx);
}

static void OnWheel(MonitorApp* app, Ctx* cx, const ScrollWheelEvent* ev) {
    (void)cx;
    float x = ev->x;
    float y = ev->y;
    float delta = ev->deltaY;
    (void)x;
    if (app->tab != 1) {
        return;
    }
    if (y < 34) {
        return;
    }
    app->tableScroll -= delta;
    if (app->tableScroll < 0) {
        app->tableScroll = 0;
    }
    float maxScroll = (float)app->sys.procs.len * 28.f;
    if (app->tableScroll > maxScroll) {
        app->tableScroll = maxScroll;
    }
}

static void PickTab(MonitorApp* app, Ctx* cx, const ClickEvent*, int64_t ix) {
    app->tab = (int)ix;
    Notify(cx);
}

// TableState::perform_sort. The cycle is three-stepped — Default becomes
// Descending, Descending becomes Ascending, Ascending goes back to Default —
// and every other column drops back to Default, since only one column carries
// the sort. A two-state toggle here was the difference from Rust: the third
// press had nowhere to go, so the column could never be given up.
static void SortBy(MonitorApp* app, Ctx* cx, const ClickEvent*, int64_t which) {
    ProcessSort field = (ProcessSort)which;
    ColumnSort was = app->sort == field ? app->sortOrder : ColumnSort::Default;
    app->sort = field;
    app->sortOrder = TableNextSort(was);
    SysSortProcesses(&app->sys, app->sort, SortIsDesc(app->sortOrder),
                     kKeepProcs);
    Notify(cx);
}

static El* SegmentedTab(Arena* a, Str label, bool selected, Listener onClick) {
    const Theme& th = ThemeDark();
    El* t = Div(a)
                ->H(24)
                ->PadX(12)
                ->ItemsCenter()
                ->JustifyCenter()
                ->Radius(6)
                ->OnClick(onClick)
                ->Child(TextEl(a, label)->Font(13)->Fg(selected ? th.tabActiveFg
                                                                : th.tabFg));
    if (selected) {
        t->Bg(th.tokens.tabActiveBg);
    }
    return t;
}

// TitleBar::new().child(TabBar.segmented()).child(total memory): the tabs and
// the memory label are the only things this example puts in the bar, and
// component::TitleBar supplies the rest — the background, the drag region and
// the window controls.
static El* TitleBar(Ctx* cx, MonitorApp* app) {
    Arena* a = cx->a;
    const Theme& th = ThemeDark();

    El* tabs = Div(a)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->Gap(2)
                   ->PadY(2)
                   ->Radius(8)
                   ->Bg(th.tokens.tabBar)
                   ->Child(SegmentedTab(a, StrL("System"), app->tab == 0,
                                        Listen(cx, &PickTab, 0)))
                   ->Child(SegmentedTab(a, StrL("Processes"), app->tab == 1,
                                        Listen(cx, &PickTab, 1)));

    double gb = (double)app->sys.memTotal / (1024.0 * 1024.0 * 1024.0);
    // mr_4 in Rust; the label is the last thing before the window controls.
    El* memLabel = Div(a)->PadR(16)->Child(
        TextEl(a, fmt("%.1f GB", gb))->Font(12)->Fg(th.mutedFg));

    return component::TitleBar::New(cx)->Child(tabs)->Child(memLabel)->IntoEl();
}

// render_chart: the title and the current value over an AreaChart of the
// history, stroked in `color` and filled with it fading to the background.
static El* ChartCard(Ctx* cx, const MonitorApp* app, Str title, const float* ys,
                     float current, Rgba color) {
    Arena* a = cx->a;
    const Theme& th = ThemeDark();
    int n = app->histN;
    El* header =
        Div(a)
            ->FlexRow()
            ->JustifyBetween()
            ->ItemsCenter()
            ->Shrink0()
            ->PadX(12)
            ->PadY(4)
            ->Child(TextEl(a, title)->Font(14)->Fg(th.foreground))
            ->Child(Div(a)->Flex1())
            ->Child(TextEl(a, FormatPctTemp(current, 1))->Font(14)->Fg(color));

    // .x(|d| d.time.clone()): "{n}s", in the frame arena.
    const char** labels = (const char**)Alloc(a, (int)sizeof(char*) * (n + 1));
    for (int i = 0; i < n; i++) {
        labels[i] = StrDup(a, fmt("%ds", app->timeHist[i])).s;
    }
    // Both cards come from this one construction site, so the chart is
    // named after its card to keep their hover and appear apart.
    El* chart =
        component::AreaChart::New(cx, ys, n)
            ->Id(title)
            ->Labels(labels)
            ->Stroke(color)
            ->Fill(RgbaOpacity(color, 0.4f), RgbaOpacity(th.background, 0.1f))
            ->TickMargin(15)
            ->IntoEl();

    return Div(a)
        ->FlexCol()
        ->Flex1()
        ->MinH(160)
        ->Gap(8)
        ->Border(1, th.border)
        ->Child(header)
        ->Child(chart);
}

static El* SystemTab(Ctx* cx, MonitorApp* app) {
    Arena* a = cx->a;
    const Theme& th = ThemeDark();
    float cpu = app->histN ? app->cpuHist[app->histN - 1] : 0;
    float mem = app->histN ? app->memHist[app->histN - 1] : 0;
    return Div(a)
        ->FlexCol()
        ->Flex1()
        ->Pad(12)
        ->Gap(16)
        ->Child(
            ChartCard(cx, app, StrL("CPU Usage"), app->cpuHist, cpu, th.red))
        ->Child(ChartCard(cx, app, StrL("Memory Usage"), app->memHist, mem,
                          th.blue));
}

static const float kColW[4] = {70, 380, 80, 100};

// render_sort_icon. Every sortable column carries one: the two chevrons at
// half opacity while the column is not the sorted one, and the single chevron
// the sort is in at full. Rust draws SortAscending / SortDescending glyphs,
// which this tree's icon set does not have, so it stands the same two chevrons
// in that component::Table does.
static El* SortIcon(Arena* a, const Theme& th, ColumnSort sort) {
    IconName name = IconName::ChevronsUpDown;
    bool on = true;
    switch (sort) {
        case ColumnSort::Ascending:
            name = IconName::ChevronUp;
            break;
        case ColumnSort::Descending:
            name = IconName::ChevronDown;
            break;
        default:
            on = false;
            break;
    }
    return Div(a)
        ->Pad(2)
        ->Radius(th.radius * 0.5f)
        ->HoverBg(th.tokens.secondary)
        ->Child(
            IconEl(a, name, 12)
                ->Fg(on ? th.secondaryFg : RgbaOpacity(th.secondaryFg, 0.5f)));
}

static El* ProcTableHeader(Ctx* cx, MonitorApp* app) {
    Arena* a = cx->a;
    const Theme& th = ThemeDark();
    const char* names[4] = {"PID", "Name", "CPU %", "Memory"};
    ProcessSort fields[4] = {ProcessSort::Pid, ProcessSort::Name,
                             ProcessSort::Cpu, ProcessSort::Memory};
    El* row = Div(a)->FlexRow()->H(28)->Shrink0()->ItemsCenter()->Bg(
        th.tokens.tableHead);
    for (int i = 0; i < 4; i++) {
        ColumnSort sort =
            fields[i] == app->sort ? app->sortOrder : ColumnSort::Default;
        // The icon is the hit box, not the whole cell — Rust hangs
        // perform_sort off the icon and leaves the head to the column itself,
        // and component::Table does the same.
        El* icon = SortIcon(a, th, sort)
                       ->OnClick(Listen(cx, &SortBy, (int64_t)fields[i]));
        row->Child(
            Div(a)
                ->W(kColW[i])
                ->H(28)
                ->PadX(8)
                ->FlexRow()
                ->ItemsCenter()
                ->JustifyBetween()
                ->Child(TextEl(a, Str(names[i]))->Font(12)->Fg(th.tableHeadFg))
                ->Child(icon));
    }
    return row;
}

static Rgba CpuColor(const Theme& th, float cpu) {
    if (cpu > 50) {
        return th.red;
    }
    if (cpu > 20) {
        return th.yellow;
    }
    return th.blue;
}

static El* ProcTableRow(Arena* a, const ProcessInfo* p, int ix) {
    const Theme& th = ThemeDark();
    El* row = Div(a)->FlexRow()->H(28)->Shrink0()->ItemsCenter();
    if (ix % 2 == 1) {
        row->Bg(th.tokens.tableEven);
    }
    row->Child(Div(a)->W(kColW[0])->H(28)->PadX(8)->ItemsCenter()->Child(
        TextEl(a, fmt("%d", (int)p->pid))->Font(12)->Fg(th.mutedFg)));
    row->Child(Div(a)->W(kColW[1])->H(28)->PadX(8)->ItemsCenter()->Child(
        TextEl(a, Str(p->name))
            ->Font(14)
            ->Fg(th.foreground)
            ->Truncate()
            ->W(kColW[1] - 16)));
    row->Child(Div(a)->W(kColW[2])->H(28)->PadX(8)->ItemsCenter()->Child(
        TextEl(a, FormatPctTemp(p->cpu, 1))
            ->Font(12)
            ->Fg(CpuColor(th, p->cpu))));
    row->Child(Div(a)->W(kColW[3])->H(28)->PadX(8)->ItemsCenter()->Child(
        TextEl(a, FormatBytesTemp(p->memory))->Font(12)->Fg(th.green)));
    return row;
}

static El* ProcessesTab(Ctx* cx, MonitorApp* app) {
    Arena* a = cx->a;
    El* body = Div(a)->FlexCol()->Flex1()->ClipY();
    int n = app->sys.procs.len;
    // virtualize a bit: skip rows above scroll
    int first = (int)(app->tableScroll / 28.f);
    if (first < 0) {
        first = 0;
    }
    if (first > 0) {
        body->Child(Div(a)->H((float)first * 28.f)->Shrink0());
    }
    int last = first + 40;
    if (last > n) {
        last = n;
    }
    for (int i = first; i < last; i++) {
        body->Child(ProcTableRow(a, &app->sys.procs[i], i));
    }
    if (last < n) {
        body->Child(Div(a)->H((float)(n - last) * 28.f)->Shrink0());
    }

    return Div(a)
        ->FlexCol()
        ->SizeFull()
        ->Child(ProcTableHeader(cx, app))
        ->Child(body);
}

static El* StatusChip(Arena* a, IconName icon, float pct) {
    const Theme& th = ThemeDark();
    return Div(a)
        ->FlexRow()
        ->W(135)
        ->Gap(8)
        ->ItemsCenter()
        ->Child(IconEl(a, icon)->Fg(th.mutedFg))
        ->Child(ProgressEl(a, pct, 48, 8))
        ->Child(TextEl(a, FormatPctTemp(pct, 0))->Font(14)->Fg(th.mutedFg));
}

static El* StatusBar(Arena* a, MonitorApp* app) {
    const Theme& th = ThemeDark();
    float cpu = app->histN ? app->cpuHist[app->histN - 1] : 0;
    float mem = app->histN ? app->memHist[app->histN - 1] : 0;

    El* left =
        Div(a)
            ->FlexRow()
            ->Gap(16)
            ->ItemsCenter()
            ->Child(StatusChip(a, IconName::HardDrive, app->sys.disk.usedPct))
            ->Child(StatusChip(a, IconName::MemoryStick, mem))
            ->Child(StatusChip(a, IconName::Cpu, cpu));

    El* right = Div(a);
    if (app->sys.battery.present) {
        IconName bi = IconName::Battery;
        if (app->sys.battery.charging) {
            bi = IconName::BatteryCharging;
        } else if (app->sys.battery.pct >= 80) {
            bi = IconName::BatteryFull;
        } else if (app->sys.battery.pct >= 30) {
            bi = IconName::BatteryMedium;
        }
        right->FlexRow()
            ->Gap(8)
            ->ItemsCenter()
            ->Child(IconEl(a, bi)->Fg(th.mutedFg))
            ->Child(TextEl(a, FormatPctTemp(app->sys.battery.pct, 0))
                        ->Font(14)
                        ->Fg(th.mutedFg));
    }

    return Div(a)
        ->FlexRow()
        ->H(28)
        ->PadX(12)
        ->ItemsCenter()
        ->JustifyBetween()
        ->BorderT(1, th.border)
        ->Bg(th.tokens.tabBar)
        ->Child(left)
        ->Child(right);
}

El* MonitorApp::Render(MonitorApp* app, Ctx* cx) {
    Arena* frame = cx->a;

    const Theme& th = ThemeDark();

    El* content = Div(frame)->FlexCol()->Flex1()->ClipY();
    if (app->tab == 0) {
        content->Child(SystemTab(cx, app));
    } else {
        content->Child(ProcessesTab(cx, app));
    }

    return Div(frame)
        ->FlexCol()
        ->SizeFull()
        ->Bg(th.tokens.background)
        ->Child(TitleBar(cx, app))
        ->Child(content)
        ->Child(StatusBar(frame, app));
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    // Without a root the icons fall back to the built-in strokes, which
    // only cover part of the set — the sort chevrons in the process
    // table's head were among the ones that drew nothing.
    AssetsAddDefaultRoots(Str{});
    Entity<MonitorApp> view = EntityNew<MonitorApp>(app);
    MonitorApp* self = view.Get(app);
    (void)self;
    ThemeSet(app, ThemeMode::Dark);
    SysStateInit(&self->sys);
    Collect(self);
    WinOpts opts = {};
    // TitleBar::window_options(): the example draws its own title bar.
    opts.clientTitleBar = true;
    Window* win =
        KitOpenWindow(app, StrL("System Monitor C++"), 680, 600, view.id, opts);
    WindowOnScrollWheel(win, ListenTo(view, &OnWheel));
    WindowSetInterval(win, 500, ListenTo(view, &OnTick));
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
