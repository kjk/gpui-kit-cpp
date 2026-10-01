/* The window-level overlay host — ported from crates/shell/src/root.rs.
 *
 * Upstream's `#[gpui::test]`s drive a ShellRoot in a real window. What stands
 * in here is a window struct with no OS window behind it, the root rendered
 * into a frame arena, and the root's own entry points — the same calls the
 * script bindings make. Three are not ported: the two background-press focus
 * tests and Escape on the stack, which need simulated input through a laid-out
 * frame. The toast clock is driven through ShellRootAdvanceToasts where Rust
 * advances the executor's clock. */

#include "Test.h"

namespace {

struct RootTestContent {
    static El* Render(RootTestContent*, Ctx* cx) { return Div(cx->a); }
};

// shell_root(cx): a window whose first view is a ShellRoot around plain
// content, rendered once so the window knows its root.
struct RootFixture {
    App app;
    Window window;
    Arena* frame = nullptr;
    Entity<ShellRoot> root = {};
    Ctx cx = {};

    RootFixture() {
        window.app = &app;
        BaseInit(&app);
        frame = ArenaNew();
        window.frameArena = frame;
        root = ShellRoot::New(&app, EntityNew<RootTestContent>(&app).id);
        window.root = root.id;
        cx = {&app, &window, frame, root.id};
        Draw();
    }

    ~RootFixture() {
        EntityDropAll(&app);
        ArenaDelete(frame);
        AppGlobalClear(&app);
    }

    El* Draw() {
        frame->Reset();
        return EntityRender(&app, &window, frame, root.id);
    }

    // view(cx): a fresh piece of content for an overlay.
    EntityId View() { return EntityNew<RootTestContent>(&app).id; }

    // A focus handle the window can restore to, as a frame's FocusCollect
    // would have left it, focused.
    FocusHandle FocusSomething() {
        FocusHandle handle = FocusHandleNew(&app);
        FocusRect rect;
        rect.id = handle.id;
        VecAppend(window.focusEls, rect);
        FocusHandleFocus(&window, handle);
        return handle;
    }

    ShellRoot* State() { return root.Get(&app); }
};

} // namespace

static void ClosingTheTopDialogLeavesTheOneBelow() {
    RootFixture f;
    EntityId first = f.View();
    EntityId second = f.View();
    utassert(ShellRootOpenDialogView(&f.cx, first) == 1);
    utassert(ShellRootOpenDialogView(&f.cx, second) == 2);
    utassert(ShellRootDialogCount(&f.cx) == 2);

    utassert(ShellRootCloseDialog(&f.cx));
    utassert(ShellRootDialogCount(&f.cx) == 1);
    utassert(ShellRootTopmostDialog(&f.cx) == first);
    // The closed dialog's content went with it.
    utassert(EntityGet(&f.app, second) == nullptr);

    // The stack empties one dialog per close, and reports when it is done.
    utassert(ShellRootCloseDialog(&f.cx));
    utassert(!ShellRootCloseDialog(&f.cx));
}

static void ClosingADialogRestoresThePreviousFocus() {
    RootFixture f;
    FocusHandle before = f.FocusSomething();
    ShellRootOpenDialogView(&f.cx, f.View());
    utassert(WindowFocused(&f.window) != before);

    ShellRootCloseDialog(&f.cx);
    utassert(WindowFocused(&f.window) == before);
}

static void ANestedDialogRestoresFocusToTheOneBelow() {
    RootFixture f;
    ShellRootOpenDialogView(&f.cx, f.View());
    FocusHandle outer = WindowFocused(&f.window);
    // The first dialog's own handle is a place focus can return to once its
    // surface has been drawn.
    FocusRect rect;
    rect.id = outer.id;
    VecAppend(f.window.focusEls, rect);

    ShellRootOpenDialogView(&f.cx, f.View());
    ShellRootCloseDialog(&f.cx);
    utassert(WindowFocused(&f.window) == outer);
}

static void ClosingAllDialogsRestoresTheFocusTheStackStartedFrom() {
    RootFixture f;
    FocusHandle before = f.FocusSomething();
    ShellRootOpenDialogView(&f.cx, f.View());
    ShellRootOpenDialogView(&f.cx, f.View());
    utassert(ShellRootCloseAllDialogs(&f.cx) == 2);

    utassert(ShellRootDialogCount(&f.cx) == 0);
    utassert(WindowFocused(&f.window) == before);
}

