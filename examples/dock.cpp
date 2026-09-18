/* crates/story/examples/dock.rs — the dock as a workspace, with the layout
   kept on disk between runs.

   A DockArea filling the window under a title bar, with a Dock on the left,
   the right and the bottom around a split centre. The title bar's Add Panel
   menu adds one to any of the four places and shows or hides the docks'
   toggle buttons; the status bar under it says what the last DockEvent was.
   Every layout change is written to `target/docks.json` — `DockDump` to a
   DockAreaState, `DockAreaStateWrite` to JSON — and the next run reads it
   back through `DockAreaStateParse` and `DockLoad`. A file written by a
   layout with a different version is offered a reset, the way Rust's prompt
   does, and a panel name this build does not answer to comes back as the
   InvalidPanel that keeps its place rather than losing it.

   Upstream's panels are the story crate's own pages — the gallery is a
   library there and every story is a Panel. The story pages here are one
   binary's, so this example brings its own labelled panels; what it is about
   is the dock, not what is inside it. */

#include "gpui.h"

using namespace gpui;

// Rust's MAIN_DOCK_AREA: the id a saved layout is written under, and the
// version a reader checks before trusting it.
static const int kLayoutVersion = 5;
static const char* kStateFile = "target/docks.json";

struct DockPanelData {
    const char* name;
    const char* title;
    const char* body;
};

static const DockPanelData kPanels[] = {
    {"ListStory", "List", "The left Dock's first tab."},
    {"ScrollbarStory", "Scrollbar", "A second panel in the left Dock."},
    {"AccordionStory", "Accordion", "And a third, in a group of its own."},
    {"ButtonStory", "Button", "The centre, which is a split of two groups."},
    {"InputStory", "Input", "The other half of the centre split."},
    {"TooltipStory", "Tooltip", "The bottom Dock."},
    {"IconStory", "Icon", "A second panel in the bottom Dock."},
    {"ImageStory", "Image", "The right Dock."},
};
static const int kNPanels = (int)(sizeof(kPanels) / sizeof(kPanels[0]));

// The panels a menu can add, cycled the way Rust's AddPanel does — it makes
// a StoryContainer of a random story each time.
static int gNextPanel = 0;

struct DockApp {
    Entity<DockState> dock = {};
    bool seeded = false;
    bool toggleButtonVisible = true;
    // The layout as it was last written, so a change that says nothing new
    // does not rewrite the file.
    Str lastSaved = {};
    // The arena a loaded layout's strings live in; the panels keep them, so
    // it outlives the load.
    Arena* loadedArena = nullptr;
    Str message = {};

    ~DockApp() {
        StrFree(lastSaved);
        StrFree(message);
        if (loadedArena) {
            ArenaDelete(loadedArena);
        }
    }
    static El* Render(DockApp* self, Ctx* cx);
};

static El* RenderPanel(Ctx* cx, void* data) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const auto* d = (const DockPanelData*)data;
    El* box = Div(a)->FlexCol()->Gap(8)->Pad(12)->W(kFill);
    box->Child(TextEl(a, Str(d->title))->Font(14)->Fg(th.foreground));
    box->Child(TextEl(a, Str(d->body))->Font(13)->Fg(th.mutedFg)->Wrap());
    return box;
}

static void Say(DockApp* self, Str what) {
    StrFree(self->message);
    self->message = StrDup(what);
}

// save_state: the tree as JSON, on disk, and only when it has changed.
static void SaveLayoutFrom(DockApp* self, DockState* s) {
    if (!s) {
        return;
    }
    DockAreaState state;
    state.hasVersion = true;
    state.version = kLayoutVersion;
    DockDump(s, &state);
    state.hasVersion = true;
    state.version = kLayoutVersion;
    StrBuilder sb;
    DockAreaStateWrite(&state, &sb);
    Str json = sb.TakeStr();
    if (self->lastSaved.s && StrEq(json, self->lastSaved)) {
        StrFree(json);
        return;
    }
    FILE* f = fopen(kStateFile, "wb");
    if (f) {
        fwrite(json.s, 1, (size_t)len(json), f);
        fclose(f);
        Say(self, StrL("Layout saved"));
    } else {
        Say(self, StrL("Could not write target/docks.json"));
    }
    StrFree(self->lastSaved);
    self->lastSaved = json;
}

static void SaveLayout(DockApp* self, Ctx* cx) {
    SaveLayoutFrom(self, self->dock.Get(cx));
}

