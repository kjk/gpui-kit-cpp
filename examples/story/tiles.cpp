#include "Story.h"

// crates/story has no tiles page at the pinned SHA; `Tiles` is a crates/ui
// widget the gallery does not put on a page. This one is a demo of its own:
// panels that float over the area rather than splitting it, each moved by
// its bar and resized by its edges.
struct TilePanelData {
    const char* title;
    const char* body;
    Bounds bounds;
};

static TilePanelData kTiles[] = {
    {"Notes",
     "Drag this bar to move the tile. It snaps flush to a "
     "neighbour's edge, and to the top and left of the area.",
     {16, 16, 300, 200}},
    {"Chart",
     "Drag an edge to resize. An edge close to a neighbour's snaps "
     "to it; one close to nothing rounds to the 8px grid.",
     {340, 16, 260, 160}},
    {"Console",
     "The tile that was moved last comes to the front.",
     {16, 240, 400, 140}},
};

const int kNTiles = (int)(sizeof(kTiles) / sizeof(kTiles[0]));

struct TilesStory {
    Entity<TilesState> tiles = {};
    bool seeded = false;
    // The layout the page last saved, as the JSON a dock area is persisted
    // as — a Tiles node with one TileMeta per tile.
    Str saved = {};

    static El* Render(TilesStory* self, Ctx* cx);
    static void OnUndo(TilesStory* self, Ctx* cx, const ClickEvent* ev);
    static void OnRedo(TilesStory* self, Ctx* cx, const ClickEvent* ev);
    static void OnSave(TilesStory* self, Ctx* cx, const ClickEvent* ev);
    static void OnRestore(TilesStory* self, Ctx* cx, const ClickEvent* ev);
};

// The tiles as a saved dock layout: one node, a TileMeta per tile, written
// out the way DockAreaState persists one.
void TilesStory::OnSave(TilesStory* self, Ctx* cx, const ClickEvent*) {
    TilesState* s = self->tiles.Get(cx);
    if (!s) {
        return;
    }
    DockAreaState state;
    state.center = state.NewNode(StrL("Tiles"));
    int n = s->items.len;
    Vec<TileMeta> metas;
    Vec<int> panels;
    VecAppendBlanks(metas, n);
    VecAppendBlanks(panels, n);
    int nMeta = TilesToMetas(s, metas.els, panels.els, n);
    Vec<int> childIx;
    for (int i = 0; i < nMeta; i++) {
        // The child a meta belongs to. Rust names it after the panel's type
        // and finds it again through the registry; the panels here are the
        // caller's own list, so the index is the name.
        VecAppend(childIx, state
                               .NewNode(StrDup(StoryFmt(cx, "%d", panels[i]))));
    }
    // NewNode grew the pool, so the node is reached again after it has.
    PanelStateNode& node = state.nodes[state.center];
    node.kind = PanelInfoKind::Tiles;
    for (int i = 0; i < nMeta; i++) {
        VecAppend(node.metas, metas[i]);
        VecAppend(node.children, childIx[i]);
    }
    VecReset(childIx);
    VecReset(metas);
    VecReset(panels);
    StrBuilder sb;
    DockAreaStateWrite(&state, &sb);
    if (self->saved.s) {
        StrFree(self->saved);
    }
    self->saved = sb.TakeStr();
    Notify(cx);
}

// And back: the layout is read and every tile goes where its meta says.
void TilesStory::OnRestore(TilesStory* self, Ctx* cx, const ClickEvent*) {
    TilesState* s = self->tiles.Get(cx);
    if (!s || !self->saved.s) {
        return;
    }
    Arena* a = ArenaNew();
    DockAreaState state;
    if (DockAreaStateParse(a, self->saved, &state) && state.center >= 0) {
        const PanelStateNode& node = state.nodes[state.center];
        int n = node.children.len < node.metas.len ? node.children.len
                                                   : node.metas.len;
        Vec<int> panels;
        for (int i = 0; i < n; i++) {
            VecAppend(panels, StrToIntUnchecked(Str(
                                  state.nodes[node.children[i]].panelName.s)));
        }
        TilesFromMetas(s, node.metas.els, panels.els, n);
        VecReset(panels);
    }
    ArenaDelete(a);
    Notify(cx);
}

void TilesStory::OnUndo(TilesStory* self, Ctx* cx, const ClickEvent*) {
    TilesState* s = self->tiles.Get(cx);
    if (s) {
        TilesUndo(s);
    }
    Notify(cx);
}

void TilesStory::OnRedo(TilesStory* self, Ctx* cx, const ClickEvent*) {
    TilesState* s = self->tiles.Get(cx);
    if (s) {
        TilesRedo(s);
    }
    Notify(cx);
}

El* TilesStory::Render(TilesStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        self->tiles = EntityNewState<TilesState>(cx->app);
        TilesState* s = self->tiles.Get(cx);
        for (int i = 0; s && i < kNTiles; i++) {
            TilesAdd(s, i, kTiles[i].bounds);
        }
    }

    El* page = Div(a)->FlexCol()->Gap(16)->W(kFill);
    El* section = StorySection(
        cx, "Tiles",
        "Panels that float over the area: move one by its bar, resize it by "
        "an edge, and undo what the last drag did.");

    component::Tiles* tiles =
        component::Tiles::New(cx, StrL("story-tiles"), self->tiles);
    for (int i = 0; i < kNTiles; i++) {
        El* body = Div(a)->FlexCol()->Gap(8)->Pad(12)->W(kFill);
        body->Child(StoryTxt(cx, Str(kTiles[i].body), 13, th.mutedFg)->Wrap());
        tiles->Panel(Str(kTiles[i].title), body);
    }

    El* row = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    row->Child(component::Button::New(cx, StrL("tiles-undo"))
                   ->Label(StrL("Undo"))
                   ->Outline()
                   ->OnClick(Listen(cx, &TilesStory::OnUndo))
                   ->IntoEl());
    row->Child(component::Button::New(cx, StrL("tiles-redo"))
                   ->Label(StrL("Redo"))
                   ->Outline()
                   ->OnClick(Listen(cx, &TilesStory::OnRedo))
                   ->IntoEl());
    row->Child(component::Button::New(cx, StrL("tiles-save"))
                   ->Label(StrL("Save layout"))
                   ->Outline()
                   ->OnClick(Listen(cx, &TilesStory::OnSave))
                   ->IntoEl());
    row->Child(component::Button::New(cx, StrL("tiles-restore"))
                   ->Label(StrL("Restore"))
                   ->Outline()
                   ->OnClick(Listen(cx, &TilesStory::OnRestore))
                   ->IntoEl());
    row->Child(StoryTxt(
        cx,
        self->saved.s ? StoryFmt(cx, "Saved layout: %d bytes", self->saved.len)
                      : StrL("Nothing saved yet"),
        13, th.mutedFg));
    El* col = Div(a)->FlexCol()->Gap(12)->W(kFill);
    col->Child(row);
    col->Child(tiles->IntoEl()
                   ->W(kFill)
                   ->H(440)
                   ->Border(1, th.border)
                   ->Radius(th.radius)
                   ->Bg(th.tokens.secondary));
    StorySectionAdd(section, col);
    page->Child(section);
    return page;
}

// Kept as a compatibility example implementation, but upstream removed Tiles
// from the gallery in 0.6.1 so it is no longer registered as a story page.
