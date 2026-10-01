#include "shell/root.h"

#include "base/animation.h"
#include "base/dialog.h"
#include "base/root.h"
#include "base/sheet.h"
#include "base/text_selection.h"
#include "base/theme.h"
#include "base/tooltip.h"
#include "shell/scope.h"

#include <math.h>

namespace gpui {

// How often the toast lifecycle clock is advanced. Toast timeouts are
// wall-clock, not frame-driven, so they have to be sampled; 50ms is below the
// threshold where a dismissal reads as late.
static const int kToastTickMs = 50;

// How many active toasts are mounted at once. Older toasts stay in the manager
// and reappear as newer ones leave, so a burst is throttled rather than lost.
static const int kToastVisibleLimit = 3;

// Paint priority of the toast layer: above POPUP_PRIORITY, because a toast
// reports the outcome of the action the user just took, and an open dropdown
// or dialog is exactly where that outcome matters most. Rust gives deferred
// elements a priority; here they share the popup layer and the priority is
// their z-index within it, so the tooltip layer stays above all of them.
static const int kToastPriority = kPopupPriority + 1;

// Width of the toast column: toasts are anchored to a viewport corner, so
// unlike every other overlay here they cannot take their measure from their
// content or from the viewport.
static const float kToastWidth = 320.f;

// How long a tooltip takes to slide and fade in.
static const float kTooltipEnterMs = 150.f;
// How long the box takes to travel when the pointer moves straight from one
// trigger to the next one beside it.
static const float kTooltipSlideMs = 200.f;
// How far apart two triggers may sit vertically and still count as the same
// row.
static const float kTooltipSameRow = 10.f;

static uint32_t ShellRootWindowKey() {
    return (uint32_t)HashClickId(StrL("gpui-shell-root"));
}

struct ShellRootWindowState {
    EntityId root = {};
};

static ShellRootWindowState* RootWindowState(Window* window) {
    if (!window) return nullptr;
    return (ShellRootWindowState*)WindowKeyedState(
        window, ShellRootWindowKey(), new ShellRootWindowState(),
        &EntityDropT<ShellRootWindowState>);
}

// The ShellRoot entity the window last rendered, which the overlay layers
// close through.
static EntityId ShellRootWindowStateRoot(Window* window) {
    ShellRootWindowState* state = RootWindowState(window);
    return state ? state->root : EntityId{};
}

const char* ToastLevelName(ToastLevel level) {
    static const char names[] = "info\0success\0warning\0error\0";
    Str name = SeqStrByIndex(names, (int)level);
    return name ? name.s : names;
}

static const char kFpsAnchorNames[] =
    "top_left\0top_right\0bottom_left\0bottom_right\0top_center\0"
    "bottom_center\0left_center\0right_center\0";

SeqStrings FpsAnchorNames() {
    return kFpsAnchorNames;
}

bool FpsAnchorFromName(Str name, FpsAnchor* out) {
    if (StrEq(name, StrL("top_left")))
        *out = FpsAnchor::TopLeft;
    else if (StrEq(name, StrL("top_right")))
        *out = FpsAnchor::TopRight;
    else if (StrEq(name, StrL("bottom_left")))
        *out = FpsAnchor::BottomLeft;
    else if (StrEq(name, StrL("bottom_right")))
        *out = FpsAnchor::BottomRight;
    else if (StrEq(name, StrL("top_center")))
        *out = FpsAnchor::TopCenter;
    else if (StrEq(name, StrL("bottom_center")))
        *out = FpsAnchor::BottomCenter;
    else if (StrEq(name, StrL("left_center")))
        *out = FpsAnchor::LeftCenter;
    else if (StrEq(name, StrL("right_center")))
        *out = FpsAnchor::RightCenter;
    else
        return false;
    return true;
}

bool ToastLevelFromName(Str name, ToastLevel* out) {
    if (StrEq(name, StrL("info")))
        *out = ToastLevel::Info;
    else if (StrEq(name, StrL("success")))
        *out = ToastLevel::Success;
    else if (StrEq(name, StrL("warning")))
        *out = ToastLevel::Warning;
    else if (StrEq(name, StrL("error")))
        *out = ToastLevel::Error;
    else
        return false;
    return true;
}

// The semantic tokens everything this root draws itself takes its look from:
// gpui-base's theme, never a component library's.
static SemanticThemeTokens RootTokens(const App* app) {
    const BaseTheme* theme = BaseThemeGlobal(app);
    return theme ? theme->tokens : SemanticThemeTokens{};
}

// The scrim behind a dialog or sheet. A fixed translucent black rather than a
// token: ColorTokens has no overlay color, and every candidate inverts with
// the palette. A scrim is a dimming, not a palette entry.
static Rgba BackdropColor() {
    return Rgba8(0, 0, 0, 128);
}

// The border color that carries a toast's level. `Warning` borrows the accent
// pair because the base palette has no warning token.
static Rgba LevelColor(ToastLevel level, const ColorTokens& colors) {
    switch (level) {
        case ToastLevel::Info:
            return colors.border;
        case ToastLevel::Success:
            return colors.primary;
        case ToastLevel::Warning:
            return colors.accentForeground;
        case ToastLevel::Error:
            return colors.destructive;
    }
    return colors.border;
}

// Whether an overlay may be opened or closed right now. Overlay changes mutate
// the window, so they are only legal from an event or a task call scope.
// Outside any scope — host code, a test — there is nothing to check. A render
// or layout scope is a script bug: reported and ignored.
static bool OverlayMutationAllowed(const char* operation) {
    if (!shell::ScopeHasCurrent()) return true;
    ScopePhase phase = shell::ScopeCurrentPhase();
    if (ScopePhaseAllowsNotify(phase)) return true;
    logf(
        "gpui-shell: `%s` is not allowed during the `%s` phase; overlays may "
        "only be opened or closed while handling an event or a task\n",
        Str(operation), Str(ScopePhaseName(phase)));
    return false;
}

// Rebuilds a script overlay's description before it draws.
//
// An overlay's content is a function, and what it closes over is somebody
// else's state — that is the contract open_dialog and open_sheet document, and
// the only one they can have: neither answers a view handle, so there is
// nothing for a script to notify when the state behind the closure moves.
// So the root rebuilds it whenever the root itself draws, which is what
// window.refresh() reaches. Marking it dirty schedules no frame of its own:
// the overlay is about to render as part of this one. A non-script overlay
// owns its own state and is left alone.
static void RebuildScriptOverlay(App* app, EntityId content, bool isScript) {
    if (!isScript) return;
    if (ScriptView* view = Entity<ScriptView>{content}.Get(app))
        view->dirty = true;
}

static void RestoreOverlayFocus(Window* win, FocusHandle restore) {
    if (!FocusHandleRestore(win, restore)) WindowSetFocusId(win, 0);
}

// Invalidates the root itself, which is what a change to one of its own fields
// wants: `Notify(cx)` would mark whichever view is calling, and the overlays
// are not in that view's description.
static void NotifyRoot(App* app, ShellRoot* root, Window* win) {
    ShellRootWindowState* state = RootWindowState(win);
    if (root && state && state->root.IsValid())
        NotifyEntity(app, state->root, win);
}

// ─── the dialog stack ──────────────────────────────────────────────────────

static bool CloseTopDialog(App* app, Window* win, ShellRoot* root);

struct ShellDialogLayer {
    App* app = nullptr;
    EntityId root = {};
    EntityId content = {};
    bool isScript = false;
    DialogOptions options = {};
    FocusHandle focus = {};
    FocusHandle restore = {};
    // Where the root last placed this layer in its stack.
    int index = 0;
    bool topmost = false;

