#include "Story.h"

// crates/story has no dock page at the pinned SHA: the dock is what the
// gallery itself is built out of, and every story is a Panel inside one. This
// page is a demo of its own, with labelled boxes for panels so it is about
// the dock and not about what is inside it.
struct DockPanelData {
    // Panel::panel_name(), which is what a saved layout stores. The Rust
    // story's panels are all "StoryContainer"; each of these is its own kind,
    // so a layout read back knows which is which.
    const char* name;
    const char* title;
    const char* body;
};

static DockPanelData kPanels[] = {
    {"ExplorerPanel", "Explorer",
     "The left Dock. Drag its inner edge to resize it, or use the "
     "toggle button in the tab bar to close it."},
    {"OutlinePanel", "Outline", "A second panel in the left Dock's tab group."},
    {"EditorPanel", "main.cpp",
     "Drag a tab onto another group to merge it, or onto an edge "
     "of one to split that group."},
    {"ReadmePanel", "README.md",
     "The centre item is a split of two tab groups. The handle "
     "between them resizes both."},
    {"PreviewPanel", "Preview", "The right half of the centre split."},
    {"TerminalPanel", "Terminal",
     "The bottom Dock keeps its tab bar when it is closed, so "
     "there is still something to click."},
    {"ProblemsPanel", "Problems", "A second panel in the bottom Dock."},
    {"PropertiesPanel", "Properties", "The right Dock."},
};

const int kNPanels = (int)(sizeof(kPanels) / sizeof(kPanels[0]));

struct DockStory {
    Entity<DockState> dock = {};
    bool seeded = false;
    // What the last DockEvent said, shown under the area.
    Str message = {};
    bool locked = false;
    // The Options menu's "Tab close buttons", off by default.
    bool closeButtonVisible = false;

    // The layout as JSON, which is what DockArea::dump answers with — the
    // story keeps it in hand rather than on disk.
    Str saved = {};
    Arena* savedArena = nullptr;

    static El* Render(DockStory* self, Ctx* cx);
    static void OnDockEvent(DockStory* self, Ctx* cx, const DockEvent* ev);
    static void OnToggleLock(DockStory* self, Ctx* cx, const ClickEvent* ev);
    static void OnToggleCloseButtons(DockStory* self, Ctx* cx,
                                     const ClickEvent* ev, intptr_t checked);
    static void OnSaveLayout(DockStory* self, Ctx* cx, const ClickEvent* ev);
    static void OnLoadLayout(DockStory* self, Ctx* cx, const ClickEvent* ev);
    static void OnLoadStale(DockStory* self, Ctx* cx, const ClickEvent* ev);
};

static El* RenderPanel(Ctx* cx, void* data) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const DockPanelData* d = (const DockPanelData*)data;
    El* box = Div(a)->FlexCol()->Gap(8)->Pad(12)->W(kFill);
    box->Child(StoryTxt(cx, Str(d->title), 14, th.foreground));
    box->Child(StoryTxt(cx, Str(d->body), 13, th.mutedFg)->Wrap());
    return box;
}

// Panel::title_suffix. Rust's dock example gives every panel a pair of icon
// buttons in its own tab bar; this is the same hook, and it is what proves
// the bar leaves room for one.
static El* RenderPanelSuffix(Ctx* cx, void* data) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const DockPanelData* d = (const DockPanelData*)data;
    El* row = Div(a)->FlexRow()->ItemsCenter()->Gap(2)->PadX(4);
    row->Child(IconEl(a, IconName::Info, 14)->Fg(th.mutedFg));
    row->Child(IconEl(a, IconName::Search, 14)->Fg(th.mutedFg));
    (void)d;
    return row;
}

void DockStory::OnDockEvent(DockStory* self, Ctx* cx, const DockEvent*) {
    if (self->message.s) {
        StrFree(self->message);
    }
    self->message = StrDup(StrL("Layout changed"));
    Notify(cx);
}

void DockStory::OnToggleLock(DockStory* self, Ctx* cx, const ClickEvent*) {
    self->locked = !self->locked;
    DockState* s = self->dock.Get(cx);
    if (s) {
        s->locked = self->locked;
    }
    Notify(cx);
}

