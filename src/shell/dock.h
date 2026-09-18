#ifndef GPUI_SHELL_DOCK_H_
#define GPUI_SHELL_DOCK_H_
/* The dockable layout a script drives — crates/shell/src/dock.rs.

   Three parts, and they are independent of each other:

   - `ScriptPanelNew` / `ShellRegisterPanel` are the Panel whose body is a
     ScriptView, carrying the script's own `serialize()` payload through
     `PanelState::info` so a layout written to disk and read back brings the
     panel's own state with it.
   - `ScriptDockSkin` is the appearance, forwarded to script. Base draws no
     chrome at all, so everything a script wants comes back through the
     renderer table; a skin with no chrome is still a working dock.
   - A **dock command** is what a chrome element *does*. Upstream cannot
     register a callback there — a chrome description is cached until its
     callback or its resolved native state changes, and a cached element has
     no script callback lifetime — so a command names a container and what to
     ask it, and is resolved against the contexts the last drawn frame
     recorded. Nothing of that machinery is needed here: base's own
     `DockBind*` calls wire the behavior straight onto the element, and
     materialization runs *inside* the chrome hook, with the live
     `DockTabGroup` / `DockCtx` in hand. `ShellDockChromeFrame` is that hand.

   Nothing here knows what a script value is: the chrome payloads are JSON and
   the chrome handlers are callback ids, which is what lets the dock work the
   same under a Rust host. */

#include "base/dock_registry.h"
#include "shell/view.h"

namespace gpui::shell {

using ShellPanelBuildFn = Entity<ScriptView> (*)(Window* window, App* app,
                                                 void* data);
using ShellPanelSerializeFn = bool (*)(Entity<ScriptView> view, App* app,
                                       void* data, StrBuilder* out);
using ShellPanelDeserializeFn = void (*)(Entity<ScriptView> view, Str json,
                                         Window* window, App* app, void* data);

struct ShellPanelScript {
    void* data = nullptr;
    ShellPanelBuildFn build = nullptr;
    ShellPanelSerializeFn serialize = nullptr;
    ShellPanelDeserializeFn deserialize = nullptr;
    void (*release)(void* data) = nullptr;
    bool closable = true;
    bool zoomable = true;
    bool visible = true;
};

// Stable, process-wide `shell:<application>/<panel>` names. The intentionally
// interned allocation is bounded by the number of panel contributions.
Str ShellPanelName(Str application, Str panel);

DockPanelDef ScriptPanelNew(App* app, Str name, Entity<ScriptView> view,
                            const ShellPanelScript* script = nullptr);
Str ShellRegisterPanel(App* app, Str application, Str panel,
                       const ShellPanelScript& script);
// The panel a `DockPanelDef` was built from, or null for one that is not a
// script panel. What a `remove_panel(id)` needs in order to answer whether
// anything was removed.
bool ShellIsScriptPanel(const DockPanelDef& def);

// Script-owned appearance over Base's dock behavior. Hooks run in a nested
// Layout scope and may return null to preserve Base's no-chrome default.
// A Rust host installs these directly; a script installs them by publishing a
// snapshot whose `dock_area(...)` node carries the handler ids.
struct ShellDockChrome {
    void* data = nullptr;
    El* (*tabBar)(Ctx* cx, void* data, const DockTabGroup* group) = nullptr;
    El* (*emptyGroup)(Ctx* cx, void* data, const DockTabGroup* group) = nullptr;
    El* (*dropIndicator)(Ctx* cx, void* data, Bounds bounds) = nullptr;
    El* (*dock)(Ctx* cx, void* data, const DockCtx* dock,
                El* content) = nullptr;
};

// The container the chrome handler running right now was drawn for.
//
// Upstream files each context away as it goes past and resolves a command
// against the table when the pointer arrives, because a Rust `TabGroupContext`
// carries `Rc` callbacks that live only for the length of one chrome call.
// Here a command is wired onto the element while it is being built, which is
// inside the call, so the live pointers are all that is needed.
struct ShellDockChromeFrame {
    EntityHandle dock = 0;
    const DockTabGroup* group = nullptr;
    const DockCtx* dockCtx = nullptr;
};

const ShellDockChromeFrame* ShellDockCurrentChrome();

// The dock content a `dock_content()` description stands for.
//
// Base hands a dock's content to the chrome as a finished element and keeps
// whatever comes back, so a chrome that wants both has to place the content
// itself. An element cannot cross into script, so the skin installs the real
// one here for the length of the call and `dock_content()` takes it.
//
// Taken, not copied: a description with two of them draws the content once
// and says so.
El* ShellDockTakeContent();

struct ScriptDockSkin {
    ShellDockChrome chrome = {};
    DockRenderer renderer = {};
    // The script half. `hooks` points at the retained entry's own record, so
    // a snapshot rebuilt while the dock stands replaces the handlers without
    // the area being rebuilt around it; `runtime` and `dock` are written the
    // same way, as the description is replayed.
    Str id = {};
    EntityHandle dock = 0;
    DockChromeHooks* hooks = nullptr;
    ShellRuntime* runtime = nullptr;

    ScriptDockSkin();
    explicit ScriptDockSkin(ShellDockChrome value);
    const DockRenderer* Renderer();
};

// Draws one piece of a dock's chrome by asking the script. The runtime owns
// the callback table and the description cache; this is only the call, so the
// skin never has to know what a script value is.
inline El* ShellDockDrawChrome(Ctx* cx, ShellRuntime* runtime,
                               EntityHandle dock, DockChromeSlot slot,
                               uint64_t key, CallbackId handler, Str payload) {
    return runtime ? runtime->DescribeDockChrome(cx, dock, slot, key, handler,
                                                 payload)
                   : nullptr;
}

void ShellTabGroupData(const DockTabGroup* group, StrBuilder* out);
void ShellDockData(const DockCtx* dock, StrBuilder* out);
// Where a dragged panel would land. `to` is what base hands the hook: the
// placeholder's box in the hovered group's own coordinates, already sprung
// and clamped. The rest is read off the area, which is where base keeps it.
void ShellDropIndicatorData(const DockState* state, Bounds to, StrBuilder* out);

} // namespace gpui::shell

#endif // GPUI_SHELL_DOCK_H_
