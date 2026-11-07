#include "perf.h"

#include <stdio.h>

namespace gpui {

static bool EqI(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) {
            return false;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static bool Starts(const char* a, const char* prefix) {
    if (!a || !prefix) {
        return false;
    }
    while (*prefix) {
        if (*a != *prefix) {
            return false;
        }
        a++;
        prefix++;
    }
    return true;
}

static void Bounce(float* offset, float* dir, float maxOff) {
    if (maxOff < 0) {
        maxOff = 0;
    }
    float next = *offset + kPerfScrollPx * *dir;
    if (next >= maxOff) {
        *dir = -1.f;
        next = maxOff;
    } else if (next <= 0.f) {
        *dir = 1.f;
        next = 0.f;
    }
    if (next < 0.f) {
        next = 0.f;
    }
    *offset = next;
}

static float HandleMax(const VirtualListScrollHandle* handle) {
    if (handle && handle->viewport > 0 &&
        handle->contentSize > handle->viewport) {
        return handle->contentSize - handle->viewport;
    }
    return 100000.f;
}

static int PageFor(PerfScenario scenario) {
    if (scenario == PerfScenario::ScrollTable ||
        scenario == PerfScenario::RefreshTable) {
        return kPerfPageTable;
    }
    if (scenario == PerfScenario::ScrollList) {
        return kPerfPageList;
    }
    if (scenario == PerfScenario::WorkspaceQuotes ||
        scenario == PerfScenario::WorkspaceScroll ||
        scenario == PerfScenario::WorkspaceHover) {
        return kPerfPageWorkspace;
    }
    return kPerfPageButton;
}

static PerfScroll ScrollFor(PerfScenario scenario) {
    if (scenario == PerfScenario::ScrollSidebar) {
        return PerfScroll::Sidebar;
    }
    if (scenario == PerfScenario::ScrollPage) {
        return PerfScroll::Page;
    }
    if (scenario == PerfScenario::ScrollTable) {
        return PerfScroll::Table;
    }
    if (scenario == PerfScenario::ScrollList) {
        return PerfScroll::List;
    }
    if (scenario == PerfScenario::WorkspaceScroll) {
        return PerfScroll::Watchlist;
    }
    return PerfScroll::Off;
}

static bool IsWorkspace(PerfScenario scenario) {
    return scenario == PerfScenario::WorkspaceQuotes ||
           scenario == PerfScenario::WorkspaceScroll ||
           scenario == PerfScenario::WorkspaceHover;
}

static void Say(FILE* file, const char* line) {
    printf("%s\n", line);
    if (file) {
        fprintf(file, "%s\n", line);
    }
}

static float At(Vec<float>& values, float p) {
    int n = len(values);
    if (n <= 0) {
        return 0;
    }
    for (int i = 1; i < n; i++) {
        float value = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > value) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = value;
    }
    int ix = (int)((float)(n - 1) * p + 0.5f);
    if (ix < 0) {
        ix = 0;
    }
    if (ix >= n) {
        ix = n - 1;
    }
    return values[ix];
}

static void Summarize(PerfRoot* root) {
    if (root->resultCount >= 9) {
        return;
    }
    PerfResult* result = &root->results[root->resultCount++];
    result->scenario = root->runs[root->run];
    result->n = len(root->samples);
    float drawSum = 0;
    float buildSum = 0;
    float layoutSum = 0;
    float paintSum = 0;
    float wallSum = 0;
    float cpuSum = 0;
    Vec<float> draws;
    Vec<float> cpus;
    Vec<float> walls;
    for (int i = 0; i < result->n; i++) {
        const PerfSample& sample = root->samples[i];
        drawSum += sample.drawMs;
        buildSum += sample.buildMs;
        layoutSum += sample.layoutMs;
        paintSum += sample.paintMs;
        wallSum += sample.wallMs;
        cpuSum += sample.cpuMs;
        VecAppend(draws, sample.drawMs);
        VecAppend(cpus, sample.cpuMs);
        VecAppend(walls, sample.wallMs);
    }
    float n = result->n > 0 ? (float)result->n : 1.f;
    result->medianMs = At(draws, 0.5f);
    result->p95Ms = At(draws, 0.95f);
    result->cpuP50Ms = At(cpus, 0.5f);
    float wallP50 = At(walls, 0.5f);
    result->buildMs = buildSum / n;
    result->layoutMs = layoutSum / n;
    result->paintMs = paintSum / n;
    result->fps = wallP50 > 0.01f ? 1000.f / wallP50 : 0;
    result
        ->threadPct = wallP50 > 0.01f ? result->cpuP50Ms / wallP50 * 100.f : 0;
    (void)drawSum;
    (void)cpuSum;
    (void)wallSum;
    uint64_t bytes = 0;
    if (SysSelfPrivateMemory(&bytes)) {
        result->memMb = (float)bytes / (1024.f * 1024.f);
    }
    VecReset(draws);
    VecReset(cpus);
    VecReset(walls);
    VecReset(root->samples);
}

static void Report(PerfRoot* root) {
    if (root->reported) {
        return;
    }
    root->reported = true;
    FILE* file = fopen("gpui-perf.txt", "w");
    Say(file,
        "scenario           n    fps  median     p95   cpuP50  thread%   build "
        " layout   paint     MB");
    for (int i = 0; i < root->resultCount; i++) {
        const PerfResult& r = root->results[i];
        char line[240];
        snprintf(line, sizeof(line),
                 "%-16s %4d %6.1f %7.2fms %7.2fms %7.2fms %6.0f%% %6.2fms "
                 "%6.2fms %6.2fms %6.0f",
                 PerfScenarioName(r.scenario), r.n, r.fps, r.medianMs, r.p95Ms,
                 r.cpuP50Ms, r.threadPct, r.buildMs, r.layoutMs, r.paintMs,
                 r.memMb);
        Say(file, line);
    }
    Say(file, "");
    Say(file,
        "median/p95: Window::draw. cpuP50: this thread's CPU milliseconds per "
        "frame.");
    Say(file,
        "thread%: cpuP50 / median wall time between frames. "
        "build/layout/paint: mean phase time.");
    Say(file,
        "retention: not implemented; every frame is built from scratch. "
        "--retention is ignored.");
    if (file) {
        fclose(file);
        Say(nullptr, "wrote gpui-perf.txt");
    }
}

static void Setup(PerfRoot* root, Ctx* cx, PerfScenario scenario) {
    Container* container = root->container.Get(cx);
    Sidebar* sidebar = root->sidebar.Get(cx);
    int page = PageFor(scenario);
    if (container) {
        container->Show(cx->app, page);
        container->refreshing = scenario == PerfScenario::RefreshTable;
        container->dir = 1;
    }
    if (sidebar) {
        sidebar->active = page;
        sidebar->dir = 1;
        sidebar->scroll = 0;
    }
    root->scroll = ScrollFor(scenario);
    root->scrollDir = 1;
    root->spinning = scenario == PerfScenario::Spinner;
    if (container && container->workspace.IsValid()) {
        Workspace* workspace = container->workspace.Get(cx);
        if (workspace) {
            workspace->streaming = IsWorkspace(scenario);
            workspace
                ->quotesPerTick = (scenario == PerfScenario::WorkspaceScroll ||
                                   scenario == PerfScenario::WorkspaceHover)
                                      ? 8
                                      : 16;
            if (workspace->list.IsValid()) {
                Watchlist* list = workspace->list.Get(cx);
                if (list) {
                    list->handle.offset = 0;
                    list->hover = -1;
                }
            }
        }
    }
    if (container && container->table.IsValid()) {
        PerfTable* table = container->table.Get(cx);
        if (table) {
            table->handle.offset = 0;
        }
    }
    if (container && container->messages.IsValid()) {
        MessageList* list = container->messages.Get(cx);
        if (list) {
            list->handle.offset = 0;
        }
    }
    if (container && container->components.IsValid()) {
        Components* components = container->components.Get(cx);
        if (components) {
            components->unread = root->unread;
        }
    }
    double now = TimeNow();
    root->lastQuote = now;
    root->lastRefresh = now;
}

static void Act(PerfRoot* root, Ctx* cx) {
    Container* container = root->container.Get(cx);
    Sidebar* sidebar = root->sidebar.Get(cx);
    double now = TimeNow();
    if (container && container->refreshing &&
        now - root->lastRefresh >= 0.033) {
        root->lastRefresh = now;
        if (container->table.IsValid()) {
            TableTick(container->table.Get(cx));
        }
    }
    if (container && container->workspace.IsValid()) {
        Workspace* workspace = container->workspace.Get(cx);
        if (workspace && workspace->streaming &&
            now - root->lastQuote >= 0.016) {
            root->lastQuote = now;
            WorkspaceTick(cx->app, cx->win, workspace);
        }
        if (workspace && root->scroll == PerfScroll::Watchlist &&
            workspace->list.IsValid()) {
            Watchlist* list = workspace->list.Get(cx);
            if (list) {
                Bounce(&list->handle.offset, &root->scrollDir,
                       HandleMax(&list->handle));
            }
        }
        if (workspace &&
            root->runs[root->run] == PerfScenario::WorkspaceHover &&
            workspace->list.IsValid()) {
            Watchlist* list = workspace->list.Get(cx);
            if (list) {
                int rows = 12;
                if (list->handle.viewport > 16.f) {
                    rows = (int)(list->handle.viewport / kPerfRowH);
                }
                if (rows < 1) {
                    rows = 1;
                }
                int span = rows * 2;
                int row = root->step % span;
                if (row >= rows) {
                    row = span - 1 - row;
                }
                list->hover = row;
            }
        }
    }
    if (root->scroll == PerfScroll::Sidebar && sidebar) {
        Bounce(&sidebar->scroll, &root->scrollDir, 6400.f);
    } else if (root->scroll == PerfScroll::Page && container) {
        Bounce(&container->scroll, &root->scrollDir, 4800.f);
    } else if (root->scroll == PerfScroll::Table && container &&
               container->table.IsValid()) {
        PerfTable* table = container->table.Get(cx);
        if (table) {
            Bounce(&table->handle.offset, &root->scrollDir,
                   HandleMax(&table->handle));
        }
    } else if (root->scroll == PerfScroll::List && container &&
               container->messages.IsValid()) {
        MessageList* list = container->messages.Get(cx);
        if (list) {
            Bounce(&list->handle.offset, &root->scrollDir,
                   HandleMax(&list->handle));
        }
    }
}

static void Demo(PerfRoot* root, Ctx* cx) {
    if (root->demoStamp <= 0) {
        root->demoStamp = TimeNow();
    }
    static const PerfScroll kOrder[] = {PerfScroll::Sidebar, PerfScroll::Page,
                                        PerfScroll::Table, PerfScroll::List};
    int phase = (int)((TimeNow() - root->demoStamp) / 6.0) % 4;
    if (phase < 0) {
        phase = 0;
    }
    if (kOrder[phase] != root->scroll) {
        root->scroll = kOrder[phase];
        root->scrollDir = 1;
        int page = kPerfPageButton;
        if (root->scroll == PerfScroll::Table) {
            page = kPerfPageTable;
        } else if (root->scroll == PerfScroll::List) {
            page = kPerfPageList;
        }
        Setup(root, cx,
              page == kPerfPageTable  ? PerfScenario::ScrollTable
              : page == kPerfPageList ? PerfScenario::ScrollList
                                      : PerfScenario::ScrollPage);
        root->scroll = kOrder[phase];
        root->spinning = false;
    }
    Act(root, cx);
}

// Returns whether another frame should be requested.
static bool Drive(PerfRoot* root, Ctx* cx) {
    if (root->demo && !root->autoRun) {
        Demo(root, cx);
        return true;
    }
    if (!root->autoRun) {
        return false;
    }
    if (root->run >= root->runCount) {
        Report(root);
        AppQuit(cx->win);
        return false;
    }
    double cpu = 0;
    bool haveCpu = SysSelfThreadCpu(&cpu);
    double wall = TimeNow();
    if (root->step > kPerfWarmup && root->lastWall >= 0 && cx->win &&
        cx->win->frameSeq > 0) {
        PerfSample sample;
        const FrameTiming& timing = cx->win
                                        ->frameTrace[(cx->win->frameSeq - 1) %
                                                     (uint64_t)kFrameTraceCap];
        sample.drawMs = timing.drawSecs * 1000.f;
        sample.buildMs = timing.buildSecs * 1000.f;
        sample.layoutMs = timing.layoutSecs * 1000.f;
        sample.paintMs = timing.paintSecs * 1000.f;
        if (haveCpu && root->lastCpu >= 0) {
            sample.cpuMs = (float)((cpu - root->lastCpu) * 1000.0);
        }
        sample.wallMs = (float)((wall - root->lastWall) * 1000.0);
        VecAppend(root->samples, sample);
    }
    if (haveCpu) {
        root->lastCpu = cpu;
    }
    root->lastWall = wall;
    if (root->step >= kPerfWarmup + root->frames) {
        Summarize(root);
        root->run++;
        root->step = 0;
        root->lastCpu = -1;
        root->lastWall = -1;
        if (root->run >= root->runCount) {
            Report(root);
            AppQuit(cx->win);
            return false;
        }
        Setup(root, cx, root->runs[root->run]);
        Act(root, cx);
        root->step = 1;
        return true;
    }
    if (root->step == 0) {
        Setup(root, cx, root->runs[root->run]);
    }
    Act(root, cx);
    root->step++;
    return true;
}

void PerfRoot::OnTick(PerfRoot* self, Ctx* cx, const TickEvent*) {
    self->unread++;
    Container* container = self->container.Get(cx);
    if (container && container->components.IsValid()) {
        Components* components = container->components.Get(cx);
        if (components) {
            components->unread = self->unread;
        }
    }
    Notify(cx);
}

El* Spinner::Render(Spinner*, Ctx* cx) {
    int lit = (int)(TimeNow() * 8.0);
    if (lit < 0) {
        lit = -lit;
    }
    lit %= 8;
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    El* row = Div(cx->a)->FlexRow()->Gap(3)->ItemsCenter();
    for (int i = 0; i < 8; i++) {
        El* dot = Div(cx->a)->W(6)->H(6)->Radius(3);
        dot->Bg(i == lit ? th.foreground : th.border);
        row->Child(dot);
    }
    return row;
}

El* PerfRoot::Render(PerfRoot* self, Ctx* cx) {
    if (!self->timerArmed && cx->win) {
        self->timerArmed = true;
        WindowSetInterval(cx->win, 2000, Listen(cx, &PerfRoot::OnTick));
    }
    bool again = Drive(self, cx);
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    const char* title = "gpui_perf";
    if (self->autoRun && self->run < self->runCount) {
        title = PerfScenarioName(self->runs[self->run]);
    }
    El* bar = Div(cx->a)
                  ->H(40)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->PadX(12)
                  ->Gap(12)
                  ->BorderB(1, th.border)
                  ->Child(TextEl(cx->a, Str(title))
                              ->Font(14)
                              ->Semibold()
                              ->Fg(th.foreground));
    if (self->spinning && self->spinner.IsValid()) {
        bar->Child(EntityRender(cx->app, cx->win, cx->a, self->spinner.id));
    }
    El* body = Div(cx->a)->Flex1()->MinH(0)->FlexRow();
    if (self->sidebar.IsValid()) {
        body->Child(EntityRender(cx->app, cx->win, cx->a, self->sidebar.id));
    }
    if (self->container.IsValid()) {
        body->Child(EntityRender(cx->app, cx->win, cx->a, self->container.id));
    }
    if (again && cx->win) {
        WindowRequestAnimationFrame(cx->win);
    }
    return Div(cx->a)
        ->SizeFull()
        ->FlexCol()
        ->Bg(th.background)
        ->Child(bar)
        ->Child(body);
}

static void PrintList() {
    static const char* kWhat[] = {
        "idle: a toolbar spinner asks for every frame",
        "scrolling the sidebar",
        "scrolling a page of components",
        "scrolling the data table",
        "refreshing the data table's rows every 33 ms",
        "scrolling a list of messages",
        "workspace quotes, 16 per 16 ms",
        "scrolling the watchlist, 8 quotes per 16 ms",
        "pointer moving over watchlist rows, 8 quotes per 16 ms",
    };
    for (int i = 0; i < (int)PerfScenario::Count; i++) {
        printf("%-16s  %s\n", PerfScenarioName((PerfScenario)i), kWhat[i]);
    }
}

} // namespace gpui

