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
   Rust keeps each window's plugin instances on its Root entity; here they
   are kept on the window (window.use_keyed_state), captured the first time
   a Root renders in it, so a view that renders a Root surface of its own
   (`RootSurface`, the shell's root) finds the same instances the Root entity
   would.

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

    // Root::register_plugin, once per application, before creating windows.
    // Registration does not retrofit windows whose plugins were already
    // captured.
    static void RegisterPlugin(App* app, const RootPlugin* plugin);

    // Root::new. Captures the window's plugins; the entity is what a window
    // mounts as its root view.
    static Entity<Root> New(App* app, Window* window, EntityId view);

    EntityId View() const { return view; }
    // Root::plugin::<V>: this window's instance of `type`, or null.
    static void* Plugin(Window* window, const RootPlugin* type);

    // Root::read / Root::update: the window's Base Root. Null when the
    // window's root view is not one.
    static Root* Read(Window* window);

    Root* Refine(const Style& s, uint32_t fields);

    static El* Render(Root* self, Ctx* cx);
};

// The per-window plugin instances, captured on first ask. Count in *n.
const RootPluginInstance* RootPlugins(Window* window, int* n);

// Root's render around content already built: prepare, the surface with the
// content and every plugin's overlay, the plugins' styles, `root`'s
// refinement, then each plugin's decoration. `root` may be null for a view
// that hosts its own content (the shell's root).
El* RootSurface(Ctx* cx, const Root* root, El* content);

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
