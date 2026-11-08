#ifndef GPUI_SHELL_SCOPE_H_
#define GPUI_SHELL_SCOPE_H_

#include "gpui/gpui.h"
#include "shell/error.h"
#include "shell/policy.h"

namespace gpui {

class ShellRuntime;

enum class ScopePhase : uint8_t {
    Render,
    Event,
    Task,
    Layout,
};

const char* ScopePhaseName(ScopePhase phase);
bool ScopePhaseAllowsNotify(ScopePhase phase);

namespace shell {

class CallScopeGuard {
  public:
    CallScopeGuard(CallScopeGuard&& other) noexcept;
    CallScopeGuard& operator=(CallScopeGuard&& other) noexcept;
    CallScopeGuard(const CallScopeGuard&) = delete;
    CallScopeGuard& operator=(const CallScopeGuard&) = delete;
    ~CallScopeGuard();

    uint64_t Generation() const { return generation; }
    bool IsActive() const { return active; }

  private:
    friend CallScopeGuard ScopeEnter(Window*, App*, ScopePhase, EntityId,
                                     Policy*, ShellRuntime*, void*);
    explicit CallScopeGuard(uint64_t generation);
    void Leave();

    uint64_t generation = 0;
    bool active = false;
};

// A scope is the only place a script-side cx may recover Window/App pointers.
// The pointers remain inaccessible after the guard leaves, even if script
// userdata carrying the generation survives.
CallScopeGuard ScopeEnter(Window* window, App* app, ScopePhase phase,
                          EntityId view = {}, Policy* policy = nullptr,
                          ShellRuntime* runtime = nullptr,
                          void* application = nullptr);
void ScopeAdopt(uint64_t generation);
uint64_t ScopeCurrentGeneration();
ScopePhase ScopeCurrentPhase();
bool ScopeHasCurrent();
Policy* ScopeCurrentPolicy();
ShellRuntime* ScopeCurrentRuntime();
EntityId ScopeCurrentView();
void* ScopeCurrentApplication();

class ScopeHostContext {
  public:
    ScopeHostContext(ScopeHostContext&& other) noexcept;
    ScopeHostContext& operator=(ScopeHostContext&& other) noexcept;
    ScopeHostContext(const ScopeHostContext&) = delete;
    ScopeHostContext& operator=(const ScopeHostContext&) = delete;
    ~ScopeHostContext();

    bool IsSet() const { return held; }
    Window* GetWindow() const { return window; }
    App* GetApp() const { return app; }

  private:
    friend ScopeHostContext ScopeCurrentHost();
    friend ScopeHostContext ScopeHostForGeneration(uint64_t, ShellError*);
    ScopeHostContext(Window* window, App* app, uint64_t frame);
    // Borrows the host of the frame with `generation`, which is the top one.
    // One borrow per frame: a second ask on the same frame answers unset,
    // while a frame entered inside it may borrow its own.
    static ScopeHostContext Acquire(Window* window, App* app,
                                    uint64_t generation);
    void Release();

    Window* window = nullptr;
    App* app = nullptr;
    // The generation of the frame this borrowed from; 0 holds nothing.
    uint64_t frame = 0;
    bool held = false;
};

ScopeHostContext ScopeCurrentHost();
ScopeHostContext ScopeHostForGeneration(uint64_t generation,
                                        ShellError* error = nullptr);
Str ScopeStaleContextMessage();

} // namespace shell
} // namespace gpui
#endif // GPUI_SHELL_SCOPE_H_
