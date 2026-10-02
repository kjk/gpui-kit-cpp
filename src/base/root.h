#ifndef GPUI_BASE_ROOT_H_
#define GPUI_BASE_ROOT_H_
/* Window roots and presentation-layer plugins — crates/base/src/root.rs

   Root is the window's outermost view: the application's content, and above
   it whatever the registered presentation plugins draw. Base owns it whether
   or not a styled component library is in the build; Component registers its
   per-window state (dialogs, sheet, notifications, touch selection, window
   chrome) as a plugin rather than owning a Root of its own.

   Rust's `trait RootPlugin: Render` becomes a function table, and the
   table's address is the plugin's TypeId: registering the same table again
   replaces its factory for future windows instead of mounting it twice.
   Each window's plugin instances are its Root entity's, made when the Root
   is created in the window and dropped with it.

   Tab / shift-tab and ctrl-c (cmd-c) are Root's key bindings in Rust. The
   runtime already walks focus (FocusNext, which honors focus traps) and
   copies the window's text selection (WindowSelectionCopy) for every window,
   so those actions have no second home here. */

#include "gpui/gpui.h"

namespace gpui {

struct Root;

// trait RootPlugin. Every entry may be null. `state` is what `build` answered
// for this window (null when there is no `build`).
struct RootPlugin {
    // The factory: the plugin's per-window state, made when a Root is first
    // built in the window. `drop` frees it with the window.
    void* (*build)(Window* window, App* app) = nullptr;
    void (*drop)(void* state) = nullptr;
    // prepare: synchronize window-scoped state before any element is built.
    void (*prepare)(void* state, Ctx* cx) = nullptr;
    // style: defaults for the root surface. The Root's own refinement is
    // applied after these and wins.
    void (*style)(void* state, El* surface, Ctx* cx) = nullptr;
    // decorate: wrap the finished surface (window chrome, say). Answer
    // `surface` itself when there is nothing to add.
    El* (*decorate)(void* state, El* surface, const Root* root,
                    Ctx* cx) = nullptr;
    // Render: the overlay the plugin mounts above application content.
    El* (*render)(void* state, Ctx* cx) = nullptr;
};

struct RootPluginInstance {
    const RootPlugin* type = nullptr;
    void* state = nullptr;
};

// The window's content and overlay host.
struct Root {
    App* app = nullptr;
    // The original application content entity.
    EntityId view = {};
    // impl Styled for Root: an instance refinement, applied after the plugin
    // defaults.
    Style style = {};
    uint32_t styleFields = 0;
    // This window's plugin instances, in registration order. Captured from
    // the registry when the Root is made in a window -- or, for one made
    // before its window opened, when it first renders there.
    Vec<RootPluginInstance> plugins;
    bool captured = false;

    ~Root();

    // Root::register_plugin, once per application, before creating windows.
    // Registration does not retrofit windows whose plugins were already
    // captured.
    static void RegisterPlugin(App* app, const RootPlugin* plugin);

    // Root::new. Captures the registered plugins for `window`; the entity is
    // what a window mounts as its root view. A null `window` leaves that to
    // the first render.
    static Entity<Root> New(App* app, Window* window, EntityId view);

    EntityId View() const { return view; }
    // Root::plugin::<V>: this window's instance of `type`, or null.
    void* Plugin(const RootPlugin* type) const;

    // Root::read / Root::update: the window's Base Root. Null when the
    // window's root view is not one.
    static Root* Read(Window* window);

    Root* Refine(const Style& s, uint32_t fields);

    static El* Render(Root* self, Ctx* cx);
};

// Root's render around content already built: prepare, the surface with the
// content and every plugin's overlay, the plugins' styles, `root`'s
// refinement, then each plugin's decoration.
El* RootSurface(Ctx* cx, Root* root, El* content);

// gpui_kit::open_window: open a window whose root view is a Base Root around
// `content`, the application content the caller built. The kit crate has no
// file of its own here; this is its one function. Call component::Init (or
// BaseInit) first.
Window* KitOpenWindow(App* app, Str title, int dipW, int dipH, EntityId content,
                      WinOpts opts);
// KitOpenWindow, then run the application until its last window closes —
// AppRunView with a Root.
int KitRunView(Str title, int dipW, int dipH, EntityId content, App* app,
               WinOpts opts);

} // namespace gpui
#endif // GPUI_BASE_ROOT_H_
