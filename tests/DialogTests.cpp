/* Ported from crates/base/src/dialog.rs.
 *
 * Rust's own cases there build a window; the two rules worth pinning are the
 * key bindings — which live in the "Dialog" key context, so they exist only
 * while `keyboard` is on and the dialog declares it — and the four conditions
 * a backdrop press has to satisfy before it dismisses. */

#include "Test.h"

static DialogAction ForChord(const char* spec) {
    KeyChord c = {};
    utassert(KeyChordParse(Str(spec), &c));
    uint32_t ctx = KeyContextOf(DialogContext());
    return DialogActionOf(KeymapMatch(c, &ctx, 1).action);
}

static void EscapeCancelsAndEnterConfirms() {
    DialogInitKeys();
    utassert(ForChord("escape") == DialogAction::Cancel);
    utassert(ForChord("enter") == DialogAction::Confirm);
    utassert(ForChord("tab") == DialogAction::None);
    utassert(ForChord("space") == DialogAction::None);
}

static void KeyboardOffRemovesTheBindings() {
    // Rust hangs the whole key context off `keyboard`, and so does this: a
    // dialog with it off never declares the context, so the chords resolve
    // against whatever is outside the dialog instead — which, with nothing
    // bound out there, is nothing at all.
    DialogInitKeys();
    KeyChord escape = {};
    utassert(KeyChordParse(StrL("escape"), &escape));
    utassert(KeymapMatch(escape, nullptr, 0).action == 0);
    uint32_t other = KeyContextOf(StrL("SomethingElse"));
    utassert(KeymapMatch(escape, &other, 1).action == 0);
}

// The two handlers a dialog keeps for its actions: on_cancel falls back to
// what the x and the backdrop do, since Rust's default on_cancel closes.
static int gRan = 0;
struct DialogRecorder {
    static El* Render(DialogRecorder*, Ctx* cx) { return Div(cx->a); }
    static void Cancel(DialogRecorder*, Ctx*, const ClickEvent*) { gRan = 1; }
    static void Ok(DialogRecorder*, Ctx*, const ClickEvent*) { gRan = 2; }
    static void Close(DialogRecorder*, Ctx*, const ClickEvent*) { gRan = 3; }
};

static void TheActionsRunTheSameHandlersTheButtonsDo() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<DialogRecorder> rec = EntityNew<DialogRecorder>(&app);
    Entity<DialogKeys> keys = EntityNewState<DialogKeys>(&app);
    DialogKeys* k = keys.Get(&app);
    k->onCancel = ListenTo(rec, &DialogRecorder::Cancel);
    k->onOk = ListenTo(rec, &DialogRecorder::Ok);
    k->onClose = ListenTo(rec, &DialogRecorder::Close);

    ActionEvent ev;
    ev.action = ActionOf(StrL("ui::Cancel"));
    gRan = 0;
    ListenerCall(&app, win, ListenTo(keys, &DialogKeys::OnAction), &ev);
    utassert(gRan == 1);

    ev.action = ActionOf(StrL("ui::Confirm"));
    gRan = 0;
    ListenerCall(&app, win, ListenTo(keys, &DialogKeys::OnAction), &ev);
    utassert(gRan == 2);

    // With no cancel of its own, escape closes.
    k->onCancel = {};
    ev.action = ActionOf(StrL("ui::Cancel"));
    gRan = 0;
    ListenerCall(&app, win, ListenTo(keys, &DialogKeys::OnAction), &ev);
    utassert(gRan == 3);

    delete win;
}

static void DialogControlsRouteFromTheirOwnDialogWhenFocusWasStolen() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Entity<DialogRecorder> rec = EntityNew<DialogRecorder>(&app);
    Ctx cx = {&app, win, arena, rec.id};

    Str trap = StrL("stolen-focus-dialog");
    El* cancel = Div(arena)->OnClickAction(action::Cancel());
    El* popup = Div(arena)->Child(cancel);
    DialogBindKeys(&cx, popup, trap, ListenTo(rec, &DialogRecorder::Cancel), {},
                   {});
    El* host = Dialog::New(&cx)->Trap(trap)->Popup(popup)->IntoEl();
    El* outside = Div(arena)->FocusId(77);
    El* root = Div(arena)->Child(outside)->Child(host);
    FocusCollect(win, root);
    WindowSetFocusId(win, 77);

    int dialogFocus = FocusTrapId(trap);
    utassert(WindowFocusedId(win) == 77);
    utassert(cancel->clickActionFocusId == dialogFocus);
    gRan = 0;
    utassert(WindowDispatchActionAtFocus(
        win, FocusHandle{cancel->clickActionFocusId}, action::Cancel()));
    utassert(gRan == 1 && WindowFocusedId(win) == 77);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    ArenaDelete(arena);
    delete win;
}