    ~ShellDialogLayer() {
        if (app && content.IsValid()) EntityDrop(app, content);
    }

    // request_close: closing goes through the root, which owns the stack.
    static void Close(ShellDialogLayer* self, Ctx* cx, const void*) {
        if (!self) return;
        ShellRoot* root = Entity<ShellRoot>{self->root}.Get(cx->app);
        if (root) CloseTopDialog(cx->app, cx->win, root);
    }

    static void OnBackdrop(ShellDialogLayer* self, Ctx* cx,
                           const MouseDownEvent* event) {
        if (!self || !event || event->button != MouseButton::Left ||
            !self->options.backdropDismissable || !self->topmost)
            return;
        WindowStopPropagation(cx);
        Close(self, cx, event);
    }

    static El* Render(ShellDialogLayer* self, Ctx* cx) {
        RebuildScriptOverlay(cx->app, self->content, self->isScript);
        SemanticThemeTokens tokens = RootTokens(cx->app);
        const ColorTokens& colors = tokens.colors;
        El* backdrop = nullptr;
        if (self->topmost) {
            // `div().absolute().inset_0()`: out of the host's flow, which
            // centers what is in it, and over the whole window. Only the
            // topmost dialog draws one: a stack dims the window once.
            backdrop =
                DialogBackdrop::New(cx)
                    ->Absolute()
                    ->Top(0)
                    ->Left(0)
                    ->Right(0)
                    ->Bottom(0)
                    ->Bg(BackdropColor())
                    ->OnMouseDown(Listen(cx, &ShellDialogLayer::OnBackdrop));
        }
        El* child = self->content.IsValid()
                        ? EntityRender(cx->app, cx->win, cx->a, self->content)
                        : nullptr;
        // `v_flex().occlude()`: the surface keeps presses off the backdrop;
        // the full-window wrapper around it is a plain div, not DialogPopup,
        // whose occlusion would swallow every press meant for the backdrop.
        El* surface = Div(cx->a)
                          ->FlexCol()
                          ->StopMouseDown()
                          ->Bg(colors.surface)
                          ->Fg(colors.surfaceForeground)
                          ->Border(1, colors.border)
                          ->Radius(tokens.radius.lg)
                          ->Pad(tokens.spacing.lg)
                          ->Child(child ? child : Div(cx->a));
        El* popup = Div(cx->a)
                        ->Absolute()
                        ->Top(0)
                        ->Left(0)
                        ->Right(0)
                        ->Bottom(0)
                        ->Flex()
                        ->ItemsCenter()
                        ->JustifyCenter()
                        ->Child(surface);
        Str trap = StrDup(cx->a, fmt("shell-dialog-%d", cx->self.index));
        // Keyboard dismissal belongs to the topmost dialog alone. Enter is
        // vetoed: the dialog's primary action is the script's.
        if (self->topmost && self->options.escapeDismissable)
            DialogBindKeys(cx, popup, trap, {}, {},
                           Listen(cx, &ShellDialogLayer::Close));
        return Dialog::New(cx)
            ->Trap(trap)
            ->Backdrop(backdrop)
            ->Popup(popup)
            ->IntoEl()
            ->Deferred()
            ->ZIndex(10 + self->index);
    }
};

static ShellDialogLayer* DialogLayerAt(App* app, ShellRoot* root, int index) {
    if (!root || index < 0 || index >= len(root->dialogs)) return nullptr;
    return Entity<ShellDialogLayer>{root->dialogs[index].layer}.Get(app);
}

static int OpenDialog(Ctx* cx, EntityId content, bool isScript,
                      DialogOptions options) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root || !content.IsValid() || !OverlayMutationAllowed("open_dialog"))
        return 0;
    Entity<ShellDialogLayer> layer = EntityNew<ShellDialogLayer>(cx->app);
    ShellDialogLayer* state = layer.Get(cx);
    if (!state) return 0;
    state->app = cx->app;
    state->root = ShellRootWindowStateRoot(cx->win);
    state->content = content;
    state->isScript = isScript;
    state->options = options;
    state->focus = FocusHandleNew(cx);
    state->restore = WindowFocused(cx->win);
    ShellDialogEntry entry;
    entry.layer = layer.id;
    if (!VecAppend(root->dialogs, entry)) {
        state->content = {};
        EntityDrop(cx->app, layer.id);
        return 0;
    }
    FocusHandleFocus(cx->win, state->focus);
    NotifyRoot(cx->app, root, cx->win);
    return len(root->dialogs);
}

