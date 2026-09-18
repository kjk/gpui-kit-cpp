/* crates/story/examples/tiles.rs — four floating panels over one area, and
   the layout kept on disk between runs.

   The tiles example is the dock one with a Tiles node for its centre: four
   panels 380 by 280 with a twenty pixel gap, moved by their bars and resized
   by their edges. Each is wrapped the way Rust's ContainerPanel wraps a
   story — a panel that adds a search field to its own title bar and carries
   the panel it holds through a save — which is what `title_suffix` on a tile
   is for.

   The layout is written to `target/tiles.json` as a dock area whose centre is
   a Tiles node with a TileMeta per tile, which is the shape `dock/state.rs`
   persists one as, and read back the same way. The panels are named by index
   there, since a name is what a saved tile is matched by and this example's
   panels are its own rather than a registry's. */

#include "gpui.h"

using namespace gpui;

static const int kLayoutVersion = 1;
static const char* kStateFile = "target/tiles.json";

// PANELS: four, alternating between the two stories upstream tiles.
struct TileDef {
    const char* title;
    const char* body;
};

static const TileDef kTiles[] = {
    {"Button", "A panel in a tile. Drag the bar to move it."},
    {"Icon", "Drag an edge to resize; a near edge snaps to a neighbour's."},
    {"Button", "The tile moved last comes to the front."},
    {"Icon", "The search field in the bar is the panel's title_suffix."},
};
static const int kNTiles = (int)(sizeof(kTiles) / sizeof(kTiles[0]));

struct TilesApp {
    Entity<TilesState> tiles = {};
    // One search field per panel, which is what ContainerPanel gives each of
    // the stories it wraps.
    InputState search[kNTiles];
    bool seeded = false;
    Str lastSaved = {};
    Str message = {};

    ~TilesApp() {
        StrFree(lastSaved);
        StrFree(message);
    }
    static El* Render(TilesApp* self, Ctx* cx);
};

static void Say(TilesApp* self, Str what) {
    StrFree(self->message);
    self->message = StrDup(what);
}

// The tiles as a saved dock layout: one Tiles node, a TileMeta per tile, and
// a child named after the panel it holds.
static Str DumpTiles(TilesState* s) {
    DockAreaState state;
    state.hasVersion = true;
    state.version = kLayoutVersion;
    state.center = state.NewNode(StrL("Tiles"));
    int n = s->items.len;
    Vec<TileMeta> metas;
    Vec<int> panels;
    VecAppendBlanks(metas, n);
    VecAppendBlanks(panels, n);
    int nMeta = TilesToMetas(s, metas.els, panels.els, n);
    Vec<int> childIx;
    for (int i = 0; i < nMeta; i++) {
        VecAppend(childIx, state.NewNode(StrDup(fmt("%d", panels[i]))));
    }
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
    return sb.TakeStr();
}

static void SaveTiles(TilesApp* self, TilesState* s) {
    if (!s) {
        return;
    }
    Str json = DumpTiles(s);
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
        Say(self, StrL("Could not write target/tiles.json"));
    }
    StrFree(self->lastSaved);
    self->lastSaved = json;
}