static void ABackdropPressDismissesOnlyWhenAllFourHold() {
    // The ordinary case: left button, closable, topmost, below the band.
    utassert(DialogBackdropCloses(true, true, MouseButton::Left, 100, 34));
    // Above the reserved band, where a title bar still is.
    utassert(!DialogBackdropCloses(true, true, MouseButton::Left, 10, 34));
    // A secondary press is not a dismissal.
    utassert(!DialogBackdropCloses(true, true, MouseButton::Right, 100, 34));
    // overlay_closable off.
    utassert(!DialogBackdropCloses(false, true, MouseButton::Left, 100, 34));
    // Under another dialog, so the press belongs to the one on top.
    utassert(!DialogBackdropCloses(true, false, MouseButton::Left, 100, 34));
    // Exactly on the boundary counts as below it, as Rust's `<` says.
    utassert(DialogBackdropCloses(true, true, MouseButton::Left, 34, 34));
}

namespace {
struct DialogHandleRecorder {
    int changes = 0;
    int triggerCalls = 0;
    DialogOpenChangeEvent last = {};

    static void OnChange(DialogHandleRecorder* self, Ctx*,
                         const DialogOpenChangeEvent* ev) {
        self->changes++;
        self->last = *ev;
    }
    static void OnTrigger(DialogHandleRecorder* self, Ctx*,
                          const MouseDownEvent*) {
        self->triggerCalls++;
    }
};
} // namespace

// DialogHandle's Entity projection preserves Rust's shared-clone behavior:
// imperative and trigger changes reach every copy and carry their reason.
static void ASharedHandleControlsTriggersAndHosts() {
    App app = {};
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    Entity<DialogHandleRecorder> recorder =
        EntityNewState<DialogHandleRecorder>(&app);
    DialogHandle handle = DialogHandle::New(&cx, false);
    DialogHandle copy = handle;
    handle.OnOpenChange(&app,
                        ListenTo(recorder, &DialogHandleRecorder::OnChange));

    utassert(!handle.IsOpen(&app) && !copy.IsOpen(&app));
    utassert(copy.Open(&cx));
    DialogHandleRecorder* seen = recorder.Get(&app);
    utassert(seen && seen->changes == 1 && seen->last.open);
    utassert(seen->last.reason == DialogChangeReason::Imperative);
    utassert(handle.IsOpen(&app));
    // Replacing true with true is the source's no-op.
    utassert(!handle.Open(&cx) && seen->changes == 1);

    utassert(handle.Close(&cx));
    El* closed = Dialog::New(&cx)->Handle(copy)->IntoEl();
    utassert(closed->accessibility.role == AccessibilityRole::None);
    El* closedAlert = AlertDialog::New(&cx)->Handle(copy)->IntoEl();
    utassert(closedAlert->accessibility.role == AccessibilityRole::None);

    El* trigger = DialogTrigger::New(
        &cx, ListenTo(recorder, &DialogHandleRecorder::OnTrigger), copy);
    utassert(trigger->onMouseDown.IsValid());
    MouseDownEvent right = {};
    right.button = MouseButton::Right;
    ListenerCall(&app, win, trigger->onMouseDown, &right);
    utassert(!handle.IsOpen(&app) && seen->triggerCalls == 0);
    MouseDownEvent down = {};
    ListenerCall(&app, win, trigger->onMouseDown, &down);
    utassert(handle.IsOpen(&app));
    utassert(seen->changes == 3 && seen->last.open);
    utassert(seen->last.reason == DialogChangeReason::TriggerPress);
    utassert(seen->triggerCalls == 1);

    El* open = Dialog::New(&cx)->Handle(handle)->IntoEl();
    utassert(open->accessibility.role == AccessibilityRole::Dialog);
    El* openAlert = AlertDialog::New(&cx)->Handle(handle)->IntoEl();
    utassert(openAlert->accessibility.role == AccessibilityRole::AlertDialog);

    EntityDropAll(&app);
    ArenaDelete(arena);
    delete win;
}

