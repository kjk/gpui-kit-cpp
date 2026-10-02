#include "base/root.h"
#include "base/text_selection.h"
#include "gpui/platform.h"

namespace gpui {

namespace {

// PluginRegistry: the factories, in registration order.
struct BaseRootPluginRegistry {
    Vec<const RootPlugin*> plugins;
    ~BaseRootPluginRegistry() { VecReset(plugins); }
};

// window.root::<Root>(): which entity is this window's Base Root, kept on
// the window so a Root can be told from any other root view.
struct BaseRootWindowState {
    EntityId root = {};
};

BaseRootWindowState* BaseRootWindowStateOf(Window* window) {
    if (!window) {
        return nullptr;
    }
    uint32_t key = (uint32_t)HashClickId(StrL("gpui-base-root"));
    return (BaseRootWindowState*)WindowKeyedState(
        window, key, new BaseRootWindowState(),
        &EntityDropT<BaseRootWindowState>);
}

// Root::new's `factories.map(|build| build(window, cx))`: factories are
// captured when the window's Root is made, so a registration after that
// affects only future windows.
void RootCapturePlugins(Root* root, Window* window) {
    if (!root || root->captured || !window) {
        return;
    }
    root->captured = true;
    BaseRootPluginRegistry* registry =
        AppGlobalGet<BaseRootPluginRegistry>(root->app);
    for (int i = 0; registry && i < registry->plugins.len; i++) {
        RootPluginInstance instance;
        instance.type = registry->plugins[i];
        if (instance.type->build) {
            instance.state = instance.type->build(window, root->app);
        }
        VecAppend(root->plugins, instance);
    }
}

void RootMount(Window* window, EntityId root) {
    if (BaseRootWindowState* state = BaseRootWindowStateOf(window)) {
        state->root = root;
    }
}

} // namespace

Root::~Root() {
    for (int i = 0; i < plugins.len; i++) {
        if (plugins[i].type->drop && plugins[i].state) {
            plugins[i].type->drop(plugins[i].state);
        }
    }
    VecReset(plugins);
}

void Root::RegisterPlugin(App* app, const RootPlugin* plugin) {
    if (!app || !plugin) {
        return;
    }
    BaseRootPluginRegistry* registry =
        AppGlobalEnsure<BaseRootPluginRegistry>(app);
    for (int i = 0; i < registry->plugins.len; i++) {
        if (registry->plugins[i] == plugin) {
            return;
        }
    }
    VecAppend(registry->plugins, plugin);
}

Entity<Root> Root::New(App* app, Window* window, EntityId view) {
    Entity<Root> e = EntityNew<Root>(app);
    if (Root* root = e.Get(app)) {
        root->app = app;
        root->view = view;
        if (window) {
            RootCapturePlugins(root, window);
            RootMount(window, e.id);
        }
    }
    return e;
}

void* Root::Plugin(const RootPlugin* type) const {
    for (int i = 0; i < plugins.len; i++) {
        if (plugins[i].type == type) {
            return plugins[i].state;
        }
    }
    return nullptr;
}

Root* Root::Read(Window* window) {
    BaseRootWindowState* state = BaseRootWindowStateOf(window);
    if (!state || !state->root.IsValid() || state->root != window->root) {
        return nullptr;
    }
    return (Root*)EntityGet(window->app, state->root);
}

Root* Root::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleFields |= fields;
    return this;
}

El* RootSurface(Ctx* cx, Root* root, El* content) {
    Arena* a = cx->a;
    if (root) {
        RootCapturePlugins(root, cx->win);
    }
    int n = root ? root->plugins.len : 0;
    const RootPluginInstance* plugins = root ? root->plugins.els : nullptr;
    for (int i = 0; i < n; i++) {
        if (plugins[i].type->prepare) {
            plugins[i].type->prepare(plugins[i].state, cx);
        }
    }
    // div().id("root").key_context("Root").relative().size_full(). A column,
    // so content that asks for the full size gets it the way it did when the
    // page was the window's root.
    El* surface = Div(a)
                      ->Id(StrL("root"))
                      ->KeyContext(StrL("Root"))
                      ->FlexCol()
                      ->SizeFull();
    surface->Child(TextSelectionLayer::New(cx));
    if (content) {
        surface->Child(content);
    }
    // The plugins' overlays, in registration order: later ones draw above
    // earlier ones, and all of them above the content.
    El* overlays = nullptr;
    for (int i = 0; i < n; i++) {
        if (!plugins[i].type->render) {
            continue;
        }
        if (El* overlay = plugins[i].type->render(plugins[i].state, cx)) {
            if (!overlays) {
                overlays =
                    Div(a)->Absolute()->Left(0)->Top(0)->Right(0)->Bottom(0);
            }
            overlays->Child(overlay);
        }
    }
    if (overlays) {
        surface->Child(overlays);
    }
    for (int i = 0; i < n; i++) {
        if (plugins[i].type->style) {
            plugins[i].type->style(plugins[i].state, surface, cx);
        }
    }
    if (root && root->styleFields) {
        StyleApplyFields(&surface->style, root->style, root->styleFields);
    }
    for (int i = 0; i < n; i++) {
        if (plugins[i].type->decorate) {
            surface = plugins[i]
                          .type->decorate(plugins[i].state, surface, root, cx);
        }
    }
    return surface;
}

El* Root::Render(Root* self, Ctx* cx) {
    if (!self) {
        return Div(cx->a)->SizeFull();
    }
    // Root::new does this on macOS: the window forwards accessibility hit
    // tests to the view, so what the page drew is reachable. The window
    // only takes it once, however many frames render its Root.
    PlatInstallAccessibilityHitTest(cx->win);
    El* content = self->view.IsValid()
                      ? EntityRender(cx->app, cx->win, cx->a, self->view)
                      : nullptr;
    return RootSurface(cx, self, content);
}

Window* KitOpenWindow(App* app, Str title, int dipW, int dipH, EntityId content,
                      WinOpts opts) {
    // The window takes its root view as it opens; the Root captures its
    // plugins for the window right after, before anything renders.
    Entity<Root> root = Root::New(app, nullptr, content);
    Window* win = WindowOpenView(app, title, dipW, dipH, root.id, opts);
    if (!win) {
        EntityDrop(app, root.id);
        return nullptr;
    }
    RootCapturePlugins(root.Get(app), win);
    RootMount(win, root.id);
    return win;
}

int KitRunView(Str title, int dipW, int dipH, EntityId content, App* app,
               WinOpts opts) {
    if (!KitOpenWindow(app, title, dipW, dipH, content, opts)) {
        return 1;
    }
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}

} // namespace gpui