// load_layout: the file, if there is one this build understands.
static bool LoadLayout(DockApp* self, Ctx* cx) {
    FILE* f = fopen(kStateFile, "rb");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return false;
    }
    if (self->loadedArena) {
        ArenaDelete(self->loadedArena);
    }
    self->loadedArena = ArenaNew();
    char* buf = (char*)Alloc(self->loadedArena, (int)size + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;

    DockAreaState state;
    if (!DockAreaStateParse(self->loadedArena, Str(buf, (int)got), &state)) {
        Say(self, StrL("target/docks.json is not a layout"));
        return false;
    }
    // "The default main layout has been updated": a file from another
    // version is not read. Rust asks first; there is no prompt to ask with
    // here, so the default layout stands and the status line says so.
    if (!state.hasVersion || state.version != kLayoutVersion) {
        Say(self, StrL("Saved layout is from another version — reset"));
        return false;
    }
    DockState* s = self->dock.Get(cx);
    if (!s || !DockLoad(s, &state, self->loadedArena,
                        component::DockInvalidPanelRender)) {
        return false;
    }
    Say(self, StrL("Layout loaded"));
    return true;
}

static void OnDockEvent(DockApp* self, Ctx* cx, const DockEvent*) {
    // DockEvent::LayoutChanged. Rust waits ten seconds and writes if the
    // layout is not the one it last wrote; the write here is small enough to
    // do on the spot, and the comparison is the same.
    SaveLayout(self, cx);
    Notify(cx);
}

// reset_default_layout.
static void SeedDefault(DockApp* self, Ctx* cx) {
    DockState* s = self->dock.Get(cx);
    if (!s) {
        return;
    }
    int panel[kNPanels];
    for (int i = 0; i < kNPanels; i++) {
        DockPanelDef def;
        def.name = Str(kPanels[i].name);
        def.title = Str(kPanels[i].title);
        def.render = RenderPanel;
        def.data = (void*)&kPanels[i];
        panel[i] = DockAddPanelDef(s, def);
    }
    // Left: a tab of its own over a group of two, split vertically.
    int leftTop = DockNewTabs(s);
    DockTabsAdd(s, leftTop, panel[0]);
    int leftBottom = DockNewTabs(s);
    DockTabsAdd(s, leftBottom, panel[1]);
    DockTabsAdd(s, leftBottom, panel[2]);
    int leftSplit = DockNewSplit(s, Axis::Vertical);
    DockSplitAdd(s, leftSplit, leftTop, 240);
    DockSplitAdd(s, leftSplit, leftBottom, 360);
    s->left.node = leftSplit;
    s->left.size = 350;

    int centerLeft = DockNewTabs(s);
    DockTabsAdd(s, centerLeft, panel[3]);
    int centerRight = DockNewTabs(s);
    DockTabsAdd(s, centerRight, panel[4]);
    int center = DockNewSplit(s, Axis::Horizontal);
    DockSplitAdd(s, center, centerLeft, 420);
    DockSplitAdd(s, center, centerRight, 320);
    s->center = center;

    int bottom = DockNewTabs(s);
    DockTabsAdd(s, bottom, panel[5]);
    DockTabsAdd(s, bottom, panel[6]);
    s->bottom.node = bottom;
    s->bottom.size = 200;

    int right = DockNewTabs(s);
    DockTabsAdd(s, right, panel[7]);
    s->right.node = right;
    s->right.size = 320;
}

// AddPanel(placement): one more panel where the menu said.
static void AddPanelTo(DockApp* self, Ctx* cx, DockPlacement where) {
    DockState* s = self->dock.Get(cx);
    if (!s) {
        return;
    }
    const DockPanelData& d = kPanels[gNextPanel % kNPanels];
    gNextPanel++;
    DockPanelDef def;
    def.name = Str(d.name);
    def.title = Str(d.title);
    def.render = RenderPanel;
    def.data = (void*)&d;
    int panel = DockAddPanelDef(s, def);
    int* side = nullptr;
    switch (where) {
        case DockPlacement::Left:
            side = &s->left.node;
            break;
        case DockPlacement::Right:
            side = &s->right.node;
            break;
        case DockPlacement::Bottom:
            side = &s->bottom.node;
            break;
        default:
            break;
    }
    if (!side) {
        // Center: the group the centre already is, or a new one.
        int node = s->center;
        if (node < 0 || s->nodes[node].split) {
            // A split centre: the first group under it takes the panel, the
            // way Rust's add_panel walks to a TabPanel.
            node = node >= 0 && s->nodes[node].child.len > 0
                       ? s->nodes[node].child[0]
                       : DockNewTabs(s);
            if (s->center < 0) {
                s->center = node;
            }
        }
        DockTabsAdd(s, node, panel);
    } else {
        if (*side < 0) {
            *side = DockNewTabs(s);
        }
        int node = *side;
        if (s->nodes[node].split) {
            node =
                s->nodes[node].child.len > 0 ? s->nodes[node].child[0] : node;
        }
        DockTabsAdd(s, node, panel);
    }
    SaveLayout(self, cx);
    Notify(cx);
}