int GpuiMain(int argc, char** argv) {
    using namespace gpui;
    bool list = false;
    bool autoRun = false;
    bool demo = false;
    bool retention = false;
    int frames = 240;
    const char* only = nullptr;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (StrEq(Str(arg), "--list")) {
            list = true;
        } else if (StrEq(Str(arg), "--auto")) {
            autoRun = true;
        } else if (StrEq(Str(arg), "--demo")) {
            demo = true;
        } else if (StrEq(Str(arg), "--retention")) {
            retention = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                i++;
            }
        } else if (StrEq(Str(arg), "--frames") && i + 1 < argc) {
            frames = 0;
            const char* n = argv[++i];
            while (*n >= '0' && *n <= '9') {
                frames = frames * 10 + (*n - '0');
                n++;
            }
        } else if (Starts(arg, "--frames=")) {
            frames = 0;
            const char* n = arg + 9;
            while (*n >= '0' && *n <= '9') {
                frames = frames * 10 + (*n - '0');
                n++;
            }
        } else if (StrEq(Str(arg), "--only") && i + 1 < argc) {
            only = argv[++i];
        } else if (Starts(arg, "--only=")) {
            only = arg + 7;
        }
    }
    if (frames < 1) {
        frames = 240;
    }
    if (list) {
        PrintList();
        if (!autoRun) {
            return 0;
        }
    }
    App* app = AppNew();
    Entity<PerfRoot> root = EntityNew<PerfRoot>(app);
    PerfRoot* self = root.Get(app);
    self->autoRun = autoRun;
    self->demo = demo;
    self->retentionAsked = retention;
    self->frames = frames;
    self->sidebar = EntityNew<Sidebar>(app);
    self->container = EntityNew<Container>(app);
    self->spinner = EntityNew<Spinner>(app);
    if (only) {
        for (int i = 0; i < (int)PerfScenario::Count; i++) {
            if (EqI(only, PerfScenarioName((PerfScenario)i))) {
                self->runs[self->runCount++] = (PerfScenario)i;
            }
        }
        if (self->runCount == 0) {
            printf("no scenario matches %s\n", only);
            PrintList();
            return 1;
        }
    } else {
        for (int i = 0; i < (int)PerfScenario::Count; i++) {
            self->runs[self->runCount++] = (PerfScenario)i;
        }
    }
    self->container.Get(app)->Show(app, kPerfPageButton);
    if (retention) {
        printf("--retention is ignored: this tree has no view retention yet\n");
    }
    return AppRunView(StrL("gpui_perf"), 1280, 820, root.id, app, WinOpts{});
}
