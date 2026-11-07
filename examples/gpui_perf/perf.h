#ifndef GPUI_EXAMPLES_GPUI_PERF_PERF_H_
#define GPUI_EXAMPLES_GPUI_PERF_PERF_H_
/* gpui-fast's crates/gpui_perf showcase, shaped so --auto measures the same
   nine scenarios. Deviations are listed in port-status.md. */

#include "gpui.h"

namespace gpui {

constexpr int kPerfPageCount = 245;
constexpr int kPerfPageButton = 9;
constexpr int kPerfPageTable = 16;
constexpr int kPerfPageList = 28;
constexpr int kPerfPageWorkspace = 244;
constexpr int kPerfWarmup = 30;
constexpr int kPerfTableRows = 5000;
constexpr int kPerfMessages = 5000;
constexpr int kPerfSymbols = 200;
constexpr int kPerfSpark = 40;
constexpr int kPerfHidden = 12;
constexpr float kPerfRowH = 32.f;
constexpr float kPerfScrollPx = 32.f;

enum class PerfKind : int {
    Components,
    Table,
    List,
    Workspace,
    Chat
};

enum class PerfScroll : int {
    Off,
    Sidebar,
    Page,
    Table,
    List,
    Watchlist
};

enum class PerfScenario : int {
    Spinner,
    ScrollSidebar,
    ScrollPage,
    ScrollTable,
    RefreshTable,
    ScrollList,
    WorkspaceQuotes,
    WorkspaceScroll,
    WorkspaceHover,
    Count
};

const char* PerfPageName(int page);
const char* PerfScenarioName(PerfScenario scenario);

struct Stock {
    float price = 0;
    float change = 0;
    float high = 0;
    float low = 0;
    uint32_t volume = 0;
};

struct PerfTable {
    static El* Render(PerfTable* self, Ctx* cx);
    static void OnScroll(PerfTable* self, Ctx* cx, const ScrollEvent* ev);
    Stock rows[kPerfTableRows] = {};
    VirtualListScrollHandle handle = {};
    int tick = 0;
    bool ready = false;
};

void TableInit(PerfTable* table);
void TableTick(PerfTable* table);

struct MessageList {
    static El* Render(MessageList* self, Ctx* cx);
    static void OnScroll(MessageList* self, Ctx* cx, const ScrollEvent* ev);
    VirtualListScrollHandle handle = {};
    float heights[kPerfMessages] = {};
    bool ready = false;
};

void MessageListInit(MessageList* list);

struct Feed {
    float last[kPerfSymbols] = {};
    float spark[kPerfSymbols * kPerfSpark] = {};
    uint32_t volume[kPerfSymbols] = {};
    float candles[120] = {};
    int tick = 0;
    int panelSymbol = 7;
    bool ready = false;
};

struct HiddenPanel {
    int seen = 0;
    static void OnFeed(HiddenPanel* self, Ctx* cx, const EntityId* id);
};

struct Watchlist {
    static El* Render(Watchlist* self, Ctx* cx);
    static void OnScroll(Watchlist* self, Ctx* cx, const ScrollEvent* ev);
    Entity<Feed> feed = {};
    VirtualListScrollHandle handle = {};
    int hover = -1;
};

struct Panels {
    static El* Render(Panels* self, Ctx* cx);
    Entity<Feed> feed = {};
};

struct Workspace {
    static El* Render(Workspace* self, Ctx* cx);
    Entity<Feed> feed = {};
    Entity<Watchlist> list = {};
    Entity<Panels> panels = {};
    Entity<HiddenPanel> hidden[kPerfHidden] = {};
    bool streaming = false;
    int quotesPerTick = 16;
    bool ready = false;
};

void WorkspacePrepare(App* app, Workspace* w);
void WorkspaceTick(App* app, Window* win, Workspace* w);

struct Components {
    static El* Render(Components* self, Ctx* cx);
    static void OnToggle(Components* self, Ctx* cx, const ClickEvent* ev,
                         int64_t key);
    int seed = 8;
    int unread = 3;
};

struct Sidebar {
    static El* Render(Sidebar* self, Ctx* cx);
    static void OnScroll(Sidebar* self, Ctx* cx, const ScrollEvent* ev);
    float scroll = 0;
    float dir = 1;
    int active = kPerfPageButton;
};

struct Container {
    static El* Render(Container* self, Ctx* cx);
    static void OnPageScroll(Container* self, Ctx* cx, const ScrollEvent* ev);
    void Show(App* app, int page);

    Entity<Components> components = {};
    Entity<PerfTable> table = {};
    Entity<MessageList> messages = {};
    Entity<Workspace> workspace = {};
    PerfKind kind = PerfKind::Components;
    int page = kPerfPageButton;
    float scroll = 0;
    float dir = 1;
    bool refreshing = false;
};

struct Spinner {
    static El* Render(Spinner* self, Ctx* cx);
};

struct PerfSample {
    float drawMs = 0;
    float cpuMs = 0;
    float wallMs = 0;
    float buildMs = 0;
    float layoutMs = 0;
    float paintMs = 0;
};

struct PerfResult {
    PerfScenario scenario = PerfScenario::Spinner;
    int n = 0;
    float fps = 0;
    float medianMs = 0;
    float p95Ms = 0;
    float cpuP50Ms = 0;
    float threadPct = 0;
    float buildMs = 0;
    float layoutMs = 0;
    float paintMs = 0;
    float memMb = 0;
};

struct PerfRoot {
    static El* Render(PerfRoot* self, Ctx* cx);
    static void OnTick(PerfRoot* self, Ctx* cx, const TickEvent* ev);

    Entity<Sidebar> sidebar = {};
    Entity<Container> container = {};
    Entity<Spinner> spinner = {};
    bool autoRun = false;
    bool demo = false;
    bool retentionAsked = false;
    int frames = 240;
    PerfScenario runs[9] = {};
    int runCount = 0;
    int run = 0;
    int step = 0;
    int unread = 3;
    bool timerArmed = false;
    double lastCpu = -1;
    double lastWall = -1;
    double lastQuote = 0;
    double lastRefresh = 0;
    double demoStamp = 0;
    PerfScroll scroll = PerfScroll::Off;
    float scrollDir = 1;
    bool spinning = false;
    Vec<PerfSample> samples{};
    PerfResult results[9] = {};
    int resultCount = 0;
    bool reported = false;
};

} // namespace gpui
#endif