enum {
    kMenuAddCenter = 1,
    kMenuAddLeft,
    kMenuAddRight,
    kMenuAddBottom,
    kMenuToggleButton
};

static void OnMenuItem(DockApp* self, Ctx* cx, const ClickEvent*, intptr_t ix) {
    // The rows in the order they are built below; a separator is a row too.
    switch (ix) {
        case 0:
            AddPanelTo(self, cx, DockPlacement::Center);
            break;
        case 2:
            AddPanelTo(self, cx, DockPlacement::Left);
            break;
        case 3:
            AddPanelTo(self, cx, DockPlacement::Right);
            break;
        case 4:
            AddPanelTo(self, cx, DockPlacement::Bottom);
            break;
        case 6: {
            self->toggleButtonVisible = !self->toggleButtonVisible;
            DockState* s = self->dock.Get(cx);
            if (s) {
                // set_dock_collapsible: the toggle button in a dock's tab
                // bar is the collapsible flag, one per side.
                s->left.collapsible = self->toggleButtonVisible;
                s->right.collapsible = self->toggleButtonVisible;
                s->bottom.collapsible = self->toggleButtonVisible;
            }
            Notify(cx);
            break;
        }
        default:
            break;
    }
}

El* DockApp::Render(DockApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        self->dock = EntityNewState<DockState>(cx->app);
        DockState* s = self->dock.Get(cx);
        if (s) {
            s->onEvent = Listen(cx, &OnDockEvent);
        }
        if (!LoadLayout(self, cx)) {
            SeedDefault(self, cx);
        }
    }

    // AppTitleBar with the Add Panel menu on its right.
    component::PopupMenu* menu =
        component::PopupMenu::New(cx, StrL("add-panel"));
    menu->Menu(StrL("Add Panel to Center"))
        ->Separator()
        ->Menu(StrL("Add Panel to Left"))
        ->Menu(StrL("Add Panel to Right"))
        ->Menu(StrL("Add Panel to Bottom"))
        ->Separator()
        ->MenuWithCheck(StrL("Show Dock Toggle Button"),
                        self->toggleButtonVisible);
    if (PopupMenuState* ms = menu->state.Get(cx)) {
        ms->onConfirm = Listen(cx, &OnMenuItem);
    }
    El* addPanel = component::DropdownMenu::New(cx, StrL("add-panel"))
                       ->Trigger(component::Button::New(cx, StrL("add-panel"))
                                     ->Icon(IconName::LayoutDashboard)
                                     ->Ghost()
                                     ->WithSize(UiSize::Small)
                                     ->IntoEl())
                       ->Menu(menu)
                       ->AnchorRight()
                       ->IntoEl();

    El* titleRow = Div(a)
                       ->FlexRow()
                       ->W(kFill)
                       ->ItemsCenter()
                       ->JustifyBetween()
                       ->PadX(8)
                       ->Child(TextEl(a, StrL("Examples"))->Font(13)->Medium())
                       ->Child(addPanel);
    El* title = component::TitleBar::New(cx)->Child(titleRow)->IntoEl();

    El* area = component::DockArea::New(cx, StrL("main-dock"), self->dock)
                   ->IntoEl();

    component::StatusBar* bar = component::StatusBar::New(cx);
    bar->Left(self->message.s ? self->message : StrL("target/docks.json"));

    return Div(a)
        ->FlexCol()
        ->SizeFull()
        ->Bg(th.tokens.background)
        ->Child(title)
        ->Child(Div(a)->Flex1()->W(kFill)->ClipY()->Child(area))
        ->Child(bar->IntoEl());
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    AssetsClear();
    AssetsAddDefaultRoots(StrL("dock"));
    Entity<DockApp> view = EntityNew<DockApp>(app);
    Window* win = WindowOpenView(app, StrL("Dock Example"), 1280, 860, view.id,
                                 WinOpts{});
    (void)win;
    int rc = AppRun(app);
    // cx.on_app_quit: the layout as it stands when the window goes, so a run
    // that only moved a tab is not lost.
    if (DockApp* self = view.Get(app)) {
        SaveLayoutFrom(self, self->dock.Get(app));
    }
    AppFree(app);
    return rc;
}
