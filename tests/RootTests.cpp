/* Ported from crates/component/src/root.rs and crates/base/src/root.rs.
 *
 * Base's Root captures the registered plugins once per window, so
 * registering the same plugin twice mounts it once and each window gets an
 * instance of its own; its surface puts every plugin's overlay above the
 * content and lets the plugins decorate it.
 *
 * Root's own rules about the layers over the page: `render_dialog_layer`
 * walks the open dialogs and lets the last one that wants an overlay show it,
 * so a stack of them tints the page once; `render_notification_layer` insets
 * the notifications by the room an open sheet takes on its own edge. */

#include "Test.h"

using namespace gpui::component;

namespace {
int gLayersBuilt = 0;
struct LayerState {
    int id = 0;
};
void* BuildLayer(Window*, App*) {
    LayerState* s = new LayerState();
    s->id = ++gLayersBuilt;
    return s;
}
void DropLayer(void* p) {
    delete (LayerState*)p;
}
El* RenderLayer(void*, Ctx* cx) {
    return Div(cx->a)->Id(StrL("layer"));
}
El* DecorateLayer(void*, El* surface, const gpui::Root*, Ctx* cx) {
    return Div(cx->a)->Id(StrL("decoration"))->Child(surface);
}
const RootPlugin kLayer = {&BuildLayer, &DropLayer,     nullptr,
                           nullptr,     &DecorateLayer, &RenderLayer};

struct Content {
    static El* Render(Content*, Ctx* cx) {
        return Div(cx->a)->Id(StrL("content"));
    }
};
} // namespace

// base root.rs: plugin_registration_is_idempotent_and_state_is_per_window.
static void PluginRegistrationIsIdempotentAndStateIsPerWindow() {
    App app;
    gpui::Root::RegisterPlugin(&app, &kLayer);
    gpui::Root::RegisterPlugin(&app, &kLayer);
    Window* wins[2] = {};
    void* states[2] = {};
    for (int i = 0; i < 2; i++) {
        wins[i] = new Window();
        wins[i]->app = &app;
        Entity<Content> content = EntityNew<Content>(&app);
        Entity<gpui::Root> root = gpui::Root::New(&app, wins[i], content.id);
        wins[i]->root = root.id;
        const gpui::Root* read = gpui::Root::Read(wins[i]);
        utassert(read && read->View() == content.id);
        utassert(read && read->plugins.len == 1);
        states[i] = read ? read->Plugin(&kLayer) : nullptr;
        utassert(states[i] != nullptr);
    }
    utassert(states[0] != states[1]);
    utassert(((LayerState*)states[0])->id != ((LayerState*)states[1])->id);

    // The surface: content first, the plugin's overlay above it, and the
    // plugin's decoration around the lot.
    Arena* arena = ArenaNew();
    Ctx cx = {&app, wins[0], arena, {}};
    El* surface = gpui::Root::Render(gpui::Root::Read(wins[0]), &cx);
    utassert(surface && base::StrEq(surface->id, StrL("decoration")));

    for (int i = 0; i < 2; i++) {
        WindowKeyedFree(wins[i]);
        delete wins[i];
    }
    ArenaDelete(arena);
    EntityDropAll(&app);
}

static El* FindRootId(El* e, Str id) {
    if (!e) {
        return nullptr;
    }
    if (e->id.s && base::StrEq(e->id, id)) {
        return e;
    }
    for (El* c = e->first; c; c = c->next) {
        if (El* found = FindRootId(c, id)) {
            return found;
        }
    }
    return nullptr;
}

// base root.rs render: the surface is `div().id("root")`, and component
// root.rs WindowState::prepare sets the window's rem size from the theme
// before anything is built.
static void TheSurfaceIsRootAndTheRemSizeIsTheThemes() {
    App app;
    component::Init(&app);
    ThemeSetFontSize(&app, 18);
    Window* win = new Window();
    win->app = &app;
    Entity<Content> content = EntityNew<Content>(&app);
    Entity<gpui::Root> root = gpui::Root::New(&app, win, content.id);
    win->root = root.id;
    utassert(WindowRemSize(win) == 16.f);
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    El* surface = gpui::Root::Render(root.Get(&app), &cx);
    El* rootEl = FindRootId(surface, StrL("root"));
    utassert(rootEl && FindRootId(rootEl, StrL("content")));
    utassert(WindowRemSize(win) == 18.f);
    ArenaDelete(arena);
    EntityDropAll(&app);
    WindowKeyedFree(win);
    delete win;
    AppGlobalClear(&app);
}

static void TheLastDialogThatWantsAnOverlayShowsIt() {
    const bool three[] = {true, false, true};
    utassert(RootDialogOverlayIndex(three, 3) == 2);
    // Only the first asked, so the overlay is under the first.
    const bool first[] = {true, false, false};
    utassert(RootDialogOverlayIndex(first, 3) == 0);
    // None of them asked, and none is shown.
    const bool none[] = {false, false};
    utassert(RootDialogOverlayIndex(none, 2) == -1);
    utassert(RootDialogOverlayIndex(nullptr, 0) == -1);
}

static void AnOpenSheetPushesTheNotificationsIn() {
    Edges e = RootNotificationInsets(true, SheetPlacement::Right, 350);
    utassertnear(e.right, 350.f);
    utassertnear(e.left, 0.f);
    utassertnear(e.top, 0.f);
    utassertnear(e.bottom, 0.f);

    e = RootNotificationInsets(true, SheetPlacement::Left, 350);
    utassertnear(e.left, 350.f);
    e = RootNotificationInsets(true, SheetPlacement::Top, 200);
    utassertnear(e.top, 200.f);
    e = RootNotificationInsets(true, SheetPlacement::Bottom, 200);
    utassertnear(e.bottom, 200.f);

    // With no sheet open they fill the window.
    e = RootNotificationInsets(false, SheetPlacement::Right, 350);
    utassertnear(e.right, 0.f);
    utassertnear(e.HorizontalAxisSum(), 0.f);
    utassertnear(e.VerticalAxisSum(), 0.f);
}

void TestRoot() {
    TestSuite("root");
    PluginRegistrationIsIdempotentAndStateIsPerWindow();
    TheLastDialogThatWantsAnOverlayShowsIt();
    AnOpenSheetPushesTheNotificationsIn();
    TheSurfaceIsRootAndTheRemSizeIsTheThemes();
}