// DockSkin::set_close_button_visible, which the Rust story offers under
// Options as a checked "Tab close buttons" item.
void DockStory::OnToggleCloseButtons(DockStory* self, Ctx* cx,
                                     const ClickEvent*, intptr_t checked) {
    self->closeButtonVisible = checked != 0;
    component::DockSkin::New(self->dock)
        .SetCloseButtonVisible(cx->app, cx->win, self->closeButtonVisible);
    Notify(cx);
}

// DockArea::add_panel: the layout the story opens with.
static void Seed(DockStory* self, Ctx* cx) {
    self->dock = EntityNewState<DockState>(cx->app);
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
        def.titleSuffix = RenderPanelSuffix;
        // Panel::zoomable is a PanelControl, and its default — Menu — puts
        // Zoom In in the ⋯ menu and nowhere else. The editor asks for Both,
        // which is what puts the maximise icon on its bar; the Rust example
        // does the same for one of its panels.
        // The Rust story's Editor is also the one panel that is not
        // closable, which shows the close buttons' gate is real.
        if (i == 2) {
            def.zoomable = DockPanelControl::Both;
            def.closable = false;
        }
        def.data = &kPanels[i];
        panel[i] = DockAddPanelDef(s, def);
    }

    // Every Dock's item is a split holding the tab group, which is what the
    // Rust example does (`DockItem::v_split(vec![DockItem::tabs(..)])`) and
    // what gives the group a parent: a tab group that is the root of its own
    // tree is locked — TabPanel::is_locked's `stack_panel.is_none()` — so its
    // tabs neither drag out nor take a drop.
    int leftTabs = DockNewTabs(s);
    DockTabsAdd(s, leftTabs, panel[0]);
    DockTabsAdd(s, leftTabs, panel[1]);
    int leftSplit = DockNewSplit(s, Axis::Vertical);
    DockSplitAdd(s, leftSplit, leftTabs, 0);
    s->left.node = leftSplit;
    s->left.size = 180;

    int centerLeft = DockNewTabs(s);
    DockTabsAdd(s, centerLeft, panel[2]);
    DockTabsAdd(s, centerLeft, panel[3]);
    int centerRight = DockNewTabs(s);
    DockTabsAdd(s, centerRight, panel[4]);
    int split = DockNewSplit(s, Axis::Horizontal);
    DockSplitAdd(s, split, centerLeft, 320);
    DockSplitAdd(s, split, centerRight, 220);
    s->center = split;

    int bottomTabs = DockNewTabs(s);
    DockTabsAdd(s, bottomTabs, panel[5]);
    DockTabsAdd(s, bottomTabs, panel[6]);
    int bottomSplit = DockNewSplit(s, Axis::Vertical);
    DockSplitAdd(s, bottomSplit, bottomTabs, 0);
    s->bottom.node = bottomSplit;
    s->bottom.size = 140;

    int rightTabs = DockNewTabs(s);
    DockTabsAdd(s, rightTabs, panel[7]);
    int rightSplit = DockNewSplit(s, Axis::Vertical);
    DockSplitAdd(s, rightSplit, rightTabs, 0);
    s->right.node = rightSplit;
    s->right.size = 180;
}

// DockArea::dump: the tree as JSON.
void DockStory::OnSaveLayout(DockStory* self, Ctx* cx, const ClickEvent*) {
    DockState* s = self->dock.Get(cx);
    if (!s) {
        return;
    }
    DockAreaState state;
    DockDump(s, &state);
    StrBuilder sb;
    DockAreaStateWrite(&state, &sb);
    if (self->saved.s) {
        StrFree(self->saved);
    }
    self->saved = sb.TakeStr();
    if (self->message.s) {
        StrFree(self->message);
    }
    self->message = StrDup(StrL("Layout saved"));
    Notify(cx);
}