static bool CloseTopDialog(App* app, Window* win, ShellRoot* root) {
    if (!root || len(root->dialogs) == 0) return false;
    if (!OverlayMutationAllowed("close_dialog")) return false;
    int top = len(root->dialogs) - 1;
    ShellDialogLayer* state = DialogLayerAt(app, root, top);
    FocusHandle restore = state ? state->restore : FocusHandle{};
    EntityId layer = root->dialogs[top].layer;
    root->dialogs.len--;
    EntityDrop(app, layer);
    RestoreOverlayFocus(win, restore);
    NotifyRoot(app, root, win);
    return true;
}

int ShellRootOpenDialog(Ctx* cx, Entity<ScriptView> content,
                        DialogOptions options) {
    return OpenDialog(cx, content.id, true, options);
}

int ShellRootOpenDialogView(Ctx* cx, EntityId content, DialogOptions options) {
    return OpenDialog(cx, content, false, options);
}

bool ShellRootCloseDialog(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    return root && CloseTopDialog(cx->app, cx->win, root);
}

int ShellRootCloseAllDialogs(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root || !OverlayMutationAllowed("close_all_dialogs")) return 0;
    int count = len(root->dialogs);
    if (count == 0) return 0;
    // The first dialog's record is the only one that describes the window as
    // it was before the stack existed; restoring through each in turn would
    // flicker focus across views that are about to be dropped.
    ShellDialogLayer* first = DialogLayerAt(cx->app, root, 0);
    FocusHandle restore = first ? first->restore : FocusHandle{};
    while (len(root->dialogs) > 0) {
        EntityId layer = root->dialogs[len(root->dialogs) - 1].layer;
        root->dialogs.len--;
        EntityDrop(cx->app, layer);
    }
    RestoreOverlayFocus(cx->win, restore);
    NotifyRoot(cx->app, root, cx->win);
    return count;
}

