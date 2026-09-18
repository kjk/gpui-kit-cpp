#ifndef GPUI_SHELL_RUNTIME_H_
#define GPUI_SHELL_RUNTIME_H_

#include "shell/metrics.h"
#include "shell/policy.h"
#include "shell/retained.h"
#include "shell/snapshot.h"

namespace gpui {

struct ViewType;
struct ViewObject;
struct ShellRuntimeImpl;
struct ShellRuntimeControl;
struct ShellRuntimeAccess;
} // namespace gpui
namespace gpui::shell {
struct MaterializedDependencies;
}
namespace gpui {
struct ShellTaskDriver;
struct InlineTokenContext;
struct InlineTokenClickEvent;

class ShellRuntime {
  public:
    static ShellRuntime* New(App* app = nullptr, ShellError* error = nullptr);
    ShellRuntime* Retain();
    void Release();

    ViewType* LoadSource(Str name, Str source, ShellError* error = nullptr);
    ViewType* LoadSource(Str name, Str source, Policy* policy,
                         ShellError* error = nullptr);
    ViewType* LoadApp(Str directory, Str entry = StrL("main.js"),
                      ShellError* error = nullptr);
    ViewType* LoadApp(Str directory, Str entry, Policy* policy,
                      ShellError* error = nullptr);
    // Reload's entry point. `reuse` is the running application's already
    // materialized dependencies, handed to its replacement instead of being
    // fetched again: the watcher only scans .js and .mjs, so a reload cannot
    // have been triggered by a manifest change and the set is provably the
    // same. Fetching it again meant a `git fetch` per dependency on the UI
    // thread, once per keystroke-triggered reload.
    ViewType* ReloadApp(Str directory, Str entry, Policy* policy,
                        const shell::MaterializedDependencies* reuse,
                        ShellError* error = nullptr);
    // Rust's `new_isolated_with_dependency_store`: an empty root is the
    // per-user Git dependency cache, and a test points this at its own.
    void SetDependencyCacheRoot(Str root);
    ViewObject* Instantiate(ViewType* type, Window* window, App* app,
                            Policy* policy = nullptr,
                            ShellError* error = nullptr, EntityId view = {});
    RenderSnapshot* BuildSnapshot(ViewObject* object, Window* window, App* app,
                                  EntityId view = {}, Policy* policy = nullptr,
                                  ShellError* error = nullptr);
    Str RenderToSpec(Arena* into, ViewObject* object, Window* window, App* app,
                     EntityId view = {}, Policy* policy = nullptr,
                     ShellError* error = nullptr);

    bool Eval(Str source, Str name = StrL("<eval>"),
              ShellError* error = nullptr);
    bool DrainJobs(int limit = 1024, ShellError* error = nullptr);
    RuntimeMetrics ReadMetrics() const;
    void RecordMaterialize(uint64_t nanos);
    void RecordStructure(bool repeated);
    int LiveCallbacks() const;
    int LiveEntities() const;
    int LiveNestedViews() const;
    int LiveTasks() const;
    int LiveTemplates() const;
    shell::RetainedEntry* Retained(shell::EntityHandle handle) const;
    EntityId NestedView(shell::EntityHandle handle, App* app) const;

    // ScriptView registers its dirty bit so cx.notify() can invalidate the
    // JavaScript description as well as the native window. Ordinary repaint
    // causes then replay the published snapshot without entering QuickJS.
    void RegisterScriptView(EntityId view, bool* dirty);
    void UnregisterScriptView(EntityId view, bool* dirty);
    void InvalidateScriptView(EntityId view);
    void ReleaseOwnedEntities(EntityId view);
    void ReleaseApplicationState(ViewObject* object);