// DockArea::load, with the panels matched by name.
static void LoadFrom(DockStory* self, Ctx* cx, Str json) {
    DockState* s = self->dock.Get(cx);
    if (!s || !json.s) {
        return;
    }
    // The strings the loaded panels keep have to outlive the frame, so the
    // story holds the arena the state is parsed into.
    if (self->savedArena) {
        ArenaDelete(self->savedArena);
    }
    self->savedArena = ArenaNew();
    DockAreaState state;
    if (!DockAreaStateParse(self->savedArena, json, &state)) {
        return;
    }
    DockLoad(s, &state, self->savedArena, component::DockInvalidPanelRender);
    if (self->message.s) {
        StrFree(self->message);
    }
    self->message = StrDup(StrL("Layout loaded"));
    Notify(cx);
}

void DockStory::OnLoadLayout(DockStory* self, Ctx* cx, const ClickEvent*) {
    LoadFrom(self, cx, self->saved);
}

// A layout written by an older build, naming a panel this one does not have:
// the dock keeps its shape and says which type is missing, which is what
// Rust's InvalidPanel is for.
void DockStory::OnLoadStale(DockStory* self, Ctx* cx, const ClickEvent*) {
    LoadFrom(self, cx,
             StrL("{\"center\":{\"panel_name\":\"TabPanel\",\"children\":["
                  "{\"panel_name\":\"EditorPanel\",\"children\":[],"
                  "\"info\":{\"panel\":null}},"
                  "{\"panel_name\":\"GitGraphPanel\",\"children\":[],"
                  "\"info\":{\"panel\":null}}],"
                  "\"info\":{\"tabs\":{\"active_index\":1}}}}"));
}

El* DockStory::Render(DockStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        Seed(self, cx);
        DockState* s = self->dock.Get(cx);
        if (s) {
            s->onEvent = Listen(cx, &DockStory::OnDockEvent);
        }
    }
    El* page = Div(a)->FlexCol()->Gap(16)->W(kFill);

    El* section =
        StorySection(cx, "Dock",
                     "A centre item with a Dock on the left, the right and "
                     "the bottom. Tabs move between groups by dragging.");
    El* box = Div(a)
                  ->FlexCol()
                  ->W(kFill)
                  ->H(520)
                  ->Border(1, th.border)
                  ->Radius(th.radius);
    box->Child(component::DockArea::New(cx, StrL("dock"), self->dock)
                   ->IntoEl());
    StorySectionAdd(section, box);

    El* row = Div(a)->FlexRow()->Gap(12)->ItemsCenter()->W(kFill);
    row->Child(
        component::Button::New(cx, StrL("dock-lock"))
            ->Label(self->locked ? StrL("Unlock layout") : StrL("Lock layout"))
            ->Compact()
            ->OnClick(Listen(cx, &DockStory::OnToggleLock))
            ->IntoEl());
    row->Child(component::Checkbox::New(cx, StrL("dock-close-buttons"))
                   ->Checked(self->closeButtonVisible)
                   ->Label(StrL("Tab close buttons"))
                   ->OnClick(Listen(cx, &DockStory::OnToggleCloseButtons))
                   ->IntoEl());
    row->Child(component::Button::New(cx, StrL("dock-save"))
                   ->Label(StrL("Save layout"))
                   ->Compact()
                   ->OnClick(Listen(cx, &DockStory::OnSaveLayout))
                   ->IntoEl());
    row->Child(component::Button::New(cx, StrL("dock-load"))
                   ->Label(StrL("Load layout"))
                   ->Compact()
                   ->Disabled(!self->saved.s)
                   ->OnClick(Listen(cx, &DockStory::OnLoadLayout))
                   ->IntoEl());
    row->Child(component::Button::New(cx, StrL("dock-stale"))
                   ->Label(StrL("Load stale layout"))
                   ->Compact()
                   ->OnClick(Listen(cx, &DockStory::OnLoadStale))
                   ->IntoEl());
    if (self->message.s) {
        row->Child(StoryTxt(cx, self->message, 13, th.mutedFg));
    }
    StorySectionAdd(section, row);
    page->Child(section);
    return page;
}

STORY_PAGE(StoryDock, DockStory);