bool ShellRootHasDialog(Ctx* cx) {
    return ShellRootDialogCount(cx) > 0;
}

int ShellRootDialogCount(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    return root ? len(root->dialogs) : 0;
}

EntityId ShellRootTopmostDialog(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    ShellDialogLayer* top =
        root ? DialogLayerAt(cx->app, root, len(root->dialogs) - 1) : nullptr;
    return top ? top->content : EntityId{};
}

// ─── the sheet ─────────────────────────────────────────────────────────────

static bool CloseSheet(App* app, Window* win, ShellRoot* root);

struct ShellSheetLayer {
    App* app = nullptr;
    EntityId root = {};
    EntityId content = {};
    bool isScript = false;
    Placement placement = Placement::Right;
    FocusHandle focus = {};
    FocusHandle restore = {};

    ~ShellSheetLayer() {
        if (app && content.IsValid()) EntityDrop(app, content);
    }

    static void Close(ShellSheetLayer* self, Ctx* cx, const void*) {
        if (!self) return;
        ShellRoot* root = Entity<ShellRoot>{self->root}.Get(cx->app);
        if (root) CloseSheet(cx->app, cx->win, root);
    }

    static El* Render(ShellSheetLayer* self, Ctx* cx) {
        RebuildScriptOverlay(cx->app, self->content, self->isScript);
        SemanticThemeTokens tokens = RootTokens(cx->app);
        const ColorTokens& colors = tokens.colors;
        El* child = self->content.IsValid()
                        ? EntityRender(cx->app, cx->win, cx->a, self->content)
                        : nullptr;
        WinSize size = WindowSize(cx->win);
        El* surface = Div(cx->a)
                          ->FlexCol()
                          ->StopMouseDown()
                          ->Absolute()
                          ->Bg(colors.surface)
                          ->Fg(colors.surfaceForeground)
                          ->Pad(tokens.spacing.lg)
                          ->Child(child ? child : Div(cx->a));
        // A third of the window on the sheet's edge, bordered on the side
        // that faces the content.
        switch (self->placement) {
            case Placement::Left:
                surface->Top(0)->Bottom(0)->Left(0)->WFrac(1.f / 3.f)->BorderR(
                    1, colors.border);
                break;
            case Placement::Right:
                surface->Top(0)->Bottom(0)->Right(0)->WFrac(1.f / 3.f)->BorderL(
                    1, colors.border);
                break;
            case Placement::Top:
                surface->Left(0)
                    ->Right(0)
                    ->Top(0)
                    ->H(size.dipH / 3.f)
                    ->BorderB(1, colors.border);
                break;
            case Placement::Bottom:
                surface->Left(0)
                    ->Right(0)
                    ->Bottom(0)
                    ->H(size.dipH / 3.f)
                    ->BorderT(1, colors.border);
                break;
        }
        return Sheet::New(cx)
            ->Trap(StrL("shell-sheet"))
            ->Overlay(Div(cx->a)
                          ->Absolute()
                          ->Top(0)
                          ->Left(0)
                          ->Right(0)
                          ->Bottom(0)
                          ->Bg(BackdropColor()))
            ->Surface(surface)
            ->RequestClose(Listen(cx, &ShellSheetLayer::Close))
            ->IntoEl()
            ->Deferred();
    }
};

