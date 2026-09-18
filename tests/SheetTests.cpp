/* Ported from crates/base/src/sheet.rs, mod tests.
 *
 * Rust's four cases drive a window and click at a point; what they are
 * checking is the overlay's press rule, which is this. */

#include "Test.h"

static void ASheetOverlayPressHasThreeOutcomes() {
    // overlay_close_requests_then_notifies: a left press on a closable
    // overlay closes it.
    utassert(SheetOverlayPressAction(true, true, MouseButton::Left, 20, false,
                                     0) == SheetOverlayPress::Close);
    // non_closable_overlay_does_not_request_close — but it is still taken, so
    // the page behind never sees it.
    utassert(SheetOverlayPressAction(true, false, MouseButton::Left, 20, false,
                                     0) == SheetOverlayPress::Swallow);
    // An overlay that is not interactive is not there at all as far as the
    // pointer is concerned.
    utassert(SheetOverlayPressAction(false, true, MouseButton::Left, 20, false,
                                     0) == SheetOverlayPress::Ignore);
    // A secondary press is taken but does not close.
    utassert(SheetOverlayPressAction(true, true, MouseButton::Right, 20, false,
                                     0) == SheetOverlayPress::Swallow);
}

static void ThePressCutoffLeavesTheBandAboveAlone() {
    // pointer_above_the_dismiss_cutoff_is_ignored, with Rust's own numbers:
    // a cutoff of 50, a press at 20 ignored and one at 80 closing.
    utassert(SheetOverlayPressAction(true, true, MouseButton::Left, 20, true,
                                     50) == SheetOverlayPress::Ignore);
    utassert(SheetOverlayPressAction(true, true, MouseButton::Left, 80, true,
                                     50) == SheetOverlayPress::Close);
    // On the line counts as below it, as Rust's `<` says.
    utassert(SheetOverlayPressAction(true, true, MouseButton::Left, 50, true,
                                     50) == SheetOverlayPress::Close);
}

static void EscapeClosesASheet() {
    utassert(SheetClosesOnKey(KeyEscape));
    utassert(!SheetClosesOnKey(KeyReturn));
    utassert(!SheetClosesOnKey(KeyTab));
}

namespace {
struct SheetRecorder {
    int events[8] = {};
    int n = 0;

    static void Request(SheetRecorder* self, Ctx*, const ClickEvent*) {
        self->events[self->n++] = 1;
    }
    static void Closed(SheetRecorder* self, Ctx*, const ClickEvent*) {
        self->events[self->n++] = 2;
    }
};
} // namespace

static ActionSlot* FindSheetAction(El* root) {
    for (ActionSlot* slot = root ? root->actions : nullptr; slot;
         slot = slot->next) {
        if (slot->action == action::Cancel()) {
            return slot;
        }
    }
    return nullptr;
}

static void TheBuiltSheetOwnsDismissalAndFocus() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Entity<SheetRecorder> recorder = EntityNewState<SheetRecorder>(&app);
    Ctx cx = {&app, win, a, recorder.id};

    El* overlay = Div(a)->Bg(Rgb(1, 2, 3));
    El* surface = Div(a)->W(80);
    El* root = Sheet::New(&cx)
                   ->Overlay(overlay)
                   ->Surface(surface)
                   ->RequestClose(Listen(&cx, &SheetRecorder::Request))
                   ->OnClose(Listen(&cx, &SheetRecorder::Closed))
                   ->IntoEl();

    int trap = FocusTrapId(StrL("sheet"));
    utassert(root->style.focusId == trap && root->style.trapId == trap);
    utassert(!root->style.tabStop && !root->style.focusRing);
    utassert(surface->style.focusId == 0 && surface->style.trapId == 0);
    utassert(win->pendingTrap == trap && win->pendingTrapHost == trap);

    // overlay, transparent capture, surface: the surface remains above the
    // capture while a press beside it reaches the capture.
    El* capture = overlay->next;
    utassert(capture && capture->next == surface);
    utassert(capture->onMouseDown.IsValid());
    MouseDownEvent press = {};
    press.button = MouseButton::Left;
    press.y = 80;
    ListenerCall(&app, win, capture->onMouseDown, &press);
    SheetRecorder* seen = recorder.Get(&app);
    utassert(seen->n == 2 && seen->events[0] == 1 && seen->events[1] == 2);
    utassert(win->stopPropagation);

    seen->n = 0;
    win->stopPropagation = false;
    ActionSlot* cancel = FindSheetAction(root);
    utassert(cancel && cancel->fn.IsValid());
    ActionEvent ev = {};
    ev.action = action::Cancel();
    ListenerCall(&app, win, cancel->fn, &ev);
    utassert(ev.propagate);
    utassert(seen->n == 2 && seen->events[0] == 1 && seen->events[1] == 2);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

