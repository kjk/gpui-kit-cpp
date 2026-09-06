#ifndef GPUI_SHELL_ROOT_H_
#define GPUI_SHELL_ROOT_H_
/* The window-level host for a shell application — crates/shell/src/root.rs. */

#include "fps/fps.h"
#include "shell/view.h"
#include "ui/sheet.h"

namespace gpui {

// Where the performance HUD sits over the window and how it behaves.
//
// What a script's `show_fps_monitor(options)` names, with the same defaults
// `gpui_fps` gives an overlay a native host places by hand.
struct FpsHudRequest {
    // Corner or edge of the window.
    FpsAnchor anchor = FpsAnchor::TopRight;
    // The per-frame budget the HUD grades frame cost against, in seconds.
    // Unset keeps the HUD's own default.
    bool hasFrameBudget = false;
    float frameBudget = 0;
};

bool FpsAnchorFromName(Str name, FpsAnchor* out);
SeqStrings FpsAnchorNames();

struct DialogOptions {
    bool escapeDismissable = true;
    bool backdropDismissable = true;

    DialogOptions& EscapeDismissable(bool value) {
        escapeDismissable = value;
        return *this;
    }
    DialogOptions& BackdropDismissable(bool value) {
        backdropDismissable = value;
        return *this;
    }
};

enum class ToastLevel : uint8_t {
    Info,
    Success,
    Warning,
    Error,
};

const char* ToastLevelName(ToastLevel level);
bool ToastLevelFromName(Str name, ToastLevel* out);

struct ToastRequest {
    Str title;
    Str description;
    ToastLevel level = ToastLevel::Info;
    int timeoutMs = 5000;
    bool hasId = false;
    Str id;
};

// ShellRoot is always the first view of a shell window. It owns the script
// content; window-owned dialog, sheet and toast entities are rendered through
// the same layer store used by the native component Root.
struct ShellRoot {
    App* app = nullptr;
    EntityId content = {};
    uint64_t nextToastOrdinal = 0;
    // The performance HUD, while a script has asked for one.
    bool fpsHudVisible = false;
    FpsHudRequest fpsHud = {};

    ~ShellRoot();

    static Entity<ShellRoot> New(App* app, EntityId content);
    static El* Render(ShellRoot* self, Ctx* cx);
};

ShellRoot* ShellRootOf(Window* window, App* app);

int ShellRootOpenDialog(Ctx* cx, Entity<ScriptView> content,
                        DialogOptions options = {});
bool ShellRootCloseDialog(Ctx* cx);
int ShellRootCloseAllDialogs(Ctx* cx);
bool ShellRootHasDialog(Ctx* cx);

bool ShellRootOpenSheet(
    Ctx* cx, Entity<ScriptView> content,
    component::SheetPlacement placement = component::SheetPlacement::Right);
bool ShellRootCloseSheet(Ctx* cx);
bool ShellRootHasSheet(Ctx* cx);

// Draws the performance HUD over the window, above every other layer, until
// ShellRootHideFpsMonitor. Calling it again moves or reconfigures the HUD that
// is already up.
//
// The root owns the HUD rather than the script's tree: the script says whether
// and where, and what it renders can neither move the HUD nor rebuild it. The
// monitor behind the overlay is the window's own — the same one the
// `fps_monitor()` element form uses — so a HUD hidden and shown again keeps its
// history. Refused, with false, from a render pass.
bool ShellRootShowFpsMonitor(Ctx* cx, const FpsHudRequest& request);
// Takes the performance HUD down. True if one was up.
bool ShellRootHideFpsMonitor(Ctx* cx);
bool ShellRootFpsMonitorVisible(Ctx* cx);

bool ShellRootPushToast(Ctx* cx, const ToastRequest& toast);
bool ShellRootRemoveToast(Ctx* cx, Str id);
void ShellRootClearToasts(Ctx* cx);
int ShellRootToastCount(Ctx* cx);

} // namespace gpui
#endif // GPUI_SHELL_ROOT_H_