static bool CloseSheet(App* app, Window* win, ShellRoot* root) {
    if (!root || !root->sheet.IsValid()) return false;
    if (!OverlayMutationAllowed("close_sheet")) return false;
    ShellSheetLayer* state = Entity<ShellSheetLayer>{root->sheet}.Get(app);
    FocusHandle restore = state ? state->restore : FocusHandle{};
    EntityId layer = root->sheet;
    root->sheet = {};
    EntityDrop(app, layer);
    RestoreOverlayFocus(win, restore);
    NotifyRoot(app, root, win);
    return true;
}

static bool OpenSheet(Ctx* cx, EntityId content, bool isScript,
                      Placement placement) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root || !content.IsValid() || !OverlayMutationAllowed("open_sheet"))
        return false;
    // Replacing rather than stacking keeps the focus record honest: the
    // incoming sheet inherits the outgoing one's restore target, so closing it
    // returns focus to the window rather than to a sheet that no longer
    // exists.
    FocusHandle restore = WindowFocused(cx->win);
    if (root->sheet.IsValid()) {
        ShellSheetLayer* current = Entity<ShellSheetLayer>{root->sheet}
                                       .Get(cx->app);
        if (current) restore = current->restore;
        EntityId outgoing = root->sheet;
        root->sheet = {};
        EntityDrop(cx->app, outgoing);
    }
    Entity<ShellSheetLayer> layer = EntityNew<ShellSheetLayer>(cx->app);
    ShellSheetLayer* state = layer.Get(cx);
    if (!state) return false;
    state->app = cx->app;
    state->root = ShellRootWindowStateRoot(cx->win);
    state->content = content;
    state->isScript = isScript;
    state->placement = placement;
    state->focus = FocusHandleNew(cx);
    state->restore = restore;
    root->sheet = layer.id;
    FocusHandleFocus(cx->win, state->focus);
    NotifyRoot(cx->app, root, cx->win);
    return true;
}

bool ShellRootOpenSheet(Ctx* cx, Entity<ScriptView> content,
                        Placement placement) {
    return OpenSheet(cx, content.id, true, placement);
}

bool ShellRootOpenSheetView(Ctx* cx, EntityId content, Placement placement) {
    return OpenSheet(cx, content, false, placement);
}

bool ShellRootCloseSheet(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    return root && CloseSheet(cx->app, cx->win, root);
}

bool ShellRootHasSheet(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    return root && root->sheet.IsValid();
}

EntityId ShellRootSheet(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    ShellSheetLayer* sheet =
        root ? Entity<ShellSheetLayer>{root->sheet}.Get(cx->app) : nullptr;
    return sheet ? sheet->content : EntityId{};
}

// ─── toasts ────────────────────────────────────────────────────────────────

int64_t ShellRootNowMs() {
    return (int64_t)(TimeNow() * 1000.0);
}

static void FreeToastId(ShellToastId* id) {
    StrFree(id->text);
    id->text = {};
}

static void FreeToastValue(ShellToastValue* value) {
    StrFree(value->title);
    StrFree(value->description);
    *value = {};
}

static void ArmToastClock(ShellRoot* root, Window* win) {
    if (root->toastTimer || !win) return;
    root->toastWindow = win;
    root->toastTimer = WindowSetInterval(
        win, kToastTickMs,
        ListenTo(Entity<ShellRoot>{ShellRootWindowStateRoot(win)},
                 &ShellRoot::OnToastTick));
}

static void AdvanceToasts(App* app, Window* win, ShellRoot* root,
                          int64_t nowMs) {
    // Timers pause while the user is reading the stack, not while the window
    // is inactive: a visible side-by-side window would hold its toasts
    // forever.
    bool paused = root->toastState.IsExpanded();
    ToastAdvance<ShellToastId, ShellToastValue> advanced =
        root->toasts.Advance(nowMs, paused);
    for (int i = 0; i < len(advanced.removed); i++) {
        FreeToastId(&advanced.removed[i].id);
        FreeToastValue(&advanced.removed[i].value);
    }
    bool changed = advanced.changed;
    VecReset(advanced.presented);
    VecReset(advanced.ending);
    VecReset(advanced.removed);
    if (root->toasts.IsEmpty() && root->toastTimer) {
        // Nothing left to retire: the clock stops until the next push, so an
        // idle window does not wake every 50ms.
        WindowCancelTimer(root->toastWindow, root->toastTimer);
        root->toastTimer = 0;
        root->toastWindow = nullptr;
    }
    if (changed) NotifyRoot(app, root, win);
}

