#ifndef GPUI_SHELL_ROOT_H_
#define GPUI_SHELL_ROOT_H_
/* The window-level overlay host for a shell application —
   crates/shell/src/root.rs.

   gpui-base ships the overlay *parts* — Dialog and Sheet each build their own
   host, ToastManager and ToastStack own stacking geometry and lifecycle,
   TooltipOverlay owns the delayed show — but nothing in Base decides what
   happens when two of them are open at once. ShellRoot is that decision: a
   stacking order plus a dismissal order, with the smallest presentation that
   makes them visible, drawn from gpui-base's semantic tokens.

   It is deliberately not the component library's Root. The shell binds to
   gpui-base only, so the equivalent host is written here rather than reused;
   a catalog whose components need that Root opens the window with it
   (ComponentWindowOpener, src/shell/component_registry.h) and this root keeps
   rendering inside it, its own overlays with it.

   Stacking order, back to front: content; the sheet (at most one); the dialog
   stack in open order, each deferred at 10 + index; the toasts, above every
   popup; the tooltip, always topmost — the window's own overlay, whose look
   this root chooses. Only the topmost dialog draws a backdrop and hears
   Escape; closing restores the focus its open took. */

#include "base/geometry.h"
#include "base/toast.h"
#include "fps/fps.h"
#include "shell/view.h"

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

// How a dialog may be dismissed. Both default to true: a dialog that refuses
// dismissal is asking a question the user has to answer, which is rare enough
// that it should be spelled out at the call site.
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

// How urgent a toast is: the four a script can name in `level`.
enum class ToastLevel : uint8_t {
    Info,
    Success,
    Warning,
    Error,
};

const char* ToastLevelName(ToastLevel level);
bool ToastLevelFromName(Str name, ToastLevel* out);

// How long a toast stays before retiring itself, unless overridden.
constexpr int kToastDefaultTimeoutMs = 5000;

// One toast to post. A value rather than a view: a toast is a sentence, not a
// layout. The strings are borrowed; the root copies what it keeps.
struct ToastRequest {
    Str title;
    Str description;
    ToastLevel level = ToastLevel::Info;
    // Rust's `Option<Duration>`: no timeout keeps the toast until dismissed.
    bool hasTimeout = true;
    int timeoutMs = kToastDefaultTimeoutMs;
    // A caller-chosen identity. Pushing the same id twice replaces the toast
    // rather than stacking a duplicate.
    bool hasId = false;
    Str id;
};

// A mounted toast's identity: the id it was pushed under, owned by the root.
struct ShellToastId {
    uint32_t key = 0;
    Str text;

    bool operator==(const ShellToastId& other) const {
        return key == other.key && StrEq(text, other.text);
    }
};

// A mounted toast's sentence, owned by the root.
struct ShellToastValue {
    Str title;
    Str description;
    ToastLevel level = ToastLevel::Info;
};

// One open dialog: its layer entity, which owns the content.
struct ShellDialogEntry {
    EntityId layer = {};
};

// ShellRoot is always the first view of a shell window — or the content of
// the Root a catalog's window opener wraps it in. It owns the script content
// and every overlay a script opens through it.
struct ShellRoot {
    App* app = nullptr;
    EntityId content = {};
    // Open dialogs, oldest first. The last is the topmost and the only
    // interactive one.
    Vec<ShellDialogEntry> dialogs;
    // ShellRoot::tooltip_overlay: the tooltip layer a script's triggers show
    // in, the root's own rather than the window's.
    EntityId tooltipOverlay = {};
    // The open sheet's layer entity, or invalid. At most one at a time: a
    // sheet is a region of the window rather than a stack of them.
    EntityId sheet = {};
    ToastManager<ShellToastId, ShellToastValue> toasts;
    ToastStackState toastState;
    FocusHandle toastFocus = {};
    // The lifecycle clock, armed while a toast is mounted.
    int toastTimer = 0;
    Window* toastWindow = nullptr;
    // Source of ids for toasts pushed without one. Monotonic so that a
    // replaced id can never collide with a live toast.
    uint64_t nextToastOrdinal = 0;
    // The performance HUD, while a script has asked for one.
    bool fpsHudVisible = false;
    FpsHudRequest fpsHud = {};

    ~ShellRoot();

    static Entity<ShellRoot> New(App* app, EntityId content);
    static El* Render(ShellRoot* self, Ctx* cx);
    static void OnToastTick(ShellRoot* self, Ctx* cx, const TickEvent* event);
    // blur_on_background_press: a press nothing took leaves no field or
    // control focused, unless a focus trap holds the focus.
    static void BlurOnBackgroundPress(ShellRoot* self, Ctx* cx,
                                      const MouseDownEvent* event);
    static void OnToastClick(ShellRoot* self, Ctx* cx, const ClickEvent* event,
                             int64_t key);
};

// ShellRoot::update: the root of the window a call is happening in, which is
// the window's first view. Null when that is not a ShellRoot, which is a host
// wiring mistake rather than a script error.
ShellRoot* ShellRootOf(Window* window, App* app);

// Opens a dialog on top of the stack. The root takes ownership of `content`
// and drops it when the dialog closes. Answers the stack depth, 0 on failure
// (a render pass, or no root). A script view's description is rebuilt
// whenever the root draws; any other view owns its own state.
int ShellRootOpenDialog(Ctx* cx, Entity<ScriptView> content,
                        DialogOptions options = {});
int ShellRootOpenDialogView(Ctx* cx, EntityId content,
                            DialogOptions options = {});
// Closes the topmost dialog and restores the focus it took. False when no
// dialog was open.
bool ShellRootCloseDialog(Ctx* cx);
// Closes every dialog, restoring focus to where the first one took it from.
// Leaves the sheet alone. Answers how many closed.
int ShellRootCloseAllDialogs(Ctx* cx);
bool ShellRootHasDialog(Ctx* cx);
int ShellRootDialogCount(Ctx* cx);
// The topmost dialog's content, or invalid.
EntityId ShellRootTopmostDialog(Ctx* cx);

// Opens a sheet on the given edge, replacing any sheet already open; the
// replacement inherits the outgoing sheet's restore target.
bool ShellRootOpenSheet(Ctx* cx, Entity<ScriptView> content,
                        Placement placement = Placement::Right);
bool ShellRootOpenSheetView(Ctx* cx, EntityId content,
                            Placement placement = Placement::Right);
bool ShellRootCloseSheet(Ctx* cx);
bool ShellRootHasSheet(Ctx* cx);
// The open sheet's content, or invalid.
EntityId ShellRootSheet(Ctx* cx);

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

// Posts a toast. Toasts never take focus: they report what already happened.
bool ShellRootPushToast(Ctx* cx, const ToastRequest& toast);
// Begins a toast's exit, answering whether it was mounted and active. The
// toast stays mounted until its exit finishes.
bool ShellRootRemoveToast(Ctx* cx, Str id);
// Begins the exit of every active toast.
void ShellRootClearToasts(Ctx* cx);
// How many toasts are mounted, including ones playing their exit.
int ShellRootToastCount(Ctx* cx);
// Advances the toast lifecycle to `nowMs` on the root's clock (TimeNow, in
// milliseconds) — what the root's own timer does every 50ms, split out so the
// lifecycle can be driven without waiting for it.
void ShellRootAdvanceToasts(Ctx* cx, int64_t nowMs);
int64_t ShellRootNowMs();

} // namespace gpui
#endif // GPUI_SHELL_ROOT_H_
