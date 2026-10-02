#include "Story.h"

// crates/story/src/stories/dock_story.rs: a DockArea of five DemoPanels —
// Explorer and Search sharing a group beside the Editor, Terminal and
// Problems in a collapsible bottom Dock — under an Options menu whose one
// row shows the tabs' close buttons. Story::paddings is 0, so the area fills
// the pane (StoryContainerBody in story.cpp).
struct DockPanelData {
    // Panel::panel_name(): what a saved layout stores.
    const char* name;
    const char* title;
    const char* body;
};

static DockPanelData kPanels[] = {
    {"DockStoryExplorer", "Explorer", "Drag this tab into another group."},
    {"DockStorySearch", "Search", "Two panels can share one tab group."},
    {"DockStoryEditor", "Editor",
     "Drop a tab near an edge to split this group."},
    {"DockStoryTerminal", "Terminal",
     "The bottom dock shares the workspace column."},
    {"DockStoryProblems", "Problems", "No problems detected."},
};

const int kNPanels = (int)(sizeof(kPanels) / sizeof(kPanels[0]));

enum {
    DockActCloseButtons = 3400,
};

struct DockStory {
    Entity<DockState> dock = {};
    bool seeded = false;
    // The Options menu's "Tab close buttons", off by default.
    bool closeButtonVisible = false;
    StoryToolbarState toolbar;

    static El* Render(DockStory* self, Ctx* cx);
};

// DemoPanel::render: div().size_full().p_4().text_color(foreground).
static El* RenderPanel(Ctx* cx, void* data) {
    const Theme& th = ThemeNow(cx->app);
    const DockPanelData* d = (const DockPanelData*)data;
    return Div(cx->a)->SizeFull()->Pad(16)->Child(
        StoryTxt(cx, Str(d->body), 16, th.foreground)->Wrap());
}

// DockSkin::set_close_button_visible.
static void DockAct(DockStory* self, Ctx* cx, const ClickEvent*, int64_t act) {
    if (act == DockActCloseButtons) {
        self->closeButtonVisible = !self->closeButtonVisible;
        component::DockSkin::New(self->dock)
            .SetCloseButtonVisible(cx->app, cx->win, self->closeButtonVisible);
    } else {
        StoryToolbarApply(&self->toolbar, nullptr, (int)act);
    }
    Notify(cx);
}

// DockStory::new_view.
static void Seed(DockStory* self, Ctx* cx) {
    self->dock = EntityNewState<DockState>(cx->app);
    DockState* s = self->dock.Get(cx);
    if (!s) {
        return;
    }
    // DockSkin::dock_area("dock-story", Some(1), ..)
    s->hasVersion = true;
    s->version = 1;
    int panel[kNPanels];
    for (int i = 0; i < kNPanels; i++) {
        DockPanelDef def;
        def.name = Str(kPanels[i].name);
        def.title = Str(kPanels[i].title);
        def.render = RenderPanel;
        def.data = &kPanels[i];
        // editor.closable = false
        if (i == 2) {
            def.closable = false;
        }
        panel[i] = DockAddPanelDef(s, def);
    }

    // DockLayout::h_split(): the Explorer/Search group at 240px, the Editor
    // taking the rest.
    int left = DockNewTabs(s);
    DockTabsAdd(s, left, panel[0]);
    DockTabsAdd(s, left, panel[1]);
    int editor = DockNewTabs(s);
    DockTabsAdd(s, editor, panel[2]);
    int split = DockNewSplit(s, Axis::Horizontal);
    DockSplitAdd(s, split, left, 240);
    DockSplitAdd(s, split, editor, 0);
    s->center = split;

    // set_dock(Bottom, tabs(terminal, problems)), 160px and collapsible. A
    // Dock's item is a split holding the group, which gives the group the
    // parent that lets its tabs move.
    int bottomTabs = DockNewTabs(s);
    DockTabsAdd(s, bottomTabs, panel[3]);
    DockTabsAdd(s, bottomTabs, panel[4]);
    int bottomSplit = DockNewSplit(s, Axis::Vertical);
    DockSplitAdd(s, bottomSplit, bottomTabs, 0);
    s->bottom.node = bottomSplit;
    s->bottom.SetSize(160);
    DockSetCollapsible(s, DockPlacement::Bottom, true);
    component::DockSkin::New(self->dock)
        .SetToggleButtonVisible(cx->app, cx->win, true);
}

El* DockStory::Render(DockStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        Seed(self, cx);
    }
    El* page = Div(a)->FlexCol()->SizeFull();
    // story_toolbar_group().p_2().dropdown_child(Options, ..)
    StoryToolbarOpt opts[1] = {
        {"Tab close buttons", self->closeButtonVisible, DockActCloseButtons},
    };
    page->Child(Div(a)->Pad(8)->W(kFill)->Child(
        StoryToolbarOptions(cx, self, opts, 1, Listen(cx, &DockAct), false)));
    page->Child(
        Div(a)
            ->FlexCol()
            ->Flex1()
            ->MinH(0)
            ->W(kFill)
            ->BorderT(1, th.border)
            ->Child(component::DockArea::New(cx, StrL("dock-story"), self->dock)
                        ->IntoEl()));
    return page;
}

STORY_PAGE(StoryDock, DockStory);