static void SheetOverlayOptionsChangeTheActualCapture() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};

    El* visual = Div(a);
    El* surface = Div(a);
    Sheet::New(&cx)
        ->Overlay(visual)
        ->Surface(surface)
        ->DismissBeforeY(50)
        ->OverlayClosable(false)
        ->IntoEl();
    El* capture = visual->next;
    utassert(capture && capture->next == surface);
    utassertnear(capture->style.absTop, 50.f);
    MouseDownEvent press = {};
    press.button = MouseButton::Left;
    press.y = 80;
    ListenerCall(&app, win, capture->onMouseDown, &press);
    utassert(win->stopPropagation);

    El* passiveVisual = Div(a);
    El* passiveSurface = Div(a);
    El* passive = Sheet::New(&cx)
                      ->Overlay(passiveVisual)
                      ->Surface(passiveSurface)
                      ->OverlayInteractive(false)
                      ->IntoEl();
    // No transparent capture child is present when pointer interaction is
    // disabled; the visual overlay is followed directly by the surface.
    utassert(passive->first == passiveVisual);
    utassert(passiveVisual->next == passiveSurface);
    utassert(passiveSurface->next == nullptr);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

static void ThemedSheetReadsItsMarginFromSheetSettings() {
    App app;
    component::Init(&app);
    ThemeSet(&app, ThemeMode::Light);
    Theme custom = ThemeLight(&app);
    custom.sheet.marginTop = 73.f;
    ThemeInstall(&app, ThemeMode::Light, custom);

    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Entity<SheetRecorder> recorder = EntityNewState<SheetRecorder>(&app);
    Ctx cx = {&app, win, a, recorder.id};
    El* customTitle = Div(a)->Id(StrL("custom-sheet-title"));
    Style refinement = {};
    refinement.pad = EdgesAll(7);
    refinement.width = 999;
    component::Sheet* themed =
        component::Sheet::New(&cx)
            ->Open(true)
            ->Title(customTitle)
            ->OverlayClosable(false)
            ->OnClose(Listen(&cx, &SheetRecorder::Closed))
            ->Refine(refinement, StyleFieldPad | StyleFieldWidth);
    for (int i = 0; i < 40; i++) {
        themed->Child(Div(a));
    }
    El* root = themed->IntoEl(WinSize{800, 600});
    El* visual = root->first;
    El* capture = visual ? visual->next : nullptr;
    El* surface = capture ? capture->next : nullptr;
    utassert(surface != nullptr);
    utassert(surface && surface->style.absolute);
    utassertnear(surface ? surface->style.absTop : 0, 73.f);
    utassertnear(surface ? surface->style.height : 0, 527.f);
    // Placement sizing follows refine_style and therefore wins over a
    // width refinement, while padding and the attached-edge border remain.
    utassertnear(surface ? surface->style.width : 0, 350.f);
    utassertnear(surface ? surface->style.pad.left : 0, 7.f);
    utassertnear(surface ? surface->style.borderL : 0, 1.f);
    utassertnear(surface ? surface->style.borderT : 0, 0.f);
    El* head = surface ? surface->first : nullptr;
    El* pane = head ? head->next : nullptr;
    El* bodyColumn = pane ? pane->first : nullptr;
    utassert(head && head->first == customTitle);
    utassertnear(pane ? pane->style.pad.left : 0, 7.f);
    int childCount = 0;
    for (El* child = bodyColumn ? bodyColumn->first : nullptr; child;
         child = child->next) {
        childCount++;
    }
    utassert(childCount == 40);

    MouseDownEvent press = {};
    press.button = MouseButton::Left;
    press.y = 100;
    ListenerCall(&app, win, capture->onMouseDown, &press);
    SheetRecorder* seen = recorder.Get(&app);
    utassert(seen && seen->n == 0);
    utassert(win->stopPropagation);

    // A bottom sheet starts at the lower edge and deliberately ignores the
    // title-bar margin, matching the source's placement match.
    El* bottomRoot = component::Sheet::New(&cx)
                         ->Open(true)
                         ->Placement(component::SheetPlacement::Bottom)
                         ->Body(Div(a))
                         ->IntoEl(WinSize{800, 600});
    El* bottomVisual = bottomRoot->first;
    El* bottomCapture = bottomVisual ? bottomVisual->next : nullptr;
    El* bottom = bottomCapture ? bottomCapture->next : nullptr;
    utassert(bottom != nullptr);
    utassertnear(bottom ? bottom->style.height : 0, 350.f);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

void TestSheet() {
    TestSuite("sheet");
    ASheetOverlayPressHasThreeOutcomes();
    ThePressCutoffLeavesTheBandAboveAlone();
    EscapeClosesASheet();
    TheBuiltSheetOwnsDismissalAndFocus();
    SheetOverlayOptionsChangeTheActualCapture();
    ThemedSheetReadsItsMarginFromSheetSettings();
}