void ShellRoot::OnToastTick(ShellRoot* self, Ctx* cx, const TickEvent*) {
    if (self) AdvanceToasts(cx->app, cx->win, self, ShellRootNowMs());
}

static bool DismissToastKey(ShellRoot* root, uint32_t key, int64_t nowMs) {
    for (int i = 0; i < root->toasts.Len(); i++) {
        const ManagedToast<ShellToastId, ShellToastValue>* entry = root->toasts
                                                                       .At(i);
        if (entry->id.key == key &&
            entry->status != ToastTransitionStatus::Ending)
            return root->toasts.Dismiss(entry->id, nowMs);
    }
    return false;
}

void ShellRoot::OnToastClick(ShellRoot* self, Ctx* cx, const ClickEvent*,
                             intptr_t key) {
    if (self && DismissToastKey(self, (uint32_t)key, ShellRootNowMs()))
        NotifyRoot(cx->app, self, cx->win);
}

bool ShellRootPushToast(Ctx* cx, const ToastRequest& request) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root || !request.title || !OverlayMutationAllowed("push_toast"))
        return false;
    ShellToastId id;
    TempStr text = request.hasId ? StrDupTemp(request.id) : TempStr{};
    if (!request.hasId) {
        root->nextToastOrdinal++;
        text =
            fmt("shell-toast-%llu", (unsigned long long)root->nextToastOrdinal);
    }
    id.key = (uint32_t)HashClickId(text);
    id.text = text;
    // A replaced toast keeps the id string it was pushed under; the manager
    // erases the old entry and takes this one in its place.
    const ShellToastId* existing = nullptr;
    for (int i = 0; i < root->toasts.Len(); i++) {
        if (root->toasts.At(i)->id == id) existing = &root->toasts.At(i)->id;
    }
    id.text = existing ? existing->text : StrDup(Str(text));
    ShellToastValue value;
    value.title = StrDup(request.title);
    value.description =
        request.description ? StrDup(request.description) : Str{};
    value.level = request.level;
    ToastOptions options = request.hasTimeout
                               ? ToastOptions::Timeout(request.timeoutMs)
                               : ToastOptions::Persistent();
    ShellToastValue replaced;
    bool hadReplaced = false;
    if (!root->toasts.Push(id, value, options, ShellRootNowMs(), &replaced,
                           &hadReplaced)) {
        if (!existing) FreeToastId(&id);
        FreeToastValue(&value);
        return false;
    }
    if (hadReplaced) FreeToastValue(&replaced);
    ArmToastClock(root, cx->win);
    NotifyRoot(cx->app, root, cx->win);
    return true;
}

bool ShellRootRemoveToast(Ctx* cx, Str id) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root || !id) return false;
    ShellToastId key;
    key.key = (uint32_t)HashClickId(id);
    key.text = id;
    bool dismissed = root->toasts.Dismiss(key, ShellRootNowMs());
    if (dismissed) NotifyRoot(cx->app, root, cx->win);
    return dismissed;
}

void ShellRootClearToasts(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root) return;
    Vec<ShellToastId> changed = root->toasts.DismissAll(ShellRootNowMs());
    bool any = len(changed) > 0;
    VecReset(changed);
    if (any) NotifyRoot(cx->app, root, cx->win);
}

int ShellRootToastCount(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    return root ? root->toasts.Len() : 0;
}

void ShellRootAdvanceToasts(Ctx* cx, int64_t nowMs) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (root) AdvanceToasts(cx->app, cx->win, root, nowMs);
}