// crates/ui/src/dialog is seven public parts plus an AlertDialog façade. Keep
// those contracts visible even though all parts ultimately build the same POD
// element tree in C++.
static void ThemedPartsAndAlertDefaultsMatchTheSource() {
    App app = {};
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    float animationDuration = component::ANIMATION_DURATION;
    utassert(animationDuration == 250.f);
    component::DialogButtonProps props;
    utassert(props.okVariant == component::ButtonVariant::Primary);
    utassert(props.cancelVariant == component::ButtonVariant::Default);
    utassert(!props.showCancel);
    props.OkText(StrL("Proceed"))
        ->CancelText(StrL("Wait"))
        ->OkVariant(component::ButtonVariant::Danger)
        ->CancelVariant(component::ButtonVariant::Ghost)
        ->ShowCancel();
    utassert(StrEq(props.okText, StrL("Proceed")));
    utassert(StrEq(props.cancelText, StrL("Wait")));
    utassert(props.okVariant == component::ButtonVariant::Danger);
    utassert(props.cancelVariant == component::ButtonVariant::Ghost);
    utassert(props.showCancel);
    utassert(props.RenderOk(&cx, StrL("ok"))->clickAction == action::Confirm());
    utassert(props.RenderCancel(&cx, StrL("cancel"))
                 ->clickAction == action::Cancel());

    El* content = component::DialogContent::New(&cx)
                      ->Child(TextEl(arena, StrL("body")))
                      ->IntoEl();
    utassert(content->style.dir == FlexDir::Col && content->style
                                                           .flexGrow == 1);
    El* header = component::DialogHeader::New(&cx)->IntoEl();
    utassert(header->style.dir == FlexDir::Col && header->style.gapY == 8);
    El* title = component::DialogTitle::New(&cx)->IntoEl();
    utassert(title->style.fontSize == 16 && title->style.fontSemibold);
    El* description = component::DialogDescription::New(&cx)->IntoEl();
    utassert(description->style.fontSize == 14 && description->style.hasColor);
    El* footer = component::DialogFooter::New(&cx)->IntoEl();
    utassert(footer->style.dir == FlexDir::Row &&
             footer->style.justify == Justify::End);
    component::DialogClose* close = component::DialogClose::New(&cx);
    utassert(close->semantic.IsCancel() && !close->semantic.IsAction());
    utassert(close->slot->clickAction == action::Cancel());
    component::DialogAction* actionPart = component::DialogAction::New(&cx);
    utassert(!actionPart->semantic.IsCancel() && actionPart->semantic
                                                     .IsAction());
    utassert(actionPart->root->clickAction == action::Confirm());

    component::AlertDialog* alert = component::AlertDialog::New(&cx);
    utassert(alert->base && !alert->base->overlayClosable);
    utassert(!alert->base->closeButton);
    utassert(alert->base->alertHost);
    alert->Confirm()->ButtonProps(props)->CloseButton();
    utassert(alert->base->buttonProps.showCancel);
    utassert(alert->base->buttonProps
                 .cancelVariant == component::ButtonVariant::Ghost);
    utassert(alert->base->closeButton);

    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

// the_backdrop_fills_the_host.
//
// The backdrop is the dimming surface every caller hands over as an
// absolutely placed element, so it has to have the host's box to resolve
// against — a collapsed wrapper leaves it zero-sized and invisible, which is
// what took the overlay away and the click-outside dismissal with it. Rust
// grew a wrapper for `on_any_mouse_down` and had to give it `absolute()
// .inset_0()`; the port hangs the backdrop straight off the full-window host,
// so there is no wrapper to collapse. This pins that.
static void TheBackdropFillsTheHost() {
    App app = {};
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    const float viewW = 1920;
    const float viewH = 1080;
    // `div().absolute().size_full()`, as the Rust test hands it over: the
    // host now lays its flow children out centered, and a backdrop is not one.
    El* backdrop = Div(arena)->Absolute()->W(kFill)->H(kFill);
    El* host = gpui::Dialog::New(&cx)
                   ->Open(true)
                   ->Backdrop(backdrop)
                   ->Popup(Div(arena)->W(100)->H(100))
                   ->IntoEl();
    // The host is `fixed`, the way Rust hangs the dialog off the window Root
    // rather than off whatever page element contains it, so it is laid out
    // inside a page of the viewport's size exactly as Root does it.
    El* page = Div(arena)->FlexCol()->W(viewW)->H(viewH)->Child(host);
    const RuntimeStyle& th = RuntimeStyleNow(&app);
    LayoutEl(&win->paint, page, 0, 0, viewW, viewH, th.fontSize, th.foreground);

    // The host covers the window, so a caller's backdrop has a box to fill:
    // a zero-sized one paints no overlay behind the dialog.
    utassertnear(host->w, viewW);
    utassertnear(host->h, viewH);
    utassertnear(backdrop->w, viewW);
    utassertnear(backdrop->h, viewH);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

// Lays `host` out the way Root does, inside a page of the viewport's size.
static void LayoutDialogHost(App* app, Window* win, Arena* arena, El* host,
                             float viewW, float viewH) {
    El* page = Div(arena)->FlexCol()->W(viewW)->H(viewH)->Child(host);
    const RuntimeStyle& th = RuntimeStyleNow(app);
    LayoutEl(&win->paint, page, 0, 0, viewW, viewH, th.fontSize, th.foreground);
}

// dialog.rs the_popup_is_centered_by_default, and alert_dialog.rs
// host_style_positions_the_popup: the host centers a popup in normal flow,
// and styling the host (Rust's refine_style, `root` here) replaces that.
// pressing_the_popup_does_not_dismiss: the popup occludes, so a press on it
// never reaches the backdrop under it — StopMouseDown is gpui's occlude().
static void ThePopupIsCenteredAndKeepsPressesOffTheBackdrop() {
    App app = {};
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    const float viewW = 1920;
    const float viewH = 1080;

    El* popup = Div(arena)->W(100)->H(100);
    El* host = gpui::Dialog::New(&cx)->Open(true)->Popup(popup)->IntoEl();
    LayoutDialogHost(&app, win, arena, host, viewW, viewH);
    utassertnear(popup->x + popup->w / 2, viewW / 2);
    utassertnear(popup->y + popup->h / 2, viewH / 2);

    El* topLeft = Div(arena)->W(100)->H(100);
    AlertDialog* alert = AlertDialog::New(&cx)->Open(true);
    alert->root->ItemsStart()->JustifyStart();
    El* alertHost = alert->Popup(topLeft)->IntoEl();
    LayoutDialogHost(&app, win, arena, alertHost, viewW, viewH);
    utassertnear(topLeft->x, 0.f);
    utassertnear(topLeft->y, 0.f);

    utassert(DialogPopup::New(&cx)->stopMouseDown);
    utassert(AlertDialogPopup::New(&cx)->stopMouseDown);
    // The backdrop itself does not occlude: its press is the dismissal.
    utassert(!DialogBackdrop::New(&cx)->stopMouseDown);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

static void OversizedDialogsAreClampedToTheViewport() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    El* host = component::Dialog::New(&cx)
                   ->Open(true)
                   ->W(800)
                   ->H(1000)
                   ->Layer(1)
                   ->IntoEl(WinSize{400, 300});
    El* popup = host ? host->last : nullptr;
    // The panel sits in the viewport-aware corner positioner.
    El* placed = popup ? popup->first : nullptr;
    El* panel = placed ? placed->first : nullptr;
    utassert(panel);
    // 16 DIP on both sides; the height keeps the 16 DIP margin top and
    // bottom plus layer one's 16 DIP step, not the tenth-of-the-viewport
    // offset, which an oversized dialog gives up.
    utassertnear(panel->style.width, 368.f);
    utassertnear(panel->style.maxH, 252.f);
    utassert(placed->style.positionerCorner);
    utassertnear(placed->style.anchorMargin, 16.f);

    // a_dialog_larger_than_the_window_stays_inside_it: laid out, the
    // oversized surface is moved up to the edge margin.
    win->paint.app = &app;
    win->paint.window = win;
    arena->Reset();
    El* top = component::Dialog::New(&cx)->Open(true)->W(800)->H(1000)->IntoEl(
        WinSize{400, 300});
    const RuntimeStyle& th = RuntimeStyleNow(&app);
    LayoutEl(&win->paint, top, 0, 0, 400, 300, th.fontSize, th.foreground);
    El* topPanel = top && top->last && top->last->first
                       ? top->last->first->first
                       : nullptr;
    utassert(topPanel);
    if (topPanel) {
        utassertnear(topPanel->y, 16.f);
        utassert(topPanel->y + topPanel->h <= 300.f - 16.f + 0.5f);
    }
    WindowKeyedFree(win);

    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

// close_trigger_supplies_accessible_button and
// close_trigger_activates_once_and_respects_cancel_veto. The trigger is a
// button with the accessible name "Close" and cancel activation; the wrapper
// around it no longer handles the click itself, so a press closes once and
// goes through Cancel, veto included. A loading button withholds the click
// and stops it reaching the wrapper (button.rs:
// base_activation_is_preserved_and_blocked_while_loading).
static void CloseTriggerSuppliesAnAccessibleButtonThatActivatesOnce() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    component::DialogClose* close =
        component::DialogClose::New(&cx)
            ->Trigger(component::Button::New(&cx, StrL("close"))
                          ->WithSize(UiSize::Small)
                          ->Ghost()
                          ->Icon(IconName::Close));
    El* wrapper = close->slot;
    El* button = wrapper ? wrapper->first : nullptr;
    utassert(wrapper && wrapper->clickAction == 0);
    utassert(button && button->accessibility.role == AccessibilityRole::Button);
    utassert(button && StrEq(button->accessibility.label, StrL("Close")));
    utassert(button && button->clickAction == action::Cancel());
    utassert(close->IntoEl() && close->IntoEl()->first == wrapper);

    // Without a trigger the wrapper keeps the click, as before.
    component::DialogClose* plain = component::DialogClose::New(&cx);
    utassert(plain->slot->clickAction == action::Cancel());

    // Loading: no activation, and the click stops at the button.
    El* busy = component::Button::New(&cx, StrL("busy"))
                   ->OnClickAction(action::Cancel())
                   ->Loading(true)
                   ->IntoEl();
    utassert(busy->clickAction == 0 && busy->stopClick);

    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

static void OkListener(void*, Ctx*, const void*) {}

// alert_dialog.rs: button_props_after_confirm_keeps_the_cancel_button,
// confirm_after_button_props_keeps_the_ok_text,
// button_props_after_on_ok_keeps_the_callback,
// the_direct_builders_match_button_props; dialog.rs:
// dialog_button_props_merge_with_what_the_dialog_already_carries.
//
// `button_props` overrides only the fields its value sets, in any call order.
static void ButtonPropsMergeWithWhatTheDialogAlreadyCarries() {
    App app = {};
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    component::DialogButtonProps deleteText;
    deleteText.OkText(StrL("Delete"));

    component::AlertDialog* after =
        component::AlertDialog::New(&cx)->Confirm()->ButtonProps(deleteText);
    utassert(after->base->buttonProps.IsCancelShown());
    utassert(StrEq(after->base->buttonProps.okText, StrL("Delete")));

    component::AlertDialog* before =
        component::AlertDialog::New(&cx)->ButtonProps(deleteText)->Confirm();
    utassert(before->base->buttonProps.IsCancelShown());
    utassert(StrEq(before->base->buttonProps.okText, StrL("Delete")));

    Listener onOk = {};
    onOk.SetFn(&OkListener);
    component::AlertDialog* callback =
        component::AlertDialog::New(&cx)->OnOk(onOk)->ButtonProps(deleteText);
    utassert(callback->base->buttonProps.onOk.Fn() == onOk.Fn());

    component::AlertDialog* direct =
        component::AlertDialog::New(&cx)
            ->Confirm()
            ->OkText(StrL("Delete"))
            ->OkVariant(component::ButtonVariant::Danger)
            ->CancelText(StrL("Keep"))
            ->CancelVariant(component::ButtonVariant::Ghost);
    component::DialogButtonProps bundle;
    bundle.ShowCancel(true)
        ->OkText(StrL("Delete"))
        ->OkVariant(component::ButtonVariant::Danger)
        ->CancelText(StrL("Keep"))
        ->CancelVariant(component::ButtonVariant::Ghost);
    component::AlertDialog* bundled = component::AlertDialog::New(&cx)
                                          ->ButtonProps(bundle);
    const component::DialogButtonProps* both[] = {&direct->base->buttonProps,
                                                  &bundled->base->buttonProps};
    for (const component::DialogButtonProps* props : both) {
        utassert(props->IsCancelShown());
        utassert(StrEq(props->okText, StrL("Delete")));
        utassert(props->hasOkVariant &&
                 props->okVariant == component::ButtonVariant::Danger);
        utassert(StrEq(props->cancelText, StrL("Keep")));
        utassert(props->hasCancelVariant &&
                 props->cancelVariant == component::ButtonVariant::Ghost);
    }

    component::DialogButtonProps keep, ok, danger;
    keep.CancelText(StrL("Keep"));
    ok.OkText(StrL("Delete"));
    danger.OkVariant(component::ButtonVariant::Danger);
    component::Dialog* dialog = component::Dialog::New(&cx)
                                    ->ButtonProps(keep)
                                    ->ButtonProps(ok)
                                    ->ButtonProps(danger);
    utassert(StrEq(dialog->buttonProps.okText, StrL("Delete")));
    utassert(StrEq(dialog->buttonProps.cancelText, StrL("Keep")));
    utassert(dialog->buttonProps.okVariant == component::ButtonVariant::Danger);

    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

// alert_dialog.rs margin_top_moves_the_alert_surface_off_the_default_offset.
// A 600px window would seat the alert at y=60. margin_top(100) seats it
// at y=100 once the entrance animation is at rest.
static void margin_top_moves_the_alert_surface_off_the_default_offset() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    auto build = [&]() {
        return component::AlertDialog::New(&cx)
            ->Title(StrL("Unsaved changes"))
            ->Description(StrL("Discard the draft?"))
            ->Confirm()
            ->MarginTop(100)
            ->Open(true)
            ->IntoEl(WinSize{800, 600});
    };
    // The first build starts the slide. A second later the surface is at rest.
    win->frameNow = 1;
    (void)build();
    win->frameNow = 2;
    arena->Reset();
    El* host = build();
    const RuntimeStyle& th = RuntimeStyleNow(&app);
    // The corner positioner clamps against the paint context's viewport.
    win->paint.viewW = 800;
    win->paint.viewH = 600;
    LayoutEl(&win->paint, host, 0, 0, 800, 600, th.fontSize, th.foreground);
    El* popup = host ? host->last : nullptr;
    El* placed = popup ? popup->first : nullptr;
    El* panel = placed ? placed->first : nullptr;
    utassert(panel);
    if (panel) {
        utassert(fabsf(panel->y - 100.f) <= 0.5f);
    }

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

void TestDialog() {
    TestSuite("dialog");
    ButtonPropsMergeWithWhatTheDialogAlreadyCarries();
    CloseTriggerSuppliesAnAccessibleButtonThatActivatesOnce();
    OversizedDialogsAreClampedToTheViewport();
    TheBackdropFillsTheHost();
    ThePopupIsCenteredAndKeepsPressesOffTheBackdrop();
    EscapeCancelsAndEnterConfirms();
    TheActionsRunTheSameHandlersTheButtonsDo();
    DialogControlsRouteFromTheirOwnDialogWhenFocusWasStolen();
    KeyboardOffRemovesTheBindings();
    ABackdropPressDismissesOnlyWhenAllFourHold();
    ASharedHandleControlsTriggersAndHosts();
    ThemedPartsAndAlertDefaultsMatchTheSource();
    margin_top_moves_the_alert_surface_off_the_default_offset();
}
