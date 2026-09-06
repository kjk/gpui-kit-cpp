#ifndef GPUI_SHELL_VIEW_H_
#define GPUI_SHELL_VIEW_H_

#include "shell/materialize.h"
#include "base/number_input.h"
#include "base/popover.h"

namespace gpui {

struct ResizablePanelEvent;

struct ShellBoolBinding {
    shell::CallbackId callback = 0;
    bool value = false;
};

struct ShellStringBinding {
    shell::CallbackId callback = 0;
    Str value;
};

struct ShellSelectBinding {
    shell::CallbackId onOpenChange = 0;
    shell::CallbackId onConfirm = 0;
    shell::CallbackId onDismiss = 0;
    bool open = false;
    bool disabled = false;
    FocusHandle triggerFocus = {};
    FocusHandle contentFocus = {};
};

struct ShellNumberBinding {
    InputState* state = nullptr;
    NumberStep step = {};
    bool hasStep = false;
    bool hasMin = false;
    double min = 0;
    bool hasMax = false;
    double max = 0;
    bool disabled = false;
    shell::CallbackId onStep = 0;
};

// The three buttons an element listened for, and the handler for each.
//
// GPUI takes the button as an argument to on_mouse_down and installs one
// listener per button; the port's element carries one listener per event, so
// the filtering moves here. Arena-allocated with the frame that built the
// element, which is the frame the press is dispatched against.
struct ShellMouseButtonBinding {
    shell::CallbackId left = 0;
    shell::CallbackId right = 0;
    shell::CallbackId middle = 0;
};

// One element's action handlers. GPUI keys a listener by the action's type and
// stops at the first match; an action here is its own id, so each registration
// is its own listener and the table is only what carries the callback.
struct ShellActionBinding {
    uint32_t action = 0;
    shell::CallbackId callback = 0;
};

// The retained native half of a script view. JavaScript runs only when dirty
// and publishes a RenderSnapshot; every ordinary repaint replays that snapshot
// through ShellMaterialize without entering the VM.
struct ScriptView {
    ShellRuntime* runtime = nullptr;
    ViewType* type = nullptr;
    ViewObject* object = nullptr;
    RenderSnapshot* snapshot = nullptr;
    Policy* policy = nullptr;
    ShellError error = {};
    EntityId self = {};
    // The palette revision the current snapshot resolved its colors against.
    // A theme change reaches every description, and nothing else would notify
    // a view about it.
    uint32_t themeRevision = 0;
    bool dirty = true;

    ~ScriptView();

    static Entity<ScriptView> New(App* app, ShellRuntime* runtime,
                                  ViewType* type, Policy* policy = nullptr);
    static El* Render(ScriptView* self, Ctx* cx);
    static void Refresh(ScriptView* self, Ctx* cx);
    static bool Reload(ScriptView* self, Ctx* cx, Str directory, Str entry,
                       ShellError* error = nullptr);

    static void OnClick(ScriptView* self, Ctx* cx, const ClickEvent* event,
                        intptr_t callback);
    static void OnChange(ScriptView* self, Ctx* cx, const ClickEvent* event,
                         intptr_t value);
    static void OnHover(ScriptView* self, Ctx* cx, const HoverEvent* event,
                        intptr_t callback);
    static void OnMouseMove(ScriptView* self, Ctx* cx,
                            const MouseMoveEvent* event, intptr_t callback);
    static void OnOpenChange(ScriptView* self, Ctx* cx,
                             const PopoverOpenChangeEvent* event,
                             intptr_t callback);
    static void OnResize(ScriptView* self, Ctx* cx,
                         const ResizablePanelEvent* event, intptr_t callback);
    static void OnBoundBool(ScriptView* self, Ctx* cx, const void* event,
                            intptr_t binding);
    static void OnBoundString(ScriptView* self, Ctx* cx,
                              const ClickEvent* event, intptr_t binding);
    static void OnItemSecondaryPress(ScriptView* self, Ctx* cx,
                                     const MouseDownEvent* event,
                                     intptr_t binding);
    static void OnSelectAction(ScriptView* self, Ctx* cx,
                               const ActionEvent* event, intptr_t binding);
    // The select root's accessible activation — `on_a11y_action(Click)`:
    // platform adapters may flatten the trigger child, so the root itself
    // opens a closed select and closes an open one. Closing runs the same
    // steps Cancel does, on_dismiss first, so a script that tracks dismissal
    // sees one however the popup was closed.
    static void OnSelectActivate(ScriptView* self, Ctx* cx,
                                 const ClickEvent* event, intptr_t binding);
    static void OnNumberStep(ScriptView* self, Ctx* cx,
                             const NumberInputEvent* event, intptr_t callback);
    static void OnNumberKey(ScriptView* self, Ctx* cx, const KeyEvent* event,
                            intptr_t binding);
    static void OnInputEvent(ScriptView* self, Ctx* cx, const InputEvent* event,
                             intptr_t handle);
    static void OnSliderEvent(ScriptView* self, Ctx* cx,
                              const SliderEvent* event, intptr_t handle);
    static void OnOtpEvent(ScriptView* self, Ctx* cx, const OtpEvent* event,
                           intptr_t handle);
    static void OnCalendarEvent(ScriptView* self, Ctx* cx,
                                const CalendarEvent* event, intptr_t handle);
    // The dock's one event: every edit to the layout, including each step of
    // a drag.
    static void OnDockEvent(ScriptView* self, Ctx* cx, const DockEvent* event,
                            intptr_t callback);
    // The keyboard and the pointer. A key event travels the focus path, so an
    // element only hears one while it — or something inside it — holds the
    // keyboard, which makes track_focus(handle) half of the registration.
    static void OnScriptKey(ScriptView* self, Ctx* cx, const KeyEvent* event,
                            intptr_t callback);
    static void OnScriptMouseDown(ScriptView* self, Ctx* cx,
                                  const MouseDownEvent* event,
                                  intptr_t binding);
    static void OnScriptMouseUp(ScriptView* self, Ctx* cx,
                                const MouseUpEvent* event, intptr_t binding);
    // The one event here that is about somewhere else, and the reason a script
    // can dismiss a surface it drew itself.
    static void OnScriptMouseDownOut(ScriptView* self, Ctx* cx,
                                     const MouseDownEvent* event,
                                     intptr_t callback);
    static void OnScriptScrollWheel(ScriptView* self, Ctx* cx,
                                    const ScrollWheelEvent* event,
                                    intptr_t callback);
    static void OnScriptAction(ScriptView* self, Ctx* cx,
                               const ActionEvent* event, intptr_t binding);
};

} // namespace gpui
#endif // GPUI_SHELL_VIEW_H_
