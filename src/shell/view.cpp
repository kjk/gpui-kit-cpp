#include "shell/view.h"
#include "shell/action.h"
#include "shell/theme_tokens.h"
#include "base/resizable.h"
#include "base/select.h"

namespace gpui {

ScriptView::~ScriptView() {
    if (runtime && self.IsValid()) {
        runtime->UnregisterScriptView(self, &dirty);
        runtime->ReleaseOwnedEntities(self);
    }
    delete snapshot;
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    PolicyRelease(policy);
    ShellErrorClear(&error);
    if (runtime) runtime->Release();
}

Entity<ScriptView> ScriptView::New(App* app, ShellRuntime* runtime,
                                   ViewType* type, Policy* policy) {
    Entity<ScriptView> entity = EntityNew<ScriptView>(app);
    ScriptView* view = entity.Get(app);
    if (!view) return {};
    view->runtime = runtime ? runtime->Retain() : nullptr;
    view->type = ViewTypeRetain(type);
    view->policy = policy ? PolicyRetain(policy) : PolicyDefault();
    view->self = entity.id;
    if (view->runtime)
        view->runtime->RegisterScriptView(entity.id, &view->dirty);
    return entity;
}

El* ScriptView::Render(ScriptView* self, Ctx* cx) {
    if (!self || !self->runtime || !self->type) {
        return Div(cx->a)
            ->Child(TextEl(cx->a, StrL("Shell view is not initialized")));
    }
    uint32_t revision = shell::ThemeTokensSync(cx->app);
    if (revision != self->themeRevision) {
        self->themeRevision = revision;
        self->dirty = true;
    }
    if (!self->object) {
        self->object = self->runtime->Instantiate(
            self->type, cx->win, cx->app, self->policy, &self->error, cx->self);
    }
    if (self->object && (self->dirty || !self->snapshot)) {
        RenderSnapshot* next =
            self->runtime->BuildSnapshot(self->object, cx->win, cx->app,
                                         cx->self, self->policy, &self->error);
        if (next) {
            // Measured here rather than anywhere else because this is the only
            // place two consecutive descriptions of one view exist at the same
            // time. Nothing acts on the answer: it counts how often a rebuild
            // produced the shape it replaced, which is what a template cache
            // would have to be able to fill instead of rebuild. A first build
            // has no predecessor and is not a data point either way.
            if (self->snapshot) {
                self->runtime->RecordStructure(self->snapshot->Structure() ==
                                               next->Structure());
            }
            delete self->snapshot;
            self->snapshot = next;
            self->dirty = false;
            ShellErrorClear(&self->error);
        }
    }
    if (self->snapshot) {
        return ShellMaterialize(cx, self->runtime, self->snapshot,
                                &self->error);
    }
    Str message = self->error.IsSet()
                      ? self->error.message
                      : StrL("The shell view did not publish a snapshot");
    return Div(cx->a)
        ->FlexCol()
        ->SizeFull()
        ->Pad(16)
        ->Gap(8)
        ->Child(TextEl(cx->a, StrL("JavaScript application error"))->Bold())
        ->Child(TextEl(cx->a, message)->Wrap());
}

void ScriptView::Refresh(ScriptView* self, Ctx* cx) {
    if (!self) return;
    self->dirty = true;
    Notify(cx);
}

bool ScriptView::Reload(ScriptView* self, Ctx* cx, Str directory, Str entry,
                        ShellError* error) {
    ShellErrorClear(error);
    if (!self || !self->runtime || !cx || cx->self != self->self) {
        ShellErrorSet(error, StrL("reload needs the live ScriptView context"));
        return false;
    }
    // The replacement inherits the dependencies the running application
    // materialized. Re-fetching them here put a `git fetch` per dependency on
    // the UI thread every time a source file was saved.
    const shell::MaterializedDependencies* reuse =
        ViewTypeDependencies(self->type);
    ViewType* nextType =
        self->runtime->ReloadApp(directory, entry, self->policy, reuse, error);
    if (!nextType) return false;
    ViewObject* nextObject = self->runtime->Instantiate(
        nextType, cx->win, cx->app, self->policy, error, cx->self);
    if (!nextObject) {
        ViewTypeRelease(nextType);
        return false;
    }

    ViewObject* oldObject = self->object;
    ViewType* oldType = self->type;
    RenderSnapshot* oldSnapshot = self->snapshot;
    self->object = nextObject;
    self->type = nextType;
    self->snapshot = nullptr;
    self->dirty = true;
    ShellErrorClear(&self->error);
    if (oldObject) self->runtime->ReleaseApplicationState(oldObject);
    delete oldSnapshot;
    ViewObjectRelease(oldObject);
    ViewTypeRelease(oldType);
    Notify(cx);
    return true;
}

void ScriptView::OnClick(ScriptView* self, Ctx* cx, const ClickEvent* event,
                         intptr_t callback) {
    if (!self || !self->runtime || !event) return;
    self->runtime
        ->DispatchClick((shell::CallbackId)callback, *event, cx->win, cx->app);
}

void ScriptView::OnChange(ScriptView* self, Ctx* cx, const ClickEvent* event,
                          intptr_t value) {
    if (!self || !self->runtime || !event || event->id <= 0) return;
    self->runtime->DispatchChange((shell::CallbackId)(uint32_t)event->id,
                                  value != 0, cx->win, cx->app);
}

void ScriptView::OnHover(ScriptView* self, Ctx* cx, const HoverEvent* event,
                         intptr_t callback) {
    if (!self || !self->runtime || !event) return;
    self->runtime->DispatchChange((shell::CallbackId)callback, event->hovered,
                                  cx->win, cx->app);
}

void ScriptView::OnMouseMove(ScriptView* self, Ctx* cx,
                             const MouseMoveEvent* event, intptr_t callback) {
    if (!self || !self->runtime || !event) return;
    self->runtime->DispatchMouseMove((shell::CallbackId)callback, *event,
                                     cx->win, cx->app);
}

void ScriptView::OnOpenChange(ScriptView* self, Ctx* cx,
                              const PopoverOpenChangeEvent* event,
                              intptr_t callback) {
    if (!self || !self->runtime || !event) return;
    self->runtime->DispatchChange((shell::CallbackId)callback, event->open,
                                  cx->win, cx->app);
}

void ScriptView::OnResize(ScriptView* self, Ctx* cx,
                          const ResizablePanelEvent* event, intptr_t callback) {
    if (!self || !self->runtime || !event) return;
    self->runtime->DispatchNumbers((shell::CallbackId)callback, event->sizes,
                                   event->count, cx->win, cx->app);
}

void ScriptView::OnBoundBool(ScriptView* self, Ctx* cx, const void*,
                             intptr_t binding) {
    ShellBoolBinding* value = (ShellBoolBinding*)binding;
    if (!self || !self->runtime || !value || !value->callback) return;
    self->runtime
        ->DispatchChange(value->callback, value->value, cx->win, cx->app);
}

void ScriptView::OnBoundString(ScriptView* self, Ctx* cx, const ClickEvent*,
                               intptr_t binding) {
    ShellStringBinding* value = (ShellStringBinding*)binding;
    if (!self || !self->runtime || !value || !value->callback) return;
    self->runtime
        ->DispatchString(value->callback, value->value, cx->win, cx->app);
}

// Only the secondary button: the row already reports its click through
// on_item_click, and a left press here would report the same interaction twice.
void ScriptView::OnItemSecondaryPress(ScriptView* self, Ctx* cx,
                                      const MouseDownEvent* event,
                                      intptr_t binding) {
    ShellStringBinding* value = (ShellStringBinding*)binding;
    if (!self || !self->runtime || !event || !value || !value->callback) return;
    if (event->button != MouseButton::Right) return;
    self->runtime->DispatchItemSecondaryClick(value->callback, value->value,
                                              *event, cx->win, cx->app);
}

// Every way of closing a select runs the same steps: on_dismiss, then the
// open state asked to close, then focus back on the trigger. A script that
// tracks dismissal has to see one however the popup was closed, and the
// accessible activation closes exactly what Escape closes.
static void ShellSelectClose(ScriptView* self, Ctx* cx,
                             ShellSelectBinding* value) {
    if (value->onDismiss)
        self->runtime->DispatchSignal(value->onDismiss, cx->win, cx->app);
    if (value->onOpenChange)
        self->runtime
            ->DispatchChange(value->onOpenChange, false, cx->win, cx->app);
    if (value->triggerFocus.IsValid())
        FocusHandleFocus(cx->win, value->triggerFocus);
}

static void ShellSelectOpen(ScriptView* self, Ctx* cx,
                            ShellSelectBinding* value) {
    if (value->onOpenChange)
        self->runtime
            ->DispatchChange(value->onOpenChange, true, cx->win, cx->app);
    if (value->contentFocus.IsValid())
        FocusHandleFocus(cx->win, value->contentFocus);
}

void ScriptView::OnSelectAction(ScriptView* self, Ctx* cx,
                                const ActionEvent* event, intptr_t binding) {
    ShellSelectBinding* value = (ShellSelectBinding*)binding;
    if (!self || !self->runtime || !event || !value) return;
    switch (SelectActionOf(event->action, value->open, value->disabled)) {
        case SelectAction::Open:
            ShellSelectOpen(self, cx, value);
            break;
        case SelectAction::Confirm:
            if (value->onConfirm)
                self->runtime
                    ->DispatchSignal(value->onConfirm, cx->win, cx->app);
            break;
        case SelectAction::Dismiss:
            ShellSelectClose(self, cx, value);
            break;
        case SelectAction::None:
            const_cast<ActionEvent*>(event)->propagate = true;
            break;
    }
}

void ScriptView::OnSelectActivate(ScriptView* self, Ctx* cx, const ClickEvent*,
                                  intptr_t binding) {
    ShellSelectBinding* value = (ShellSelectBinding*)binding;
    if (!self || !self->runtime || !value || value->disabled) return;
    if (value->open) {
        ShellSelectClose(self, cx, value);
        return;
    }
    ShellSelectOpen(self, cx, value);
}

void ScriptView::OnNumberStep(ScriptView* self, Ctx* cx,
                              const NumberInputEvent* event,
                              intptr_t callback) {
    if (!self || !self->runtime || !event || !callback) return;
    self->runtime->DispatchString((shell::CallbackId)callback,
                                  event->action == StepAction::Increment
                                      ? StrL("increment")
                                      : StrL("decrement"),
                                  cx->win, cx->app);
}

void ScriptView::OnNumberKey(ScriptView* self, Ctx* cx, const KeyEvent* event,
                             intptr_t binding) {
    ShellNumberBinding* value = (ShellNumberBinding*)binding;
    if (!self || !event || !value) return;
    StepAction action;
    if (!NumberStepForKey(event->vk, &action)) return;
    Listener onStep = value->onStep ? Listen(cx, &ScriptView::OnNumberStep,
                                             (intptr_t)value->onStep)
                                    : Listener{};
    const NumberStep* step =
        value->onStep || !value->hasStep ? nullptr : &value->step;
    if (NumberInputApplyStep(value->state, cx->app, cx->win, action, step,
                             value->hasMin, value->min, value->hasMax,
                             value->max, value->disabled, onStep)) {
        const_cast<KeyEvent*>(event)->propagate = false;
        Notify(cx);
    }
}

void ScriptView::OnInputEvent(ScriptView* self, Ctx* cx,
                              const InputEvent* event, intptr_t handle) {
    if (!self || !self->runtime || !event) return;
    self->runtime->DispatchInputEvent((shell::EntityHandle)handle, *event,
                                      cx->win, cx->app);
}

void ScriptView::OnSliderEvent(ScriptView* self, Ctx* cx,
                               const SliderEvent* event, intptr_t handle) {
    if (!self || !self->runtime || !event) return;
    self->runtime->DispatchSliderEvent((shell::EntityHandle)handle, *event,
                                       cx->win, cx->app);
}

void ScriptView::OnOtpEvent(ScriptView* self, Ctx* cx, const OtpEvent* event,
                            intptr_t handle) {
    if (!self || !self->runtime || !event) return;
    self->runtime->DispatchOtpEvent((shell::EntityHandle)handle, *event,
                                    cx->win, cx->app);
}

void ScriptView::OnCalendarEvent(ScriptView* self, Ctx* cx,
                                 const CalendarEvent* event, intptr_t handle) {
    if (!self || !self->runtime || !event) return;
    self->runtime->DispatchCalendarEvent((shell::EntityHandle)handle, *event,
                                         cx->win, cx->app);
}

void ScriptView::OnDockEvent(ScriptView* self, Ctx* cx, const DockEvent* event,
                             intptr_t callback) {
    if (!self || !self->runtime || !event ||
        event->kind != DockEventKind::LayoutChanged) {
        return;
    }
    // The event carries nothing: what changed is the whole layout, and dump()
    // is how a subscriber reads it.
    self->runtime
        ->DispatchSignal((shell::CallbackId)callback, cx->win, cx->app);
}

void ScriptView::OnScriptKey(ScriptView* self, Ctx* cx, const KeyEvent* event,
                             intptr_t callback) {
    if (!self || !self->runtime || !event) return;
    // A handler that leaves `propagate` set passes the keystroke on outwards,
    // which is cx.propagate(); clearing it is cx.stop_propagation().
    bool propagate = true;
    self->runtime->DispatchKey((shell::CallbackId)callback, *event, &propagate,
                               cx->win, cx->app);
    const_cast<KeyEvent*>(event)->propagate = propagate;
}

static shell::CallbackId MouseButtonCallback(
    const ShellMouseButtonBinding* binding, MouseButton button) {
    if (!binding) return 0;
    if (button == MouseButton::Right) return binding->right;
    if (button == MouseButton::Middle) return binding->middle;
    return binding->left;
}

void ScriptView::OnScriptMouseDown(ScriptView* self, Ctx* cx,
                                   const MouseDownEvent* event,
                                   intptr_t binding) {
    auto* buttons = (const ShellMouseButtonBinding*)binding;
    shell::CallbackId callback =
        event ? MouseButtonCallback(buttons, event->button) : 0;
    if (!self || !self->runtime || !callback) return;
    self->runtime->DispatchMouseButton(
        callback, event->button, event->x, event->y, event->clickCount,
        event->modifiers, event->el, true, cx->win, cx->app);
}

void ScriptView::OnScriptMouseUp(ScriptView* self, Ctx* cx,
                                 const MouseUpEvent* event, intptr_t binding) {
    auto* buttons = (const ShellMouseButtonBinding*)binding;
    shell::CallbackId callback =
        event ? MouseButtonCallback(buttons, event->button) : 0;
    if (!self || !self->runtime || !callback) return;
    self->runtime->DispatchMouseButton(callback, event->button, event->x,
                                       event->y, 1, event->modifiers, event->el,
                                       true, cx->win, cx->app);
}

void ScriptView::OnScriptMouseDownOut(ScriptView* self, Ctx* cx,
                                      const MouseDownEvent* event,
                                      intptr_t callback) {
    if (!self || !self->runtime || !event) return;
    self->runtime->DispatchMouseButton(
        (shell::CallbackId)callback, event->button, event->x, event->y,
        event->clickCount, event->modifiers, event->el, true, cx->win, cx->app);
}

void ScriptView::OnScriptScrollWheel(ScriptView* self, Ctx* cx,
                                     const ScrollWheelEvent* event,
                                     intptr_t callback) {
    if (!self || !self->runtime || !event) return;
    bool propagate = true;
    self->runtime
        ->DispatchScrollWheel((shell::CallbackId)callback, *event, Bounds{},
                              false, &propagate, cx->win, cx->app);
    const_cast<ScrollWheelEvent*>(event)->propagate = propagate;
}

void ScriptView::OnScriptAction(ScriptView* self, Ctx* cx,
                                const ActionEvent* event, intptr_t binding) {
    auto* bound = (const ShellActionBinding*)binding;
    if (!self || !self->runtime || !event || !bound) return;
    Str id = shell::ShellActionScriptId(event->action);
    // An action this element does not handle re-opens propagation, which is
    // what lets it carry on to an element further out.
    bool propagate = false;
    self->runtime
        ->DispatchAction(bound->callback, id, &propagate, cx->win, cx->app);
    const_cast<ActionEvent*>(event)->propagate = propagate;
}

} // namespace gpui