static El* ToastLayer(ShellRoot* self, Ctx* cx, const SemanticThemeTokens& t) {
    if (self->toasts.IsEmpty()) return nullptr;
    ToastVisible<ShellToastId, ShellToastValue> visible[16];
    int count = self->toasts.Visible(kToastVisibleLimit, visible, 16);
    if (count > 16) count = 16;
    ToastStack* stack =
        ToastStack::New(cx, StrL("shell-toasts"), &self->toastState);
    for (int i = 0; i < count; i++) {
        const ShellToastId& id = *visible[i].id;
        const ShellToastValue& toast = *visible[i].value;
        Str name = StrDup(cx->a, id.text);
        El* body = Toast::New(cx, name)
                       ->TransitionStatus(visible[i].status)
                       ->IntoEl()
                       ->StopMouseDown()
                       ->OnClick(Listen(cx, &ShellRoot::OnToastClick,
                                        (intptr_t)id.key))
                       ->FlexCol()
                       ->Gap(t.spacing.xxs)
                       ->Pad(t.spacing.md)
                       ->Radius(t.radius.md)
                       ->Bg(t.colors.surface)
                       ->Fg(t.colors.surfaceForeground)
                       ->Border(1, LevelColor(toast.level, t.colors))
                       ->Child(TextEl(cx->a, StrDup(cx->a, toast.title)));
        if (toast.description) {
            body->Child(
                Div(cx->a)
                    ->Fg(t.colors.mutedForeground)
                    ->Child(TextEl(cx->a, StrDup(cx->a, toast.description))));
        }
        stack->Item(name, body);
    }
    return stack->Placement(Anchor::TopRight)
        ->Focus(self->toastFocus)
        ->IntoEl()
        ->FlexCol()
        ->Absolute()
        ->Top(t.spacing.lg)
        ->Right(t.spacing.lg)
        ->W(kToastWidth)
        ->Deferred()
        ->ZIndex(kToastPriority);
}

// ─── the tooltip ───────────────────────────────────────────────────────────

// What an appearing tooltip does on its way in: the whole of the root's
// presentation for the layer. Enter slides up and fades; a switch between two
// triggers on one row slides across, because the box that was already up is
// the same box.
static El* RenderTooltip(Ctx* cx, El* view, const TooltipTransition& transition,
                         void*) {
    El* element = Div(cx->a)->Child(view);
    if (transition.kind == TooltipTransitionKind::Switch) {
        if (fabsf(transition.current.y - transition.previous.y) >=
            kTooltipSameRow)
            return element;
        float travelled = (transition.current.x + transition.current.w / 2.f) -
                          (transition.previous.x + transition.previous.w / 2.f);
        return EffectTransition::New(cx, kTooltipSlideMs)
            ->Ease(&EaseInOutCubic)
            ->SlideX(-travelled, 0.f)
            ->Apply(element,
                    StrDup(cx->a, fmt("shell-tooltip-slide-%llu",
                                      (unsigned long long)transition.epoch)));
    }
    return EffectTransition::New(cx, kTooltipEnterMs)
        ->Ease(&EaseOutCubic)
        ->SlideY(4.f, 0.f)
        ->Fade(0.f, 1.f)
        ->Apply(element,
                StrDup(cx->a, fmt("shell-tooltip-enter-%llu",
                                  (unsigned long long)transition.epoch)));
}

// The window's one tooltip layer. Rust's root owns a TooltipOverlay of its
// own; the runtime here already keeps one per window (an El::Tip trigger
// reaches it without knowing what roots the window), so the root owns only
// the decision to have one, and what an appearing tooltip looks like.
static void InstallTooltipLook(Window* win, App* app) {
    if (!win || !app) return;
    if (!win->tooltip.IsValid())
        win->tooltip = EntityNew<TooltipOverlay>(app).id;
    TooltipOverlay* overlay = Entity<TooltipOverlay>{win->tooltip}.Get(app);
    if (overlay && overlay->renderer != &RenderTooltip)
        overlay->RenderWith(&RenderTooltip);
}

// ─── the root ──────────────────────────────────────────────────────────────

ShellRoot::~ShellRoot() {
    for (int i = 0; app && i < len(dialogs); i++)
        EntityDrop(app, dialogs[i].layer);
    VecReset(dialogs);
    if (app && sheet.IsValid()) EntityDrop(app, sheet);
    for (int i = 0; i < len(toasts.entries); i++) {
        FreeToastId(&toasts.entries[i].id);
        FreeToastValue(&toasts.entries[i].value);
    }
    VecReset(toasts.entries);
    VecReset(toastState.entries);
    VecReset(toastState.heights);
    if (app && content.IsValid()) EntityDrop(app, content);
}

Entity<ShellRoot> ShellRoot::New(App* app, EntityId content) {
    Entity<ShellRoot> root = EntityNew<ShellRoot>(app);
    if (ShellRoot* state = root.Get(app)) {
        state->app = app;
        state->content = content;
        state->toasts = ToastManager<ShellToastId, ShellToastValue>::New(
            ToastMotion::Sonner());
        state->toastFocus = FocusHandleNew(app);
    }
    return root;
}