// load_tiles: the metas the file holds, back onto the tiles they name.
static bool LoadTiles(TilesApp* self, TilesState* s) {
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
    Arena* a = ArenaNew();
    char* buf = (char*)Alloc(a, (int)size + 1);
    if (!buf) {
        fclose(f);
        ArenaDelete(a);
        return false;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;
    DockAreaState state;
    bool ok = DockAreaStateParse(a, Str(buf, (int)got), &state);
    if (ok && (!state.hasVersion || state.version != kLayoutVersion)) {
        Say(self, StrL("Saved layout is from another version — reset"));
        ok = false;
    }
    if (ok && state.center >= 0) {
        const PanelStateNode& node = state.nodes[state.center];
        int n = node.metas.len < node.children.len ? node.metas.len
                                                   : node.children.len;
        Vec<TileMeta> metas;
        Vec<int> panels;
        for (int i = 0; i < n; i++) {
            VecAppend(metas, node.metas[i]);
            // The child's name is the panel index it was saved under.
            Str name = state.nodes[node.children[i]].panelName;
            int p = 0;
            for (int k = 0; k < len(name); k++) {
                if (name.s[k] >= '0' && name.s[k] <= '9') {
                    p = p * 10 + (name.s[k] - '0');
                }
            }
            VecAppend(panels, p);
        }
        if (n > 0) {
            TilesFromMetas(s, metas.els, panels.els, n);
            Say(self, StrL("Layout loaded"));
        } else {
            ok = false;
        }
        VecReset(metas);
        VecReset(panels);
    } else {
        ok = false;
    }
    ArenaDelete(a);
    return ok;
}

// init_default_layout: 380x280 panels, 20px apart, four to a row.
static void SeedDefault(TilesState* s) {
    const float panelW = 380;
    const float panelH = 280;
    const float gap = 20;
    const float startX = 20;
    const float startY = 20;
    const int cols = 4;
    for (int i = 0; i < kNTiles; i++) {
        int row = i / cols;
        int col = i % cols;
        Bounds b = {startX + (panelW + gap) * (float)col,
                    startY + (panelH + gap) * (float)row, panelW, panelH};
        TilesAdd(s, i, b);
    }
}

static void OnUndo(TilesApp* self, Ctx* cx, const ClickEvent*) {
    if (TilesState* s = self->tiles.Get(cx)) {
        TilesUndo(s);
        SaveTiles(self, s);
    }
    Notify(cx);
}

static void OnRedo(TilesApp* self, Ctx* cx, const ClickEvent*) {
    if (TilesState* s = self->tiles.Get(cx)) {
        TilesRedo(s);
        SaveTiles(self, s);
    }
    Notify(cx);
}

El* TilesApp::Render(TilesApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        self->tiles = EntityNewState<TilesState>(cx->app);
        TilesState* s = self->tiles.Get(cx);
        if (s) {
            SeedDefault(s);
            LoadTiles(self, s);
        }
        for (int i = 0; i < kNTiles; i++) {
            InputSetPlaceholder(&self->search[i], StrL("Search..."));
        }
    }
    // A tile whose search field has focus takes the keyboard.
    for (int i = 0; i < kNTiles; i++) {
        if (self->search[i].focused) {
            cx->win->input = &self->search[i];
        }
    }

    component::Tiles* tiles =
        component::Tiles::New(cx, StrL("story-tiles"), self->tiles);
    for (int i = 0; i < kNTiles; i++) {
        El* body = Div(a)->FlexCol()->Gap(8)->Pad(12)->W(kFill);
        body->Child(
            TextEl(a, Str(kTiles[i].body))->Font(13)->Fg(th.mutedFg)->Wrap());
        // title_suffix: `div().w_24().h_6().rounded(radius_lg).border_1()`
        // around an xsmall Input with no appearance of its own.
        El* suffix = Div(a)
                         ->W(96)
                         ->H(24)
                         ->PadX(2)
                         ->Radius(th.radiusLg)
                         ->Border(1, th.inputBorder)
                         ->Child(component::Input::New(
                                     cx, StrDup(a, fmt("tile-search-%d", i)),
                                     &self->search[i])
                                     ->WithSize(UiSize::XSmall)
                                     ->Appearance(false)
                                     ->IntoEl());
        tiles->Panel(Str(kTiles[i].title), body, suffix);
    }

    El* bar = Div(a)
                  ->FlexRow()
                  ->W(kFill)
                  ->ItemsCenter()
                  ->Gap(8)
                  ->PadX(8)
                  ->PadY(4)
                  ->Bg(th.tokens.statusBar)
                  ->BorderT(1, th.statusBarBorder);
    bar->Child(component::Button::New(cx, StrL("tiles-undo"))
                   ->Label(StrL("Undo"))
                   ->Ghost()
                   ->WithSize(UiSize::XSmall)
                   ->OnClick(Listen(cx, &OnUndo))
                   ->IntoEl());
    bar->Child(component::Button::New(cx, StrL("tiles-redo"))
                   ->Label(StrL("Redo"))
                   ->Ghost()
                   ->WithSize(UiSize::XSmall)
                   ->OnClick(Listen(cx, &OnRedo))
                   ->IntoEl());
    bar->Child(
        TextEl(a, self->message.s ? self->message : StrL("target/tiles.json"))
            ->Font(13)
            ->Fg(th.mutedFg));

    return Div(a)
        ->FlexCol()
        ->SizeFull()
        ->Bg(th.tokens.background)
        ->Child(component::TitleBar::New(cx)
                    ->Child(TextEl(a, StrL("Tiles"))->Font(13)->Medium())
                    ->IntoEl())
        ->Child(Div(a)->Flex1()->W(kFill)->Child(tiles->IntoEl()))
        ->Child(bar);
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    AssetsClear();
    AssetsAddDefaultRoots(StrL("tiles"));
    Entity<TilesApp> view = EntityNew<TilesApp>(app);
    // Rust asks for 1600x1200 and caps it at 85% of the display; nothing
    // here can ask how big the display is, so this is the size that fits the
    // four tiles and their gaps without assuming a big screen.
    Window* win = WindowOpenView(app, StrL("Tiles Example"), 1400, 900, view.id,
                                 WinOpts{});
    (void)win;
    int rc = AppRun(app);
    // The layout as it stands when the window goes.
    if (TilesApp* self = view.Get(app)) {
        SaveTiles(self, self->tiles.Get(app));
    }
    AppFree(app);
    return rc;
}