static void ASheetIsReplacedRatherThanStacked() {
    RootFixture f;
    FocusHandle before = f.FocusSomething();
    EntityId first = f.View();
    EntityId second = f.View();
    utassert(ShellRootOpenSheetView(&f.cx, first, Placement::Right));
    utassert(ShellRootOpenSheetView(&f.cx, second, Placement::Left));
    utassert(ShellRootSheet(&f.cx) == second);
    utassert(EntityGet(&f.app, first) == nullptr);

    // The replacement inherited the first sheet's restore target, so one
    // close returns to the window rather than to the sheet it replaced.
    utassert(ShellRootCloseSheet(&f.cx));
    utassert(WindowFocused(&f.window) == before);
    utassert(!ShellRootCloseSheet(&f.cx));
}

// Every layer painting at once is the case the stacking order exists for.
static void EveryLayerDrawsTogether() {
    RootFixture f;
    ShellRootOpenSheetView(&f.cx, f.View(), Placement::Right);
    ShellRootOpenDialogView(&f.cx, f.View());
    ShellRootOpenDialogView(&f.cx, f.View());
    ToastRequest saved;
    saved.title = StrL("Saved");
    utassert(ShellRootPushToast(&f.cx, saved));

    El* root = f.Draw();
    utassert(root != nullptr);
    utassert(ShellRootDialogCount(&f.cx) == 2);
    utassert(ShellRootSheet(&f.cx).IsValid());
    utassert(ShellRootToastCount(&f.cx) == 1);
    // Back to front: the selection layer, the content, the sheet, the two
    // dialogs and the toast stack — the overlays deferred, the dialogs over
    // the sheet and the toasts over every dialog.
    El* children[8] = {};
    int count = 0;
    for (El* c = root ? root->first : nullptr; c && count < 8; c = c->next)
        children[count++] = c;
    utassert(count == 6);
    if (count == 6) {
        utassert(children[2]->style.deferred);
        utassert(children[3]->style.deferred && children[3]
                                                        ->style.zIndex == 10);
        utassert(children[4]->style.deferred && children[4]
                                                        ->style.zIndex == 11);
        utassert(children[5]->style.deferred &&
                 children[5]->style.zIndex > children[4]->style.zIndex);
    }
}

static void ADismissedToastIsUnmountedOnceItsExitCompletes() {
    RootFixture f;
    ToastRequest saved;
    saved.title = StrL("Saved");
    saved.hasId = true;
    saved.id = StrL("saved");
    utassert(ShellRootPushToast(&f.cx, saved));
    utassert(ShellRootToastCount(&f.cx) == 1);

    utassert(ShellRootRemoveToast(&f.cx, StrL("saved")));
    // Still mounted: dismissal starts the exit transition.
    utassert(ShellRootToastCount(&f.cx) == 1);

    ShellRootAdvanceToasts(&f.cx, ShellRootNowMs() + 1000);
    utassert(ShellRootToastCount(&f.cx) == 0);
    // Nothing left to retire, so the clock is off.
    utassert(f.State() && f.State()->toastTimer == 0);
}

// a_toast_retires_itself_when_its_timeout_elapses, and its twin for a window
// in the background: the clock does not care whether the window is active.
static void AToastRetiresItselfWhenItsTimeoutElapses() {
    RootFixture f;
    ToastRequest saved;
    saved.title = StrL("Saved");
    saved.timeoutMs = 1000;
    utassert(ShellRootPushToast(&f.cx, saved));
    utassert(f.State() && f.State()->toastTimer != 0);
    int64_t now = ShellRootNowMs();
    // Entering, then present, then past its timeout, then gone.
    ShellRootAdvanceToasts(&f.cx, now + 500);
    ShellRootAdvanceToasts(&f.cx, now + 1600);
    ShellRootAdvanceToasts(&f.cx, now + 2000);
    utassert(ShellRootToastCount(&f.cx) == 0);
}