El* ShellRoot::Render(ShellRoot* self, Ctx* cx) {
    if (!self) return Div(cx->a)->SizeFull();
    if (ShellRootWindowState* state = RootWindowState(cx->win))
        state->root = cx->self;
    InstallTooltipLook(cx->win, cx->app);
    SemanticThemeTokens tokens = RootTokens(cx->app);
    // The window's base text size, from the theme rather than from the
    // runtime's default rem. Everything this root draws itself — toasts, the
    // sheet, the dialog scrim's chrome — states no size of its own and
    // inherits this one. `md` is the base by the library's own convention,
    // and the default `md` is the same 16px, so a theme that says nothing
    // about type is drawn exactly as before.
    //
    // No `id("shell-root")`: ids here fold every ancestor's name into their
    // own, and naming the root would re-key every element's state under it.
    El* root = Div(cx->a)
                   ->FlexCol()
                   ->SizeFull()
                   ->Font(tokens.typography.md.size)
                   ->Bg(tokens.colors.background)
                   ->Fg(tokens.colors.foreground);
    // Painted back to front; see the stacking order in root.h.
    root->Child(TextSelectionLayer::New(cx));
    if (El* content = self->content.IsValid()
                          ? EntityRender(cx->app, cx->win, cx->a, self->content)
                          : nullptr)
        root->Child(content);
    if (self->sheet.IsValid()) {
        if (El* sheet = EntityRender(cx->app, cx->win, cx->a, self->sheet))
            root->Child(sheet);
    }
    int top = len(self->dialogs) - 1;
    for (int i = 0; i <= top; i++) {
        ShellDialogLayer* layer = DialogLayerAt(cx->app, self, i);
        if (!layer) continue;
        layer->index = i;
        layer->topmost = i == top;
        if (El* dialog =
                EntityRender(cx->app, cx->win, cx->a, self->dialogs[i].layer))
            root->Child(dialog);
    }
    if (El* toasts = ToastLayer(self, cx, tokens)) root->Child(toasts);
    // The HUD, when a script has asked for one. Above every other layer: it
    // is a diagnostic, and a dialog over it would hide the reading the
    // dialog's own frames are producing.
    if (self->fpsHudVisible) {
        // The same keyed slot the `fps_monitor()` element form uses, so the
        // monitor is one per window and a HUD hidden and shown again keeps its
        // history.
        auto* slot = KeyedState<Entity<FpsMonitor>>(
            cx, (uint32_t)HashClickId(StrL("gpui-fps-monitor")));
        if (slot) {
            if (!slot->IsValid()) *slot = EntityNew<FpsMonitor>(cx);
            FpsOverlayOpts opts;
            opts.anchor = self->fpsHud.anchor;
            if (self->fpsHud.hasFrameBudget) {
                opts.frameBudget = self->fpsHud.frameBudget;
            }
            root->Child(FpsOverlayEl(cx, *slot, opts));
        }
    }
    return root;
}

ShellRoot* ShellRootOf(Window* window, App* app) {
    ShellRootWindowState* state = RootWindowState(window);
    if (!state || !state->root.IsValid()) return nullptr;
    // The window's first view, or — when a catalog's window opener wrapped
    // the window in the Base Root its components need — that Root's content.
    // Rust asks `window.root::<ShellRoot>()` alone, which a wrapped window
    // never answers; the overlays a script opens would then have no host.
    bool rooted = state->root == window->root;
    if (!rooted) {
        Root* base = Root::Read(window);
        rooted = base && base->view == state->root;
    }
    if (!rooted) return nullptr;
    return Entity<ShellRoot>{state->root}.Get(app);
}

bool ShellRootShowFpsMonitor(Ctx* cx, const FpsHudRequest& request) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root || !OverlayMutationAllowed("show_fps_monitor")) return false;
    root->fpsHud = request;
    root->fpsHudVisible = true;
    NotifyRoot(cx->app, root, cx->win);
    return true;
}

bool ShellRootHideFpsMonitor(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root || !OverlayMutationAllowed("hide_fps_monitor")) return false;
    if (!root->fpsHudVisible) return false;
    root->fpsHudVisible = false;
    NotifyRoot(cx->app, root, cx->win);
    return true;
}

bool ShellRootFpsMonitorVisible(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    return root && root->fpsHudVisible;
}

} // namespace gpui