    void DispatchClick(shell::CallbackId callback, const ClickEvent& event,
                       Window* window, App* app);
    void DispatchMouseMove(shell::CallbackId callback,
                           const MouseMoveEvent& event, Window* window,
                           App* app);
    void DispatchChange(shell::CallbackId callback, bool value, Window* window,
                        App* app);
    void DispatchIndex(shell::CallbackId callback, uint32_t value,
                       Window* window, App* app);
    void DispatchNumbers(shell::CallbackId callback, const float* values,
                         int count, Window* window, App* app);
    void DispatchString(shell::CallbackId callback, Str value, Window* window,
                        App* app);
    void DispatchSignal(shell::CallbackId callback, Window* window, App* app);
    // The keyboard, the pointer and one named action.
    //
    // The keystroke is handed over twice, and both forms earn their place.
    // `key` and `modifiers` are what the event holds, and a script that wants
    // one half of a chord reads them; `keystroke` is the "cmd-shift-s"
    // spelling a key binding is written in, which is the form a comparison is
    // actually written against.
    void DispatchKey(shell::CallbackId callback, const KeyEvent& event,
                     bool* propagate, Window* window, App* app);
    void DispatchMouseButton(shell::CallbackId callback, MouseButton button,
                             float x, float y, int clickCount,
                             Modifiers modifiers, Bounds bounds, bool hasBounds,
                             Window* window, App* app);
    void DispatchScrollWheel(shell::CallbackId callback,
                             const ScrollWheelEvent& event, Bounds bounds,
                             bool hasBounds, bool* propagate, Window* window,
                             App* app);
    void DispatchAction(shell::CallbackId callback, Str action, bool* propagate,
                        Window* window, App* app);
    void DispatchCalendarEvent(shell::EntityHandle handle,
                               const CalendarEvent& event, Window* window,
                               App* app);
    // Draws one piece of a dock's chrome by asking the script, and replays a
    // cached description when neither the handler nor the container's state
    // has moved. See src/shell/dock.h.
    El* DescribeDockChrome(Ctx* cx, shell::EntityHandle dock,
                           shell::DockChromeSlot slot, uint64_t key,
                           shell::CallbackId handler, Str payload);
    void DispatchItemSecondaryClick(shell::CallbackId callback, Str key,
                                    const MouseDownEvent& event, Window* window,
                                    App* app);
    void DispatchInputEvent(shell::EntityHandle handle, const InputEvent& event,
                            Window* window, App* app);
    void DispatchSliderEvent(shell::EntityHandle handle,
                             const SliderEvent& event, Window* window,
                             App* app);
    void DispatchOtpEvent(shell::EntityHandle handle, const OtpEvent& event,
                          Window* window, App* app);
    void RenderVirtualItems(shell::CallbackId render, shell::CallbackId getKey,
                            shell::CallbackId onItemClick,
                            shell::CallbackId onItemSecondaryClick, int first,
                            int end, Ctx* cx, El** out);
    // Token chips are built while materializing, not while describing. A
    // dedicated generation holds callbacks the renderer registers (a nested
    // button's on_click) and is retired at the next materialize, after last
    // frame's clicks have already run.
    void BeginTokenFrame();
    El* RenderInlineToken(shell::CallbackId render,
                          const InlineTokenContext* ctx, Str text, Ctx* cx);
    void DispatchTokenClick(shell::CallbackId click,
                            const InlineTokenClickEvent* ev, Str text, Ctx* cx);

  private:
    friend struct ShellRuntimeAccess;
    friend struct ShellTaskDriver;
    friend void ShellRuntimeRetireSnapshot(void*, uint64_t);
    void ResumeTask(uint32_t id, Ctx* cx);
    ShellRuntime();
    ~ShellRuntime();

    uint32_t refs = 1;
    ShellRuntimeImpl* impl = nullptr;
    ShellRuntimeControl* control = nullptr;
};

ViewType* ViewTypeRetain(ViewType* type);
void ViewTypeRelease(ViewType* type);

// The dependencies the application this type came from materialized. Reload
// hands them to the replacement rather than fetching them again.
const shell::MaterializedDependencies* ViewTypeDependencies(ViewType* type);
ViewObject* ViewObjectRetain(ViewObject* object);
void ViewObjectRelease(ViewObject* object);
void ShellSetDevelopmentMode(bool enabled);
bool ShellDevelopmentMode();

struct ShellExitRequest {
    int code = 0;
    EntityId view = {};
};
using ShellExitHandler = void (*)(const ShellExitRequest&, Ctx*);
void ShellOnExitRequest(ShellExitHandler handler);

} // namespace gpui
#endif // GPUI_SHELL_RUNTIME_H_