static void APersistentToastStaysUntilDismissed() {
    RootFixture f;
    ToastRequest saved;
    saved.title = StrL("Saved");
    saved.hasTimeout = false;
    utassert(ShellRootPushToast(&f.cx, saved));
    int64_t now = ShellRootNowMs();
    ShellRootAdvanceToasts(&f.cx, now + 500);
    ShellRootAdvanceToasts(&f.cx, now + 60000);
    utassert(ShellRootToastCount(&f.cx) == 1);
    ShellRootClearToasts(&f.cx);
    ShellRootAdvanceToasts(&f.cx, now + 61000);
    utassert(ShellRootToastCount(&f.cx) == 0);
}

static void PushingTheSameToastIdReplacesIt() {
    RootFixture f;
    for (int i = 0; i < 3; i++) {
        ToastRequest saved;
        saved.title = StrL("Saved");
        saved.hasId = true;
        saved.id = StrL("saved");
        utassert(ShellRootPushToast(&f.cx, saved));
    }
    utassert(ShellRootToastCount(&f.cx) == 1);
}

static void ToastsPushedWithoutAnIdStack() {
    RootFixture f;
    for (int i = 0; i < 3; i++) {
        ToastRequest saved;
        saved.title = StrL("Saved");
        utassert(ShellRootPushToast(&f.cx, saved));
    }
    utassert(ShellRootToastCount(&f.cx) == 3);
}

static void ScriptFacingNamesRoundTrip() {
    const ToastLevel levels[] = {ToastLevel::Info, ToastLevel::Success,
                                 ToastLevel::Warning, ToastLevel::Error};
    for (ToastLevel level : levels) {
        ToastLevel parsed = ToastLevel::Info;
        utassert(ToastLevelFromName(Str(ToastLevelName(level)), &parsed));
        utassert(parsed == level);
    }
    ToastLevel fatal = ToastLevel::Info;
    utassert(!ToastLevelFromName(StrL("fatal"), &fatal));
}

// toast_request_builder_keeps_every_field: the request is a plain struct
// here, so what is left to pin is the default timeout.
static void AToastRequestDefaultsToTheStandardTimeout() {
    ToastRequest toast;
    utassert(toast.hasTimeout && toast.timeoutMs == kToastDefaultTimeoutMs);
    static_assert(kToastDefaultTimeoutMs == 5000,
                  "ToastRequest::DEFAULT_TIMEOUT");
    utassert(toast.level == ToastLevel::Info && !toast.hasId);
}

static void DialogOptionsDefaultToDismissable() {
    DialogOptions options;
    utassert(options.escapeDismissable && options.backdropDismissable);
    options.EscapeDismissable(false).BackdropDismissable(false);
    utassert(!options.escapeDismissable && !options.backdropDismissable);
}

// The bare runtime's window has no Root around the ShellRoot, and the root
// draws its own surface: the semantic background, not a component library's.
static void TheRootReadsOnlyBaseTokens() {
    RootFixture f;
    BaseTheme theme;
    theme.tokens.colors.background = Rgba8(1, 2, 3, 255);
    BaseThemeSet(&f.app, theme);
    El* root = f.Draw();
    utassert(root && root->style.bg.color.r == 1 &&
             root->style.bg.color.g == 2 && root->style.bg.color.b == 3);
}

void TestShellRoot() {
    TestSuite("shell_root");
    ClosingTheTopDialogLeavesTheOneBelow();
    ClosingADialogRestoresThePreviousFocus();
    ANestedDialogRestoresFocusToTheOneBelow();
    ClosingAllDialogsRestoresTheFocusTheStackStartedFrom();
    ASheetIsReplacedRatherThanStacked();
    EveryLayerDrawsTogether();
    ADismissedToastIsUnmountedOnceItsExitCompletes();
    AToastRetiresItselfWhenItsTimeoutElapses();
    APersistentToastStaysUntilDismissed();
    PushingTheSameToastIdReplacesIt();
    ToastsPushedWithoutAnIdStack();
    ScriptFacingNamesRoundTrip();
    AToastRequestDefaultsToTheStandardTimeout();
    DialogOptionsDefaultToDismissable();
    TheRootReadsOnlyBaseTokens();
}
